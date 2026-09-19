/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdint.h>
#include <string.h>
#include "Logger.h"
#include "RaftWebConnManager.h"
#include "RaftWebConnection.h"
#include "RaftWebHandler.h"
#include "RaftWebHandlerWS.h"
#include "RaftWebResponder.h"
#include "RaftUtils.h"
#include "RaftMainTask.h"
#include "esp_heap_caps.h"

const static char* MODULE_PREFIX = "WebConnMgr";

// Note: connection servicing (HTTP, REST API and WebSocket receive/send) runs on the main task from loop()
// Servicing connections from a separate task is not supported as REST API handlers would then race
// the loop() code of the SysMods they call into

#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
#include "esp_heap_trace.h"
#endif

// Warn
#define WARN_ON_NO_EMPTY_SLOTS_FOR_CONNECTION

// Debug
// #define DEBUG_WEB_CONN_MANAGER
// #define DEBUG_WEB_SERVER_HANDLERS
// #define DEBUG_WEBSOCKETS
// #define DEBUG_WEBSOCKETS_SEND
// #define DEBUG_WEBSOCKETS_SEND_DETAIL
// #define DEBUG_NEW_RESPONDER
// #define DEBUG_WEBCONN_SERVICE_TIMING
// #define DEBUG_CAN_SEND_TIMING

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnManager::RaftWebConnManager() :
    _newConnQueue(_newConnQueueMaxLen)
{
    // Setup callback for new connections
    _connClientListener.setHandOffNewConnCB(std::bind(&RaftWebConnManager::handleNewConnection, this, std::placeholders::_1));
}

RaftWebConnManager::~RaftWebConnManager()
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

