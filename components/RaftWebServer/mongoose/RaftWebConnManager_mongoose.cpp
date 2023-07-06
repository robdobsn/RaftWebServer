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

// Debug
// #define DEBUG_WEB_SERVER_MONGOOSE_LOG_LEVEL MG_LL_DEBUG
// #define DEBUG_WEB_SERVER_MONGOOSE
// #define DEBUG_WEB_SERVER_EVENT
// #define DEBUG_WEB_SERVER_HANDLERS
// #define DEBUG_WEB_SERVER_EVENT_DETAILS
// #define DEBUG_WEB_SERVER_EVENT_VERBOSE

#if defined (FEATURE_WEB_SERVER_USE_MONGOOSE) || defined(DEBUG_WEB_SERVER_HANDLERS)
const static char* MODULE_PREFIX = "WebConnMgrMongoose";
#endif

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

void RaftWebConnManager_mongoose::setup(const RaftWebServerSettings &settings)
{
    // Store settings
    _webServerSettings = settings;

    // Create mongoose event manager
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

    // Setup listening address
    _mongooseListeningAddr = "http://0.0.0.0:" + String(settings._serverTCPPort);

#ifdef DEBUG_WEB_SERVER_MONGOOSE_LOG_LEVEL
    // Debug
    mg_log_set(DEBUG_WEB_SERVER_MONGOOSE_LOG_LEVEL);  // Set log level
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
#ifdef DEBUG_WEB_SERVER_MONGOOSE
        LOG_I(MODULE_PREFIX, "Listening on %s", _mongooseListeningAddr.c_str());
#endif
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

#ifdef DEBUG_WEB_SERVER_HANDLERS
    LOG_I(MODULE_PREFIX, "addHandler %s", pHandler->getName());
#endif
    _webHandlers.push_back(pHandler);
    return true;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if channel can send a message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_mongoose::canSend(uint32_t channelID, bool& noConn)
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
#endif
    return false;
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

void RaftWebConnManager_mongoose::staticEventHandler(struct mg_connection *pConn, int ev, void *ev_data, void *fn_data)
{
    // Use non-static function
    RaftWebConnManager_mongoose *pConnManager = (RaftWebConnManager_mongoose *)fn_data;
    if (pConnManager == NULL)
    {
        LOG_E(MODULE_PREFIX, "staticEventHandler pConnManager is NULL");
        return;
    }
    pConnManager->eventHandler(pConn, ev, ev_data);
}

void RaftWebConnManager_mongoose::eventHandler(struct mg_connection *pConn, int ev, void *ev_data)
{

#ifdef DEBUG_WEB_SERVER_EVENT_DETAILS
    debugEvent(pConn, ev, ev_data);
#endif

    // Check if this is a new connection
    if (ev == MG_EV_ACCEPT)
    {
        // Clear the data field as this is used for offsets, channelIDs, etc.
        memset(pConn->data, 0, MG_DATA_SIZE);
    }

    // Iterate through all handlers that are not file handlers to see if one can handle this request
    // This is to ensure that other handlers get the first chance to handle the request
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        if (pHandler->isFileHandler())
            continue;
        bool rslt = pHandler->handleRequest(pConn, ev, ev_data);
#ifdef DEBUG_WEB_SERVER_EVENT
        if (ev != MG_EV_POLL)
        {
#ifndef DEBUG_WEB_SERVER_EVENT_VERBOSE
            if (rslt)
#endif
            {
                LOG_I(MODULE_PREFIX, "eventHandler - event %s memAvailable %d handler %s rslt %d", 
                        mongooseEventToString(ev), heap_caps_get_free_size(MALLOC_CAP_8BIT), pHandler->getName(), rslt);
            }
        }
#endif
        if (rslt)
        {
            return;
        }
    }

    // Now iterate through file handlers
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        if (!pHandler->isFileHandler())
            continue;
        bool rslt = pHandler->handleRequest(pConn, ev, ev_data);
#ifdef DEBUG_WEB_SERVER_MONGOOSE
        if (ev != MG_EV_POLL)
        {
#ifndef DEBUG_WEB_SERVER_EVENT_VERBOSE
            if (rslt)
#endif
            {
                LOG_I(MODULE_PREFIX, "eventHandler - event %s handler %s rslt %d", 
                        mongooseEventToString(ev), pHandler->getName(), rslt);
            }
        }
#endif
        if (rslt)
        {
            return;
        }
    }

    // No handler found
    if (ev == MG_EV_HTTP_MSG)
    {
        mg_http_reply(pConn, 404, "", "Content not found");
    }
}

const char* RaftWebConnManager_mongoose::mongooseEventToString(int ev)
{
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
}

void RaftWebConnManager_mongoose::debugEvent(struct mg_connection *pConn, int ev, void *ev_data)
{
    
    if ((ev == MG_EV_HTTP_MSG) || (ev == MG_EV_HTTP_CHUNK))
    {
        // Debug Mongoose message
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        struct mg_str* cl = mg_http_get_header(hm, "Content-Length");
        struct mg_str unknown = mg_str_n("?", 1);
        if (cl == NULL) cl = &unknown;
        int numHeaders = 0;
        for (int i=0; i<MG_MAX_HTTP_HEADERS; i++)
        {
            if (hm->headers[i].name.len > 0)
                numHeaders++;
        }
        char buf[20] = "0";
        mg_http_get_var(&hm->query, "offset", buf, sizeof(buf));
        int offset = strtol(buf, NULL, 0);
        LOG_I(MODULE_PREFIX, "eventHandler %p %s %.*s %.*s numHdrs %d contLen %.*s query %.*s proto %.*s bodyLen %d headLen %d chunkLen %d msgLen %d offset %d", pConn, 
                mongooseEventToString(ev),
                (int) hm->method.len, hm->method.ptr,
                (int) hm->uri.len, hm->uri.ptr, 
                numHeaders,
                (int) cl->len, cl->ptr,
                (int) hm->query.len, hm->query.ptr,
                (int) hm->proto.len, hm->proto.ptr,
                hm->body.len, 
                hm->head.len, 
                hm->chunk.len, 
                hm->message.len,
                offset);

#ifdef DEBUG_WEB_SERVER_EVENT_VERBOSE
        for (int i=0; i<MG_MAX_HTTP_HEADERS; i++)
        {
            if (hm->headers[i].name.len > 0)
            {
                LOG_I(MODULE_PREFIX, "eventHandler --- hdr %d %.*s = %.*s", i, 
                        (int) hm->headers[i].name.len, hm->headers[i].name.ptr,
                        (int) hm->headers[i].value.len, hm->headers[i].value.ptr);
            }
        }
#endif

    }
    else if (ev != MG_EV_POLL)
    {
        LOG_I(MODULE_PREFIX, "%p %s", pConn, mongooseEventToString(ev));
    }
}

#endif
