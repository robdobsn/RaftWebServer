/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include "RaftWebConnManagerEspIdf.h"
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

const static char* MODULE_PREFIX = "WebConnMgrEspIdf";

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

RaftWebConnManagerEspIdf::RaftWebConnManagerEspIdf()
{
    // Mutex controlling endpoint access
    _endpointsMutex = xSemaphoreCreateMutex();

    // TODO
    // // Setup callback for new connections
    // _connClientListener.setHandOffNewConnCB(std::bind(&RaftWebConnManagerEspIdf::handleNewConnection, this, std::placeholders::_1));
}

RaftWebConnManagerEspIdf::~RaftWebConnManagerEspIdf()
{
    // Delete handlers
    for (RaftWebHandler *pHandler : _webHandlers)
    {
        delete pHandler;
    }

    // Delete mutex
    if (_endpointsMutex)
        vSemaphoreDelete(_endpointsMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManagerEspIdf::setup(RaftWebServerSettings &settings)
{
    // Store settings
    _webServerSettings = settings;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    // TODO handle other config like server port

    _serverHandle = nullptr;
    if (httpd_start(&_serverHandle, &config) == ESP_OK)
    {
        LOG_I(MODULE_PREFIX, "httpd_start OK");
    }
    else
    {
        LOG_E(MODULE_PREFIX, "httpd_start failed");
    }

    // // TODO 
    // // Register handler
    // httpd_uri_t uri_root = {
    //     .uri = "/",
    //     .method = HTTP_GET,
    //     .handler = get_req_handler,
    //     .user_ctx = this};
    // if (httpd_register_uri_handler(_serverHandle, &uri_root) == ESP_OK)
    // {
    //     LOG_I(MODULE_PREFIX, "httpd_register_uri_handler OK");
    // }
    // else
    // {
    //     LOG_E(MODULE_PREFIX, "httpd_register_uri_handler failed");
    // }

    // TODO
//     // Create slots
//     _webConnections.resize(_webServerSettings._numConnSlots);

// #ifndef ESP8266
//     // Create queue for new connections
//     _newConnQueue = xQueueCreate(_newConnQueueMaxLen, sizeof(RaftClientConnBase*));
// #endif

// #ifdef USE_THREAD_FOR_CLIENT_CONN_SERVICING
//     // Start task to service connections
//     xTaskCreatePinnedToCore(&clientConnHandlerTask, "clientConnTask", RD_WEB_CONN_STACK_SIZE, this, 6, NULL, 0);
// #endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManagerEspIdf::service()
{
    // TODO
// #ifndef USE_THREAD_FOR_CLIENT_CONN_SERVICING
//     serviceConnections();
// #endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManagerEspIdf::addHandler(RaftWebHandler *pHandler)
{
    // Check handler valid
    if (!pHandler)
        return false;

    // Give handler the web-server settings
    pHandler->setWebServerSettings(_webServerSettings);

    // Give handler the standard headers
    pHandler->setStandardHeaders(_stdResponseHeaders);

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

    // TODO - generalise this
    // Register handler with esp idf
    String matchPath = pHandler->getBaseURL();
    if (!matchPath.endsWith("/"))
        matchPath += "/";
    matchPath += "*";
    httpd_uri_t uri_root = {
        .uri = matchPath.c_str(),
        .method = HTTP_GET,
        .handler = staticESPIDFRequestHandler,
        .user_ctx = pHandler};
    if (httpd_register_uri_handler(_serverHandle, &uri_root) == ESP_OK)
    {
        LOG_I(MODULE_PREFIX, "httpd_register_uri_handler OK");
    }
    else
    {
        LOG_E(MODULE_PREFIX, "httpd_register_uri_handler failed");
    }

    return true;
}

// char off_resp[] = "<!DOCTYPE html><html><head><style type=\"text/css\">html {  font-family: Arial;  display: inline-block;  margin: 0px auto;  text-align: center;}h1{  color: #070812;  padding: 2vh;}.button {  display: inline-block;  background-color: #b30000; //red color  border: none;  border-radius: 4px;  color: white;  padding: 16px 40px;  text-decoration: none;  font-size: 30px;  margin: 2px;  cursor: pointer;}.button2 {  background-color: #364cf4; //blue color}.content {   padding: 50px;}.card-grid {  max-width: 800px;  margin: 0 auto;  display: grid;  grid-gap: 2rem;  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));}.card {  background-color: white;  box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);}.card-title {  font-size: 1.2rem;  font-weight: bold;  color: #034078}</style>  <title>ESP32 WEB SERVER</title>  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">  <link rel=\"icon\" href=\"data:,\">  <link rel=\"stylesheet\" href=\"https://use.fontawesome.com/releases/v5.7.2/css/all.css\"    integrity=\"sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr\" crossorigin=\"anonymous\">  <link rel=\"stylesheet\" type=\"text/css\" ></head><body>  <h2>ESP32 WEB SERVER</h2>  <div class=\"content\">    <div class=\"card-grid\">      <div class=\"card\">        <p><i class=\"fas fa-lightbulb fa-2x\" style=\"color:#c81919;\"></i>     <strong>GPIO2</strong></p>        <p>GPIO state: <strong> ON</strong></p>        <p>          <a href=\"/led2on\"><button class=\"button\">ON</button></a>          <a href=\"/led2off\"><button class=\"button button2\">OFF</button></a>        </p>      </div>    </div>  </div><div id=\"webSocketState\">WebSocket is connected!</div></body></html>";

// esp_err_t send_web_page(httpd_req_t *req)
// {
//     int response = httpd_resp_send(req, off_resp, HTTPD_RESP_USE_STRLEN);
//     return response;
// }

// esp_err_t get_req_handler(httpd_req_t *req)
// {
//     return send_web_page(req);
// }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send to all server-side events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManagerEspIdf::serverSideEventsSendMsg(const char *eventContent, const char *eventGroup)
{
    // TODO
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Request handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t RaftWebConnManagerEspIdf::staticESPIDFRequestHandler(httpd_req_t *req)
{
    // return get_req_handler(req);

    // Check valid
    if (!req->user_ctx)
        return ESP_FAIL;

    // Debug
    LOG_I(MODULE_PREFIX, "staticESPIDFRequestHandler %s", req->uri);

    // TODO handle adding response headers + content type CORS etc

    // Get handler from user context
    RaftWebHandler *pHandler = (RaftWebHandler *)req->user_ctx;

    // Handle request
    return pHandler->handleRequest(req);

    // // Check if a responder for this request has been set already
    // if (req->sess_ctx == nullptr)
    // {
    //     // Form the params to getNewResponder
    //     RaftWebRequestHeader header;

    //     // Extract query string
    //     header.URIAndParams = req->uri;
    //     header.URL = header.URIAndParams.substring(0, header.URIAndParams.indexOf("?"));
    //     header.params = header.URIAndParams.substring(header.URIAndParams.indexOf("?") + 1);

    //     // TODO - extract method
    //     header.extract.method = WEB_METHOD_GET;
    //     header.reqConnType = REQ_CONN_TYPE_HTTP;

    //     // TODO - extract headers
    //     for (auto& headerName: potentialHeaderNames())
    //     {
    //         // Get length of header str
    //         size_t valueLen = httpd_req_get_hdr_value_len(req, headerName.c_str());
    //         if (valueLen != 0)
    //         {
    //             // Get name
    //             RdJson::NameValuePair headerNameValue;
    //             headerNameValue.name = headerName;

    //             // Prepare vector for value
    //             std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> headerValueVec;
    //             headerValueVec.resize(valueLen + 1);

    //             // Get header value
    //             httpd_req_get_hdr_value_str(req, headerName.c_str(), 
    //                 (char*)headerValueVec.data(), valueLen + 1);

    //             // Convert to str
    //             Raft::strFromBuffer(headerValueVec.data(), valueLen, headerNameValue.value);

    //             // Add to headers
    //             header.nameValues.push_back(headerNameValue);
    //         }
    //     }

    //     // Form the params to getNewResponder
    //     RaftWebRequestParams params(nullptr);
                
    //     // TODO sort out rawSendOnConn
    //             // std::bind(&RaftWebConnManagerEspIdf::rawSendOnConn, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    //     // Debug
    //     LOG_I(MODULE_PREFIX, "creating session for %s params %s fullURI %s method %s numHeaders %d", 
    //             header.URL.c_str(),
    //             header.params.c_str(),
    //             header.URIAndParams.c_str(),
    //             RaftWebInterface::getHTTPMethodStr(header.extract.method),
    //             header.nameValues.size());

    //     // // Create responder
    //     RaftHttpStatusCode statusCode = HTTP_STATUS_NOTFOUND;
    //     RaftWebResponder *pResponder = pHandler->getNewResponder(header, params, statusCode);

    //     LOG_I(MODULE_PREFIX, "created responder %p", pResponder);

    //     // Set responder
    //     req->sess_ctx = pResponder;

    //     // Set the free_ctx to ensure the responder is cleaned up properly
    //     req->free_ctx = staticESPIDFResponderFreeContext;

    //     // call the responder
    //     // TODO - check if this is needed
    //     RaftWebConnection webConn;
    //     pResponder->startResponding(webConn);

    //     // TODO 
    //     // Service the connection

    // }

    // // Handle request
    // if (req->sess_ctx)
    // {
    //     // Get responder
    //     RaftWebResponder *pResponder = (RaftWebResponder *)req->sess_ctx;

    //     uint32_t respSize = 0;
    //     do
    //     {
    //         // Get response
    //         uint8_t* pRespBuffer = nullptr;
    //         respSize = pResponder->getResponseNext(pRespBuffer, pHandler->getMaxResponseSize());

    //         // Send response
    //         esp_err_t err = httpd_resp_send_chunk(req, (const char*)pRespBuffer, respSize);

    //         // Debug
    //         LOG_I(MODULE_PREFIX, "sent response %p size %d err %d responderActive %d", 
    //                 pRespBuffer, respSize, err, pResponder->isActive());
    //     }
    //     while ((respSize > 0) && (pResponder->isActive()));

    //     // Send final chunk with size 0
    //     httpd_resp_send_chunk(req, (const char*)nullptr, 0);
    // }

    return ESP_OK;
}

// Static free context handler
void RaftWebConnManagerEspIdf::staticESPIDFResponderFreeContext(void *ctx)
{
    // Get the responder
    RaftWebResponder* pResponder = (RaftWebResponder*)ctx;

    // Debug
    LOG_I(MODULE_PREFIX, "freeing responder %p", pResponder);

    // Delete
    delete pResponder;
}
