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
#include "RaftWebResponder.h"
#include <RaftWebRequestParams.h>
#include <RaftWebConnection.h>
#include <RaftWebSocketLink.h>
#include <RaftWebDataFrame.h>
#include <Logger.h>
#include <ThreadSafeQueue.h>
#include "RaftWebInterface.h"

class RaftWebHandlerWS;
class RaftWebServerSettings;
class ProtocolEndpointManager;

class RaftWebResponderWS : public RaftWebResponder
{
public:
    RaftWebResponderWS(RaftWebHandlerWS* pWebHandler, const RaftWebRequestParams& params,
            const String& reqStr,
            RaftWebSocketCanAcceptCB canAcceptMsgCB, RaftWebSocketMsgCB sendMsgCB,
            uint32_t channelID, uint32_t packetMaxBytes, uint32_t txQueueSize,
            uint32_t pingIntervalMs, uint32_t disconnIfNoPongMs, const String& contentType);
    virtual ~RaftWebResponderWS();

    // Service - called frequently
    virtual void service() override final;

    // Handle inbound data
    virtual bool handleData(const uint8_t* pBuf, uint32_t dataLen) override final;

    // Start responding
    virtual bool startResponding(RaftWebConnection& request) override final;

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
    RaftWebHandlerWS* _pWebHandler;

    // Params
    RaftWebRequestParams _reqParams;

    // Websocket callback
    RaftWebSocketCB _webSocketCB;

    // Websocket link
    RaftWebSocketLink _webSocketLink;

    // Can accept message function
    RaftWebSocketCanAcceptCB _canAcceptMsgCB;

    // Send message function
    RaftWebSocketMsgCB _sendMsgCB;

    // ChannelID
    uint32_t _channelID = UINT32_MAX;

    // Vars
    String _requestStr;

    // Queue for sending frames over the web socket
    ThreadSafeQueue<RaftWebDataFrame> _txQueue;
    static const uint32_t MAX_WAIT_FOR_TX_QUEUE_MS = 2;

    // Max packet size
    uint32_t _packetMaxBytes = 5000;

    // Callback on websocket activity
    void webSocketCallback(RaftWebSocketEventCode eventCode, const uint8_t* pBuf, uint32_t bufLen);

    // Debug
    static const uint32_t MAX_DEBUG_TEXT_STR_LEN = 100;
    static const uint32_t MAX_DEBUG_BIN_HEX_LEN = 50;    
};

#endif
