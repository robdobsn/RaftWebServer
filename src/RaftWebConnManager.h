/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebServerSettings.h"
#include <RaftWebConnection.h>
#include <RaftWebSocketDefs.h>
#include <RaftClientListener.h>
#ifndef ESP8266
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#else
#include "ESP8266Utils.h"
#endif
#include <list>

class RaftWebHandler;
class RaftWebHandlerWS;
class RaftWebRequestHeader;
class RaftWebResponder;
class RaftWebRequestParams;

class RaftWebConnManager
{
public:
    // Constructor / Destructor
    RaftWebConnManager();
    virtual ~RaftWebConnManager();

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
    bool addHandler(RaftWebHandler* pHandler);

    // Add response headers
    void addResponseHeader(RdJson::NameValuePair headerInfo)
    {
        _stdResponseHeaders.push_back(headerInfo);
    }

    // Get new responder
    // NOTE: this returns a new object or NULL
    // NOTE: if a new object is returned the caller is responsible for deleting it when appropriate
    RaftWebResponder* getNewResponder(const RaftWebRequestHeader& header, 
                const RaftWebRequestParams& params, RaftHttpStatusCode& statusCode);

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
    std::list<RaftWebHandler*> _webHandlers;

    // Standard response headers
    std::list<RdJson::NameValuePair> _stdResponseHeaders;

    // Connections
    std::vector<RaftWebConnection> _webConnections;

    // Client Connection Listener
    RaftClientListener _connClientListener;

    // Client connection handler task
    static void clientConnHandlerTask(void* pvParameters);

    // Helpers
    bool accommodateConnection(RaftClientConnBase* pClientConn);
    bool findEmptySlot(uint32_t& slotIx);
    void serviceConnections();
    bool allocateWebSocketChannelID(uint32_t& channelID);
    // Handle an incoming connection
    bool handleNewConnection(RaftClientConnBase* pClientConn);

};
