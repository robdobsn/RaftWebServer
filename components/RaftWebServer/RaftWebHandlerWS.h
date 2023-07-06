/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebHandler.h"
#include <ArduinoOrAlt.h>
#include <Logger.h>
#include <ConfigBase.h>
#include <vector>
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <mongoose.h>
#include <ThreadSafeQueue.h>
#include <RaftWebDataFrame.h>
#else
#include <RaftWebRequestHeader.h>
#include <RaftWebResponderWS.h>
#endif

class RaftWebHandlerWS : public RaftWebHandler
{
public:
    RaftWebHandlerWS(const ConfigBase& config,
            RaftWebSocketCanAcceptCB canAcceptRxMsgCB, RaftWebSocketMsgCB rxMsgCB);
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
    uint32_t getMaxConnections() const
    {
        return _maxConnections;
    }

    // Setup websocket channel ID
    void setupWebSocketChannelID(uint32_t wsConnIdx, uint32_t chanID)
    {
        // Check valid
        if (wsConnIdx >= _connectionSlots.size())
            return;
        _connectionSlots[wsConnIdx].channelID = chanID;
        _connectionSlots[wsConnIdx].isUsed = false;
    }

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

    // Handle request
    virtual bool handleRequest(struct mg_connection *pConn, int ev, void *ev_data) override final;

    // Check is a message can be sent
    virtual bool canSend(uint32_t channelID, bool& noConn) override final;
    // Send message (on a channel)
    virtual bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID) override final;

#else

    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, 
                RaftHttpStatusCode &statusCode
                ) override final;

    void responderDelete(RaftWebResponderWS* pResponder);

    // Check if channel can send
    virtual bool canSend(uint32_t channelID, bool& noConn) override final;

    // Send message on a channel
    virtual bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, 
                uint32_t channelID) override final;

#endif

private:
    // Max connections
    uint32_t _maxConnections = 0;

    // Path for websocket endpoint
    String _wsPath;

    // Max packet size
    uint32_t _pktMaxBytes = DEFAULT_WS_PKT_MAX_BYTES;

    // Tx queue max
    uint32_t _txQueueMax = DEFAULT_WS_TX_QUEUE_MAX;

    // Ping/pong interval and timeout
    uint32_t _pingIntervalMs = DEFAULT_WS_PING_MS;
    uint32_t _noPongMs = DEFAULT_WS_NO_PONG_MS;

    // Content type
    bool _isBinaryWS = true;

    // WS interface functions
    RaftWebSocketCanAcceptCB _canAcceptRxMsgCB;
    RaftWebSocketMsgCB _rxMsgCB;

    // Web socket protocol connection slot info
    class ConnSlotRec
    {
    public:
        // Is used
        bool isUsed = false;

        // Channel ID
        uint32_t channelID = UINT32_MAX;

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
        // Last ping request time
        uint32_t lastPingRequestTimeMs = 0;

        // Connection
        struct mg_connection* pConn = NULL;
#endif
    };
    std::vector<ConnSlotRec> _connectionSlots;

    // Last ping check time
    uint32_t _lastConnCheckTimeMs = 0;
    static const uint32_t CONNECTIVITY_CHECK_INTERVAL_MS = 1000;

    // Defaults
    static const uint32_t DEFAULT_WS_PKT_MAX_BYTES = 1024;
    static const uint32_t DEFAULT_WS_TX_QUEUE_MAX = 10;
    static const uint32_t DEFAULT_WS_PING_MS = 2000;
    static const uint32_t DEFAULT_WS_NO_PONG_MS = 6000;
    static const uint32_t DEFAULT_WS_IDLE_CLOSE_MS = 0;

    // Handle connection slots
    int findFreeConnectionSlot();
    int findConnectionSlotByChannelID(uint32_t channelID);

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    // Queue for sending frames over the web socket
    ThreadSafeQueue<RaftWebDataFrame> _txQueue;
    static const uint32_t MAX_WAIT_FOR_TX_QUEUE_MS = 2;
    static const uint32_t MAX_TIME_IN_QUEUE_MS = 5000;

    // Get/set channel ID in connection info
    uint32_t getChannelIDFromConnInfo(struct mg_connection *pConn);
    void setChannelIDInConnInfo(struct mg_connection *pConn, uint32_t channelID);  

    // Handle connection slots
    int findConnectionSlotByConn(struct mg_connection *pConn);

#endif
};
