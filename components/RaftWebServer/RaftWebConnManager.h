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
#include "RaftThreading.h"
#include "ThreadSafeQueue.h"

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

    // Service - must be called from the main task (the task running SysManager::loop())
    // All connection servicing (HTTP, REST API and WebSocket receive/send) is done from here
    void loop();
    
    // Listen for client connections
    void listenForClients(int port, uint32_t numConnSlots)
    {
        _connClientListener.listenForClients(port, numConnSlots);
    }

    // Handler
    bool addHandler(RaftWebHandler* pHandler, bool highPriority = false);

     // Check if a channel is currently connected (does not perform send-readiness checks)
     // NOTE: main task only - not thread-safe
     bool isChannelConnected(uint32_t channelID);

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

    // NOTE: the send functions below are main task only (the task running SysManager::loop())
    // The send path is not protected by locks (a connection can be closed and its responder deleted
    // by the main task at any time) so other tasks must hand-off to the main task (e.g. via a queue)
    // Calls from other tasks are detected by RAFT_CHECK_MAIN_TASK (see RaftMainTask.h)

    // Check if channel can send a message (main task only)
    bool canSendBufOnChannel(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);

    // Send a buffer on a channel (main task only)
    bool sendBufOnChannel(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID);

    // Send to all server-side events (main task only)
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

    // Get web server settings
    const RaftWebServerSettings& getWebServerSettings() const
    {
        return _webServerSettings;
    }

private:
    // New connection queue
    ThreadSafeQueue<RaftClientConnBase*> _newConnQueue;
    static const int _newConnQueueMaxLen = 10;

    // Web server settings
    RaftWebServerSettings _webServerSettings;

    // Handlers
    std::list<RaftWebHandler*> _webHandlers;

    // Connections
    std::vector<RaftWebConnection> _webConnections;

    // Client Connection Listener
    RaftClientListener _connClientListener;

    // Thread handles
    RaftThreadHandle _socketListenerTaskHandle = RAFT_THREAD_HANDLE_INVALID;

    // Helpers
    static void socketListenerTask(void* pvParameters);
    bool accommodateConnection(RaftClientConnBase* pClientConn);
    bool findEmptySlot(uint32_t& slotIx);
    void serviceConnections();
    bool allocateWebSocketChannelID(uint32_t& channelID);
    // Handle an incoming connection (called from the socket listener task - only accesses _newConnQueue)
    bool handleNewConnection(RaftClientConnBase* pClientConn);

#ifdef DEBUG_WEBCONN_SERVICE_TIMING
    // Debug
    ExecTimer _debugTimerExistingConns;
    ExecTimer _debugTimerNewConns;
    uint32_t _debugLastReportMs = 0;
#endif
};
