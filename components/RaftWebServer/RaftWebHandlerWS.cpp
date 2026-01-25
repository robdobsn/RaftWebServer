/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebHandlerWS.h"
#include "RaftUtils.h"

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include "RaftWebConnManager_mongoose.h"
#else
#include "RaftWebConnManager.h"
#endif

#define WARN_ON_RESPONDER_NOT_FOUND

// #define WARN_WS_CLOSE_UNKNOWN_CONNECTION
// #define DEBUG_WS_SEND_APP_DATA_FAIL
// #define DEBUG_WEB_HANDLER_WS
// #define DEBUG_WS_SEND_APP_DATA
// #define DEBUG_WS_RECV_APP_DATA
// #define DEBUG_WS_OPEN_CLOSE
// #define DEBUG_WS_PING_PONG

static const char* MODULE_PREFIX = "RaftWebHandlerWS";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebHandlerWS::RaftWebHandlerWS(const RaftJsonIF& config,
        RaftWebSocketInboundCanAcceptFnType inboundCanAcceptCB, 
        RaftWebSocketInboundHandleMsgFnType rxMsgCB)
        :   _inboundCanAcceptCB(inboundCanAcceptCB), 
            _rxMsgCB(rxMsgCB)

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
            , _txQueue(config.getLong("txQueueMax", 10))
#endif
{
    // Websocket config
    _wsPath = config.getString("pfix", "ws");
    _wsPath = _wsPath.startsWith("/") ? _wsPath : "/" + _wsPath;
    _pktMaxBytes = config.getLong("pktMaxBytes", DEFAULT_WS_PKT_MAX_BYTES);
    _txQueueMax = config.getLong("txQueueMax", DEFAULT_WS_TX_QUEUE_MAX);
    _pingIntervalMs = config.getLong("pingMs", DEFAULT_WS_PING_MS);
    bool closeIfNoPong = config.getBool("closeIfNoPong", false);
    _noPongMs = (closeIfNoPong && (_pingIntervalMs != 0)) ? _pingIntervalMs * 2 + 2000 : 0;
    _isBinaryWS = config.getString("content", "binary").equalsIgnoreCase("binary");

    // Setup channelIDs mapping
    _maxConnections = config.getLong("maxConn", 1);
    _connectionSlots.clear();
    _connectionSlots.resize(_maxConnections);

#ifdef DEBUG_WS_OPEN_CLOSE
    // Debug
    LOG_I(MODULE_PREFIX, "RaftWebHandlerWS: wsPath %s pktMaxBytes %d txQueueMax %d pingMs %d noPongMs %d isBinary %s obj %p",
            _wsPath.c_str(), _pktMaxBytes, _txQueueMax, _pingIntervalMs, _noPongMs, _isBinaryWS ? "Y" : "N", this);
#endif
}

