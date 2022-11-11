/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebServerSettings.h"
#include "RdWebConnManager.h"
#include "RdWebHandler.h"
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
    bool addHandler(RdWebHandler* pHandler);

    // Check if channel can send
    bool canSend(uint32_t channelID, bool& noConn)
    {
        return _connManager.canSend(channelID, noConn);
    }

    // Send message on a channel
    bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, 
                bool allChannels, uint32_t channelID)
    {
        return _connManager.sendMsg(pBuf, bufLen, allChannels, channelID);
    }

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

private:

#ifndef ESP8266
    // Helpers
    static void socketListenerTask(void* pvParameters);
#else
    WiFiServer* _pWiFiServer;
#endif
    // Settings
    RaftWebServerSettings _webServerSettings;

    // Connection manager
    RdWebConnManager _connManager;

};

