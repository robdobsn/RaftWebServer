/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftUtils.h"
#include "RaftWebResponderWS.h"
#include "RaftWebConnection.h"
#include "RaftWebServerSettings.h"
#include "RaftWebInterface.h"
#include "RaftWebHandlerWS.h"

// Warn
#define WARN_WS_SEND_APP_DATA_FAIL
#define WARN_WS_PACKET_TOO_BIG
#define WARN_ON_SEND_INACTIVE

// Debug
// #define DEBUG_RESPONDER_WS
// #define DEBUG_RESPONDER_IS_READY_TO_SEND
// #define DEBUG_WS_SEND_APP_DATA
// #define DEBUG_WS_SEND_APP_DATA_DETAIL
// #define DEBUG_WEBSOCKETS_OPEN_CLOSE
// #define DEBUG_WEBSOCKETS_TRAFFIC
// #define DEBUG_WEBSOCKETS_TRAFFIC_BINARY_DETAIL
// #define DEBUG_WEBSOCKETS_PING_PONG
// #define DEBUG_WS_IS_ACTIVE
// #define DEBUG_WS_SERVICE
// #define DEBUG_IS_READY_TO_SEND_TIMING
// #define DEBUG_WEBSOCKET_CONN_STATUS
// #define DEBUG_WEBSOCKET_RESPONSE_DETAIL
// #define DEBUG_WS_TX_QUEUE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebResponderWS::RaftWebResponderWS(RaftWebHandlerWS* pWebHandler, const RaftWebRequestParams& params, 
            const String& reqStr,
            RaftWebSocketInboundCanAcceptFnType inboundCanAcceptCB, 
            RaftWebSocketInboundHandleMsgFnType inboundMsgCB,
            uint32_t channelID, uint32_t packetMaxBytes, uint32_t txQueueSize,
            uint32_t pingIntervalMs, uint32_t disconnIfNoPongMs, bool isBinary)
    :   _reqParams(params), _inboundCanAcceptCB(inboundCanAcceptCB), 
        _inboundMsgCB(inboundMsgCB)
#ifdef WEBSOCKET_SEND_USE_TX_QUEUE
        , _txQueue(txQueueSize)
