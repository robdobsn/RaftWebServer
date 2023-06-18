/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include "RaftWebConnManager_espidf.h"
#include "RaftWebHandler.h"
#include <stdint.h>
#include <string.h>
#include <RaftUtils.h>

const static char* MODULE_PREFIX = "WebConnMgrEspIdf";

// Debug
// #define DEBUG_WEB_SERVER_ESP_IDF
// #define DEBUG_WEB_SERVER_HANDLERS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnManager_espidf::RaftWebConnManager_espidf()
{
}

RaftWebConnManager_espidf::~RaftWebConnManager_espidf()
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

void RaftWebConnManager_espidf::setup(RaftWebServerSettings &settings)
{
    // Store settings
    _webServerSettings = settings;

#if defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    // TODO handle other config like server port

    // Start server
    _serverHandle = nullptr;
    if (httpd_start(&_serverHandle, &config) == ESP_OK)
    {
        LOG_I(MODULE_PREFIX, "httpd_start OK");
    }
    else
    {
        LOG_E(MODULE_PREFIX, "httpd_start failed");
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_espidf::service()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnManager_espidf::addHandler(RaftWebHandler *pHandler)
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
        // TODO implement websockets
#ifdef DEBUG_WEB_SERVER_HANDLERS
        LOG_I(MODULE_PREFIX, "addHandler NOT ADDING %s as no websocket configs", pHandler->getName());
#endif
        return false;
    }

#ifdef DEBUG_WEB_SERVER_HANDLERS
    LOG_I(MODULE_PREFIX, "addHandler %s", pHandler->getName());
#endif
    _webHandlers.push_back(pHandler);

// #if defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
//     // TODO - generalise this
//     // Register handler with esp idf
//     String matchPath = pHandler->getBaseURL();
//     if (!matchPath.endsWith("/"))
//         matchPath += "/";
//     matchPath += "*";
//     httpd_uri_t uri_root = {
//         .uri = matchPath.c_str(),
//         .method = HTTP_GET,
//         .handler = staticRequestHandler,
//         .user_ctx = pHandler};
//     if (httpd_register_uri_handler(_serverHandle, &uri_root) == ESP_OK)
//     {
//         LOG_I(MODULE_PREFIX, "httpd_register_uri_handler OK");
//     }
//     else
//     {
//         LOG_E(MODULE_PREFIX, "httpd_register_uri_handler failed");
//     }
// #endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send to all server-side events
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnManager_espidf::serverSideEventsSendMsg(const char *eventContent, const char *eventGroup)
{
    // TODO
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Request handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO remove
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

#if defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
esp_err_t RaftWebConnManager_espidf::staticRequestHandler(httpd_req_t *req)
{
    // TODO remove
    // TEST CODE TO SEND A SINGLE PAGE
    // return get_req_handler(req);

    // Check valid
    if (!req->user_ctx)
        return ESP_FAIL;

    // Debug
    LOG_I(MODULE_PREFIX, "staticRequestHandler %s", req->uri);

    // TODO handle adding response headers + content type CORS etc

#if defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
    // Get handler from user context
    RaftWebHandler *pHandler = (RaftWebHandler *)req->user_ctx;

    // Handle request
    return pHandler->handleRequest(req);
#else
    return ESP_OK;
#endif
}
#endif
