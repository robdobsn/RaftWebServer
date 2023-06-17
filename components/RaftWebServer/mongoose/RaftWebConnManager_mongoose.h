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

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <mongoose.h>
#endif

class RaftWebHandler;

class RaftWebConnManager_mongoose
{
public:
    // Constructor / Destructor
    RaftWebConnManager_mongoose();
    virtual ~RaftWebConnManager_mongoose();

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
    bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, 
                bool allWebSockets, uint32_t channelID)
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

    // Handlers
    std::list<RaftWebHandler*> _webHandlers;

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    // Is setup
    bool _isSetup = false;

    // Mongoose event manager
    mg_mgr _mongooseMgr;

    // Mongoose listening address
    String _mongooseListeningAddr;

    // Static connection handler
    static void staticEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data);

    // Non-static connection handler
    void eventHandler(struct mg_connection *c, int ev, void *ev_data);
#endif

};
