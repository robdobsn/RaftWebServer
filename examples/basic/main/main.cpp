/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @file		main
/// @brief		Example main file for the basic example webserver
/// @details	Handles initialisation and startup of the system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SYSTEM_NAME "BasicWebServerExample"
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
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "CommsCoreIF.h"
#include "NetworkSystem.h"
#include "FileSystem.h"
#include "RaftWebServer.h"
#include "RaftWebHandlerStaticFiles.h"

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
    std::vector<String> stdRespHeaders = {
        {"Access-Control-Allow-Origin", "*"}
    };
    RaftWebServerSettings settings(
            RaftWebServerSettings::DEFAULT_HTTP_PORT, 
            RaftWebServerSettings::DEFAULT_CONN_SLOTS, 
            RaftWebServerSettings::DEFAULT_ENABLE_WEBSOCKETS, 
            RaftWebServerSettings::DEFAULT_ENABLE_FILE_SERVER, 
            RaftWebServerSettings::DEFAULT_TASK_CORE,
            RaftWebServerSettings::DEFAULT_TASK_PRIORITY,
            RaftWebServerSettings::DEFAULT_TASK_STACK_BYTES,
            RaftWebServerSettings::DEFAULT_SEND_BUFFER_MAX_LEN,
            CommsCoreIF::CHANNEL_ID_REST_API,
            stdRespHeaders,
            nullptr,
            nullptr);
    webServer.setup(settings);

    // Log out system info
    ESP_LOGI(MODULE_PREFIX, "%s %s (built " __DATE__ " " __TIME__ ") Heap %d", 
                        SYSTEM_NAME, SYSTEM_VERSION, heap_caps_get_free_size(MALLOC_CAP_8BIT));

    // Web server static files
    String servePaths = "/" + fileSystem.getDefaultFSRoot() + ",/files/local=/local,/files/sd=/sd";
    RaftWebHandlerStaticFiles* pHandlerFiles = new RaftWebHandlerStaticFiles(servePaths.c_str(), NULL);
    bool handlerAddOk = webServer.addHandler(pHandlerFiles);
    LOG_I(MODULE_PREFIX, "serveStaticFiles paths %s addResult %s", 
                        servePaths.c_str(), 
                        handlerAddOk ? "OK" : "FILE SERVER DISABLED");
    if (!handlerAddOk)
        delete pHandlerFiles;

    // Loop forever
    while (1)
    {
        // Yield for 1 tick
        vTaskDelay(1);

        // Service network
        networkSystem.loop();

        // Service webserver
        webServer.loop();
    }
}
