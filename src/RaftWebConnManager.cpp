/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include "RaftWebConnManager.h"
#include "RaftWebConnection.h"
#include "RaftWebHandler.h"
#include "RaftWebHandlerWS.h"
#include "RaftWebResponder.h"
#include <stdint.h>
#include <string.h>
#include <RaftUtils.h>
#ifdef ESP8266
#include "ESP8266Utils.h"
#endif

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

RaftWebConnManager::RaftWebConnManager()
{
    // Connection queue
    _newConnQueue = nullptr;

    // Mutex controlling endpoint access
    _endpointsMutex = xSemaphoreCreateMutex();

    // Setup callback for new connections
    _connClientListener.setHandOffNewConnCB(std::bind(&RaftWebConnManager::handleNewConnection, this, std::placeholders::_1));
}

RaftWebConnManager::~RaftWebConnManager()
{
    if (_endpointsMutex)
        vSemaphoreDelete(_endpointsMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager::setup(RaftWebServerSettings &settings)
{
    // Store settings
    _webServerSettings = settings;

    // Create slots
    _webConnections.resize(_webServerSettings._numConnSlots);

#ifndef ESP8266
    // Create queue for new connections
    _newConnQueue = xQueueCreate(_newConnQueueMaxLen, sizeof(RaftClientConnBase*));
#endif

#ifdef USE_THREAD_FOR_CLIENT_CONN_SERVICING
    // Start task to service connections
    xTaskCreatePinnedToCore(&clientConnHandlerTask, "clientConnTask", RD_WEB_CONN_STACK_SIZE, this, 6, NULL, 0);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager::service()
{
#ifndef USE_THREAD_FOR_CLIENT_CONN_SERVICING
    serviceConnections();
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Client Connection Handler Task
// Handles client connections received on a queue and processes their HTTP requests
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager::clientConnHandlerTask(void *pvParameters)
{
#ifdef USE_THREAD_FOR_CLIENT_CONN_SERVICING
    // Get pointer to specific RaftWebServer object
    RaftWebConnManager *pConnMgr = (RaftWebConnManager *)pvParameters;

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

void RaftWebConnManager::serviceConnections()
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
        
#ifndef ESP8266
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
#endif // ESP8266
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accommodate new connections if possible
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager::accommodateConnection(RaftClientConnBase* pClientConn)
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

bool RaftWebConnManager::findEmptySlot(uint32_t &slotIdx)
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

bool RaftWebConnManager::addHandler(RaftWebHandler *pHandler)
{
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

RaftWebResponder *RaftWebConnManager::getNewResponder(const RaftWebRequestHeader &header,
                                                  const RaftWebRequestParams &params, 
                                                  RaftHttpStatusCode &statusCode)
{
    // Iterate handlers to find one that gives a responder
    statusCode = HTTP_STATUS_NOTFOUND;
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        if (pHandler)
        {
            // Get a responder
            RaftWebResponder *pResponder = pHandler->getNewResponder(header, params,
                                                _webServerSettings, statusCode);

#ifdef DEBUG_NEW_RESPONDER
            LOG_I(MODULE_PREFIX, "getNewResponder url %s handlerType %s handlerBaseURL %s result %s httpStatus %s",
                  header.URL.c_str(), 
                  pHandler->getName(),
                  pHandler->getBaseURL().c_str(),
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
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if channel is ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager::canSend(uint32_t& channelID, bool& noConn)
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

bool RaftWebConnManager::sendMsg(const uint8_t* pBuf, uint32_t bufLen,
                                        bool allChannels, uint32_t channelID)
{
    bool anyOk = false;
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

        // Flag indicating we should send
        bool sendOnThisSocket = allChannels;
        if (!sendOnThisSocket)
        {
            // Get responder
            RaftWebResponder *pResponder = _webConnections[i].getResponder();
            if (!pResponder)
                continue;

            // Get channelID
            uint32_t usedChannelID = 0;
            if (!pResponder->getChannelID(usedChannelID))
                continue;

            // Check for the channelID of the message
            if (usedChannelID == channelID)
                sendOnThisSocket = true;
        }

        // Send if appropriate
        if (sendOnThisSocket)
            anyOk |= _webConnections[i].sendOnConn(pBuf, bufLen);
    }
    return anyOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send to all server-side events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager::serverSideEventsSendMsg(const char *eventContent, const char *eventGroup)
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

bool RaftWebConnManager::handleNewConnection(RaftClientConnBase* pClientConn)
{
#ifndef ESP8266
#ifdef DEBUG_WEB_CONN_MANAGER
    LOG_I(MODULE_PREFIX, "handleNewConnection %d", pClientConn->getClientId());
#endif
    // Add to queue for handling
    return xQueueSendToBack(_newConnQueue, &pClientConn, pdMS_TO_TICKS(10)) == pdTRUE;
#else  // ESP8266
    // Get any new connection from queue
    if (pClientConn)
    {
        // Put the connection into our connection list if we can
        if (!accommodateConnection(pClientConn))
        {
            // Debug
            LOG_W(MODULE_PREFIX, "Can't handle connId %d", pClientConn->getClientId());
            return false;
        }
    }
    return true;
#endif // ESP8266
}