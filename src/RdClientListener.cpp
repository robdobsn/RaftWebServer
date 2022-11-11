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
        int socketId = socket(AF_INET , SOCK_STREAM , 0);
        if (socketId < 0)
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
        int bind_err = bind(socketId, (struct sockaddr *)&bindAddr, sizeof(bindAddr));
        if (bind_err != 0)
        {
            LOG_W(MODULE_PREFIX, "socketListenerTask failed to bind on port %d errno %d",
                                port, errno);
            shutdown(socketId, 0);
            close(socketId);
            vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
            continue;
        }

        // Listen for clients
        int listen_error = listen(socketId, numConnSlots);
        if (listen_error != 0)
        {
            LOG_W(MODULE_PREFIX, "socketListenerTask failed to listen errno %d", errno);
            shutdown(socketId, 0);
            close(socketId);
            vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
            continue;
        }
        LOG_I(MODULE_PREFIX, "socketListenerTask listening");

        // Wait for connection
        while (true)
        {
            // Client info
            struct sockaddr_storage clientInfo;
            socklen_t clientInfoLen = sizeof(clientInfo);
            int sockClient = accept(socketId, (struct sockaddr *)&clientInfoLen, &clientInfoLen);
            if(sockClient < 0)
            {
                LOG_W(MODULE_PREFIX, "socketListenerTask failed to accept %d", errno);
                bool socketReconnNeeded = false;
                switch(errno)
                {
                    case ENETDOWN:
                    case EPROTO:
                    case ENOPROTOOPT:
                    case EHOSTDOWN:
                    case EHOSTUNREACH:
                    case EOPNOTSUPP:
                    case ENETUNREACH:
                        vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
                        break;
                    case EWOULDBLOCK:
                        break;
                    default:
                        socketReconnNeeded = true;
                        break;
                }
                if (socketReconnNeeded)
                    break;
                continue;
            }
            else
            {
    #ifdef DEBUG_NEW_CONNECTION
                LOG_I(MODULE_PREFIX, "socketListenerTask newConn handle %d", sockClient);
    #endif

                // Construct an RdClientConnNetconn object
                RdClientConnBase* pClientConn = new RdClientConnSockets(sockClient);

                // Hand off the connection to the connection manager via a callback
                if (!(_handOffNewConnCB && _handOffNewConnCB(pClientConn)))
                {
                    // Debug
    #ifdef DEBUG_NEW_CONNECTION
                    LOG_I(MODULE_PREFIX, "listen NEW CONN REJECTED %p", pClientConn);
    #endif
                    // No room so delete (which closes the connection)
                    delete pClientConn;
                }
                else
                {

                    // Debug
    #ifdef DEBUG_NEW_CONNECTION
                    LOG_I(MODULE_PREFIX, "listen NEW CONN ACCEPTED %p", pClientConn);
    #endif
                }
            }
        }

        // Listener exited
        shutdown(socketId, 0);
        close(socketId);
        LOG_E(MODULE_PREFIX,"socketListenerTask socket closed");

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
