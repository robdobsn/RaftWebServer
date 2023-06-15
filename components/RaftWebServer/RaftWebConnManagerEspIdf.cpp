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

#ifdef FEATURE_WEB_SERVER_USE_ESP_IDF
#include "esp_http_server.h"
#endif

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
    if (_endpointsMutex)
        vSemaphoreDelete(_endpointsMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

char off_resp[] = "<!DOCTYPE html><html><head><style type=\"text/css\">html {  font-family: Arial;  display: inline-block;  margin: 0px auto;  text-align: center;}h1{  color: #070812;  padding: 2vh;}.button {  display: inline-block;  background-color: #b30000; //red color  border: none;  border-radius: 4px;  color: white;  padding: 16px 40px;  text-decoration: none;  font-size: 30px;  margin: 2px;  cursor: pointer;}.button2 {  background-color: #364cf4; //blue color}.content {   padding: 50px;}.card-grid {  max-width: 800px;  margin: 0 auto;  display: grid;  grid-gap: 2rem;  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));}.card {  background-color: white;  box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);}.card-title {  font-size: 1.2rem;  font-weight: bold;  color: #034078}</style>  <title>ESP32 WEB SERVER</title>  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">  <link rel=\"icon\" href=\"data:,\">  <link rel=\"stylesheet\" href=\"https://use.fontawesome.com/releases/v5.7.2/css/all.css\"    integrity=\"sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr\" crossorigin=\"anonymous\">  <link rel=\"stylesheet\" type=\"text/css\" ></head><body>  <h2>ESP32 WEB SERVER</h2>  <div class=\"content\">    <div class=\"card-grid\">      <div class=\"card\">        <p><i class=\"fas fa-lightbulb fa-2x\" style=\"color:#c81919;\"></i>     <strong>GPIO2</strong></p>        <p>GPIO state: <strong> ON</strong></p>        <p>          <a href=\"/led2on\"><button class=\"button\">ON</button></a>          <a href=\"/led2off\"><button class=\"button button2\">OFF</button></a>        </p>      </div>    </div>  </div><div id=\"webSocketState\">WebSocket is connected!</div></body></html>";

esp_err_t send_web_page(httpd_req_t *req)
{
    int response = httpd_resp_send(req, off_resp, HTTPD_RESP_USE_STRLEN);
    return response;
}

esp_err_t get_req_handler(httpd_req_t *req)
{
    return send_web_page(req);
}

void RaftWebConnManagerEspIdf::setup(RaftWebServerSettings &settings)
{
    // Store settings
    _webServerSettings = settings;

#ifdef FEATURE_WEB_SERVER_USE_ESP_IDF
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    _serverHandle = nullptr;
    if (httpd_start(&_serverHandle, &config) == ESP_OK)
    {
        LOG_I(MODULE_PREFIX, "httpd_start OK");
    }
    else
    {
        LOG_E(MODULE_PREFIX, "httpd_start failed");
    }

    // TODO 
    // Register handler
    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_req_handler,
        .user_ctx = this};
    if (httpd_register_uri_handler(_serverHandle, &uri_root) == ESP_OK)
    {
        LOG_I(MODULE_PREFIX, "httpd_register_uri_handler OK");
    }
    else
    {
        LOG_E(MODULE_PREFIX, "httpd_register_uri_handler failed");
    }

#endif

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

