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
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "CommsCoreIF.h"
#include "NetworkSystem.h"
#include "FileSystem.h"
#include "RestAPIEndpointManager.h"
#include "RaftWebServer.h"
#include "RaftWebHandlerStaticFiles.h"
#include "RaftWebHandlerRestAPI.h"
#include "RaftWebHandlerWS.h"

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

RaftRetCode testEndpointCallback(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    respStr = "Hello from testEndpointCallback";
    return RAFT_RET_OK;
}

bool wsCanAcceptInbound(uint32_t channelID)
{
    return true;
}

void wsInboundHandleMsg(uint32_t channelID, const uint8_t* pMsg, uint32_t msgLen)
{
    // LOG_I(MODULE_PREFIX, "wsInboundHandleMsg, channel Id %d msglen %d", channelID, msgLen);
}

RaftRetCode uploadFileComplete(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
#ifdef DEBUG_FILE_MANAGER_UPLOAD
    LOG_I(MODULE_PREFIX, "uploadFileComplete %s", reqStr.c_str());
#endif
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
}

FILE* pGlobalFilePtr = NULL;

RaftRetCode uploadFileBlock(const String& req, FileStreamBlock& fileStreamBlock, const APISourceInfo& sourceInfo)
{
    LOG_I(MODULE_PREFIX, "uploadFileBlock %s filename %s blockLen %d firstBlock %d finalBlock %d",
            req.c_str(), fileStreamBlock.filename, fileStreamBlock.blockLen, 
            fileStreamBlock.firstBlock, fileStreamBlock.finalBlock);

    // Check for first block
    if (fileStreamBlock.firstBlock)
    {
        // Check for existing file
        if (pGlobalFilePtr)
        {
            LOG_E(MODULE_PREFIX, "uploadFileBlock file already open, closing");
            fclose(pGlobalFilePtr);
            pGlobalFilePtr = NULL;
            return RaftRetCode::RAFT_RET_BUSY;
        }

        // Open file
        String filename = String("/local/") + fileStreamBlock.filename;
        pGlobalFilePtr = fopen(filename.c_str(), "w+");
        if (!pGlobalFilePtr)
        {
            LOG_E(MODULE_PREFIX, "uploadFileBlock failed to open file %s", filename.c_str());
            return RaftRetCode::RAFT_RET_CANNOT_START;
        }

        // Debug
        LOG_I(MODULE_PREFIX, "uploadFileBlock opened file %s", filename.c_str());
    }

    // Check for file open ok
    if (!pGlobalFilePtr)
    {
        LOG_E(MODULE_PREFIX, "uploadFileBlock file not open");
        return RaftRetCode::RAFT_RET_CANNOT_START;
    }

    // Seek to position
    if (fseek(pGlobalFilePtr, fileStreamBlock.filePos, SEEK_SET) != 0)
    {
        LOG_E(MODULE_PREFIX, "uploadFileBlock failed to seek to position %d", fileStreamBlock.filePos);
        return RaftRetCode::RAFT_RET_OTHER_FAILURE;
    }

    // Write data to file
    const uint8_t* pData = fileStreamBlock.pBlock;
    uint32_t dataPos = 0;
    const uint32_t WRITE_CHUNK_LEN = 1000;
    while (dataPos < fileStreamBlock.blockLen)
    {
        uint32_t writeLen = fileStreamBlock.blockLen - dataPos;
        if (writeLen > WRITE_CHUNK_LEN)
        {
            writeLen = WRITE_CHUNK_LEN;
        }
        if (fwrite(pData + dataPos, 1, writeLen, pGlobalFilePtr) != writeLen)
        {
            LOG_E(MODULE_PREFIX, "uploadFileBlock failed to write len %d at %d - total blockLen %d", 
                    writeLen, dataPos, fileStreamBlock.blockLen);
            return RaftRetCode::RAFT_RET_OTHER_FAILURE;
        }
        dataPos += writeLen;
    }

    // Check for final block
    if (fileStreamBlock.finalBlock)
    {
        // Close file
        fclose(pGlobalFilePtr);
        pGlobalFilePtr = NULL;

        // Debug
        LOG_I(MODULE_PREFIX, "uploadFileBlock closed file %s", fileStreamBlock.filename);
    }
    return RaftRetCode::RAFT_RET_OK;
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

    // Add a test REST API endpoint
    restAPIEndpointManager.addEndpoint("test", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                        testEndpointCallback,
                        "test");

    // Add an upload REST API endpoint
    restAPIEndpointManager.addEndpoint("upload", 
                RestAPIEndpoint::ENDPOINT_CALLBACK, 
                RestAPIEndpoint::ENDPOINT_POST,
                uploadFileComplete,
                "Upload file", 
                "application/json", 
                nullptr,
                RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
                nullptr, 
                nullptr,
                uploadFileBlock);

    // Web server static files
    String servePaths = "/" + fileSystem.getDefaultFSRoot() + ",/files/local=/local,/files/sd=/sd";
    RaftWebHandlerStaticFiles* pHandlerFiles = new RaftWebHandlerStaticFiles(servePaths.c_str(), NULL);
    bool handlerAddOk = webServer.addHandler(pHandlerFiles);
    LOG_I(MODULE_PREFIX, "serveStaticFiles paths %s addResult %s", 
                        servePaths.c_str(), 
                        handlerAddOk ? "OK" : "FILE SERVER DISABLED");
    if (!handlerAddOk)
        delete pHandlerFiles;

    // Add an API handler
    RaftWebHandlerRestAPI* pHandlerAPI = new RaftWebHandlerRestAPI("/api", &matchEndpoint);
    handlerAddOk = webServer.addHandler(pHandlerAPI);
    LOG_I(MODULE_PREFIX, "serveAPI url %s addResult %s", "/api", handlerAddOk ? "OK" : "API SERVER DISABLED");
    if (!handlerAddOk)
        delete pHandlerAPI;

    // Add websocket handler
    RaftJson wsJsonConfig = R"(
        {
            "pfix": "ws",
            "pcol": "RICSerial",
            "maxConn": 4,
            "txQueueMax": 20,
            "pktMaxBytes": 5000,
            "pingMs": 2000
        }
    )";
    RaftWebHandlerWS* pHandlerWS = new RaftWebHandlerWS(wsJsonConfig, 
            wsCanAcceptInbound,
            wsHandleInboundMessage
         );
    const uint32_t CHANNEL_ID_NUMBER_BASE = 200;
    const uint32_t NUM_WS_CHANNELS_TO_USE = 2;
    const uint32_t MAX_WS_CHANNELS = 4;
    handlerAddOk = webServer.addHandler(pHandlerWS);
    LOG_I(MODULE_PREFIX, "serveWS url %s addResult %s", "/ws", handlerAddOk ? "OK" : "WS SERVER DISABLED");
    if (!pHandlerWS)
    {
        delete pHandlerWS;
    }
    else
    {
        // Add 4 websocket channels
        for (int i = 0; i < MAX_WS_CHANNELS; i++)
        {
            pHandlerWS->setupWebSocketChannelID(i, i + CHANNEL_ID_NUMBER_BASE);
        }
    }

    // Test creating a small file and reading it back to ensure the filesystem works
    String testFileName = "/local/test.txt";
    FILE* testFile = fopen(testFileName.c_str(), "w+");
    if (testFile)
    {
        fprintf(testFile, "Hello world");
        fclose(testFile);
        testFile = fopen(testFileName.c_str(), "r");
        if (testFile)
        {
            char buf[100];
            fgets(buf, sizeof(buf), testFile);
            fclose(testFile);
            LOG_I(MODULE_PREFIX, "File test OK - read back '%s'", buf);
        }
        else
        {
            LOG_E(MODULE_PREFIX, "File test failed to read file");
        }
    }
    else
    {
        LOG_E(MODULE_PREFIX, "File test failed to create file");
    }

    // Send periodic messages on websocket
    uint32_t wsSendMsgLoopCtr = 0;
    uint32_t wsMsgCtr = 0;

    // Loop forever
    while (1)
    {
        // Yield for 1 tick
        vTaskDelay(1);

        // Service network
        networkSystem.service();

        // Service webserver
        webServer.service();

        // Check for sending a message on a websocket
        if (wsSendMsgLoopCtr++ == 200)
        {
            wsSendMsgLoopCtr = 0;

            // Send a message on each websocket channel
            for (int i = 0; i < NUM_WS_CHANNELS_TO_USE; i++)
            {
                // Build message (with channel ID as first byte)
                String msgStr = "Hello from server ch " + String(i) + " count " + String(wsMsgCtr++);
                webServer.sendMsg((uint8_t*)msgStr.c_str(), msgStr.length(), CHANNEL_ID_NUMBER_BASE + i);
            }
        }
    }
}
