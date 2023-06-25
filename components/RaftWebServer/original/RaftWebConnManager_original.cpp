/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include "RaftWebConnManager_original.h"
#include "RaftWebConnection.h"
#include "RaftWebHandler.h"
#include "RaftWebHandlerWS.h"
#include "RaftWebResponder.h"
#include <stdint.h>
#include <string.h>
#include <RaftUtils.h>

const static char* MODULE_PREFIX = "WebConnMgr";

// #define USE_THREAD_FOR_CLIENT_CONN_SERVICING

#ifdef USE_THREAD_FOR_CLIENT_CONN_SERVICING
#define RD_WEB_CONN_STACK_SIZE 5000
#endif

#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
#include "esp_heap_trace.h"
#endif

// Debug
// #define DEBUG_WEB_CONN_MANAGER
// #define DEBUG_WEB_SERVER_HANDLERS
// #define DEBUG_WEBSOCKETS
// #define DEBUG_WEBSOCKETS_SEND_DETAIL
// #define DEBUG_NEW_RESPONDER

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnManager_original::RaftWebConnManager_original()
{
    // Connection queue
    _newConnQueue = nullptr;

    // Setup callback for new connections
    _connClientListener.setHandOffNewConnCB(std::bind(&RaftWebConnManager_original::handleNewConnection, this, std::placeholders::_1));
}

RaftWebConnManager_original::~RaftWebConnManager_original()
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

