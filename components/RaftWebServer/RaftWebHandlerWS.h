/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebHandler.h"
#include "RaftArduino.h"
#include "Logger.h"
#include <vector>
#include "RaftWebRequestHeader.h"
#include "RaftWebResponderWS.h"

class RaftWebHandlerWS : public RaftWebHandler
{
public:
    RaftWebHandlerWS(const RaftJsonIF& config,
            RaftWebSocketInboundCanAcceptFnType inboundCanAcceptCB, 
            RaftWebSocketInboundHandleMsgFnType rxMsgCB);
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

    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, 
                RaftHttpStatusCode &statusCode
                ) override final;

    void responderDelete(RaftWebResponderWS* pResponder);
    void responderInactive(uint32_t channelID);

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
    uint32_t _noPongMs = 0;

    // Content type
    bool _isBinaryWS = true;

    // WS interface functions
    RaftWebSocketInboundCanAcceptFnType _inboundCanAcceptCB;
    RaftWebSocketInboundHandleMsgFnType _rxMsgCB;

    // Web socket protocol connection slot info
    class ConnSlotRec
    {
    public:
        // Is used
        bool isUsed = false;

        // Channel ID
        uint32_t channelID = UINT32_MAX;
    };
    std::vector<ConnSlotRec> _connectionSlots;

    // Last ping check time
    uint32_t _lastConnCheckTimeMs = 0;
    static const uint32_t CONNECTIVITY_CHECK_INTERVAL_MS = 1000;

    // Defaults
    static const uint32_t DEFAULT_WS_PKT_MAX_BYTES = 100000;
    static const uint32_t DEFAULT_WS_TX_QUEUE_MAX = 20;
    static const uint32_t DEFAULT_WS_PING_MS = 30000;
    static const uint32_t DEFAULT_WS_IDLE_CLOSE_MS = 0;

    // Handle connection slots
    int findFreeConnectionSlot();
    int findConnectionSlotByChannelID(uint32_t channelID);
};
