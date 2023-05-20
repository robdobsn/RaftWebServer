/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @file		main
/// @brief		Example main file for the basic example webserver
/// @details	Handles initialisation and startup of the system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SYSTEM_NAME "BasicWebServerExample"
#define SYSTEM_VERSION "1.0"
#define DEFAULT_FRIENDLY_NAME SYSTEM_NAME

#define SSID "SET_SSID_HERE"
#define PASSWORD "SET_PASSWORD_HERE"

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
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
#include <RdWebHandlerStaticFiles.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Standard Entry Point
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

    // // API Endpoints
    // RestAPIEndpointManager _restAPIEndpointManager;
    // _sysModManager.setRestAPIEndpoints(_restAPIEndpointManager);

    // // FileManager
    // FileManager _fileManager("FileManager", defaultSystemConfig, &_sysTypeConfig, nullptr);

    // NetworkManager
    // NetworkManager _networkManager("NetMan", defaultSystemConfig, &_sysTypeConfig, nullptr, DEFAULT_HOSTNAME);

    // // Log manager
    // LogManager _LogManager("LogManager", defaultSystemConfig, &_sysTypeConfig, &_sysModMutableConfig);
    
    // Log out system info
    ESP_LOGI(MODULE_PREFIX, "%s %s (built " __DATE__ " " __TIME__ ") Heap %d", 
                        SYSTEM_NAME, SYSTEM_VERSION, heap_caps_get_free_size(MALLOC_CAP_8BIT));


//     // SysTypeManager endpoints
//     _sysTypeManager.addRestAPIEndpoints(_restAPIEndpointManager);

//     // Initialise the system module manager here so that API endpoints are registered
//     // before file system ones
//     _sysModManager.setup();

    // Web server static files
    String baseUrl = "/";
    String baseFolder = ("/" + fileSystem.getDefaultFSRoot());
    RdWebHandlerStaticFiles* pHandler = new RdWebHandlerStaticFiles(baseUrl.c_str(), baseFolder.c_str(), NULL, "index.html");
    bool handlerAddOk = webServer.addHandler(pHandler);
    LOG_I(MODULE_PREFIX, "serveStaticFiles url %s folder %s addResult %s", baseUrl.c_str(), baseFolder.c_str(), 
                handlerAddOk ? "OK" : "FILE SERVER DISABLED");
    if (!handlerAddOk)
        delete pHandler;

    // _webServer.serveStaticFiles("/files/local", "/local/");
    // _webServer.serveStaticFiles("/files/sd", "/sd/");
    // _webServer.serveStaticFiles("/", ("/" + fileSystem.getDefaultFSRoot()).c_str());

// #ifdef FEATURE_WEB_SERVER_OR_WEB_SOCKETS
//     // Start webserver
//     _webServer.beginServer();
// #endif

    // Loop forever
    while (1)
    {
        // Yield for 1 tick
        vTaskDelay(1);

        // Service network
        networkSystem.service();

        // Service webserver
        webServer.service();

//         // Service all the system modules
//         _sysModManager.service();

//         // Test and Monitoring
// #ifdef DEBUG_SHOW_TASK_INFO
//         if (Raft::isTimeout(millis(), lasttaskdumpms, DEBUG_SHOW_TASK_MS))
//         {
//             tasks_info();
//             lasttaskdumpms = millis();
//         }
// #endif
// #ifdef DEBUG_SHOW_RUNTIME_STATS
//         if (Raft::isTimeout(millis(), laststatsdumpms, DEBUG_SHOW_RUNTIME_STATS_MS))
//         {
//             runtime_stats();
//             laststatsdumpms = millis();
//         }
// #endif
    }
}
