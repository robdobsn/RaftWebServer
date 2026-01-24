/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include "RaftWebServerSettings.h"
#include "RaftJson.h"

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include "mongoose.h"
#endif

class RaftWebHandler;

class RaftWebConnManager_mongoose
{
public:
    // Constructor / Destructor
    RaftWebConnManager_mongoose();
    virtual ~RaftWebConnManager_mongoose();

    // Setup
    void setup(const RaftWebServerSettings& settings);

    // Service
    void loop();
    
    // Handler
    bool addHandler(RaftWebHandler* pHandler);

    // Check if a channel is currently connected (does not perform send-readiness checks)
    bool isChannelConnected(uint32_t channelID);

    // Get server settings
    const RaftWebServerSettings& getServerSettings() const
    {
        return _webServerSettings;
    }

    // Check if channel can send a message
    bool canSendBufOnChannel(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn);

    // Send a buffer on a channel
    bool sendMsgBufOnChannel(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID);

    // Send to all server-side events
    void serverSideEventsSendMsg(const char* eventContent, const char* eventGroup);

    // Convert mongoose event to string
    static const char* mongooseEventToString(int ev);

private:

    // Web server settings
    RaftWebServerSettings _webServerSettings;

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
    static void staticEventHandler(struct mg_connection *pConn, int ev, void *ev_data, void *fn_data);

    // Non-static connection handler
    void eventHandler(struct mg_connection *pConn, int ev, void *ev_data);

    // Debug event
    void debugEvent(struct mg_connection *pConn, int ev, void *ev_data);
#endif

};
