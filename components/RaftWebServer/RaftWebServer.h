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
#include <RaftJson.h>

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <RaftWebConnManager_mongoose.h>
#else
#include <RaftWebConnManager.h>
#endif

class RaftWebServer
{
public:

    // Constructor
    RaftWebServer();

    // Setup the web server and start listening
    void setup(const RaftWebServerSettings& settings); 

    // Service
    void service();
    
    // Handler
    bool addHandler(RaftWebHandler* pHandler);

    // Check if channel can send
    bool canSendBufferOnChannel(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
    {
        return _connManager.canSendBufOnChannel(channelID, msgType, noConn);
    }

    // Send message on a channel
    bool sendBufferOnChannel(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
    {
        return _connManager.sendBufOnChannel(pBuf, bufLen, channelID);
    }

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

private:

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    // Connection manager
    RaftWebConnManager_mongoose _connManager;
#else
    // Connection manager
    RaftWebConnManager _connManager;
#endif

};

