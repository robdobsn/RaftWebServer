/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "RaftArduino.h"

class RaftWebServerSettings
{
public:
    static const int DEFAULT_HTTP_PORT = 80;
    static const int DEFAULT_CONN_SLOTS = 10;
    static const int DEFAULT_ENABLE_WEBSOCKETS = true;
    static const int DEFAULT_ENABLE_FILE_SERVER = true;
    static constexpr const char* DEFAULT_REST_API_PREFIX = "/api";
    static const std::vector<String> DEFAULT_STD_RESPONSE_HEADERS;

    // Task settings
    static const int DEFAULT_TASK_CORE = 0;
    static const int DEFAULT_TASK_PRIORITY = 9;
    static const int DEFAULT_TASK_STACK_BYTES = 5000;   

    // Send buffer max length
    static const int DEFAULT_SEND_BUFFER_MAX_LEN = 5000;

    // Constructor
    RaftWebServerSettings()
    {
    }
    RaftWebServerSettings(int port, uint32_t connSlots, bool wsEnable, 
            bool enFileServer, uint32_t tskCore,
            uint32_t tskPriority, uint32_t tskStackSize,
            uint32_t sendBufMaxLen,
            uint32_t restAPIChanID, 
            std::vector<String>& stdRespHdrs,
            const char* p404PageSource,
            const char* pMimeTypes,
            uint32_t clearPendDurationMs)
    {
        serverTCPPort = port;
        numConnSlots = connSlots;
        enableWebSockets = wsEnable;
        enableFileServer = enFileServer;
        taskCore = tskCore;
        taskPriority = tskPriority;
        taskStackSize = tskStackSize;
        sendBufferMaxLen = sendBufMaxLen;
        restAPIChannelID = restAPIChanID;
        pageSource404 = p404PageSource ? p404PageSource : "";
        mimeTypes = pMimeTypes ? pMimeTypes : "";
        String headersStr;
        for (auto& header : stdRespHdrs)
        {
            headersStr += header;
            headersStr += "\r\n";
        }
        stdRespHeaders = headersStr;
        clearPendingDurationMs = clearPendDurationMs;
    }

    // TCP port of server
    int serverTCPPort = DEFAULT_HTTP_PORT;

    // Number of connection slots
    uint32_t numConnSlots = DEFAULT_CONN_SLOTS;

    // Enable websockets
    bool enableWebSockets = DEFAULT_ENABLE_WEBSOCKETS;

    // Enable file server
    bool enableFileServer = DEFAULT_ENABLE_FILE_SERVER;

    // Task settings
    uint32_t taskCore = DEFAULT_TASK_CORE;
    uint32_t taskPriority = DEFAULT_TASK_PRIORITY;
    uint32_t taskStackSize = DEFAULT_TASK_STACK_BYTES;

    // Max length of send buffer
    uint32_t sendBufferMaxLen = DEFAULT_SEND_BUFFER_MAX_LEN;

    // Channel ID for REST API
    uint32_t restAPIChannelID = UINT32_MAX;

    // Standard response headers - added to every response
    String stdRespHeaders;

    // 404 page source - note that this MUST be either NULL or a pointer to a string that is
    // valid for the lifetime of the program
    String pageSource404;

    // MIME types for file serving - this must be a string in the form ext1=type1,ext2=type2,..
    // or NULL for the default
    String mimeTypes;

    // Connection clear pending duration ms
    static const uint32_t CONNECTION_CLEAR_PENDING_MS_DEFAULT = 0;
    uint32_t clearPendingDurationMs = CONNECTION_CLEAR_PENDING_MS_DEFAULT;
};
