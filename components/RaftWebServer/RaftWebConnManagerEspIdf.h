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
    
    // Listen for client connections
    void listenForClients(int port, uint32_t numConnSlots)
    {
        // ESP IDF handles this itself
    }

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

private:

    // Mutex for handling endpoints
    SemaphoreHandle_t _endpointsMutex;

    // Web server settings
    RaftWebServerSettings _webServerSettings;

    // Standard response headers
    std::list<RdJson::NameValuePair> _stdResponseHeaders;

    // Server handle
    void* _serverHandle;
};
