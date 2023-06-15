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

#ifndef FEATURE_WEB_SERVER_USE_ESP_IDF
#include "RaftWebConnDefs.h"
#include "RaftWebConnManager.h"
#else
#include "RaftWebConnManagerEspIdf.h"
#endif

#ifdef ESP8266
#include <ESP8266WiFi.h>
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
#ifndef FEATURE_WEB_SERVER_USE_ESP_IDF
        return _connManager.canSend(channelID, noConn);
#else
        return true;
#endif
    }

    // Send message on a channel
    bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, 
                bool allChannels, uint32_t channelID)
    {
#ifndef FEATURE_WEB_SERVER_USE_ESP_IDF
        return _connManager.sendMsg(pBuf, bufLen, allChannels, channelID);
#else
        // TODO - implement
        return true;
#endif
    }

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

private:

    // Comms interface
#if defined(ESP8266)
    WiFiServer* _pWiFiServer;
#elif !defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
    // Helpers
    static void socketListenerTask(void* pvParameters);
#endif

    // Settings
    RaftWebServerSettings _webServerSettings;

#ifndef FEATURE_WEB_SERVER_USE_ESP_IDF
    // Connection manager
    RaftWebConnManager _connManager;
#else
    // Connection manager
    RaftWebConnManagerEspIdf _connManager;
#endif

};

