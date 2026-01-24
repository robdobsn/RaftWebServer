/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// WebServer
//
// Rob Dobson 2012-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "WebServer.h"
#include "WebServerResource.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "RestAPIEndpointManager.h"
#ifdef ESP_PLATFORM
#include "NetworkSystem.h"
#endif
#include "FileSystem.h"
#include "RaftWebServer.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"
#include "RaftWebHandlerStaticFiles.h"
#include "RaftWebHandlerRestAPI.h"
#include "RaftWebHandlerWS.h"

// This define enables checking of channel connection state for websockets
// Comment the following to use canSendBufferOnChannel (which gets actual busy state of channel but
// may be more expensive to call)
#define USE_IS_CHANNEL_CONNECTED_FOR_WEBSOCKETS

// #define DEBUG_WEBSERVER_WEBSOCKETS
#define DEBUG_API_WEB_CERTS

static const char* MODULE_PREFIX = "WebServer";

// Singleton
WebServer* WebServer::_pThisWebServer = nullptr;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WebServer::WebServer(const char *pModuleName, RaftJsonIF& sysConfig) 
        : RaftSysMod(pModuleName, sysConfig)
{
    // Singleton
    _pThisWebServer = this;
}

WebServer::~WebServer()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::setup()
{
    // Hook change of config
    configRegisterChangeCallback(std::bind(&WebServer::configChanged, this));

    // Apply config
    applySetup();
}

void WebServer::configChanged()
{
    // Reset config
    LOG_D(MODULE_PREFIX, "configChanged");
    applySetup();
}

