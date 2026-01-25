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
#ifndef WEB_CONN_USE_LWIP
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
#define WARN_ON_FATAL_ERROR

// Debug
// #define DEBUG_SOCKET_CLOSE
// #define DEBUG_SOCKET_EAGAIN
// #define DEBUG_SOCKET_SEND
// #define DEBUG_SOCKET_SEND_VERBOSE
// #define DEBUG_TIME_RECV_FN_SLOW_US 1000

RaftClientConnSockets::RaftClientConnSockets(int client, bool traceConn)
{
    _client = client;
    _traceConn = traceConn;
    if (_traceConn)
    {
        LOG_I(MODULE_PREFIX, "RaftClientConnSockets CREATED client connId %d this=%p", _client, this);
    }
}

RaftClientConnSockets::~RaftClientConnSockets()
{
    if (_traceConn)
    {
#ifdef RD_CLIENT_CONN_SOCKETS_CONN_STATS
        double connOpenTimeSecs = Raft::timeElapsed(millis(), _connOpenTimeMs) / 1000.0;
        LOG_I(MODULE_PREFIX, "RaftClientConnSockets CLOSED client connId %d bytesRead %d bytesWritten %d connOpenTimeSecs %.2f this=%p",
                    _client, _bytesRead, _bytesWritten, connOpenTimeSecs, this);
#else
        LOG_I(MODULE_PREFIX, "RaftClientConnSockets CLOSED client connId %d this=%p", _client, this);
#endif
    }
    shutdown(_client, SHUT_RDWR);
    delay(20);
    close(_client);
}

void RaftClientConnSockets::setup(bool blocking)
{
    // Set SO_LINGER with short timeout for balanced close behavior
    struct linger ling = {0};
    ling.l_onoff = 1;   // Enable linger
    ling.l_linger = 2;  // Wait up to 2 seconds for graceful close, then force close
    setsockopt(_client, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    
    // Set SO_REUSEADDR to allow immediate port reuse
    int reuseAddr = 1;
    setsockopt(_client, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr));
    
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
    // Check if socket is still valid
    if (_client < 0)
    {
        return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

#ifdef DEBUG_CAN_SEND_SELECT_TIMING
    uint64_t startUs = micros();
#endif

    // Use select to check if socket is ready to send
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(_client, &writefds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

#ifdef DEBUG_CAN_SEND_SELECT_TIMING
    uint64_t beforeSelectUs = micros();
#endif

    int rslt = select(_client + 1, NULL, &writefds, NULL, &tv);

#ifdef DEBUG_CAN_SEND_SELECT_TIMING
    uint64_t afterSelectUs = micros();
    uint32_t selectUs = afterSelectUs - beforeSelectUs;
    uint32_t totalUs = afterSelectUs - startUs;
    if (totalUs > 1000) // Log if > 1ms
    {
        LOG_I(MODULE_PREFIX, "canSend conn %d totalUs %d selectUs %d setupUs %d result %d",
                    getClientId(), totalUs, selectUs, (uint32_t)(beforeSelectUs - startUs), rslt);
    }
#endif

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
        LOG_W(MODULE_PREFIX, "sendDataBuffer conn %d isActive FALSE", getClientId());
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
            // Fatal errors (connection reset, broken pipe, etc.) - immediately close socket
            // to prevent zombie connections that continue trying to send
            if ((opErrno == ECONNRESET) || (opErrno == EPIPE) || (opErrno == ENOTCONN) || 
                (opErrno == ECONNABORTED) || (opErrno == ENETDOWN) || (opErrno == ENETRESET))
            {
#ifdef WARN_SOCKET_SEND_FAIL
                LOG_W(MODULE_PREFIX, "sendDataBuffer FATAL errno %d conn %d - closing socket immediately", 
                            opErrno, getClientId());
#endif
                // Immediately close the socket to free resources
                shutdown(_client, SHUT_RDWR);
                close(_client);
                _client = -1;
                return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
            }
#ifdef WARN_SOCKET_SEND_FAIL
            LOG_W(MODULE_PREFIX, "sendDataBuffer failed errno error %d conn %d bufLen %d totalMs %d", 
                        opErrno, getClientId(), bufLen, (int)Raft::timeElapsed(millis(), startMs));
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
    // Check if socket is still valid
    if (_client < 0)
    {
        dataBuf.clear();
        return RaftClientConnRslt::CLIENT_CONN_RSLT_CONN_CLOSED;
    }

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
            case ECONNRESET:
            case EPIPE:
            case ENOTCONN:
            case ECONNABORTED:
                // Fatal connection errors - immediately close socket
#ifdef WARN_ON_FATAL_ERROR
                LOG_W(MODULE_PREFIX, "service read FATAL error %d - closing socket", errno);
#endif
                shutdown(_client, SHUT_RDWR);
                close(_client);
                _client = -1;
                getDataEnd();
                return RaftClientConnRslt::CLIENT_CONN_RSLT_CONN_CLOSED;
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
#ifdef DEBUG_SOCKET_CLOSE
        LOG_I(MODULE_PREFIX, "service read conn closed gracefully - closing socket");
#endif
        // Immediately close the socket
        shutdown(_client, SHUT_RDWR);
        close(_client);
        _client = -1;
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
