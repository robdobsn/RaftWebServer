/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include "RaftWebServerSettings.h"
#include "CommsChannelMsg.h"
#include "RaftWebConnection.h"
#include "RaftWebSocketDefs.h"
#include "RaftClientListener.h"
#include "ExecTimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// #define DEBUG_WEBCONN_SERVICE_TIMING

class RaftWebHandler;
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
    void setup(const RaftWebServerSettings& settings);

    // Service
    void loop();
    
    // Listen for client connections
    void listenForClients(int port, uint32_t numConnSlots)
    {
        _connClientListener.listenForClients(port, numConnSlots);
    }

    // Handler
    bool addHandler(RaftWebHandler* pHandler, bool highPriority = false);

    // Get new responder
    // NOTE: this returns a new object or NULL
    // NOTE: if a new object is returned the caller is responsible for deleting it when appropriate
    RaftWebResponder* getNewResponder(const RaftWebRequestHeader& header, 
                const RaftWebRequestParams& params, RaftHttpStatusCode& statusCode);

    // Get server settings
    const RaftWebServerSettings& getServerSettings() const
    {
        return _webServerSettings;
    }

    // Check if channel can send a message
    bool canSendBufOnChannel(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);

    // Send a buffer on a channel
    bool sendBufOnChannel(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID);

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

    // Get web server settings
    const RaftWebServerSettings& getWebServerSettings() const
    {
        return _webServerSettings;
    }

private:
    // New connection queue
    QueueHandle_t _newConnQueue;
    static const int _newConnQueueMaxLen = 10;

    // Web server settings
    RaftWebServerSettings _webServerSettings;

    // Handlers
    std::list<RaftWebHandler*> _webHandlers;

    // Connections
    std::vector<RaftWebConnection> _webConnections;

    // Client Connection Listener
    RaftClientListener _connClientListener;

    // Client connection handler task
    static void clientConnHandlerTask(void* pvParameters);

    // Helpers
    static void socketListenerTask(void* pvParameters);
    bool accommodateConnection(RaftClientConnBase* pClientConn);
    bool findEmptySlot(uint32_t& slotIx);
    void serviceConnections();
    bool allocateWebSocketChannelID(uint32_t& channelID);
    // Handle an incoming connection
    bool handleNewConnection(RaftClientConnBase* pClientConn);

#ifdef DEBUG_WEBCONN_SERVICE_TIMING
    // Debug
    ExecTimer _debugTimerExistingConns;
    ExecTimer _debugTimerNewConns;
    uint32_t _debugLastReportMs = 0;
#endif
};
