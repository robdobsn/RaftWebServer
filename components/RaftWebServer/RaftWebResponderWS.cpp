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
// #define DEBUG_WEBSOCKET_ANY 

#ifdef DEBUG_WEBSOCKET_ANY
static const char *MODULE_PREFIX = "RaftWebRespWS";
#endif

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

    // Init socket link
    _webSocketLink.setup(std::bind(&RaftWebResponderWS::onWebSocketEvent, this, 
                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                params.getWebConnRawSend(), pingIntervalMs, true, disconnIfNoPongMs, isBinary);
}

RaftWebResponderWS::~RaftWebResponderWS()
{
#if !defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    if (_pWebHandler)
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
        LOG_I(MODULE_PREFIX, "loop isActive %d _txQueueCount %d", 
                _isActive, _txQueue.count());
#else
        LOG_I(MODULE_PREFIX, "loop isActive %d", _isActive);
#endif
    }
#endif

    // Check if line active
    if (!_webSocketLink.isActive())
    {
#ifdef DEBUG_WS_IS_ACTIVE
        LOG_I(MODULE_PREFIX, "loop INACTIVE link");
#endif
        _isActive = false;
        return;
    }

#ifdef WEBSOCKET_SEND_USE_TX_QUEUE
    // Check for data waiting to be sent
    RaftWebDataFrame frame;
    if (_txQueue.get(frame))
    {
        // Send
        RaftWebConnSendRetVal retVal = _webSocketLink.sendMsg(_webSocketLink.msgOpCodeDefault(), frame.getData(), frame.getLen());

#ifdef DEBUG_WS_SEND_APP_DATA
        LOG_W(MODULE_PREFIX, "loop connId %d sendMsg sent len %d retc %d",
                    _reqParams.connId, frame.getLen(), retVal);
#endif

        // Check result
        if (retVal == WEB_CONN_SEND_FAIL)
        {
            _isActive = false;
#ifdef DEBUG_WS_IS_ACTIVE
            LOG_I(MODULE_PREFIX, "loop connId %d INACTIVE sendMsg failed", _reqParams.connId);
#endif
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
        _isActive = false;
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start responding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::startResponding(RaftWebConnection& request)
{
    // Set link to upgrade-request already received state
    _webSocketLink.upgradeReceived(request.getHeader().webSocketKey, 
                        request.getHeader().webSocketVersion);

    // Now active
    _isActive = true;
#if defined(DEBUG_RESPONDER_WS) || defined(DEBUG_WS_IS_ACTIVE)
    LOG_I(MODULE_PREFIX, "startResponding connId %d ISACTIVE", _reqParams.connId);
#endif
    return _isActive;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if any reponse data is available
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::responseAvailable()
{
    return _isActive && _webSocketLink.isTxDataAvailable();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get response next
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebResponderWS::getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen)
{
    // Get from the WSLink
    uint32_t respLen = _webSocketLink.getTxData(pBuf, bufMaxLen);

    // Done response
#ifdef DEBUG_RESPONDER_WS
    if (respLen > 0) {
#ifdef DEBUG_WS_SEND_APP_DATA_DETAIL
    if (_isBinary)
    {
        String outStr;
        Raft::getHexStrFromBytes(pBuf, respLen < MAX_DEBUG_BIN_HEX_LEN ? respLen : MAX_DEBUG_BIN_HEX_LEN, outStr);
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d isActive %d len %d %s%s",
                    _reqParams.connId, _isActive, respLen, outStr.c_str(),
                    respLen < MAX_DEBUG_BIN_HEX_LEN ? "" : " ...");
    }
    else
    {
        String outStr(pBuf, respLen < MAX_DEBUG_TEXT_STR_LEN ? respLen : MAX_DEBUG_TEXT_STR_LEN);
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d isActive %d len %d %s%s",
                    _reqParams.connId, _isActive, respLen, outStr.c_str(),
                    respLen < MAX_DEBUG_TEXT_STR_LEN ? "" : " ...");
    }
#else
    LOG_I(MODULE_PREFIX, "getResponseNext respLen %d isActive %d", respLen, _isActive);
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
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ready to send data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderWS::isReadyToSend()
{
    bool isReady = _webSocketLink.isActiveAndUpgraded();
    if (isReady)
        isReady = _reqParams.getWebConnReadyToSend() ? _reqParams.getWebConnReadyToSend()() == WEB_CONN_SEND_OK : true;
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
        _isActive = false;
#ifdef DEBUG_WS_IS_ACTIVE
        LOG_I(MODULE_PREFIX, "encodeAndSendData connId %d failed INACTIVE", _reqParams.connId);
#endif
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