#endif
{
    // Store socket info
    _pWebHandler = pWebHandler;
    _requestStr = reqStr;
    _channelID = channelID;
    _packetMaxBytes = packetMaxBytes;
    _isBinary = isBinary;

#ifdef DEBUG_RESPONDER_WS
    // Log responder creation
    LOG_I(MODULE_PREFIX, "CREATED responder this=%p connId %d channelID %d", this, params.connId, _channelID);
#endif

    // Init socket link
    _webSocketLink.setup(std::bind(&RaftWebResponderWS::onWebSocketEvent, this, 
                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                params.getWebConnRawSend(), pingIntervalMs, true, disconnIfNoPongMs, isBinary);
}

RaftWebResponderWS::~RaftWebResponderWS()
{
#ifdef DEBUG_RESPONDER_WS
    // Log responder destruction
    LOG_I(MODULE_PREFIX, "DESTROYED responder this=%p connId %d channelID %d slotFreed %d", 
            this, _reqParams.connId, _channelID, _slotFreed);
#endif
            
#if !defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    // Only call responderDelete if slot wasn't already freed
    if (_pWebHandler && !_slotFreed)
        _pWebHandler->responderDelete(this);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebResponderWS::loop()
{
    // Service the link
    _webSocketLink.loop();

#ifdef DEBUG_WS_SERVICE
    if (Raft::isTimeout(millis(), _debugLastServiceMs, 1000))
    {
        _debugLastServiceMs = millis();
#ifdef WEBSOCKET_SEND_USE_TX_QUEUE
        LOG_I(MODULE_PREFIX, "loop connStatus %d _txQueueCount %d", 
                _connStatus, _txQueue.count());
#else
        LOG_I(MODULE_PREFIX, "loop connStatus %d", _connStatus);
#endif
    }
#endif

    // Check if line active
    if (!_webSocketLink.isActive())
    {
#ifdef DEBUG_WS_IS_ACTIVE
        LOG_I(MODULE_PREFIX, "loop INACTIVE link");
#endif
        _connStatus = CONN_INACTIVE;
        // Immediately free the connection slot to allow reconnection
        if (!_slotFreed)
        {
            _pWebHandler->responderInactive(_channelID);
            _slotFreed = true;
        }
        return;
    }

#ifdef WEBSOCKET_SEND_USE_TX_QUEUE
    // Check for data waiting to be sent
    RaftWebDataFrame frame;
    if (_txQueue.get(frame))
    {
#ifdef DEBUG_WS_TX_QUEUE
        // Always log data being sent for debugging reconnection issues
        LOG_I(MODULE_PREFIX, "loop connId %d about to send %d bytes from txQueue", _reqParams.connId, frame.getLen());
#endif

        // Send
        RaftWebConnSendRetVal retVal = _webSocketLink.sendMsg(_webSocketLink.msgOpCodeDefault(), frame.getData(), frame.getLen());

#ifdef DEBUG_WS_SEND_APP_DATA
        LOG_W(MODULE_PREFIX, "loop connId %d sendMsg sent len %d retc %d",
                    _reqParams.connId, frame.getLen(), retVal);
#endif

        // Check result
        if (retVal == WEB_CONN_SEND_FAIL)
        {
            _connStatus = CONN_INACTIVE;
            // Immediately free the connection slot to allow reconnection
            if (!_slotFreed)
            {
                _pWebHandler->responderInactive(_channelID);
                _slotFreed = true;
            }
#ifdef DEBUG_WS_IS_ACTIVE
            LOG_I(MODULE_PREFIX, "loop connId %d INACTIVE sendMsg failed", _reqParams.connId);
#endif
            // Immediately stop processing this dead connection
            return;
        }
        else if (retVal != WEB_CONN_SEND_OK)
        {
#ifdef DEBUG_WS_SEND_APP_DATA
            LOG_W(MODULE_PREFIX, "loop connId %d send msg failed retVal %s",
                    _reqParams.connId, RaftWebConnDefs::getSendRetValStr(retVal));      
#endif
        }
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle inbound data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::handleInboundData(const uint8_t* pBuf, uint32_t dataLen)
{
#ifdef DEBUG_RESPONDER_WS
#ifdef DEBUG_WS_SEND_APP_DATA_DETAIL
    if (_isBinary)
    {
        String outStr;
        Raft::getHexStrFromBytes(pBuf, dataLen < MAX_DEBUG_BIN_HEX_LEN ? dataLen : MAX_DEBUG_BIN_HEX_LEN, outStr);
        LOG_I(MODULE_PREFIX, "handleInboundData connId %d len %d %s%s",
                    _reqParams.connId, dataLen, outStr.c_str(),
                    dataLen < MAX_DEBUG_BIN_HEX_LEN ? "" : " ...");
    }
    else
    {
        String outStr(pBuf, dataLen < MAX_DEBUG_TEXT_STR_LEN ? dataLen : MAX_DEBUG_TEXT_STR_LEN);
        LOG_I(MODULE_PREFIX, "handleInboundData connId %d len %d %s%s",
                    _reqParams.connId, dataLen, outStr.c_str(),
                    dataLen < MAX_DEBUG_TEXT_STR_LEN ? "" : " ...");
    }
#else
    LOG_I(MODULE_PREFIX, "handleInboundData connId %d len %d", _reqParams.connId, dataLen);
#endif
#endif

    // Handle it with link
    _webSocketLink.handleRxData(pBuf, dataLen);

    // Check if the link is still active
    if (!_webSocketLink.isActive())
    {
#ifdef DEBUG_WS_IS_ACTIVE
        LOG_I(MODULE_PREFIX, "handleInboundData connId %d INACTIVE link", _reqParams.connId);
#endif
        _connStatus = CONN_INACTIVE;
        // Immediately free the connection slot to allow reconnection
        if (!_slotFreed)
        {
            _pWebHandler->responderInactive(_channelID);
            _slotFreed = true;
        }
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start responding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::startResponding(RaftWebConnection& request)
{
#ifdef DEBUG_WEBSOCKET_CONN_STATUS
    LOG_I(MODULE_PREFIX, "startResponding connId %d wsKey %s wsVer %s", 
            _reqParams.connId, 
            request.getHeader().webSocketKey.c_str(),
            request.getHeader().webSocketVersion.c_str());
#endif
    
    // Set link to upgrade-request already received state
    _webSocketLink.upgradeReceived(request.getHeader().webSocketKey, 
                        request.getHeader().webSocketVersion);

    // Set to CONNECTING status - waiting for handshake to complete
    // This allows the connection handler to service the responder
    // but prevents application data from being sent on the channel
    _connStatus = CONN_CONNECTING;
#ifdef DEBUG_WEBSOCKET_CONN_STATUS
    LOG_I(MODULE_PREFIX, "startResponding connId %d status=CONN_CONNECTING - waiting for upgrade response", _reqParams.connId);
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get connection status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnStatus RaftWebResponderWS::getConnStatus()
{
    // Return current connection status
    // CONN_INACTIVE: Not connected, can be cleaned up
    // CONN_CONNECTING: Handshake in progress, connection handler should service but no app data
    // CONN_ACTIVE: Fully connected, can send/receive application data
#ifdef DEBUG_WEBSOCKET_CONN_STATUS
    LOG_I(MODULE_PREFIX, "getConnStatus connId %d status=%d", 
            _reqParams.connId, _connStatus);
#endif
    return _connStatus;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if any reponse data is available
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::responseAvailable()
{
    // Check if upgrade response needs to be sent
    bool txAvail = _webSocketLink.isTxDataAvailable();
    
    // Return true if upgrade response is pending (when status is CONN_CONNECTING)
    // Once upgrade is sent, we'll be CONN_ACTIVE and txAvail becomes false
    // Application data flows through the channel send mechanism, not through responseAvailable
    bool result = (_connStatus == CONN_CONNECTING) && txAvail;
#ifdef DEBUG_WEBSOCKET_RESPONSE_DETAIL
    LOG_I(MODULE_PREFIX, "responseAvailable connId %d status=%d txAvail=%d result=%d", 
            _reqParams.connId, _connStatus, txAvail, result);
#endif
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get response next
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebResponderWS::getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen)
{
#ifdef DEBUG_WEBSOCKET_RESPONSE_DETAIL
    // Debug: Always log that we're being called
    LOG_I(MODULE_PREFIX, "getResponseNext connId %d CALLED bufMaxLen=%d status=%d", 
            _reqParams.connId, bufMaxLen, _connStatus);
#endif
    
    // Get from the WSLink
    uint32_t respLen = _webSocketLink.getTxData(pBuf, bufMaxLen);

    // If upgrade response was sent, transition from CONNECTING to ACTIVE
    if (respLen > 0 && _connStatus == CONN_CONNECTING)
    {
        _connStatus = CONN_ACTIVE;
#ifdef DEBUG_WEBSOCKET_CONN_STATUS
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d NOW ACTIVE (status=CONN_ACTIVE) after sending upgrade response", _reqParams.connId);
#endif
    }

#ifdef DEBUG_WEBSOCKET_RESPONSE_DETAIL
    // Always log handshake response for debugging
    if (respLen > 0)
    {
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d sending %d bytes", _reqParams.connId, respLen);
    }
    else
    {
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d NO DATA to send", _reqParams.connId);
    }
#endif

    // Done response
#ifdef DEBUG_RESPONDER_WS
    if (respLen > 0) {
#ifdef DEBUG_WS_SEND_APP_DATA_DETAIL
    if (_isBinary)
    {
        String outStr;
        Raft::getHexStrFromBytes(pBuf, respLen < MAX_DEBUG_BIN_HEX_LEN ? respLen : MAX_DEBUG_BIN_HEX_LEN, outStr);
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d connStatus %d len %d %s%s",
                    _reqParams.connId, _connStatus, respLen, outStr.c_str(),
                    respLen < MAX_DEBUG_BIN_HEX_LEN ? "" : " ...");
    }
    else
    {
        String outStr(pBuf, respLen < MAX_DEBUG_TEXT_STR_LEN ? respLen : MAX_DEBUG_TEXT_STR_LEN);
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d connStatus %d len %d %s%s",
                    _reqParams.connId, _connStatus, respLen, outStr.c_str(),
                    respLen < MAX_DEBUG_TEXT_STR_LEN ? "" : " ...");
    }
#else
    LOG_I(MODULE_PREFIX, "getResponseNext respLen %d connStatus %d", respLen, _connStatus);
#endif
    }
#endif
    return respLen;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get content type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* RaftWebResponderWS::getContentType()
{
    return "application/octet-stream";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Leave connection open
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::leaveConnOpen()
{
    // Keep connection open unless we've gone inactive and freed the slot
    // (which means the WebSocket has genuinely closed, not just temporarily idle)
    return !_slotFreed;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ready to send data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::isReadyToSend()
{
#ifdef DEBUG_IS_READY_TO_SEND_TIMING
    uint64_t startUs = micros();
#endif

    bool isReady = _webSocketLink.isActiveAndUpgraded();
    
#ifdef DEBUG_RESPONDER_IS_READY_TO_SEND
    // Always log to debug reconnection issue
    LOG_I(MODULE_PREFIX, "isReadyToSend connId %d isReady=%d status=%d", 
            _reqParams.connId, isReady, _connStatus);
#endif

#ifdef DEBUG_IS_READY_TO_SEND_TIMING
    uint64_t afterLinkCheckUs = micros();
#endif

    if (isReady)
        isReady = _reqParams.getWebConnReadyToSend() ? _reqParams.getWebConnReadyToSend()() == WEB_CONN_SEND_OK : true;

#ifdef DEBUG_IS_READY_TO_SEND_TIMING
    uint64_t endUs = micros();
    uint32_t totalUs = endUs - startUs;
    uint32_t connCheckUs = endUs - afterLinkCheckUs;
    if (totalUs > 1000) // Log if > 1ms
    {
        LOG_I(MODULE_PREFIX, "isReadyToSend connId %d totalUs %d linkCheckUs %d connCheckUs %d result %d",
                    _reqParams.connId, totalUs, (uint32_t)(afterLinkCheckUs - startUs), connCheckUs, isReady);
    }
#endif

#ifdef DEBUG_RESPONDER_IS_READY_TO_SEND
    LOG_I(MODULE_PREFIX, "isReadyToSend connId %d %s",
                _reqParams.connId, isReady ? "READY" : "NOT READY");
#endif
    return isReady;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Encode and send data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::encodeAndSendData(const uint8_t* pBuf, uint32_t bufLen)
{
#ifdef DEBUG_WS_SEND_APP_DATA
    LOG_I(MODULE_PREFIX, "encodeAndSendData connId %d len=%d status=%d", 
            _reqParams.connId, bufLen, _connStatus);
#endif
    
    // CRITICAL: Check if connection is ready (handshake must be complete)
    // This prevents data from being sent during WebSocket upgrade
    if (_connStatus != CONN_ACTIVE)
    {
#ifdef WARN_ON_SEND_INACTIVE
        LOG_W(MODULE_PREFIX, "encodeAndSendData connId %d REJECTED - not ACTIVE (status=%d)",
                _reqParams.connId, _connStatus);
#endif
        return false;
    }
    
#ifdef WEBSOCKET_SEND_USE_TX_QUEUE
    // Check packet size limit
    if (bufLen > _packetMaxBytes)
    {
#ifdef WARN_WS_PACKET_TOO_BIG
        LOG_W(MODULE_PREFIX, "encodeAndSendData connId %d TOO BIG len %d maxLen %d", 
                    _reqParams.connId, bufLen, _packetMaxBytes);
#endif
        return false;
    }
    // Add to queue - don't block if full
    RaftWebDataFrame frame(_channelID, pBuf, bufLen, millis());
    bool putRslt = _txQueue.put(frame, MAX_WAIT_FOR_TX_QUEUE_MS);
    if (!putRslt)
    {
#ifdef WARN_WS_SEND_APP_DATA_FAIL
        LOG_W(MODULE_PREFIX, "encodeAndSendData connId %d add to txQueue failed len %d count %d maxLen %d",
                    _reqParams.connId, bufLen, _txQueue.count(), _txQueue.maxLen());
#endif
    }
    else
    {
#ifdef DEBUG_WS_SEND_APP_DATA
        LOG_W(MODULE_PREFIX, "encodeAndSendData connId %d len %d",
                    _reqParams.connId, bufLen);
#endif
    }
    return putRslt;
#else
    // Send
    RaftWebConnSendRetVal retVal = _webSocketLink.sendMsg(_webSocketLink.msgOpCodeDefault(), pBuf, bufLen);

#ifdef DEBUG_WS_SEND_APP_DATA
    LOG_W(MODULE_PREFIX, "encodeAndSendData connId %d sent len %d retc %d",
                _reqParams.connId, bufLen, retVal);
#endif

    // Check result
    if (retVal == WEB_CONN_SEND_FAIL)
    {
        _connStatus = CONN_INACTIVE;
        // Immediately free the connection slot to allow reconnection
        if (!_slotFreed)
        {
            _pWebHandler->responderInactive(_channelID);
            _slotFreed = true;
        }
#ifdef DEBUG_WS_IS_ACTIVE
        LOG_I(MODULE_PREFIX, "encodeAndSendData connId %d failed INACTIVE - stopping immediately", _reqParams.connId);
#endif
        // Immediately return false to signal connection is dead
        return false;
    }
    else if (retVal != WEB_CONN_SEND_OK)
    {
#ifdef DEBUG_WS_SEND_APP_DATA
        LOG_W(MODULE_PREFIX, "encodeAndSendData connId %d failed retVal %s",
                _reqParams.connId, RaftWebConnDefs::getSendRetValStr(retVal));      
#endif
    }
    return retVal == WEB_CONN_SEND_OK;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Websocket callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebResponderWS::onWebSocketEvent(RaftWebSocketEventCode eventCode, const uint8_t* pBuf, uint32_t bufLen)
{
#ifdef DEBUG_WEBSOCKETS
	const static char* MODULE_PREFIX = "wsCB";
#endif
	switch(eventCode) 
    {
		case WEBSOCKET_EVENT_CONNECT:
        {
#ifdef DEBUG_WEBSOCKETS_OPEN_CLOSE
			LOG_I(MODULE_PREFIX, "onWebSocketEvent connId %d connected!", _reqParams.connId);
#endif
			break;
        }
		case WEBSOCKET_EVENT_DISCONNECT_EXTERNAL:
        {
#ifdef DEBUG_WEBSOCKETS_OPEN_CLOSE
			LOG_I(MODULE_PREFIX, "onWebSocketEvent connId %d sent disconnect", _reqParams.connId);
#endif
			break;
        }
		case WEBSOCKET_EVENT_DISCONNECT_INTERNAL:
        {
#ifdef DEBUG_WEBSOCKETS_OPEN_CLOSE
			LOG_I(MODULE_PREFIX, "onWebSocketEvent connId %d disconnect", _reqParams.connId);
#endif
			break;
        }
		case WEBSOCKET_EVENT_DISCONNECT_ERROR:
        {
#ifdef DEBUG_WEBSOCKETS_OPEN_CLOSE
			LOG_I(MODULE_PREFIX, "onWebSocketEvent connId %d disconnect due to error", _reqParams.connId);
#endif
			break;
        }
		case WEBSOCKET_EVENT_TEXT:
        {
            // Handle the inbound message
            if (_inboundMsgCB && (pBuf != NULL))
                _inboundMsgCB(_channelID, (uint8_t*) pBuf, bufLen);
#ifdef DEBUG_WEBSOCKETS_TRAFFIC
            String msgText;
            if (pBuf)
                msgText = String(pBuf, bufLen);
			LOG_I(MODULE_PREFIX, "onWebSocketEvent connId %d rx text len %i content %s",
                    _reqParams.connId, bufLen, msgText.c_str());
#endif
			break;
        }
		case WEBSOCKET_EVENT_BINARY:
        {
            // Handle the inbound message
            if (_inboundMsgCB && (pBuf != NULL))
                _inboundMsgCB(_channelID, (uint8_t*) pBuf, bufLen);
#ifdef DEBUG_WEBSOCKETS_TRAFFIC
			LOG_I(MODULE_PREFIX, "onWebSocketEvent rx binary len %i", bufLen);
#endif
#ifdef DEBUG_WEBSOCKETS_TRAFFIC_BINARY_DETAIL
            String rxDataStr;
            Raft::getHexStrFromBytes(pBuf, bufLen < MAX_DEBUG_BIN_HEX_LEN ? bufLen : MAX_DEBUG_BIN_HEX_LEN, rxDataStr);
			LOG_I(MODULE_PREFIX, "onWebSocketEvent connId %d rx binary len %s%s",
                    _reqParams.connId,
                    rxDataStr.c_str(),
                    bufLen < MAX_DEBUG_BIN_HEX_LEN ? "" : "...");
#endif
			break;
        }
		case WEBSOCKET_EVENT_PING:
        {
#ifdef DEBUG_WEBSOCKETS_PING_PONG
			LOG_I(MODULE_PREFIX, "onWebSocketEvent connId %d rx ping len %i", 
                    _reqParams.connId, bufLen);
#endif
			break;
        }
		case WEBSOCKET_EVENT_PONG:
        {
#ifdef DEBUG_WEBSOCKETS_PING_PONG
			LOG_I(MODULE_PREFIX, "onWebSocketEvent connId %d rx pong", _reqParams.connId);
#endif
		    break;
        }
        default:
        {
            break;
        }
	}
}
