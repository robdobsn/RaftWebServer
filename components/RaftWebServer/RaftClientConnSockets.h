/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftClientConnBase.h"

#define RD_CLIENT_CONN_SOCKETS_CONN_STATS
// #define RAFT_CLIENT_USE_PRE_ALLOCATED_BUFFER_FOR_RX

class RaftClientConnSockets : public RaftClientConnBase
{
public:
    RaftClientConnSockets(int client, bool traceConn);
    virtual ~RaftClientConnSockets();

    // Client ID
    virtual uint32_t getClientId() override final
    {
        return (uint32_t) _client;
    }

    // Check if sending is ok
    virtual RaftWebConnSendRetVal canSend() override final;
    
    // Send data
    virtual RaftWebConnSendRetVal sendDataBuffer(const uint8_t* pBuf, uint32_t bufLen, 
                        uint32_t maxRetryMs, uint32_t& bytesWritten) override final;

    // Setup
    virtual void setup(bool blocking) override final;

    // Data access
    virtual RaftClientConnRslt getDataStart(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& dataBuf) override final;
    virtual void getDataEnd() override final;

private:
    int _client = -1;
    bool _traceConn = false;

#ifdef RAFT_CLIENT_USE_PRE_ALLOCATED_BUFFER_FOR_RX
    // Pre-allocated Rx data buffer
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _rxDataBuf;
#endif

#ifdef RD_CLIENT_CONN_SOCKETS_CONN_STATS
    uint64_t _connOpenTimeMs = 0;
    uint32_t _bytesRead = 0;
    uint32_t _bytesWritten = 0;
    uint32_t _lastAccessTimeMs = 0;
#endif

#ifdef CONFIG_LWIP_TCP_MSS
    static const uint32_t WEB_CONN_MAX_RX_BUFFER = CONFIG_LWIP_TCP_MSS;
#else
    static const uint32_t WEB_CONN_MAX_RX_BUFFER = 1440;
#endif
};
