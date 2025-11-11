/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftClientConnSockets.h"
#include "RaftWebInterface.h"
#include "ArduinoTime.h"
#include "RaftUtils.h"
#include "RaftThreading.h"
#ifdef WEB_CONN_USE_BERKELEY_SOCKETS
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#else
#include "lwip/api.h"
#include "lwip/sockets.h"
#endif

static const char *MODULE_PREFIX = "RaftClientConnSockets";

// Warn
#define WARN_SOCKET_SEND_FAIL

// Debug
// #define DEBUG_SOCKET_EAGAIN
// #define DEBUG_SOCKET_SEND
// #define DEBUG_SOCKET_SEND_VERBOSE
// #define DEBUG_TIME_RECV_FN_SLOW_US 1000

RaftClientConnSockets::RaftClientConnSockets(int client, bool traceConn)
{
    _client = client;
    _traceConn = traceConn;
}

RaftClientConnSockets::~RaftClientConnSockets()
{
    if (_traceConn)
    {
#ifdef RD_CLIENT_CONN_SOCKETS_CONN_STATS
        double connOpenTimeSecs = Raft::timeElapsed(millis(), _connOpenTimeMs) / 1000.0;
        LOG_I(MODULE_PREFIX, "RaftClientConnSockets CLOSED client connId %d bytesRead %d bytesWritten %d connOpenTimeSecs %.2f",
                    _client, _bytesRead, _bytesWritten, connOpenTimeSecs);
#else
        LOG_I(MODULE_PREFIX, "RaftClientConnSockets CLOSED client connId %d", _client);
#endif
    }
    shutdown(_client, SHUT_RDWR);
    delay(20);
    close(_client);
}

void RaftClientConnSockets::setup(bool blocking)
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

    // Set socket options
    int on = 1;
    setsockopt(_client, IPPROTO_TCP, TCP_NODELAY, (char *) &on, sizeof(on));

#ifdef RAFT_CLIENT_USE_PRE_ALLOCATED_BUFFER_FOR_RX
    // Allocate databuf
    _rxDataBuf.resize(WEB_CONN_MAX_RX_BUFFER);
#endif
}

