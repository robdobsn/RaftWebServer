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

RdClientConnSockets::RdClientConnSockets(int client, bool traceConn)
{
    _client = client;
    _traceConn = traceConn;
}

RdClientConnSockets::~RdClientConnSockets()
{
    if (_traceConn)
    {
#ifdef RD_CLIENT_CONN_SOCKETS_CONN_STATS
        double connOpenTimeSecs = Raft::timeElapsed(millis(), _connOpenTimeMs) / 1000.0;
        LOG_I(MODULE_PREFIX, "RdClientConnSockets CLOSED client connId %d bytesRead %d bytesWritten %d connOpenTimeSecs %.2f",
                    _client, _bytesRead, _bytesWritten, connOpenTimeSecs);
#else
        LOG_I(MODULE_PREFIX, "RdClientConnSockets CLOSED client connId %d", _client);
#endif
    }
    shutdown(_client, SHUT_RDWR);
    delay(20);
    close(_client);
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

    // Set close on EXEC
    fcntl(_client, F_SETFD, FD_CLOEXEC);
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
            LOG_W(MODULE_PREFIX, "write failed errno error %d conn %d bufLen %d totalMs %d", 
                        errno, getClientId(), bufLen, Raft::timeElapsed(millis(), startMs));
#endif
            return RdWebConnSendRetVal::WEB_CONN_SEND_FAIL;
        }

#ifdef DEBUG_SOCKET_SEND
        LOG_I(MODULE_PREFIX, "write ok conn %d bufLen %d", getClientId(), bufLen);
#endif

        // Update stats
#ifdef RD_CLIENT_CONN_SOCKETS_CONN_STATS
        _bytesWritten += bufLen;
        _lastAccessTimeMs = millis();
#endif

        // Success
        return RdWebConnSendRetVal::WEB_CONN_SEND_OK;
    }
}

ClientConnRslt RdClientConnSockets::getDataStart(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& dataBuf)
{
    // End any current data operation
    getDataEnd();

    // Resize data buffer to max size
    dataBuf.resize(WEB_CONN_MAX_RX_BUFFER);

    // Check for data
    int32_t bufLen = recv(_client, dataBuf.data(), dataBuf.size(), MSG_DONTWAIT);

    // Error handling
    if (bufLen < 0)
    {
        dataBuf.clear();
        ClientConnRslt connRslt = ClientConnRslt::CLIENT_CONN_RSLT_OK;
        switch(errno)
        {
            case EWOULDBLOCK:
                break;
            default:
                LOG_W(MODULE_PREFIX, "service read error %d", errno);
                connRslt = ClientConnRslt::CLIENT_CONN_RSLT_ERROR;
                break;
        }
        getDataEnd();
        return connRslt;
    }
    
    // Check for connection closed
    if (bufLen == 0)
    {
        dataBuf.clear();
        LOG_W(MODULE_PREFIX, "service read conn closed %d", errno);
        getDataEnd();
        return ClientConnRslt::CLIENT_CONN_RSLT_CONN_CLOSED;
    }

    // Stats
#ifdef RD_CLIENT_CONN_SOCKETS_CONN_STATS
    _bytesRead += bufLen;
    _lastAccessTimeMs = millis();
#endif

    // Return received data
    dataBuf.resize(bufLen);
    return ClientConnRslt::CLIENT_CONN_RSLT_OK;
}

void RdClientConnSockets::getDataEnd()
{
}

#endif
