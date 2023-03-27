/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef ESP8266

#include "RdClientConnBase.h"

#define RD_CLIENT_CONN_SOCKETS_CONN_STATS

class RdClientConnSockets : public RdClientConnBase
{
public:
    RdClientConnSockets(int client, bool traceConn);
    virtual ~RdClientConnSockets();

    // Client ID
    virtual uint32_t getClientId() override final
    {
        return (uint32_t) _client;
    }

    // Write
    virtual RdWebConnSendRetVal write(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs) override final;

    // Setup
    virtual void setup(bool blocking) override final;

    // Data access
    virtual uint8_t* getDataStart(uint32_t& dataLen, bool& errorOccurred, bool& connClosed) override final;
    virtual void getDataEnd() override final;

private:
    int _client = -1;
    uint8_t* _pDataBuf = nullptr;
    bool _traceConn = false;

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

#endif
