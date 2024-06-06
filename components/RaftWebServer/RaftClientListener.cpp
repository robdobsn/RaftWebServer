/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftClientListener.h"
#include "RaftClientConnNetconn.h"
#include "RaftClientConnSockets.h"
#include "RaftWebInterface.h"
#include "Logger.h"
#include "ArduinoTime.h"
#include "lwip/api.h"
#include "lwip/sockets.h"
#include "esp_tls.h"

static const char *MODULE_PREFIX = "RaftClientListener";

#define WEB_CONN_USE_BERKELEY_SOCKETS

#define WARN_ON_LISTENER_ERROR
// #define DEBUG_NEW_CONNECTION

esp_tls_cfg_server_t configure_tls()
{
    // Define your CA certificate here
    extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");
    extern const uint8_t server_cert_pem_end[] asm("_binary_server_cert_pem_end");
    extern const uint8_t server_key_pem_start[] asm("_binary_server_key_pem_start");
    extern const uint8_t server_key_pem_end[] asm("_binary_server_key_pem_end");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    esp_tls_cfg_server_t cfg = {
        .cacert_buf = server_cert_pem_start,
        .cacert_bytes = (unsigned int) (server_cert_pem_end - server_cert_pem_start),
        .servercert_buf = server_cert_pem_start,
        .servercert_bytes = (unsigned int) (server_cert_pem_end - server_cert_pem_start),
        .serverkey_buf = server_key_pem_start,
        .serverkey_bytes = (unsigned int) (server_key_pem_end - server_key_pem_start),
    };
#pragma GCC diagnostic pop
    return cfg;
}

void RaftClientListener::listenForClients(int port, uint32_t numConnSlots)
{
    // Get the TLS configuration
    esp_tls_cfg_server_t tlsConfig = configure_tls();
    
    // TODO fixx this
    bool enableTLS = false;
    
    // Loop forever
    while (1)
    {

#ifdef WEB_CONN_USE_BERKELEY_SOCKETS

        // Create socket
        int listenerSocketId = socket(AF_INET , SOCK_STREAM, 0);
        if (listenerSocketId < 0)
        {
            LOG_W(MODULE_PREFIX, "socketListenerTask failed to create socket");
            vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
            continue;
        }

        // Set non-blocking
        fcntl(listenerSocketId, F_SETFL, fcntl(listenerSocketId, F_GETFL, 0) | O_NONBLOCK);

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
                        LOG_W(MODULE_PREFIX, "socketListenerTask (listenerSocketId %d port %d) failed to accept errno %d", listenerSocketId, port, errorNumber);
                        vTaskDelay(WEB_SERVER_SOCKET_RETRY_DELAY_MS / portTICK_PERIOD_MS);
                        consecErrorCount++;
                        break;
                    case EWOULDBLOCK:
                    case EINPROGRESS:
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
                vTaskDelay(1);
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

                // Check TLS
                esp_tls_t *pTLS = nullptr;
                if (enableTLS)
                {
                    // Initialize TLS connection
                    pTLS = esp_tls_init();
                    if (esp_tls_server_session_create(&tlsConfig, sockClient, pTLS) != 0)
                    {
                        LOG_W(MODULE_PREFIX, "socketListenerTask (listenerSocketId %d port %d) TLS connection failed", listenerSocketId, port);
                        esp_tls_conn_destroy(pTLS);
                        close(sockClient);
                        continue;
                    }
                }

                // Construct an RaftClientConnSockets object with TLS
                RaftClientConnBase* pClientConn = new RaftClientConnSockets(sockClient, pTLS, TRACE_CONN);

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
                // Construct an RaftClientConnNetconn object
                RaftClientConnBase* pClientConn = new RaftClientConnNetconn(pNewConnection);

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
                            RaftWebInterface::espIdfErrToStr(errCode),
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
