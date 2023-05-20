/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef ESP8266

#include "RaftClientConnESP8266.h"
#include "RaftWebInterface.h"
#include <Logger.h>

static const char *MODULE_PREFIX = "RaftClientConn8266";

RaftClientConnESP8266::RaftClientConnESP8266(WiFiClient* client)
{
    _client = client;
    _pDataBuf = nullptr;
}

RaftClientConnESP8266::~RaftClientConnESP8266()
{
    if (_client)
    {
        _client->stop();
        delete _client;
    }
    delete _pDataBuf;
}

void RaftClientConnESP8266::setup(bool blocking)
{
}

RaftWebConnSendRetVal RaftClientConnESP8266::write(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs)
{
    // Check active
    if (!isActive())
    {
        LOG_W(MODULE_PREFIX, "write conn %d isActive FALSE", getClientId());
        return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

    // Write
    int err = ERR_OK;
    size_t written = _client->write((const char*)pBuf, bufLen);
    if (written != bufLen)
    {
        LOG_I(MODULE_PREFIX, "connectionWrite written %d != size %d", written, size);
        err = ERR_CONN;
    }
    return (err = ERR_OK) ? RaftWebConnSendRetVal::WEB_CONN_SEND_OK : RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
}

uint8_t* RaftClientConnESP8266::getDataStart(uint32_t& dataLen, bool& errorOccurred, bool& connClosed)
{
    // End any current data operation
    getDataEnd();

    // Create data buffer
    _pDataBuf = new uint8_t[WEB_CONN_MAX_RX_BUFFER];
    if (!_pDataBuf)
    {
        LOG_E(MODULE_PREFIX, "service failed to alloc %d", getClientId());
        return nullptr;
    }

    // Check for data
    if (_pConn)
    {
        if (_pConn->connected())
        {
            while (_pConn->available()) 
            {
                bufLen = _pConn->read(_pDataBuf, WEB_CONN_MAX_RX_BUFFER);
            }
        }
        return _pDataBuf;
    }

    // Clean up
    getDataEnd();
    return nullptr;
}

void RaftClientConnESP8266::getDataEnd()
{
    // Delete buffer
    if (_pDataBuf)
        delete _pDataBuf;
    _pDataBuf = nullptr;

}

#endif