void WebServer::applySetup()
{
    // Enable
    _webServerEnabled = configGetBool("enable", false);

    // Port
    _port = configGetLong("webServerPort", RaftWebServerSettings::DEFAULT_HTTP_PORT);

    // Access control allow origin all
    std::vector<String> stdRespHeaders;
    configGetArrayElems("stdRespHeaders", stdRespHeaders);

    // REST API prefix
    _restAPIPrefix = configGetString("apiPrefix", RaftWebServerSettings::DEFAULT_REST_API_PREFIX);

    // File server enable
    bool enableFileServer = configGetBool("fileServer", true);

    // Num connection slots
    uint32_t numConnSlots = configGetLong("numConnSlots", RaftWebServerSettings::DEFAULT_CONN_SLOTS);

    // Websockets
    configGetArrayElems("websockets", _webSocketConfigs);

    // Get Task settings
    uint32_t taskCore = configGetLong("taskCore", RaftWebServerSettings::DEFAULT_TASK_CORE);
    int32_t taskPriority = configGetLong("taskPriority", RaftWebServerSettings::DEFAULT_TASK_PRIORITY);
    uint32_t taskStackSize = configGetLong("taskStack", RaftWebServerSettings::DEFAULT_TASK_STACK_BYTES);

    // Get server send buffer max length
    uint32_t sendBufferMaxLen = configGetLong("sendMax", RaftWebServerSettings::DEFAULT_SEND_BUFFER_MAX_LEN);

    // Get static file paths
    String staticFilePaths = configGetString("staticFilePaths", "");

    // Clear pending duration ms
    uint32_t clearPendingDurationMs = configGetLong("clearPendingMs", 0);

    // Setup server if required
    if (_webServerEnabled)
    {
        // Start server
        if (!_isWebServerSetup)
        {
            RaftWebServerSettings settings(_port, numConnSlots, _webSocketConfigs.size() > 0, 
                    enableFileServer, taskCore, taskPriority, taskStackSize, sendBufferMaxLen,
                    CommsCoreIF::CHANNEL_ID_REST_API, stdRespHeaders, nullptr, nullptr,
                    clearPendingDurationMs);
            _raftWebServer.setup(settings);
        }

        // Serve static paths if enabled
        if (enableFileServer)
        {
            serveStaticFiles(staticFilePaths.isEmpty() ? nullptr : staticFilePaths.c_str(), nullptr);
        }
        _isWebServerSetup = true;
    }

    // Note: webSocketSetup() is called in postSetup() after CommsCore is available
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Post-setup - called after all SysMods are set up
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::postSetup()
{
    // Setup websockets now that CommsCore is available
    webSocketSetup();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::loop()
{
    // Service
    _raftWebServer.loop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    // Add endpoint to configure web server itself
    // REST API endpoints
    endpointManager.addEndpoint("webcerts", 
            RestAPIEndpoint::ENDPOINT_CALLBACK, 
            RestAPIEndpoint::ENDPOINT_POST,
            std::bind(&WebServer::apiWebCertificates, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            "webcerts/set - POST JSON web certificates for web server", 
            "application/json", 
            nullptr,
            RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
            nullptr, 
            std::bind(&WebServer::apiWebCertsBody, this, 
                    std::placeholders::_1, std::placeholders::_2, 
                    std::placeholders::_3, std::placeholders::_4,
                    std::placeholders::_5, std::placeholders::_6),
            nullptr);
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints added webcerts API");

    // Setup endpoints
    setupEndpoints();
}

void WebServer::setupEndpoints()
{
    // Handle REST endpoints
    LOG_I(MODULE_PREFIX, "setupEndpoints serverEnabled %s port %d apiPrefix %s", 
            _webServerEnabled ? "Y" : "N", _port, 
            _restAPIPrefix.c_str());
    RaftWebHandlerRestAPI* pHandler = new RaftWebHandlerRestAPI(_restAPIPrefix,
                std::bind(&WebServer::restAPIMatchEndpoint, this, std::placeholders::_1, 
                        std::placeholders::_2, std::placeholders::_3));

    // Add REST API handlers before other handlers
    if (!_raftWebServer.addHandler(pHandler, true))
        delete pHandler;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API Endpoints for configuring web server
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set system settings (completion)
/// @param reqStr request string
/// @param respStr response string
/// @param sourceInfo source information
RaftRetCode WebServer::apiWebCertificates(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Note that this is called after the body of the POST is complete
#ifdef DEBUG_API_WEB_CERTS
    LOG_I(MODULE_PREFIX, "apiWebCertificates request %s", reqStr.c_str());
#endif
    String rebootRequired = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    if (rebootRequired.equalsIgnoreCase("set"))
    {
        LOG_I(MODULE_PREFIX, "apiWebCertificates set %s certslen %zu", reqStr.c_str(), _certsTempStorage.size());
        _certsTempStorage.clear();
    }

    // Result
    Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
    return RaftRetCode::RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set system settings body
/// @param reqStr request string
/// @param pData pointer to data
/// @param len length of data
RaftRetCode WebServer::apiWebCertsBody(const String& reqStr, const uint8_t *pData, size_t len, 
                size_t index, size_t total, const APISourceInfo& sourceInfo)
{
    if (len == total)
    {
        // Form the JSON document
        _certsTempStorage.assign(pData, pData + len);
        // Make sure it is null-terminated
        if (_certsTempStorage[_certsTempStorage.size() - 1] != 0)
            _certsTempStorage.push_back(0);
        return RAFT_OK;
    }

    // Check if first block
    if (index == 0)
        _certsTempStorage.clear();

    // Append to the existing buffer
    _certsTempStorage.insert(_certsTempStorage.end(), pData, pData+len);

    // Check for complete
    if (_certsTempStorage.size() == total)
    {
        // Check the buffer is null-terminated
        if (_certsTempStorage[_certsTempStorage.size() - 1] != 0)
            _certsTempStorage.push_back(0);
    }
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static Resources
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Add resources to the web server
void WebServer::addStaticResources(const WebServerResource *pResources, int numResources)
{
}

void WebServer::addStaticResource(const WebServerResource *pResource, const char *pAliasPath)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static Files
// 
// Serve static files from the file system
// @param servePaths (comma separated and can include uri=path pairs separated by =)
// @param cacheControl (nullptr or cache control header value eg "no-cache, no-store, must-revalidate")
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::serveStaticFiles(const char* servePaths, const char* cacheControl)
{
    // Handle default serve path
    String servePathsStr = servePaths ? servePaths : "";
    if (servePathsStr.length() == 0)
    {
        servePathsStr = String("/=/") + fileSystem.getDefaultFSRoot();
        servePathsStr += ",/files/local=/local";
        servePathsStr += ",/files/sd=/sd";
    }

    // Handle file systems
    RaftWebHandlerStaticFiles* pHandler = new RaftWebHandlerStaticFiles(servePathsStr.c_str(), cacheControl);
    bool handlerAddOk = _raftWebServer.addHandler(pHandler);
    LOG_I(MODULE_PREFIX, "serveStaticFiles servePaths %s addResult %s", servePathsStr.c_str(), 
                handlerAddOk ? "OK" : "FILE SERVER DISABLED");
    if (!handlerAddOk)
        delete pHandler;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Async Events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::enableServerSideEvents(const String& eventsURL)
{
}

void WebServer::sendServerSideEvent(const char* eventContent, const char* eventGroup)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Web sockets
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void WebServer::webSocketSetup()
{
    // Comms channel
    static const CommsChannelSettings commsChannelSettings;

    // Add websocket handler
#ifdef DEBUG_WEBSERVER_WEBSOCKETS
    LOG_I(MODULE_PREFIX, "webSocketSetup num websocket configs %d", _webSocketConfigs.size());
#endif
    CommsCoreIF* pCommsCore = getCommsCore();
    if (!pCommsCore)
        return;

    // Create websockets
    for (uint32_t wsIdx = 0; wsIdx < _webSocketConfigs.size(); wsIdx++)        
    {
        // Get config
        RaftJson jsonConfig = _webSocketConfigs[wsIdx];

        // Setup WebHandler for Websockets    
        RaftWebHandlerWS* pHandler = new RaftWebHandlerWS(jsonConfig, 
                std::bind(&CommsCoreIF::inboundCanAccept, pCommsCore, 
                        std::placeholders::_1),
                std::bind(&CommsCoreIF::inboundHandleMsg, pCommsCore, 
                        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
                );
        if (!pHandler)
            continue;

        // Add handler
        if (!_raftWebServer.addHandler(pHandler))
        {
            delete pHandler;
            continue;
        }

        // Register a channel  with the protocol endpoint manager
        // for each possible connection
        uint32_t maxConn = pHandler->getMaxConnections();
        for (uint32_t connIdx = 0; connIdx < maxConn; connIdx++)
        {
            String interfaceName = jsonConfig.getString("pfix", "ws");
            String wsName = interfaceName + "_" + connIdx;
            String protocol = jsonConfig.getString("pcol", "RICSerial");
            uint32_t wsChanID = pCommsCore->registerChannel(
                    protocol.c_str(), 
                    interfaceName.c_str(),
                    wsName.c_str(),
                    [this](CommsChannelMsg& msg) { 
                        return _raftWebServer.sendBufferOnChannel(msg.getBuf(), msg.getBufLen(), 
                                msg.getChannelID());
                    },
                    [this](uint32_t channelID, CommsMsgTypeCode msgType, bool& noConn) {
                        (void)msgType;
#ifdef USE_IS_CHANNEL_CONNECTED_FOR_WEBSOCKETS
                        // Use isChannelConnected to check if there is a connection (less expensive)
                        noConn = ! _raftWebServer.isChannelConnected(channelID);
                        return !noConn;
#else
                        // Use canSendBufferOnChannel to check if we can send
                        return _raftWebServer.canSendBufferOnChannel(channelID, msgType, noConn); 
#endif
                    },
                    &commsChannelSettings);

            // Set into the websocket handler so channel IDs match up
            pHandler->setupWebSocketChannelID(connIdx, wsChanID);

            // Debug
#ifdef DEBUG_WEBSERVER_WEBSOCKETS
            LOG_I(MODULE_PREFIX, "webSocketSetup prefix %s wsName %s protocol %s maxConn %d channelID %d", 
                        interfaceName.c_str(), 
                        wsName.c_str(),
                        protocol.c_str(), 
                        maxConn, 
                        wsChanID);
#endif
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callback used to match endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool WebServer::restAPIMatchEndpoint(const char* url, RaftWebServerMethod method,
                    RaftWebServerRestEndpoint& endpoint)
{
    // Check valid
    if (!getRestAPIEndpointManager())
        return false;

    // Rest API match
    RestAPIEndpoint::EndpointMethod restAPIMethod = convWebToRESTAPIMethod(method);
    RestAPIEndpoint* pEndpointDef = getRestAPIEndpointManager()->getMatchingEndpoint(url, restAPIMethod, false);
    if (pEndpointDef)
    {
        endpoint.restApiFn = pEndpointDef->_callbackMain;
        endpoint.restApiFnBody = pEndpointDef->_callbackBody;
        endpoint.restApiFnChunk = pEndpointDef->_callbackChunk;
        endpoint.restApiFnIsReady = pEndpointDef->_callbackIsReady;
        return true;
    }
    return false;
}
