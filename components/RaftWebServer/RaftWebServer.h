/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebServerSettings.h"
#include "RaftWebHandler.h"
#include "RaftJson.h"
#include "RaftWebConnManager.h"

class RaftWebServer
{
public:

    // Constructor
    RaftWebServer();

    // Setup the web server and start listening
    void setup(const RaftWebServerSettings& settings); 

    // Service - must be called from the main task (the task running SysManager::loop())
    void loop();
    
    // Handler
    bool addHandler(RaftWebHandler* pHandler, bool highPriority = false);

    // NOTE: the channel/send functions below are main task only (the task running SysManager::loop())
    // The send path is not thread-safe - other tasks must hand-off to the main task (e.g. via a queue)

    // Check if channel can send (main task only)
    bool canSendBufferOnChannel(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
    {
        return _connManager.canSendBufOnChannel(channelID, msgType, noConn);
    }

    // Check if a channel is currently connected (does not perform send-readiness checks) (main task only)
    bool isChannelConnected(uint32_t channelID)
    {
        return _connManager.isChannelConnected(channelID);
    }

    // Send message on a channel (main task only)
    bool sendBufferOnChannel(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
    {
        return _connManager.sendBufOnChannel(pBuf, bufLen, channelID);
    }

    // Send to all server-side events (main task only)
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

private:

    // Connection manager
    RaftWebConnManager _connManager;

};