void RaftWebConnManager_original::setup(const RaftWebServerSettings &settings)
{
    // Store settings
    _webServerSettings = settings;

    // Create slots
    _webConnections.resize(_webServerSettings._numConnSlots);

    // Create queue for new connections
    _newConnQueue = xQueueCreate(_newConnQueueMaxLen, sizeof(RaftClientConnBase*));

#ifdef USE_THREAD_FOR_CLIENT_CONN_SERVICING
    // Start task to service connections
    xTaskCreatePinnedToCore(&clientConnHandlerTask, "clientConnTask", RD_WEB_CONN_STACK_SIZE, this, 6, NULL, 0);
#endif

	// Start task to handle listen for connections
	xTaskCreatePinnedToCore(&socketListenerTask,"socketLstnTask", 
            settings._taskStackSize,
            this, 
            settings._taskPriority, 
            NULL, 
            settings._taskCore);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_original::service()
{
#ifndef USE_THREAD_FOR_CLIENT_CONN_SERVICING
    serviceConnections();
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Web Server Task
// Listen for connections and add to queue for handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_original::socketListenerTask(void* pvParameters) 
{
	// Get pointer to specific RaftWebServer object
	RaftWebConnManager_original* pWS = (RaftWebConnManager_original*)pvParameters;

    // Listen for client connections
    pWS->listenForClients(pWS->_webServerSettings._serverTCPPort, 
                    pWS->_webServerSettings._numConnSlots);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Client Connection Handler Task
// Handles client connections received on a queue and processes their HTTP requests
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_original::clientConnHandlerTask(void *pvParameters)
{
#ifdef USE_THREAD_FOR_CLIENT_CONN_SERVICING
    // Get pointer to specific RaftWebServer object
    RaftWebConnManager_original *pConnMgr = (RaftWebConnManager_original *)pvParameters;

    // Handle connection
    const static char *MODULE_PREFIX = "clientConnTask";

    // Debug
    LOG_I(MODULE_PREFIX, "clientConnHandlerTask starting");

    // Service connections
    while (1)
    {
        pConnMgr->serviceConnections();
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service Connections
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_original::serviceConnections()
{
    // Service existing connections or close them if inactive
    for (RaftWebConnection &webConn : _webConnections)
    {
        // Service connection
        webConn.service();
    }

    // Get any new connection from queue
    if (_newConnQueue == nullptr)
        return;
        
    RaftClientConnBase* pClientConn = nullptr;
    if (xQueueReceive(_newConnQueue, &pClientConn, 1) == pdTRUE)
    {
#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
        heap_trace_start(HEAP_TRACE_LEAKS);
#endif
        // Put the connection into our connection list if we can
        if (!accommodateConnection(pClientConn))
        {
            // Debug
            LOG_W(MODULE_PREFIX, "serviceConn can't handle connClient %d", pClientConn->getClientId());

            // Delete client (which closes any connection)
            delete pClientConn;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accommodate new connections if possible
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_original::accommodateConnection(RaftClientConnBase* pClientConn)
{
    // Handle the new connection if we can
    uint32_t slotIdx = 0;
    if (!findEmptySlot(slotIdx))
        return false;

        // Debug
#ifdef DEBUG_WEB_CONN_MANAGER
    LOG_I(MODULE_PREFIX, "accommodateConnection connClient %d", pClientConn->getClientId());
#endif

    // Place new connection in slot - after this point the WebConnection is responsible for deleting
    if (!_webConnections[slotIdx].setNewConn(pClientConn, this, _webServerSettings._sendBufferMaxLen))
        return false;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Find an empty slot
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_original::findEmptySlot(uint32_t &slotIdx)
{
    // Check for inactive slots
    for (uint32_t i = 0; i < _webConnections.size(); i++)
    {
        // Check
        if (_webConnections[i].isActive())
            continue;

        // Return inactive
        slotIdx = i;
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_original::addHandler(RaftWebHandler *pHandler)
{
    // Check handler valid
    if (!pHandler)
        return false;
        
    // Give handler the web-server settings
    pHandler->setWebServerSettings(_webServerSettings);

    // Check if we can add this handler
    if (pHandler->isFileHandler() && !_webServerSettings._enableFileServer)
    {
#ifdef DEBUG_WEB_SERVER_HANDLERS
        LOG_I(MODULE_PREFIX, "addHandler NOT ADDING %s as file server disabled", pHandler->getName());
#endif
        return false;
    }
    else if (pHandler->isWebSocketHandler() && (!_webServerSettings._enableWebSockets))
    {
#ifdef DEBUG_WEB_SERVER_HANDLERS
        LOG_I(MODULE_PREFIX, "addHandler NOT ADDING %s as no websocket configs", pHandler->getName());
#endif
        return false;
    }

#ifdef DEBUG_WEB_SERVER_HANDLERS
    LOG_I(MODULE_PREFIX, "addHandler %s", pHandler->getName());
#endif
    _webHandlers.push_back(pHandler);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get new responder
// NOTE: this returns a new object or nullptr
// NOTE: if a new object is returned the caller is responsible for deleting it when appropriate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebResponder *RaftWebConnManager_original::getNewResponder(const RaftWebRequestHeader &header,
                                                  const RaftWebRequestParams &params, 
                                                  RaftHttpStatusCode &statusCode)
{
    // Iterate handlers to find one that gives a responder
    statusCode = HTTP_STATUS_NOTFOUND;
#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        if (pHandler)
        {
            // Get a responder
            RaftWebResponder *pResponder = pHandler->getNewResponder(header, params, statusCode);

#ifdef DEBUG_NEW_RESPONDER
            LOG_I(MODULE_PREFIX, "getNewResponder url %s uriAndParams %s params %s versStr %s numHeaders %d reqConnType %d handlerType %s result %s httpStatus %s",
                  header.URL.c_str(), 
                  header.URIAndParams.c_str(),
                  header.params.c_str(),
                  header.versStr.c_str(),
                  header.nameValues.size(),
                  header.reqConnType,
                  pHandler->getName(),
                  pResponder ? "OK" : "NoMatch",
                  RaftWebInterface::getHTTPStatusStr(statusCode));
#endif

            // Return responder if there is one
            if (pResponder)
                return pResponder;

            // Check status and return status code if something matched but there was
            // another error
            if (statusCode != HTTP_STATUS_NOTFOUND)
                break;
        }
    }
#endif
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if channel is ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_original::canSend(uint32_t& channelID, bool& noConn)
{
    // Find websocket responder corresponding to channel
    for (uint32_t i = 0; i < _webConnections.size(); i++)
    {
        // Check active
        if (!_webConnections[i].isActive())
            continue;

        // Get responder
        RaftWebResponder* pResponder = _webConnections[i].getResponder();
        if (!pResponder)
            continue;

        // Get channelID
        uint32_t usedChannelID = 0;
        if (!pResponder->getChannelID(usedChannelID))
            continue;

        // Check for channelID match
        if (usedChannelID == channelID)
        {
            return pResponder->readyForData();
        }
    }

    // If channel doesn't exist (maybe it has just closed) then
    // indicate no connection so that messages can be discarded
    noConn = true;
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message on channel
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_original::sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
{
    bool sendOk = false;
    for (uint32_t i = 0; i < _webConnections.size(); i++)
    {
#ifdef DEBUG_WEBSOCKETS_SEND_DETAIL
        {
            uint32_t debugChanId = 0;
            bool debugChanIdOk = false;
            if (_webConnections[i].getResponder())
                debugChanIdOk = _webConnections[i].getResponder()->getChannelID(debugChanId);
            LOG_I(MODULE_PREFIX, "sendMsg webConn %d active %d responder %p chanID %d ",
                  i,
                  _webConnections[i].isActive(),
                  _webConnections[i].getResponder(),
                  debugChanIdOk ? debugChanId : -1);
        }
#endif

        // Check active
        if (!_webConnections[i].isActive())
            continue;

        // Get responder
        RaftWebResponder *pResponder = _webConnections[i].getResponder();
        if (!pResponder)
            continue;

        // Get channelID
        uint32_t usedChannelID = 0;
        if (!pResponder->getChannelID(usedChannelID))
            continue;

        // Check for the channelID of the message
        if (usedChannelID != channelID)
            continue;

        // Send if appropriate
        sendOk = _webConnections[i].sendOnConn(pBuf, bufLen);
    }
    return sendOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send to all server-side events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_original::serverSideEventsSendMsg(const char *eventContent, const char *eventGroup)
{
    for (uint32_t i = 0; i < _webConnections.size(); i++)
    {
        // Check active
        if (!_webConnections[i].isActive())
            continue;

        if (_webConnections[i].getHeader().reqConnType == REQ_CONN_TYPE_EVENT)
            _webConnections[i].sendOnSSEvents(eventContent, eventGroup);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Incoming connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_original::handleNewConnection(RaftClientConnBase* pClientConn)
{
#ifdef DEBUG_WEB_CONN_MANAGER
    LOG_I(MODULE_PREFIX, "handleNewConnection %d", pClientConn->getClientId());
#endif
    // Add to queue for handling
    return xQueueSendToBack(_newConnQueue, &pClientConn, pdMS_TO_TICKS(10)) == pdTRUE;
}