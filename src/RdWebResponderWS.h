/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef ESP8266

#include <ArduinoOrAlt.h>
#include "RdWebResponder.h"
#include <RdWebRequestParams.h>
#include <RdWebConnection.h>
#include <RdWebSocketLink.h>
#include <RdWebDataFrame.h>
#include <Logger.h>
#include <ThreadSafeQueue.h>
#include "RdWebInterface.h"

class RdWebHandlerWS;
class RaftWebServerSettings;
class ProtocolEndpointManager;

class RdWebResponderWS : public RdWebResponder
{
public:
    RdWebResponderWS(RdWebHandlerWS* pWebHandler, const RdWebRequestParams& params,
            const String& reqStr, const RaftWebServerSettings& webServerSettings,
            RdWebSocketCanAcceptCB canAcceptMsgCB, RdWebSocketMsgCB sendMsgCB,
            uint32_t channelID, uint32_t packetMaxBytes, uint32_t txQueueSize,
            uint32_t pingIntervalMs, uint32_t disconnIfNoPongMs);
    virtual ~RdWebResponderWS();

    // Service - called frequently
    virtual void service() override final;

    // Handle inbound data
    virtual bool handleData(const uint8_t* pBuf, uint32_t dataLen) override final;

    // Start responding
    virtual bool startResponding(RdWebConnection& request) override final;

    // Get response next
    virtual uint32_t getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen) override final;

    // Get content type
    virtual const char* getContentType() override final;

    // Leave connection open
    virtual bool leaveConnOpen() override final;

    // Send standard headers
    virtual bool isStdHeaderRequired() override final
    {
        return false;
    }

    // Send a frame of data
    virtual bool sendFrame(const uint8_t* pBuf, uint32_t bufLen) override final;

    // Get responder type
    virtual const char* getResponderType() override final
    {
        return "WS";
    }

    // Get channelID for responder
    virtual bool getChannelID(uint32_t& channelID)
    {
        channelID = _channelID;
        return true;
    }

    // Ready for data
    virtual bool readyForData() override final;

private:
    // Handler
    RdWebHandlerWS* _pWebHandler;

    // Params
    RdWebRequestParams _reqParams;

    // Websocket callback
    RdWebSocketCB _webSocketCB;

    // Websocket link
    RdWebSocketLink _webSocketLink;

    // Can accept message function
    RdWebSocketCanAcceptCB _canAcceptMsgCB;

    // Send message function
    RdWebSocketMsgCB _sendMsgCB;

    // ChannelID
    uint32_t _channelID = UINT32_MAX;

    // Vars
    String _requestStr;

    // Queue for sending frames over the web socket
    ThreadSafeQueue<RdWebDataFrame> _txQueue;
    static const uint32_t MAX_WAIT_FOR_TX_QUEUE_MS = 2;

    // Max packet size
    uint32_t _packetMaxBytes = 5000;

    // Callback on websocket activity
    void webSocketCallback(RdWebSocketEventCode eventCode, const uint8_t* pBuf, uint32_t bufLen);

    // Debug
    static const uint32_t MAX_DEBUG_TEXT_STR_LEN = 100;
    static const uint32_t MAX_DEBUG_BIN_HEX_LEN = 50;    
};

#endif
