/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebHandler.h"
#include <Logger.h>
#include <RaftWebRequestHeader.h>
#include <RaftWebResponderWS.h>
#include <ConfigBase.h>
#include <vector>
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <mongoose.h>
#include <ThreadSafeQueue.h>
#endif

class RaftWebHandlerWS : public RaftWebHandler
{
public:
    RaftWebHandlerWS(const ConfigBase& config,
            RaftWebSocketCanAcceptCB canAcceptRxMsgCB, RaftWebSocketMsgCB rxMsgCB)
            :   _canAcceptRxMsgCB(canAcceptRxMsgCB), 
                _rxMsgCB(rxMsgCB)

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
                , _txQueue(config.getLong("txQueueMax", 10))
#endif
    {
        // Store config
        _wsConfig = config;

        // Setup channelIDs mapping
        uint32_t maxConn = _wsConfig.getLong("maxConn", 1);
        _channelIDUsage.clear();
        _channelIDUsage.resize(maxConn);
    }
    virtual ~RaftWebHandlerWS()
    {
    }
    virtual bool isWebSocketHandler() const override final
    {
        return true;
    }
    virtual const char* getName() const override
    {
        return "HandlerWS";
    }

    // Setup websocket channel ID
    void setupWebSocketChannelID(uint32_t wsConnIdx, uint32_t chanID)
    {
        // Check valid
        if (wsConnIdx >= _channelIDUsage.size())
            return;
        _channelIDUsage[wsConnIdx].channelID = chanID;
        _channelIDUsage[wsConnIdx].isUsed = false;
    }

#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, 
                RaftHttpStatusCode &statusCode
                ) override final;

    void responderDelete(RaftWebResponderWS* pResponder);

#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

    // Handle request
    virtual bool handleRequest(struct mg_connection *pConn, int ev, void *ev_data) override final;
    // Check is a message can be sent
    virtual bool canSend(uint32_t& channelID, bool& noConn) override final;
    // Send message (on a channel)
    virtual bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID) override final;

#endif

private:
    // Config
    ConfigBase _wsConfig;

    // WS interface functions
    RaftWebSocketCanAcceptCB _canAcceptRxMsgCB;
    RaftWebSocketMsgCB _rxMsgCB;

    // Web socket protocol channelIDs
    class ChannelIDUsage
    {
    public:
        uint32_t channelID = UINT32_MAX;
        bool isUsed = false;
    };
    std::vector<ChannelIDUsage> _channelIDUsage;

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
   // Queue for sending frames over the web socket
    ThreadSafeQueue<RaftWebDataFrame> _txQueue;
    static const uint32_t MAX_WAIT_FOR_TX_QUEUE_MS = 2;
    static const uint32_t MAX_TIME_IN_QUEUE_MS = 5000;
#endif
};
