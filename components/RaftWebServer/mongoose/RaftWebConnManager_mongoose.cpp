/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include "RaftWebConnManager_mongoose.h"
#include "RaftWebHandler.h"
#include <stdint.h>
#include <string.h>
#include <RaftUtils.h>

const static char* MODULE_PREFIX = "WebConnMgrMongoose";

// Debug
// #define DEBUG_WEB_SERVER_MONGOOSE
// #define DEBUG_WEB_SERVER_HANDLERS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnManager_mongoose::RaftWebConnManager_mongoose()
{
}

RaftWebConnManager_mongoose::~RaftWebConnManager_mongoose()
{
    // Delete handlers
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        delete pHandler;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_mongoose::setup(RaftWebServerSettings &settings)
{
    // Store settings
    _webServerSettings = settings;

    // Create mongoose event manager
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

    // Setup listening address
    _mongooseListeningAddr = "http://0.0.0.0:" + String(settings._serverTCPPort);

#ifdef DEBUG_WEB_SERVER_MONGOOSE
    // Debug
    mg_log_set(MG_LL_DEBUG);  // Set log level
#endif

    // Init manager
    mg_mgr_init(&_mongooseMgr);

    // Listen on port
    struct mg_connection *pConn = mg_http_listen(&_mongooseMgr, _mongooseListeningAddr.c_str(), staticEventHandler, this);

    // Check if listening
    if (pConn == nullptr)
    {
        LOG_E(MODULE_PREFIX, "Cannot listen on %s", _mongooseListeningAddr.c_str());
    }
    else
    {
        LOG_I(MODULE_PREFIX, "Listening on %s", _mongooseListeningAddr.c_str());
        _isSetup = true;
    }

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_mongoose::service()
{
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    // Service mongoose
    if (_isSetup)
        mg_mgr_poll(&_mongooseMgr, 0);  
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_mongoose::addHandler(RaftWebHandler *pHandler)
{
    // Check handler valid
    if (!pHandler)
        return false;

    // Give handler the web-server settings
    pHandler->setWebServerSettings(_webServerSettings);

    // Give handler the standard headers
    pHandler->setStandardHeaders(_stdResponseHeaders);

#ifdef DEBUG_WEB_SERVER_HANDLERS
    LOG_I(MODULE_PREFIX, "addHandler %s", pHandler->getName());
#endif
    _webHandlers.push_back(pHandler);
    return true;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if channel can send a message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_mongoose::canSend(uint32_t& channelID, bool& noConn)
{
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

    // Check setup
    noConn = true;
    if (!_isSetup)
        return false;

    // See if a suitable handler exists
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        if (!pHandler->isWebSocketHandler())
            continue;
        if (pHandler->canSend(channelID, noConn))
            return true;
    }

    return false;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send a message on a channel
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_mongoose::sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
{
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

    // Check setup
    if (!_isSetup)
        return false;

    // See if a suitable handler exists
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        if (!pHandler->isWebSocketHandler())
            continue;
        if (pHandler->sendMsg(pBuf, bufLen, channelID))
            return true;
    }

#endif

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send to all server-side events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_mongoose::serverSideEventsSendMsg(const char *eventContent, const char *eventGroup)
{
    // TODO
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Request handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

void RaftWebConnManager_mongoose::staticEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data)
{
    // Use non-static function
    RaftWebConnManager_mongoose *pConnManager = (RaftWebConnManager_mongoose *)fn_data;
    if (pConnManager == NULL)
    {
        LOG_E(MODULE_PREFIX, "staticEventHandler pConnManager is NULL");
        return;
    }
    pConnManager->eventHandler(c, ev, ev_data);
}

void RaftWebConnManager_mongoose::eventHandler(struct mg_connection *c, int ev, void *ev_data)
{

#ifdef DEBUG_WEB_SERVER_MONGOOSE

    if (ev == MG_EV_HTTP_MSG)
    {
        // Debug Mongoose message
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
        struct mg_http_message tmp = {0};
#pragma GCC diagnostic pop
        mg_http_parse((char *) c->send.buf, c->send.len, &tmp);
        struct mg_str* cl = mg_http_get_header(&tmp, "Content-Length");
        struct mg_str unknown = mg_str_n("?", 1);
        if (cl == NULL) cl = &unknown;
        LOG_I(MODULE_PREFIX, "%p HTTP_MSG %.*s %.*s %.*s C-L %.*s", c, (int) hm->method.len, hm->method.ptr,
                (int) hm->uri.len, hm->uri.ptr, (int) tmp.uri.len, tmp.uri.ptr,
                (int) cl->len, cl->ptr);
    }
    else if (ev != MG_EV_POLL)
    {
        LOG_I(MODULE_PREFIX, "%p %s", c, mongooseEventToString(ev));
    }
#endif

    // Iterate through all handlers that are not file handlers to see if one can handle this request
    // This is to ensure that other handlers get the first chance to handle the request
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        if (pHandler->isFileHandler())
            continue;
        if (pHandler->handleRequest(c, ev, ev_data))
            return;
    }

    // Now iterate through file handlers
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        if (!pHandler->isFileHandler())
            continue;
        if (pHandler->handleRequest(c, ev, ev_data))
            return;
    }

    // No handler found
    LOG_E(MODULE_PREFIX, "eventHandler - no handler found");
    mg_http_reply(c, 404, "", "Content not found");

//     static const char *s_root_dir = ".";
//     static const char *s_ssi_pattern = "";
//     if (ev == MG_EV_HTTP_MSG) {
//     struct mg_http_message *hm = (struct mg_http_message *)ev_data;
//     struct mg_http_message tmp = {0};
//     struct mg_str unknown = mg_str_n("?", 1), *cl;
//     struct mg_http_serve_opts opts = {0};
//     opts.root_dir = s_root_dir;
//     opts.ssi_pattern = s_ssi_pattern;
//     mg_http_serve_dir(c, hm, &opts);
//     mg_http_parse((char *) c->send.buf, c->send.len, &tmp);
//     cl = mg_http_get_header(&tmp, "Content-Length");
//     if (cl == NULL) cl = &unknown;
//     MG_INFO(("%.*s %.*s %.*s %.*s", (int) hm->method.len, hm->method.ptr,
//              (int) hm->uri.len, hm->uri.ptr, (int) tmp.uri.len, tmp.uri.ptr,
//              (int) cl->len, cl->ptr));
//   }
}


const char* RaftWebConnManager_mongoose::mongooseEventToString(int ev)
{
#ifdef DEBUG_WEB_SERVER_MONGOOSE
    switch (ev)
    {
    case MG_EV_ERROR: return "MG_EV_ERROR";
    case MG_EV_OPEN:  return "MG_EV_OPEN";
    case MG_EV_POLL:  return "MG_EV_POLL";
    case MG_EV_RESOLVE: return "MG_EV_RESOLVE";
    case MG_EV_CONNECT: return "MG_EV_CONNECT";
    case MG_EV_ACCEPT: return "MG_EV_ACCEPT";
    case MG_EV_TLS_HS: return "MG_EV_TLS_HS";
    case MG_EV_READ: return "MG_EV_READ";
    case MG_EV_WRITE: return "MG_EV_WRITE";
    case MG_EV_CLOSE: return "MG_EV_CLOSE";
    case MG_EV_HTTP_MSG: return "MG_EV_HTTP_MSG";
    case MG_EV_HTTP_CHUNK: return "MG_EV_HTTP_CHUNK";
    case MG_EV_WS_OPEN: return "MG_EV_WS_OPEN";
    case MG_EV_WS_MSG: return "MG_EV_WS_MSG";
    case MG_EV_WS_CTL: return "MG_EV_WS_CTL";
    case MG_EV_MQTT_CMD: return "MG_EV_MQTT_CMD";
    case MG_EV_MQTT_MSG: return "MG_EV_MQTT_MSG";
    case MG_EV_MQTT_OPEN: return "MG_EV_MQTT_OPEN";
    case MG_EV_SNTP_TIME: return "MG_EV_SNTP_TIME";
    case MG_EV_USER: return "MG_EV_USER";
    default: return "UNKNOWN";
    }
#endif
    return "";
}

#endif
