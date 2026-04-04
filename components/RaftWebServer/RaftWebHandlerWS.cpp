/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebHandlerWS.h"
#include "RaftUtils.h"
#include "RaftWebConnManager.h"

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
    _maxConnections = config.getLong("maxConn", 5);
    _connectionSlots.clear();
    _connectionSlots.resize(_maxConnections);

#ifdef DEBUG_WS_OPEN_CLOSE
    // Debug
    LOG_I(MODULE_PREFIX, "RaftWebHandlerWS: wsPath %s pktMaxBytes %d txQueueMax %d pingMs %d noPongMs %d isBinary %s obj %p",
            _wsPath.c_str(), _pktMaxBytes, _txQueueMax, _pingIntervalMs, _noPongMs, _isBinaryWS ? "Y" : "N", this);
#endif
}

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
