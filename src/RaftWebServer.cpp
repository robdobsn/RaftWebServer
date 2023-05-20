/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include "RaftWebServer.h"
#include "RaftWebConnManager.h"
#include "RaftWebConnDefs.h"
#include <stdint.h>
#include <string.h>

#ifndef ESP8266
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif
#endif

static const char *MODULE_PREFIX = "RaftWebServer";

#define INFO_WEB_SERVER_SETUP
// #define DEBUG_NEW_CONNECTION

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebServer::RaftWebServer()
{
#ifdef ESP8266
    _pWiFiServer = NULL;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup Web
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebServer::setup(RaftWebServerSettings& settings)
{
	// Settings
    _webServerSettings = settings;
    
#ifdef INFO_WEB_SERVER_SETUP
    LOG_I(MODULE_PREFIX, "setup port %d numConnSlots %d wsEn %d enableFileServer %d", 
            settings._serverTCPPort, settings._numConnSlots, 
            settings._enableWebSockets, settings._enableFileServer);
#endif

    // Start network interface if not already started
#ifndef ESP8266
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0)
        if (esp_netif_init() != ESP_OK) {
            LOG_E(MODULE_PREFIX, "could not start netif");
        }
#else
        tcpip_adapter_init();
#endif
#else
    if (!_pWiFiServer)
        _pWiFiServer = new WiFiServer(settings._serverTCPPort);
    if (!_pWiFiServer)
        return;
    _pWiFiServer->begin();
#endif

	// Setup connection manager
	_connManager.setup(_webServerSettings);

#ifndef ESP8266
	// Start task to handle listen for connections
	xTaskCreatePinnedToCore(&socketListenerTask,"socketLstnTask", 
            settings._taskStackSize,
            this, 
            settings._taskPriority, 
            NULL, 
            settings._taskCore);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebServer::service()
{
#ifdef ESP8266
    // Check for new connection
    if (_pWiFiServer)
    {
        WiFiClient client = _pWiFiServer->available();
        if (client)
        {
            WiFiClient* pClient = new WiFiClient(client);
            // Handle the connection
#ifdef DEBUG_NEW_CONNECTION
            LOG_I(MODULE_PREFIX, "New client");
#endif
            if (!_connManager.handleNewConnection(pClient))
            {
                LOG_W(MODULE_PREFIX, "No room so client stopped");
                pClient->stop();
                delete pClient;
            }
        }
    }
#endif
    // Service connection manager
    _connManager.service();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebServer::addHandler(RaftWebHandler* pHandler)
{
    return _connManager.addHandler(pHandler);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Web Server Task
// Listen for connections and add to queue for handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ESP8266
void RaftWebServer::socketListenerTask(void* pvParameters) 
{
	// Get pointer to specific RaftWebServer object
	RaftWebServer* pWS = (RaftWebServer*)pvParameters;

    // Listen for client connections
    pWS->_connManager.listenForClients(pWS->_webServerSettings._serverTCPPort, 
                    pWS->_webServerSettings._numConnSlots);
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configure
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebServer::addResponseHeader(RdJson::NameValuePair headerInfo)
{
    _connManager.addResponseHeader(headerInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send to all server-side events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebServer::serverSideEventsSendMsg(const char* eventContent, const char* eventGroup)
{
	_connManager.serverSideEventsSendMsg(eventContent, eventGroup);
}
