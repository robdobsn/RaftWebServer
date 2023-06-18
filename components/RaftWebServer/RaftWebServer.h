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
#include <RdJson.h>

#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
#undef FEATURE_WEB_SERVER_USE_ESP_IDF
#undef FEATURE_WEB_SERVER_USE_MONGOOSE
#include <RaftWebConnManager_original.h>
#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#undef FEATURE_WEB_SERVER_USE_ESP_IDF
#undef FEATURE_WEB_SERVER_USE_ORIGINAL
#include <RaftWebConnManager_mongoose.h>
#else
#error "No web server implementation selected"
#endif

class RaftWebServer
{
public:

    // Constructor
    RaftWebServer();

    // Setup the web server and start listening
    void setup(RaftWebServerSettings& settings); 

    // Service
    void service();
    
    // Configure
    void addResponseHeader(RdJson::NameValuePair headerInfo);
    
    // Handler
    bool addHandler(RaftWebHandler* pHandler);

    // Check if channel can send
    bool canSend(uint32_t channelID, bool& noConn)
    {
        return _connManager.canSend(channelID, noConn);
    }

    // Send message on a channel
    bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
    {
        return _connManager.sendMsg(pBuf, bufLen, channelID);
    }

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

private:

#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
    // Connection manager
    RaftWebConnManager_original _connManager;
#else
    // Connection manager
    RaftWebConnManager_mongoose _connManager;
#endif

};

