/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RdClientListener.h"
#include "RdClientConnNetconn.h"
#include "RdClientConnSockets.h"
#include "RdWebInterface.h"
#include <Logger.h>
#include <ArduinoTime.h>
#include "lwip/api.h"
#include "lwip/sockets.h"

static const char *MODULE_PREFIX = "RdClientListener";

#define WEB_CONN_USE_BERKELEY_SOCKETS

#define WARN_ON_LISTENER_ERROR
// #define DEBUG_NEW_CONNECTION

#ifndef ESP8266

void RdClientListener::listenForClients(int port, uint32_t numConnSlots)
{

    // Loop forever
    while (1)
    {

#ifdef WEB_CONN_USE_BERKELEY_SOCKETS

        // Create socket
        int listenerSocketId = socket(AF_INET , SOCK_STREAM , 0);
        if (listenerSocketId < 0)
        {
            LOG_W(MODULE_PREFIX, "socketListenerTask failed to create socket");
            vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
            continue;
        }

        // Form address to be used in bind call - IPV4 assumed
        struct sockaddr_in bindAddr;
        memset(&bindAddr, 0, sizeof(bindAddr));
        bindAddr.sin_addr.s_addr = INADDR_ANY;
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(port);

        // Bind to IP and port
        int bind_err = bind(listenerSocketId, (struct sockaddr *)&bindAddr, sizeof(bindAddr));
        int errorNumber = errno;
        if (bind_err != 0)
        {
            LOG_W(MODULE_PREFIX, "socketListenerTask (listenerSocketId %d) failed to bind on port %d errno %d",
                                listenerSocketId, port, errorNumber);
            shutdown(listenerSocketId, SHUT_RDWR);
            vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / 10 / portTICK_PERIOD_MS);
            close(listenerSocketId);
            vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
            continue;
        }

        // Listen for clients
        int listen_error = listen(listenerSocketId, numConnSlots);
        errorNumber = errno;
        if (listen_error != 0)
        {
            LOG_W(MODULE_PREFIX, "socketListenerTask (listenerSocketId %d) failed to listen errno %d", listenerSocketId, errorNumber);
            shutdown(listenerSocketId, SHUT_RDWR);
            vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / 10 / portTICK_PERIOD_MS);
            close(listenerSocketId);
            vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
            continue;
        }
        LOG_I(MODULE_PREFIX, "socketListenerTask (listenerSocketId %d) listening on port %d", listenerSocketId, port);

        // Wait for connection
        uint32_t consecErrorCount = 0;
        while (true)
        {
            // Client info
            struct sockaddr_storage clientInfo;
            socklen_t clientInfoLen = sizeof(clientInfo);
            int sockClient = accept(listenerSocketId, (struct sockaddr *)&clientInfo, &clientInfoLen);
            int errorNumber = errno;
            if(sockClient < 0)
            {
                LOG_W(MODULE_PREFIX, "socketListenerTask (listenerSocketId %d port %d) failed to accept errno %d", listenerSocketId, port, errorNumber);
                bool socketReconnNeeded = false;
                switch(errorNumber)
                {
                    case ENETDOWN:
                    case EPROTO:
                    case ENOPROTOOPT:
                    case EHOSTDOWN:
                    case ECONNABORTED:
                    case ENOBUFS:
                    case EHOSTUNREACH:
                    case EOPNOTSUPP:
                    case ENETUNREACH:
                    case ENFILE:
                        vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
                        consecErrorCount++;
                        break;
                    case EWOULDBLOCK:
                        break;
                    default:
                        socketReconnNeeded = true;
                        break;
                }
                if ((socketReconnNeeded) || (consecErrorCount > 50))
                {
                    LOG_I(MODULE_PREFIX, "socketListenerTask (listenId %d port %d) socket RECONN REQD error %d reconnNeeded %d consecErrCount %d", 
                            listenerSocketId, port, errorNumber, socketReconnNeeded, consecErrorCount);
                    break;
                }
                continue;
            }
            else
            {
                // Clear error count
                consecErrorCount = 0;
    #ifdef DEBUG_NEW_CONNECTION
                {
                    char ipAddrStr[INET6_ADDRSTRLEN > INET_ADDRSTRLEN ? INET6_ADDRSTRLEN : INET_ADDRSTRLEN] = "";
                    switch(clientInfo.ss_family) {
                        case AF_INET: {
                            struct sockaddr_in *addr_in = (struct sockaddr_in *)&clientInfo;
                            inet_ntop(AF_INET, &(addr_in->sin_addr), ipAddrStr, INET_ADDRSTRLEN);
                            break;
                        }
                        case AF_INET6: {
                            struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&clientInfo;
                            inet_ntop(AF_INET6, &(addr_in6->sin6_addr), ipAddrStr, INET6_ADDRSTRLEN);
                            break;
                        }
                        default:
                            break;
                    }
                    LOG_I(MODULE_PREFIX, "socketListenerTask (listenId %d port %d) newConn connId %d from %s", 
                            listenerSocketId, port, sockClient, ipAddrStr);
                }
                static const bool TRACE_CONN = true;
    #else
                static const bool TRACE_CONN = false;
    #endif

                // Construct an RdClientConnSockets object
                RdClientConnBase* pClientConn = new RdClientConnSockets(sockClient, TRACE_CONN);

                // Hand off the connection to the connection manager via a callback
                if (!(_handOffNewConnCB && _handOffNewConnCB(pClientConn)))
                {
                    // Debug
    #ifdef DEBUG_NEW_CONNECTION
                    LOG_I(MODULE_PREFIX, "listen (listenId %d port %d) NEW CONN REJECTED connId %d pClient %p", 
                        listenerSocketId, port, sockClient, pClientConn);
    #endif
                    // No room so delete (which closes the connection)
                    delete pClientConn;
                }
                else
                {

                    // Debug
    #ifdef DEBUG_NEW_CONNECTION
                    LOG_I(MODULE_PREFIX, "listen (listenerSocketId %d port %d) NEW CONN ACCEPTED connId %d pClient %p", 
                        listenerSocketId, port, sockClient, pClientConn);
    #endif
                }
            }
        }

        // Listener exited
        // shutdown(listenerSocketId, 0);
        close(listenerSocketId);
        LOG_E(MODULE_PREFIX,"socketListenerTask (listenerSocketId %d port %d) listener stopped", listenerSocketId, port);

        // Delay hoping networking recovers
        vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);

