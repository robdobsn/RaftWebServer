/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @file		main
/// @brief		Example main file for the perftest example webserver
/// @details	Handles initialisation and startup of the system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SYSTEM_NAME "PerftestWebServerExample"
#define SYSTEM_VERSION "1.0"
#define DEFAULT_FRIENDLY_NAME SYSTEM_NAME

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#endif

#ifndef SSID
#define SSID "SET_SSID_HERE"
#endif
#ifndef PASSWORD
#define PASSWORD "SET_PASSWORD_HERE"
#endif

static const char* MODULE_PREFIX = "MainTask";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Includes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <esp_heap_caps.h>

#include <CommsCoreIF.h>
#include <NetworkSystem.h>
#include <FileSystem.h>
#include <RaftWebServer.h>
#include <RaftWebHandlerStaticFiles.h>
#include <RaftWebHandlerRestAPI.h>
#include <RestAPIEndpointManager.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Standard Entry Point
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RestAPIEndpointManager restAPIEndpointManager;

// Mapping from web-server method to RESTAPI method enums
RestAPIEndpoint::EndpointMethod convWebToRESTAPIMethod(RaftWebServerMethod method)
{
    switch(method)
    {
        case WEB_METHOD_POST: return RestAPIEndpoint::ENDPOINT_POST;
        case WEB_METHOD_PUT: return RestAPIEndpoint::ENDPOINT_PUT;
        case WEB_METHOD_DELETE: return RestAPIEndpoint::ENDPOINT_DELETE;
        case WEB_METHOD_OPTIONS: return RestAPIEndpoint::ENDPOINT_OPTIONS;
        default: return RestAPIEndpoint::ENDPOINT_GET;
    }
}

bool matchEndpoint(const char* url, RaftWebServerMethod method,
                    RaftWebServerRestEndpoint& endpoint)
{
    // Rest API match
    RestAPIEndpoint::EndpointMethod restAPIMethod = convWebToRESTAPIMethod(method);
    RestAPIEndpoint* pEndpointDef = restAPIEndpointManager.getMatchingEndpoint(url, restAPIMethod, false);
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

void testEndpointCallback(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    respStr = "Hello from testEndpointCallback";
}

extern "C" void app_main(void)
{
    // Initialize flash
    esp_err_t flashInitResult = nvs_flash_init();
    if (flashInitResult != ESP_OK)
    {
        // Error message
        ESP_LOGE(MODULE_PREFIX, "nvs_flash_init() failed with error %s (%d)", esp_err_to_name(flashInitResult), flashInitResult);

        // Clear flash if possible
        if ((flashInitResult == ESP_ERR_NVS_NO_FREE_PAGES) || (flashInitResult == ESP_ERR_NVS_NEW_VERSION_FOUND))
        {
            esp_err_t flashEraseResult = nvs_flash_erase();
            if (flashEraseResult != ESP_OK)
            {
                ESP_LOGE(MODULE_PREFIX, "nvs_flash_erase() failed with error %s (%d)", 
                                esp_err_to_name(flashEraseResult), flashEraseResult);
            }
            flashInitResult = nvs_flash_init();
            if (flashInitResult != ESP_OK)
            {
                // Error message
                ESP_LOGW(MODULE_PREFIX, "nvs_flash_init() failed a second time with error %s (%d)", 
                                esp_err_to_name(flashInitResult), flashInitResult);
            }
        }
    }

    // Setup networking
    networkSystem.setup(true, false, SYSTEM_NAME, true, false, false);
    networkSystem.configureWiFi(SSID, PASSWORD, SYSTEM_NAME, "", "");

    // File system
    fileSystem.setup(true, false, false, false, -1, -1, -1, -1, false, false);

    // WebServer
    RaftWebServer webServer;

    // Settings
    RaftWebServerSettings settings(80, 
            10, 
            false, 
            true, 
            0,
            9,
            3000,
            1000,
            CommsCoreIF::CHANNEL_ID_REST_API);
    webServer.setup(settings);

    // Log out system info
    ESP_LOGI(MODULE_PREFIX, "%s %s (built " __DATE__ " " __TIME__ ") Heap %d", 
                        SYSTEM_NAME, SYSTEM_VERSION, heap_caps_get_free_size(MALLOC_CAP_8BIT));

    // Add a REST API endpoint
    restAPIEndpointManager.addEndpoint("test", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                        testEndpointCallback,
                        "test");

    // Web server static files
    String baseUrl = "/";
    String baseFolder = ("/" + fileSystem.getDefaultFSRoot());
    RaftWebHandlerStaticFiles* pHandlerFiles = new RaftWebHandlerStaticFiles(baseUrl.c_str(), baseFolder.c_str(), NULL, "index.html");
    bool handlerAddOk = webServer.addHandler(pHandlerFiles);
    LOG_I(MODULE_PREFIX, "serveStaticFiles url %s folder %s addResult %s", baseUrl.c_str(), baseFolder.c_str(), 
                handlerAddOk ? "OK" : "FILE SERVER DISABLED");
    if (!handlerAddOk)
        delete pHandlerFiles;

    // Add an API handler
    RaftWebHandlerRestAPI* pHandlerAPI = new RaftWebHandlerRestAPI("/api", &matchEndpoint);
    handlerAddOk = webServer.addHandler(pHandlerAPI);
    LOG_I(MODULE_PREFIX, "serveAPI url %s addResult %s", "/api", handlerAddOk ? "OK" : "API SERVER DISABLED");
    if (!handlerAddOk)
        delete pHandlerAPI;

    // Loop forever
    while (1)
    {
        // Yield for 1 tick
        vTaskDelay(1);

        // Service network
        networkSystem.service();

        // Service webserver
        webServer.service();
    }
}