RaftWebConnSendRetVal RaftClientConnSockets::canSend()
{
    // Use select to check if socket is ready to send
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(_client, &writefds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    int rslt = select(_client + 1, NULL, &writefds, NULL, &tv);
    if (rslt < 0)
    {
        LOG_W(MODULE_PREFIX, "canSend conn %d select error %d", getClientId(), errno);
        return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }
    if (rslt == 0)
    {
        return RaftWebConnSendRetVal::WEB_CONN_SEND_EAGAIN;
    }
    return RaftWebConnSendRetVal::WEB_CONN_SEND_OK;
}

RaftWebConnSendRetVal RaftClientConnSockets::sendDataBuffer(const uint8_t* pBuf, uint32_t bufLen,   
                        uint32_t maxRetryMs, uint32_t& bytesWritten)
{
    // Check active
    bytesWritten = 0;
    if (!isActive())
    {
        LOG_W(MODULE_PREFIX, "write conn %d isActive FALSE", getClientId());
        return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

    // Write using socket
#ifdef DEBUG_SOCKET_SEND
    uint64_t startUs = micros();
#endif
    uint32_t startMs = millis();
    while (true)
    {
        int rslt = send(_client, pBuf, bufLen, 0);
        int opErrno = errno;

#ifdef DEBUG_SOCKET_SEND_VERBOSE
        // Debug
        LOG_I(MODULE_PREFIX, "sendDataBuffer bufLen %d rslt %d errno %s elapsed %duS", 
                    bufLen, rslt, rslt < 0 ? String(opErrno).c_str() : "N/A", (int) Raft::timeElapsed(micros(), startUs));
#endif

        if (rslt < 0)
        {
            if ((opErrno == EAGAIN) || (opErrno == EINPROGRESS))
            {
                if ((maxRetryMs == 0) || Raft::isTimeout(millis(), startMs, maxRetryMs))
                {
                    if (maxRetryMs != 0)
                    {
#ifdef WARN_SOCKET_SEND_FAIL
                        LOG_W(MODULE_PREFIX, "sendDataBuffer EAGAIN timed-out conn %d bufLen %d retry %dms", 
                                        getClientId(), bufLen, maxRetryMs);
#else
#ifdef DEBUG_SOCKET_EAGAIN
                        LOG_I(MODULE_PREFIX, "sendDataBuffer EAGAIN timed-out conn %d bufLen %d retry %dms", 
                                        getClientId(), bufLen, maxRetryMs);
#endif
#endif
                    }
                    else
                    {
#ifdef DEBUG_SOCKET_EAGAIN
                        LOG_I(MODULE_PREFIX, "sendDataBuffer EAGAIN returning conn %d bufLen %d retry %dms", 
                                        getClientId(), bufLen, maxRetryMs);
#endif
                    }
                    return RaftWebConnSendRetVal::WEB_CONN_SEND_EAGAIN;
                }
#ifdef DEBUG_SOCKET_EAGAIN
                LOG_I(MODULE_PREFIX, "sendDataBuffer failed errno %d conn %d bufLen %d retrying for %dms", 
                                opErrno, getClientId(), bufLen, maxRetryMs);
#endif
                RaftThread_sleep(1);
                continue;
            }
#ifdef WARN_SOCKET_SEND_FAIL
            LOG_W(MODULE_PREFIX, "sendDataBuffer failed errno error %d conn %d bufLen %d totalMs %d", 
                        opErrno, getClientId(), bufLen, Raft::timeElapsed(millis(), startMs));
#endif
            return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
        }

        // Ok - rslt is number of bytes written
        bytesWritten = rslt;

#ifdef DEBUG_SOCKET_SEND
        uint64_t elapsedUs = Raft::timeElapsed(micros(), startUs);
        LOG_I(MODULE_PREFIX, "sendDataBuffer ok conn %d bufLen %d bytesWritten %d took %duS",
                    getClientId(), bufLen, bytesWritten, (int)elapsedUs);
#endif

        // Update stats
#ifdef RD_CLIENT_CONN_SOCKETS_CONN_STATS
        _bytesWritten += bytesWritten;
        _lastAccessTimeMs = millis();
#endif

        // Success
        return RaftWebConnSendRetVal::WEB_CONN_SEND_OK;
    }
}

RaftClientConnRslt RaftClientConnSockets::getDataStart(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& dataBuf)
{
    // End any current data operation
    getDataEnd();

#ifndef RAFT_CLIENT_USE_PRE_ALLOCATED_BUFFER_FOR_RX
    // Resize data buffer to max size
    dataBuf.resize(WEB_CONN_MAX_RX_BUFFER);
#endif

    // Check for data
#ifdef DEBUG_TIME_RECV_FN_SLOW_US
    uint64_t startUs = micros();
#endif
#ifdef RAFT_CLIENT_USE_PRE_ALLOCATED_BUFFER_FOR_RX
    int32_t bufLen = recv(_client, _rxDataBuf.data(), _rxDataBuf.size(), MSG_DONTWAIT);
#else
    int32_t bufLen = recv(_client, dataBuf.data(), dataBuf.size(), MSG_DONTWAIT);
#endif
#ifdef DEBUG_TIME_RECV_FN_SLOW_US
    uint32_t elapsedUs = micros() - startUs;
    if (elapsedUs > DEBUG_TIME_RECV_FN_SLOW_US)
        LOG_I(MODULE_PREFIX, "getDataStart recv took %dus", (int)elapsedUs);
#endif

    // Error handling
    if (bufLen < 0)
    {
        dataBuf.clear();
        RaftClientConnRslt connRslt = RaftClientConnRslt::CLIENT_CONN_RSLT_OK;
        switch(errno)
        {
            case EWOULDBLOCK:
            case EINPROGRESS:
                break;
            default:
                LOG_W(MODULE_PREFIX, "service read error %d", errno);
                connRslt = RaftClientConnRslt::CLIENT_CONN_RSLT_ERROR;
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
        return RaftClientConnRslt::CLIENT_CONN_RSLT_CONN_CLOSED;
    }

    // Stats
#ifdef RD_CLIENT_CONN_SOCKETS_CONN_STATS
    _bytesRead += bufLen;
    _lastAccessTimeMs = millis();
#endif

    // Return received data
#ifdef RAFT_CLIENT_USE_PRE_ALLOCATED_BUFFER_FOR_RX
    dataBuf.assign(_rxDataBuf.data(), _rxDataBuf.data() + bufLen);
#else
    dataBuf.resize(bufLen);
#endif

    return RaftClientConnRslt::CLIENT_CONN_RSLT_OK;
}

void RaftClientConnSockets::getDataEnd()
{
}
