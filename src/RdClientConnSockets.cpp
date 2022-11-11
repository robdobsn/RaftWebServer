/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ESP8266

#include "RdClientConnSockets.h"
#include "RdWebInterface.h"
#include "ArduinoTime.h"
#include "RaftUtils.h"
#include <Logger.h>
#include "lwip/api.h"
#include "lwip/sockets.h"

static const char *MODULE_PREFIX = "RdClientConnSockets";

// Warn
#define WARN_SOCKET_SEND_FAIL

// Debug
// #define DEBUG_SOCKET_EAGAIN
// #define DEBUG_SOCKET_SEND

RdClientConnSockets::RdClientConnSockets(int client)
{
    _client = client;
    _pDataBuf = nullptr;
}

RdClientConnSockets::~RdClientConnSockets()
{
    // shutdown(_client, 0);
    close(_client);
    delete _pDataBuf;
}

void RdClientConnSockets::setup(bool blocking)
{
    // Check for non blocking
    if (!blocking)
    {
        // Set non-blocking socket
        int flags = fcntl(_client, F_GETFL, 0);
        if (flags != -1)
        flags = flags | O_NONBLOCK;
        fcntl(_client, F_SETFL, flags);
    }
}

RdWebConnSendRetVal RdClientConnSockets::write(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs)
{
    // Check active
    if (!isActive())
    {
        LOG_W(MODULE_PREFIX, "write conn %d isActive FALSE", getClientId());
        return RdWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

    // Write using socket
    uint32_t startMs = millis();
    while (true)
    {
        int rslt = send(_client, pBuf, bufLen, 0);
        if (rslt < 0)
        {
            if (errno == EAGAIN)
            {
                if ((maxRetryMs == 0) || Raft::isTimeout(millis(), startMs, maxRetryMs))
                {
                    if (maxRetryMs != 0)
                    {
#ifdef WARN_SOCKET_SEND_FAIL
                        LOG_W(MODULE_PREFIX, "write EAGAIN timed-out conn %d bufLen %d retry %dms", getClientId(), bufLen, maxRetryMs);
#else
#ifdef DEBUG_SOCKET_EAGAIN
                        LOG_I(MODULE_PREFIX, "write EAGAIN timed-out conn %d bufLen %d retry %dms", getClientId(), bufLen, maxRetryMs);
#endif
#endif
                    }
                    else
                    {
#ifdef DEBUG_SOCKET_EAGAIN
                        LOG_I(MODULE_PREFIX, "write EAGAIN returning conn %d bufLen %d retry %dms", getClientId(), bufLen, maxRetryMs);
#endif
                    }
                    return RdWebConnSendRetVal::WEB_CONN_SEND_EAGAIN;
                }
#ifdef DEBUG_SOCKET_EAGAIN
                LOG_I(MODULE_PREFIX, "write failed errno %d conn %d bufLen %d retrying for %dms", errno, getClientId(), bufLen, maxRetryMs);
#endif
                vTaskDelay(1);
                continue;
            }
#ifdef WARN_SOCKET_SEND_FAIL
            LOG_I(MODULE_PREFIX, "write failed errno error %d conn %d bufLen %d", errno, getClientId(), bufLen);
#endif
            return RdWebConnSendRetVal::WEB_CONN_SEND_FAIL;
        }

#ifdef DEBUG_SOCKET_SEND
        LOG_I(MODULE_PREFIX, "write ok conn %d bufLen %d", getClientId(), bufLen);
#endif

        // Success
        return RdWebConnSendRetVal::WEB_CONN_SEND_OK;
    }
}

uint8_t* RdClientConnSockets::getDataStart(uint32_t& dataLen, bool& errorOccurred, bool& connClosed)
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
    int32_t bufLen = recv(_client, _pDataBuf, WEB_CONN_MAX_RX_BUFFER, MSG_DONTWAIT);
    if (bufLen < 0)
    {
        switch(errno)
        {
            case EWOULDBLOCK:
                bufLen = 0;
                break;
            default:
                LOG_W(MODULE_PREFIX, "service read error %d", errno);
                errorOccurred = true;
                break;
        }
        getDataEnd();
        return nullptr;
    }
    else if (bufLen == 0)
    {
        LOG_W(MODULE_PREFIX, "service read conn closed %d", errno);
        connClosed = true;
        getDataEnd();
        return nullptr;
    }

    // Return received data
    dataLen = bufLen;
    return _pDataBuf;
}

void RdClientConnSockets::getDataEnd()
{
    // Delete buffer
    if (_pDataBuf)
        delete _pDataBuf;
    _pDataBuf = nullptr;
}

#endif