void RaftWebConnManager::setup(const RaftWebServerSettings &settings)
{
    // Store settings
    _webServerSettings = settings;

    // Create slots
    _webConnections.resize(_webServerSettings.numConnSlots);

    // Core for listener task (an invalid core would assert in xTaskCreatePinnedToCore so fall back to core 0)
    uint32_t taskCore = settings.taskCore;
#ifdef portNUM_PROCESSORS
    if (taskCore >= (uint32_t)portNUM_PROCESSORS)
    {
        LOG_W(MODULE_PREFIX, "setup taskCore %d invalid - using core 0", (int)taskCore);
        taskCore = 0;
    }
#endif

	// Start task to handle listen for connections - pinned to the configured core (default 0, alongside lwIP)
	// Note: this task only accepts connections and hands them to the main task via _newConnQueue
	RaftThread_start(_socketListenerTaskHandle, &socketListenerTask, this,
            settings.taskStackSize, "socketLstnTask",
            settings.taskPriority, taskCore, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager::loop()
{
    // Service connections (on the main task)
    serviceConnections();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Socket Listener Task
// Listen for connections and add to queue for handling (by the main task)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager::socketListenerTask(void* pvParameters) 
{
	// Get pointer to specific object
	RaftWebConnManager* pWebConnMgr = (RaftWebConnManager*)pvParameters;

    // Listen for client connections
    pWebConnMgr->listenForClients(pWebConnMgr->getWebServerSettings().serverTCPPort, 
                    pWebConnMgr->getWebServerSettings().numConnSlots);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service Connections
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager::serviceConnections()
{
#ifdef DEBUG_WEBCONN_SERVICE_TIMING
    // Check if time to report
    if (Raft::isTimeout(millis(), _debugLastReportMs, 5000))
    {
        LOG_I(MODULE_PREFIX, "serviceConnections existing %d new %d", 
              (int)_debugTimerExistingConns.getMaxUs(), 
              (int)_debugTimerNewConns.getMaxUs());
        _debugLastReportMs = millis();
        _debugTimerExistingConns.clear();
        _debugTimerNewConns.clear();
    }
    _debugTimerExistingConns.started();
#endif

    // Service existing connections or close them if inactive
    for (RaftWebConnection &webConn : _webConnections)
    {
        // Service connection
        webConn.loop();
    }

    // Check heap after servicing all connections
#ifdef DEBUG_HEAP_ON_LIFECYCLE
    if (!heap_caps_check_integrity_all(true))
    {
        ESP_LOGE(MODULE_PREFIX, "HEAP CORRUPT after serviceConnections loop");
    }
#endif

#ifdef DEBUG_WEBCONN_SERVICE_TIMING
    _debugTimerExistingConns.ended();
#endif

    // Get any new connection from queue
#ifdef DEBUG_WEBCONN_SERVICE_TIMING
    _debugTimerNewConns.started();
#endif

    RaftClientConnBase* pClientConn = nullptr;
    if (_newConnQueue.get(pClientConn, 0))
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

        // Check heap after accommodating new connection
#ifdef DEBUG_HEAP_ON_LIFECYCLE
        if (!heap_caps_check_integrity_all(true))
        {
            ESP_LOGE(MODULE_PREFIX, "HEAP CORRUPT after accommodateConnection");
        }
#endif
    }

#ifdef DEBUG_WEBCONN_SERVICE_TIMING
    _debugTimerNewConns.ended();
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accommodate new connections if possible
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager::accommodateConnection(RaftClientConnBase* pClientConn)
{
    // Handle the new connection if we can
    uint32_t slotIdx = 0;
    if (!findEmptySlot(slotIdx))
    {
#ifdef WARN_ON_NO_EMPTY_SLOTS_FOR_CONNECTION
        LOG_W(MODULE_PREFIX, "accommodateConnection no empty slot for connClient %d", pClientConn->getClientId());
#endif
        return false;
    }

        // Debug
#ifdef DEBUG_WEB_CONN_MANAGER
    LOG_I(MODULE_PREFIX, "accommodateConnection connClient %d", pClientConn->getClientId());
#endif

    // Place new connection in slot - after this point the WebConnection is responsible for deleting
    if (!_webConnections[slotIdx].setNewConn(pClientConn, this, _webServerSettings.sendBufferMaxLen,
                    _webServerSettings.clearPendingDurationMs))
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
// Check if a channel is currently connected (does not perform send-readiness checks)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager::isChannelConnected(uint32_t channelID)
{
    // Main task only (connections are not protected by locks)
    RAFT_CHECK_MAIN_TASK(MODULE_PREFIX, "isChannelConnected");

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
            return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager::addHandler(RaftWebHandler *pHandler, bool highPriority)
{
    // Check handler valid
    if (!pHandler)
        return false;
        
    // Give handler pointer to connManager
    pHandler->setConnManager(this);

    // Give handler the web-server settings
    pHandler->setWebServerSettings(_webServerSettings);

    // Check if we can add this handler
    if (pHandler->isFileHandler() && !_webServerSettings.enableFileServer)
    {
#ifdef DEBUG_WEB_SERVER_HANDLERS
        LOG_I(MODULE_PREFIX, "addHandler NOT ADDING %s as file server disabled", pHandler->getName());
#endif
        return false;
    }
    else if (pHandler->isWebSocketHandler() && (!_webServerSettings.enableWebSockets))
    {
#ifdef DEBUG_WEB_SERVER_HANDLERS
        LOG_I(MODULE_PREFIX, "addHandler NOT ADDING %s as no websocket configs", pHandler->getName());
#endif
        return false;
    }

#ifdef DEBUG_WEB_SERVER_HANDLERS
    LOG_I(MODULE_PREFIX, "addHandler %s", pHandler->getName());
#endif
    if (highPriority)
        _webHandlers.push_front(pHandler);
    else
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
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if channel is ready to send
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager::canSendBufOnChannel(uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn)
{
    // Main task only (connections are not protected by locks)
    RAFT_CHECK_MAIN_TASK(MODULE_PREFIX, "canSendBufOnChannel");

#ifdef DEBUG_CAN_SEND_TIMING
    uint64_t startUs = micros();
    uint32_t iterCount = 0;
#endif

    // Find websocket responder corresponding to channel
    for (uint32_t i = 0; i < _webConnections.size(); i++)
    {
#ifdef DEBUG_CAN_SEND_TIMING
        iterCount++;
#endif

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
#ifdef DEBUG_CAN_SEND_TIMING
            uint64_t beforeIsReadyUs = micros();
#endif
            bool result = pResponder->isReadyToSend();
#ifdef DEBUG_CAN_SEND_TIMING
            uint64_t endUs = micros();
            uint32_t totalUs = endUs - startUs;
            uint32_t isReadyUs = endUs - beforeIsReadyUs;
            if (totalUs > 1000) // Log if > 1ms
            {
                LOG_I(MODULE_PREFIX, "canSendBufOnChannel chanID %d totalUs %d isReadyUs %d iters %d connIdx %d result %d",
                            channelID, totalUs, isReadyUs, iterCount, i, result);
            }
#endif
            return result;
        }
    }

#ifdef DEBUG_CAN_SEND_TIMING
    uint64_t endUs = micros();
    uint32_t totalUs = endUs - startUs;
    if (totalUs > 1000) // Log if > 1ms
    {
        LOG_I(MODULE_PREFIX, "canSendBufOnChannel chanID %d NOT FOUND totalUs %d iters %d",
                    channelID, totalUs, iterCount);
    }
#endif

    // If channel doesn't exist (maybe it has just closed) then
    // indicate no connection so that messages can be discarded
    noConn = true;
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send buffer on channel
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager::sendBufOnChannel(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
{
    // Main task only (the send path is not protected by locks)
    RAFT_CHECK_MAIN_TASK(MODULE_PREFIX, "sendBufOnChannel");

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
        sendOk = pResponder->encodeAndSendData(pBuf, bufLen);

        // Debug
#ifdef DEBUG_WEBSOCKETS_SEND
        LOG_I(MODULE_PREFIX, "sendMsg webConn %d active %d responder %p chanID %d sendOk %d",
              i,
              _webConnections[i].isActive(),
              _webConnections[i].getResponder(),
              usedChannelID,
              sendOk);
#endif

    }
    return sendOk;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send to all server-side events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager::serverSideEventsSendMsg(const char *eventContent, const char *eventGroup)
{
    // Main task only (the send path is not protected by locks)
    RAFT_CHECK_MAIN_TASK(MODULE_PREFIX, "serverSideEventsSendMsg");

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
#ifdef DEBUG_WEB_CONN_MANAGER
    LOG_I(MODULE_PREFIX, "handleNewConnection %d", pClientConn->getClientId());
#endif
    // Add to queue for handling
    return _newConnQueue.put(pClientConn, 10);
}