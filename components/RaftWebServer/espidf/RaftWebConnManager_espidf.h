/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftWebServerSettings.h>
#include <RdJson.h>
#include <list>

#if defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
#include "esp_http_server.h"
#endif

class RaftWebHandler;

class RaftWebConnManager_espidf
{
public:
    // Constructor / Destructor
    RaftWebConnManager_espidf();
    virtual ~RaftWebConnManager_espidf();

    // Setup
    void setup(RaftWebServerSettings& settings);

    // Service
    void service();
    
    // Handler
    bool addHandler(RaftWebHandler* pHandler);

    // Add response headers
    void addResponseHeader(RdJson::NameValuePair headerInfo)
    {
        _stdResponseHeaders.push_back(headerInfo);
    }

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
    bool canSend(uint32_t& channelID, bool& noConn)
    {
        // TODO - implement
        return true;
    }

    // Send a message on a channel
    bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
    {
        // TODO - implement
        return true;
    }

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

private:

    // Web server settings
    RaftWebServerSettings _webServerSettings;

    // Standard response headers
    std::list<RdJson::NameValuePair> _stdResponseHeaders;

    // Server handle
    void* _serverHandle;

    // Handlers
    std::list<RaftWebHandler*> _webHandlers;

#if defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
    // Static request handler
    static esp_err_t staticRequestHandler(httpd_req_t *req);
#endif

};