#else // Use LWIP NetConn

        // Create netconn and bind to port
        struct netconn* pListener = netconn_new(NETCONN_TCP);
        netconn_bind(pListener, NULL, port);
        netconn_listen(pListener);
        LOG_I(MODULE_PREFIX, "web server listening");

        // Wait for connection
        while (true) 
        {
            // Accept connection
            struct netconn* pNewConnection;
            err_t errCode = netconn_accept(pListener, &pNewConnection);

            // Check new connection valid
            if ((errCode == ERR_OK) && pNewConnection)
            {
                // Construct an RdClientConnNetconn object
                RdClientConnBase* pClientConn = new RdClientConnNetconn(pNewConnection);

                // Hand off the connection to the connection manager via a callback
                if (!(_handOffNewConnCB && _handOffNewConnCB(pClientConn)))
                {

                    // Debug
    #ifdef DEBUG_NEW_CONNECTION
                    LOG_I(MODULE_PREFIX, "listen NEW CONN REJECTED %d", (uint32_t)pNewConnection);
    #endif
                    // No room so delete (which closes the connection)
                    delete pClientConn;
                }
                else
                {

                    // Debug
    #ifdef DEBUG_NEW_CONNECTION
                    LOG_I(MODULE_PREFIX, "listen NEW CONN ACCEPTED %d", (uint32_t)pNewConnection);
    #endif
                }
            }
            else
            {
                // Debug
    #ifdef WARN_ON_LISTENER_ERROR
                LOG_W(MODULE_PREFIX, "listen error in new connection %s conn %d", 
                            RdWebInterface::espIdfErrToStr(errCode),
                            (uint32_t)pNewConnection);
    #endif
                break;
            }
        }

        // Listener exited
        netconn_close(pListener);
        netconn_delete(pListener);

#endif
        // Some kind of network failure if we get here
        LOG_E(MODULE_PREFIX,"socketListenerTask connClientListener exited");

        // Delay hoping networking recovers
        delay(5000);
    }
}

#endif
