/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftWebServerSettings.h>
#include <RdJson.h>
#include <list>
#include "esp_http_server.h"

class RaftWebHandler;

class RaftWebConnManagerEspIdf
{
public:
    // Constructor / Destructor
    RaftWebConnManagerEspIdf();
    virtual ~RaftWebConnManagerEspIdf();

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

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

private:

    // Mutex for handling endpoints
    SemaphoreHandle_t _endpointsMutex;

    // Web server settings
    RaftWebServerSettings _webServerSettings;

    // Standard response headers
    std::list<RdJson::NameValuePair> _stdResponseHeaders;

    // Server handle
    void* _serverHandle;

    // Handlers
    std::list<RaftWebHandler*> _webHandlers;

    // Static request handler
    static esp_err_t staticESPIDFRequestHandler(httpd_req_t *req);

    // Static free context handler
    static void staticESPIDFResponderFreeContext(void *ctx);

    // Possible header names - ESP IDF doesn't seem to have a way to iterate through headers in requests
    static std::list<String> potentialHeaderNames()
    { 
        return {
            "Accept-Encoding"
        };
    }

};
