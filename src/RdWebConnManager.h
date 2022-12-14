/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebServerSettings.h"
#include <RdWebConnection.h>
#include <RdWebSocketDefs.h>
#include <RdClientListener.h>
#ifndef ESP8266
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#else
#include "ESP8266Utils.h"
#endif
#include <list>

class RdWebHandler;
class RdWebHandlerWS;
class RdWebRequestHeader;
class RdWebResponder;
class RdWebRequestParams;

class RdWebConnManager
{
public:
    // Constructor / Destructor
    RdWebConnManager();
    virtual ~RdWebConnManager();

    // Setup
    void setup(RaftWebServerSettings& settings);

    // Service
    void service();
    
    // Listen for client connections
    void listenForClients(int port, uint32_t numConnSlots)
    {
        _connClientListener.listenForClients(port, numConnSlots);
    }

    // Handler
    bool addHandler(RdWebHandler* pHandler);

    // Add response headers
    void addResponseHeader(RdJson::NameValuePair headerInfo)
    {
        _stdResponseHeaders.push_back(headerInfo);
    }

    // Get new responder
    // NOTE: this returns a new object or NULL
    // NOTE: if a new object is returned the caller is responsible for deleting it when appropriate
    RdWebResponder* getNewResponder(const RdWebRequestHeader& header, 
                const RdWebRequestParams& params, RdHttpStatusCode& statusCode);

    // Get standard response headers
    std::list<RdJson::NameValuePair>* getStdResponseHeaders()
    {
        return &_stdResponseHeaders;
    }

    // Get server settings
    RaftWebServerSettings getServerSettings()
    {
        return _webServerSettings;
    }

    // Check if channel can send a message
    bool canSend(uint32_t& channelID, bool& noConn);

    // Send a message on a channel
    bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, 
                bool allWebSockets, uint32_t channelID);

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

private:
#ifndef ESP8266
    // New connection queue
    QueueHandle_t _newConnQueue;
    static const int _newConnQueueMaxLen = 10;
#endif

    // Mutex for handling endpoints
    SemaphoreHandle_t _endpointsMutex;

    // Web server settings
    RaftWebServerSettings _webServerSettings;

    // Handlers
    std::list<RdWebHandler*> _webHandlers;

    // Standard response headers
    std::list<RdJson::NameValuePair> _stdResponseHeaders;

    // Connections
    std::vector<RdWebConnection> _webConnections;

    // Client Connection Listener
    RdClientListener _connClientListener;

    // Client connection handler task
    static void clientConnHandlerTask(void* pvParameters);

    // Helpers
    bool accommodateConnection(RdClientConnBase* pClientConn);
    bool findEmptySlot(uint32_t& slotIx);
    void serviceConnections();
    bool allocateWebSocketChannelID(uint32_t& channelID);
    // Handle an incoming connection
    bool handleNewConnection(RdClientConnBase* pClientConn);

};
