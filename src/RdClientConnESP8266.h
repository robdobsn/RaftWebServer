/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef ESP8266

#include "RdClientConnBase.h"

class RdClientConnESP8266 : public RdClientConnBase
{
public:
    RdClientConnESP8266(WiFiClient* client);
    virtual ~RdClientConnESP8266();

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
    WiFiClient* _client;
    uint8_t _pDataBuf;

    static const uint32_t WEB_CONN_MAX_RX_BUFFER = 1000;

};

#endif
