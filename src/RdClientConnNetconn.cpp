/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ESP8266

#include "RdClientConnNetconn.h"
#include "RdWebInterface.h"
#include <Logger.h>

static const char *MODULE_PREFIX = "RdClientConnNetconn";

RdClientConnNetconn::RdClientConnNetconn(struct netconn* client)
{
    _client = client;
    _pInbuf = nullptr;
}

RdClientConnNetconn::~RdClientConnNetconn()
{
    // Need to close the connection as we can't handle it
    netconn_close(_client);
    netconn_delete(_client);
}

void RdClientConnNetconn::setup(bool blocking)
{
    // If non-blocking then set the receive timeout to a short time
    if (!blocking)
       netconn_set_recvtimeout(_client, 1);
}

RdWebConnSendRetVal RdClientConnNetconn::write(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs)
{
    // Check active
    if (!isActive())
    {
        LOG_W(MODULE_PREFIX, "write conn %d isActive FALSE", getClientId());
        return RdWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

    esp_err_t err = netconn_write(_client, pBuf, bufLen, NETCONN_COPY);
    if (err != ERR_OK)
    {
        LOG_W(MODULE_PREFIX, "write failed err %s (%d) connClient %d",
                    RdWebInterface::espIdfErrToStr(err), err, getClientId());
    }
    return (err = ERR_OK) ? RdWebConnSendRetVal::WEB_CONN_SEND_OK : RdWebConnSendRetVal::WEB_CONN_SEND_FAIL;
}

uint8_t* RdClientConnNetconn::getDataStart(uint32_t& dataLen, bool& errorOccurred, bool& connClosed)
{
    // End any current data operation
    getDataEnd();

    // Check for data
    dataLen = 0;
    bool dataReady = getRxData(&_pInbuf, connClosed);

    // Get any found data
    uint8_t *pBuf = nullptr;
    uint16_t bufLen = 0;
    if (dataReady && _pInbuf)
    {
        // Get a buffer
        err_t err = netbuf_data(_pInbuf, (void **)&pBuf, &bufLen);
        if ((err != ERR_OK) || !pBuf)
        {
            LOG_W(MODULE_PREFIX, "service netconn_data error %s buf %d connClient %d", 
                        RdWebInterface::espIdfErrToStr(err), (uint32_t)pBuf, getClientId());
            errorOccurred = true;
            return nullptr;
        }
        dataLen = bufLen;
        return pBuf;
    }
    return nullptr;
}

void RdClientConnNetconn::getDataEnd()
{
    if (_pInbuf)
        netbuf_delete(_pInbuf);
    _pInbuf = nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get Rx data
// True if data is available
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RdClientConnNetconn::getRxData(struct netbuf** pInbuf, bool& closeRequired)
{
    closeRequired = false;
    // Debug
#ifdef DEBUG_WEB_REQUEST_READ_START_END
    LOG_I(MODULE_PREFIX, "getRxData reading from client connClient %d", getClientId());
#endif

    // See if data available
    err_t err = netconn_recv(_client, pInbuf);

    // Check for timeout
    if (err == ERR_TIMEOUT)
    {
        // Debug
#ifdef DEBUG_WEB_REQUEST_READ_START_END
        LOG_I(MODULE_PREFIX, "getRxData read nothing available connClient %d", getClientId());
#endif

        // Nothing to do
        return false;
    }

    // Check for closed
    if (err == ERR_CLSD)
    {
        // Debug
#ifdef DEBUG_WEB_REQUEST_READ_START_END
        LOG_I(MODULE_PREFIX, "getRxData read connection closed connClient %d", getClientId());
#endif

        // Link is closed
        closeRequired = true;
        return false;
    }

    // Check for other error
    if (err != ERR_OK)
    {
#ifdef WARN_WEB_CONN_ERROR_CLOSE
        LOG_W(MODULE_PREFIX, "getRxData netconn_recv error %s connClient %d", 
                    RdWebInterface::espIdfErrToStr(err), getClientId());
#endif
        closeRequired = true;
        return false;
    }

    // Debug
#ifdef DEBUG_WEB_REQUEST_READ_START_END
    LOG_I(MODULE_PREFIX, "getRxData has read from client OK connClient %d", getClientId());
#endif

    // Data available in pInbuf
    return true;
}

#endif
