/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ESP8266

#include "RdWebResponderWS.h"
#include <RdWebConnection.h>
#include "RaftWebServerSettings.h"
#include "RdWebInterface.h"
#include "RdWebHandlerWS.h"
#include <Logger.h>
#include <Utils.h>

// Warn
#define WARN_WS_SEND_APP_DATA_FAIL
#define WARN_WS_PACKET_TOO_BIG

// Debug
// #define DEBUG_RESPONDER_WS
// #define DEBUG_WS_SEND_APP_DATA
// #define DEBUG_WS_SEND_APP_DATA_ASCII
// #define DEBUG_WEBSOCKETS_OPEN_CLOSE
// #define DEBUG_WEBSOCKETS_TRAFFIC
// #define DEBUG_WEBSOCKETS_TRAFFIC_BINARY_DETAIL
// #define DEBUG_WEBSOCKETS_PING_PONG

#if defined(WARN_WS_SEND_APP_DATA_FAIL)
static const char *MODULE_PREFIX = "RdWebRespWS";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RdWebResponderWS::RdWebResponderWS(RdWebHandlerWS* pWebHandler, const RdWebRequestParams& params, 
            const String& reqStr, const RaftWebServerSettings& webServerSettings,
            RdWebSocketCanAcceptCB canAcceptMsgCB, RdWebSocketMsgCB sendMsgCB,
            uint32_t channelID, uint32_t packetMaxBytes, uint32_t txQueueSize,
            uint32_t pingIntervalMs, uint32_t disconnIfNoPongMs)
    :   _reqParams(params), _canAcceptMsgCB(canAcceptMsgCB), 
        _sendMsgCB(sendMsgCB), _txQueue(txQueueSize)
{
    // Store socket info
    _pWebHandler = pWebHandler;
    _requestStr = reqStr;
    _channelID = channelID;
    _packetMaxBytes = packetMaxBytes;

    // Init socket link
    _webSocketLink.setup(std::bind(&RdWebResponderWS::webSocketCallback, this, 
                            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                params.getWebConnRawSend(), pingIntervalMs, true, disconnIfNoPongMs);
}

RdWebResponderWS::~RdWebResponderWS()
{
    if (_pWebHandler)
        _pWebHandler->responderDelete(this);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RdWebResponderWS::service()
{
    // Service the link
    _webSocketLink.service();

    // Check if line active
    if (!_webSocketLink.isActive())
    {
        _isActive = false;
        return;
    }

    // Check for data waiting to be sent
    RdWebDataFrame frame;
    if (_txQueue.get(frame))
    {
#ifdef DEBUG_WS_SEND_APP_DATA
        LOG_W(MODULE_PREFIX, "service sendMsg len %d", frame.getLen());
#endif
        // Send
        if (!_webSocketLink.sendMsg(WEBSOCKET_OPCODE_BINARY, frame.getData(), frame.getLen()))
            _isActive = false;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle inbound data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebResponderWS::handleData(const uint8_t* pBuf, uint32_t dataLen)
{
#ifdef DEBUG_RESPONDER_WS
#ifdef DEBUG_WS_SEND_APP_DATA_ASCII
    String outStr;
    Utils::strFromBuffer(pBuf, dataLen < MAX_DEBUG_TEXT_STR_LEN ? dataLen : MAX_DEBUG_TEXT_STR_LEN, outStr, false);
    LOG_I(MODULE_PREFIX, "handleData len %d %s%s", dataLen, outStr.c_str(),
                dataLen < MAX_DEBUG_TEXT_STR_LEN ? "" : " ...");
#else
    LOG_I(MODULE_PREFIX, "handleData len %d", dataLen);
#endif
#endif

    // Handle it with link
    _webSocketLink.handleRxData(pBuf, dataLen);

    // Check if the link is still active
    if (!_webSocketLink.isActive())
    {
        _isActive = false;
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ready for data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebResponderWS::readyForData()
{
    if (_canAcceptMsgCB)
        return _canAcceptMsgCB(_channelID);
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start responding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebResponderWS::startResponding(RdWebConnection& request)
{
    // Set link to upgrade-request already received state
    _webSocketLink.upgradeReceived(request.getHeader().webSocketKey, 
                        request.getHeader().webSocketVersion);

    // Now active
    _isActive = true;
#ifdef DEBUG_RESPONDER_WS
    LOG_I(MODULE_PREFIX, "startResponding isActive %d", _isActive);
#endif
    return _isActive;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get response next
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RdWebResponderWS::getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen)
{
    // Get from the WSLink
    uint32_t respLen = _webSocketLink.getTxData(pBuf, bufMaxLen);

    // Done response
#ifdef DEBUG_RESPONDER_WS
    if (respLen > 0) {
#ifdef DEBUG_WS_SEND_APP_DATA_ASCII
    String outStr;
    Utils::strFromBuffer(pBuf, respLen, outStr, false);
    LOG_I(MODULE_PREFIX, "getResponseNext respLen %d isActive %d %s", respLen, _isActive, outStr.c_str());
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

const char* RdWebResponderWS::getContentType()
{
    return "application/octet-stream";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Leave connection open
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebResponderWS::leaveConnOpen()
{
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Send a frame of data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdWebResponderWS::sendFrame(const uint8_t* pBuf, uint32_t bufLen)
{
    // Check packet size limit
    if (bufLen > _packetMaxBytes)
    {
#ifdef WARN_WS_PACKET_TOO_BIG
        LOG_W(MODULE_PREFIX, "sendFrame TOO BIG len %d maxLen %d", bufLen, _packetMaxBytes);
#endif
        return false;
    }
    // Add to queue - don't block if full
    RdWebDataFrame frame(pBuf, bufLen);
    bool putRslt = _txQueue.put(frame, MAX_WAIT_FOR_TX_QUEUE_MS);
    if (!putRslt)
    {
#ifdef WARN_WS_SEND_APP_DATA_FAIL
        LOG_W(MODULE_PREFIX, "sendFrame add to txQueue failed len %d count %d maxLen %d", bufLen, _txQueue.count(), _txQueue.maxLen());
#endif
    }
    else
    {
#ifdef DEBUG_WS_SEND_APP_DATA
        LOG_W(MODULE_PREFIX, "sendFrame len %d", bufLen);
#endif
    }
    return putRslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Websocket callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RdWebResponderWS::webSocketCallback(RdWebSocketEventCode eventCode, const uint8_t* pBuf, uint32_t bufLen)
{
#ifdef DEBUG_WEBSOCKETS
	const static char* MODULE_PREFIX = "wsCB";
#endif
	switch(eventCode) 
    {
		case WEBSOCKET_EVENT_CONNECT:
        {
#ifdef DEBUG_WEBSOCKETS_OPEN_CLOSE
			LOG_I(MODULE_PREFIX, "webSocketCallback connected!");
#endif
			break;
        }
		case WEBSOCKET_EVENT_DISCONNECT_EXTERNAL:
        {
#ifdef DEBUG_WEBSOCKETS_OPEN_CLOSE
			LOG_I(MODULE_PREFIX, "webSocketCallback sent disconnect");
#endif
			break;
        }
		case WEBSOCKET_EVENT_DISCONNECT_INTERNAL:
        {
#ifdef DEBUG_WEBSOCKETS_OPEN_CLOSE
			LOG_I(MODULE_PREFIX, "webSocketCallback was disconnected");
#endif
			break;
        }
		case WEBSOCKET_EVENT_DISCONNECT_ERROR:
        {
#ifdef DEBUG_WEBSOCKETS_OPEN_CLOSE
			LOG_I(MODULE_PREFIX, "webSocketCallback was disconnected due to an error");
#endif
			break;
        }
		case WEBSOCKET_EVENT_TEXT:
        {
            // Send the message
            if (_sendMsgCB && (pBuf != NULL))
                _sendMsgCB(_channelID, (uint8_t*) pBuf, bufLen);
#ifdef DEBUG_WEBSOCKETS_TRAFFIC
            String msgText;
            if (pBuf)
                Utils::strFromBuffer(pBuf, bufLen, msgText);
			LOG_I(MODULE_PREFIX, "webSocketCallback rx text len %i content %s", bufLen, msgText.c_str());
#endif
			break;
        }
		case WEBSOCKET_EVENT_BINARY:
        {
            // Send the message
            if (_sendMsgCB && (pBuf != NULL))
                _sendMsgCB(_channelID, (uint8_t*) pBuf, bufLen);
#ifdef DEBUG_WEBSOCKETS_TRAFFIC
			LOG_I(MODULE_PREFIX, "webSocketCallback rx binary len %i", bufLen);
#endif
#ifdef DEBUG_WEBSOCKETS_TRAFFIC_BINARY_DETAIL
            String rxDataStr;
            Utils::getHexStrFromBytes(pBuf, bufLen < MAX_DEBUG_BIN_HEX_LEN ? bufLen : MAX_DEBUG_BIN_HEX_LEN, rxDataStr);
			LOG_I(MODULE_PREFIX, "webSocketCallback rx binary len %s%s", rxDataStr.c_str(),
                    bufLen < MAX_DEBUG_BIN_HEX_LEN ? "" : "...");
#endif
			break;
        }
		case WEBSOCKET_EVENT_PING:
        {
#ifdef DEBUG_WEBSOCKETS_PING_PONG
			LOG_I(MODULE_PREFIX, "webSocketCallback rx ping len %i", bufLen);
#endif
			break;
        }
		case WEBSOCKET_EVENT_PONG:
        {
#ifdef DEBUG_WEBSOCKETS_PING_PONG
			LOG_I(MODULE_PREFIX, "webSocketCallback sent pong");
#endif
		    break;
        }
        default:
        {
            break;
        }
	}
}

#endif