#if !defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getNewResponder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebResponder* RaftWebHandlerWS::getNewResponder(const RaftWebRequestHeader& requestHeader, 
            const RaftWebRequestParams& params, 
            RaftHttpStatusCode &statusCode
            )
{
    // Check if websocket request
    if (requestHeader.reqConnType != REQ_CONN_TYPE_WEBSOCKET)
        return NULL;

#ifdef DEBUG_WEB_HANDLER_WS
    LOG_I(MODULE_PREFIX, "getNewResponder: req %s this prefix %s", requestHeader.URIAndParams.c_str(), _wsPath.c_str());
#endif

    // Check for WS prefix
    if (!requestHeader.URL.equals(_wsPath))
    {
#ifdef DEBUG_WEB_HANDLER_WS        
        LOG_I(MODULE_PREFIX, "getNewResponder unmatched ws req %s != expected %s", 
                        requestHeader.URL.c_str(), _wsPath.c_str());
#endif
        // We don't change the status code here as we didn't find a match
        return NULL;
    }

    // Find a free connection slot
    int connSlotIdx = findFreeConnectionSlot();
    if (connSlotIdx < 0)
    {
        statusCode = HTTP_STATUS_SERVICEUNAVAILABLE;
        LOG_W(MODULE_PREFIX, "getNewResponder pfix %s no free connections", _wsPath.c_str());
        return NULL;
    }

    // Looks like we can handle this so create a new responder object
    uint32_t channelID = _connectionSlots[connSlotIdx].channelID;
    
#ifdef DEBUG_WEB_HANDLER_WS        
    // Log slot allocation
    LOG_I(MODULE_PREFIX, "getNewResponder allocating slot %d channelID %d wasUsed %d", 
            connSlotIdx, channelID, _connectionSlots[connSlotIdx].isUsed);
#endif
    
    RaftWebResponder* pResponder = new RaftWebResponderWS(this, params, requestHeader.URL, 
                _inboundCanAcceptCB, 
                _rxMsgCB, 
                channelID,
                _pktMaxBytes,
                _txQueueMax,
                _pingIntervalMs,
                _noPongMs,
                _isBinaryWS
                );

    if (pResponder)
    {
        statusCode = HTTP_STATUS_OK;
        _connectionSlots[connSlotIdx].isUsed = true;
    }

    // Debug
#ifdef DEBUG_WEB_HANDLER_WS
    LOG_I(MODULE_PREFIX, "getNewResponder constructed new responder %lx channelID %d uri %s", 
                    (unsigned long)pResponder, channelID, requestHeader.URL.c_str());
#endif
    // Return new responder - caller must clean up by deleting object when no longer needed
    return pResponder;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// responderDelete
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebHandlerWS::responderDelete(RaftWebResponderWS* pResponder)
{
    // Get the channelID
    uint32_t channelID = UINT32_MAX;
    if (pResponder->getChannelID(channelID))
    {
        // Find the connection slot
        int connSlotIdx = findConnectionSlotByChannelID(channelID);
        if (connSlotIdx < 0)
        {
            // Slot may have already been freed by responderInactive() - this is OK
#ifdef DEBUG_WS_OPEN_CLOSE
            LOG_I(MODULE_PREFIX, "responderDelete slot already freed channelID %d", channelID);
#endif
            return;
        }

        // Clear the connection slot
        _connectionSlots[connSlotIdx].isUsed = false;
#ifdef DEBUG_WS_OPEN_CLOSE
        LOG_I(MODULE_PREFIX, "responderDelete freed slot channelID %d connSlotIdx %d", channelID, connSlotIdx);
#endif
    }
    else
    {
        LOG_W(MODULE_PREFIX, "responderDelete FAIL NO CHANNELID responder %p channelID %d", pResponder, channelID);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// responderInactive
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebHandlerWS::responderInactive(uint32_t channelID)
{
    // Find the connection slot
    int connSlotIdx = findConnectionSlotByChannelID(channelID);
    if (connSlotIdx < 0)
    {
#ifdef WARN_ON_RESPONDER_NOT_FOUND
        LOG_W(MODULE_PREFIX, "responderInactive NOT FOUND channelID %d", channelID);
#endif
        return;
    }

    // Clear the connection slot immediately to allow reconnection with same ID
    _connectionSlots[connSlotIdx].isUsed = false;

#ifdef DEBUG_WEB_HANDLER_WS            
    LOG_I(MODULE_PREFIX, "responderInactive freed slot channelID %d connSlotIdx %d", channelID, connSlotIdx);
#endif
}

#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// handleRequest (mongoose)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebHandlerWS::handleRequest(struct mg_connection *pConn, int ev, void *ev_data)
{
    // Check for HTTP message
    if (ev == MG_EV_HTTP_MSG) 
    {
        // Mongoose http message
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        // Get request string
        String reqStr = String(hm->uri.ptr, hm->uri.len);

        // Check if this is for the right WS endpoint
        if (!reqStr.startsWith(_wsPath))
            return false;

        // Get method string
        String methodStr = String(hm->method.ptr, hm->method.len);

        // Check for an available connection slot
        int connSlotIdx = findFreeConnectionSlot();
        if (connSlotIdx < 0)
        {
            // No free connection slots
            mg_http_reply(pConn, HTTP_STATUS_SERVICEUNAVAILABLE, "", "");
            LOG_W(MODULE_PREFIX, "handleRequest FAIL pfix %s no free channels", _wsPath.c_str());

            // Return true to indicate we "handled" the request
            return true;
        }

        // Upgrade
        mg_ws_upgrade(pConn, hm, NULL);

        // Put the channel ID into the connection data field
        setChannelIDInConnInfo(pConn, _connectionSlots[connSlotIdx].channelID);
        _connectionSlots[connSlotIdx].isUsed = true;
        _connectionSlots[connSlotIdx].pConn = pConn;
        _connectionSlots[connSlotIdx].lastPingRequestTimeMs = millis();

        // Debug
#ifdef DEBUG_WS_OPEN_CLOSE        
        LOG_I(MODULE_PREFIX, "handleRequest upgraded OK reqStr %s connSlotIdx %d chID %d pConn %p", 
                        reqStr.c_str(), connSlotIdx, _connectionSlots[connSlotIdx].channelID, pConn);
#endif

        // Handled ok
        return true;
    }

    // Check for incoming WS message
    if (ev == MG_EV_WS_MSG)
    {
        // Check this WS handler is for this connection
        if (findConnectionSlotByConn(pConn) < 0)
        {
            // Not for us
            return false;
        }

        // WS message
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;

        // Check the message type
        uint8_t wsMsgType = wm->flags & 0x0F;

        // Handle text and binary messages
        if ((wsMsgType == WEBSOCKET_OP_TEXT) || (wsMsgType == WEBSOCKET_OP_BINARY))
        {
            // Get the channel ID from the connection data field
            uint32_t channelID = getChannelIDFromConnInfo(pConn);

    #ifdef DEBUG_WEB_HANDLER_WS
            // Debug
            LOG_I(MODULE_PREFIX, "handleRequest incomingMsg chID %d len %d pConn %p obj %p", channelID, wm->data.len, pConn, this);
    #endif

            // Check message callback
            if (_rxMsgCB)
            {
                // Call callback with the message
                _rxMsgCB(channelID, (const uint8_t*)wm->data.ptr, wm->data.len);
            }
            return true;
        }
    }

    // Check for WS close
    if (ev == MG_EV_CLOSE)
    {
        // Find the connection
        int connSlotIdx = findConnectionSlotByConn(pConn);
        if (connSlotIdx < 0)
        {
            // Not handled by this WS handler
            return false;
        }

        // Debug
#ifdef DEBUG_WS_OPEN_CLOSE
        uint32_t channelID = _connectionSlots[connSlotIdx].channelID;
        LOG_I(MODULE_PREFIX, "handleRequest CLOSED chID %d, pConn %p obj %p", channelID, pConn, this);
#endif

        // Slot no longer used
        _connectionSlots[connSlotIdx].isUsed = false;
        _connectionSlots[connSlotIdx].pConn = nullptr;
        return true;
    }

    // Check for POLL event
    if (ev == MG_EV_POLL)
    {
        // Use poll event for sending
        // Check for data waiting to be sent
        RaftWebDataFrame frame;
        if (_txQueue.peek(frame))
        {
            // Get the channel ID from the connection data field
            uint32_t channelID = getChannelIDFromConnInfo(pConn);

            // Check the message is for this channel
            if (channelID == frame.getChannelID())
            {
                // Send the message
                mg_ws_send(pConn, frame.getData(), frame.getLen(), _isBinaryWS ? WEBSOCKET_OP_BINARY : WEBSOCKET_OP_TEXT);

                // Debug
#ifdef DEBUG_WS_SEND_APP_DATA
                LOG_I(MODULE_PREFIX, "sendMsg OK %s chID %d len %d pConn %p obj %p", 
                                _isBinaryWS ? "BINARY" : "TEXT", channelID, frame.getLen(),  pConn, this);
#endif
                // Remove from queue
                _txQueue.get(frame);
            }
            else
            {
                // Check for timeout on message
                if (Raft::isTimeout(millis(), frame.getFrameTimeMs(), MAX_TIME_IN_QUEUE_MS))
                {
                    // Timeout - remove from queue
                    _txQueue.get(frame);
#ifdef DEBUG_WS_SEND_APP_DATA_FAIL                    
                    LOG_W(MODULE_PREFIX, "sendMsg FAILED timeout chID %d len %d pConn %p obj %p", 
                                channelID, frame.getLen(), pConn, this);
#endif
                }
            }
        }
        else
        {
            // See if a connectivity checks are due
            if (Raft::isTimeout(millis(), _lastConnCheckTimeMs, CONNECTIVITY_CHECK_INTERVAL_MS))
            {
                // Update last ping connectivity check time
                _lastConnCheckTimeMs = millis();

                // Find the connection
                int connSlotIdx = findConnectionSlotByConn(pConn);
                if (connSlotIdx < 0)
                {
                    // Not handled by this WS handler
                    return false;
                }

                // Check if a ping on this connection is due
                if (Raft::isTimeout(millis(), _connectionSlots[connSlotIdx].lastPingRequestTimeMs, _pingIntervalMs))
                {
                    // Send a ping
                    mg_ws_send(pConn, NULL, 0, WEBSOCKET_OP_PING);

                    // Debug
#ifdef DEBUG_WS_PING_PONG
                    LOG_I(MODULE_PREFIX, "sendMsg ping pConn %p obj %p", pConn, this);
#endif
                    // Update last ping time
                    _connectionSlots[connSlotIdx].lastPingRequestTimeMs = millis();
                }

                // Handled
                return true;
            }
        }
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ChannelID handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebHandlerWS::getChannelIDFromConnInfo(struct mg_connection *pConn)
{
    const uint8_t* pData = (uint8_t*)pConn->data + RAFT_MG_HTTP_DATA_CHANNEL_ID_POS;
    return Raft::getBEUint32AndInc(pData);
}

void RaftWebHandlerWS::setChannelIDInConnInfo(struct mg_connection *pConn, uint32_t channelID)
{
    Raft::setBEUint32((uint8_t*)pConn->data, RAFT_MG_HTTP_DATA_CHANNEL_ID_POS, channelID);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// canSend
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebHandlerWS::canSend(uint32_t channelID, bool& noConn)
{
    // Find the connection slot
    int connSlotIdx = findConnectionSlotByChannelID(channelID);
    if (connSlotIdx < 0)
    {
        // Not found
        noConn = true;
        return false;
    }

    // Check queue
    noConn = false;
    return _txQueue.canAcceptData();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sendMsg
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebHandlerWS::sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
{
    // Find the connection slot
    int connSlotIdx = findConnectionSlotByChannelID(channelID);
    if (connSlotIdx < 0)
        return false;

    // Add to queue - don't block if full
    RaftWebDataFrame frame(channelID, pBuf, bufLen, millis());
    bool putRslt = _txQueue.put(frame, MAX_WAIT_FOR_TX_QUEUE_MS);
    if (!putRslt)
    {
#ifdef DEBUG_WS_SEND_APP_DATA_FAIL
        LOG_W(MODULE_PREFIX, "sendMsg add to txQueue failed len %d count %d maxLen %d", bufLen, _txQueue.count(), _txQueue.maxLen());
#endif
        return false;
    }
#ifdef DEBUG_WS_SEND_APP_DATA
        LOG_I(MODULE_PREFIX, "sendMsg len %d", bufLen);
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Connection slot handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int RaftWebHandlerWS::findConnectionSlotByConn(struct mg_connection* pConn)
{
    // Find a free connection slot
    for (int i = 0; i < _connectionSlots.size(); i++)
    {
        if (_connectionSlots[i].isUsed && (_connectionSlots[i].pConn == pConn))
        {
            return i;
        }
    }
    return -1;
}

#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Connection slot handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int RaftWebHandlerWS::findFreeConnectionSlot()
{
    // Find a free connection slot
    for (int i = 0; i < _connectionSlots.size(); i++)
    {
        if (!_connectionSlots[i].isUsed)
        {
            return i;
        }
    }
    return -1;
}

int RaftWebHandlerWS::findConnectionSlotByChannelID(uint32_t channelID)
{
    // Find a free connection slot
    for (int i = 0; i < _connectionSlots.size(); i++)
    {
        if (_connectionSlots[i].isUsed && (_connectionSlots[i].channelID == channelID))
        {
            return i;
        }
    }
    return -1;
}
