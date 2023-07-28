/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftArduino.h>
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

// #define WEBSOCKET_SEND_USE_TX_QUEUE

class RaftWebResponderWS : public RaftWebResponder
{
public:
    RaftWebResponderWS(RaftWebHandlerWS* pWebHandler, const RaftWebRequestParams& params,
            const String& reqStr,
            RaftWebSocketInboundCanAcceptFnType inboundCanAcceptCB, 
            RaftWebSocketInboundHandleMsgFnType inboundMsgCB,
            uint32_t channelID, uint32_t packetMaxBytes, uint32_t txQueueSize,
            uint32_t pingIntervalMs, uint32_t disconnIfNoPongMs, bool isBinary);
    virtual ~RaftWebResponderWS();

    // Service - called frequently
    virtual void service() override final;

    // Handle inbound data
    virtual bool handleInboundData(const uint8_t* pBuf, uint32_t dataLen) override final;

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

    // Ready to send data
    virtual bool isReadyToSend() override final;

    // Send a frame of data
    virtual bool encodeAndSendData(const uint8_t* pBuf, uint32_t bufLen) override final;

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

private:
    // Handler
    RaftWebHandlerWS* _pWebHandler;

    // Params
    RaftWebRequestParams _reqParams;

    // Websocket callback
    RaftWebSocketCB _webSocketCB;

    // Websocket link
    RaftWebSocketLink _webSocketLink;

    // Is binary
    bool _isBinary = false;

    // Can accept inbound message
    RaftWebSocketInboundCanAcceptFnType _inboundCanAcceptCB;

    // Inbound message callback
    RaftWebSocketInboundHandleMsgFnType _inboundMsgCB;

    // ChannelID
    uint32_t _channelID = UINT32_MAX;

    // Vars
    String _requestStr;

#ifdef WEBSOCKET_SEND_USE_TX_QUEUE
    // Queue for sending frames over the web socket
    ThreadSafeQueue<RaftWebDataFrame> _txQueue;
    static const uint32_t MAX_WAIT_FOR_TX_QUEUE_MS = 2;
#endif

    // Max packet size
    uint32_t _packetMaxBytes = 5000;

    // Debug last service
    uint32_t _debugLastServiceMs = 0;

    // Callback on websocket activity
    void onWebSocketEvent(RaftWebSocketEventCode eventCode, const uint8_t* pBuf, uint32_t bufLen);

    // Debug
    static const uint32_t MAX_DEBUG_TEXT_STR_LEN = 100;
    static const uint32_t MAX_DEBUG_BIN_HEX_LEN = 50;    
};
