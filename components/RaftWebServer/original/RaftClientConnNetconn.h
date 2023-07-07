/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "lwip/api.h"
#include "RaftClientConnBase.h"

class RaftClientConnNetconn : public RaftClientConnBase
{
public:
    RaftClientConnNetconn(struct netconn* client);
    virtual ~RaftClientConnNetconn();

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

    // Check if sending is ok
    virtual RaftWebConnSendRetVal canSend() override final
    {
        return RaftWebConnSendRetVal::WEB_CONN_SEND_OK;
    }

    // Send data
    virtual RaftWebConnSendRetVal sendDataBuffer(const uint8_t* pBuf, uint32_t bufLen, 
                        uint32_t maxRetryMs, uint32_t& bytesWritten) override final;

    // Setup
    virtual void setup(bool blocking) override final;

    // Data access
    virtual RaftClientConnRslt getDataStart(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& dataBuf) override final;
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
