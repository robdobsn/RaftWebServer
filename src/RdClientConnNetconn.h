/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef ESP8266

#include "lwip/api.h"
#include "RdClientConnBase.h"

class RdClientConnNetconn : public RdClientConnBase
{
public:
    RdClientConnNetconn(struct netconn* client);
    virtual ~RdClientConnNetconn();

    // Current state
    virtual const char* getStateStr() override final
    {
        if (!_client)
            return "null";
        switch(_client->state)
        {
            default: return "none";
            case NETCONN_WRITE: return "write";
            case NETCONN_LISTEN: return "listen";
            case NETCONN_CONNECT: return "connect";
            case NETCONN_CLOSE: return "close";
        }
    }

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
    virtual ClientConnRslt getDataStart(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& dataBuf) override final;
    virtual void getDataEnd() override final;

private:
    bool getRxData(struct netbuf** pInbuf, bool& closeRequired);
    struct netconn* _client;
    struct netbuf* _pInbuf;

#ifdef CONFIG_LWIP_TCP_MSS
    static const uint32_t WEB_CONN_MAX_RX_BUFFER = CONFIG_LWIP_TCP_MSS;
#else
    static const uint32_t WEB_CONN_MAX_RX_BUFFER = 1440;
#endif

};

#endif
