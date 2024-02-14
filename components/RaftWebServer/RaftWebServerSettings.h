/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "lwip/api.h"
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
            bool enableFileServer, uint32_t taskCore,
            uint32_t taskPriority, uint32_t taskStackSize,
            uint32_t sendBufferMaxLen,
            uint32_t restAPIChannelID, 
            std::vector<String>& stdRespHeaders,
            const char* p404PageSource,
            const char* pMimeTypes)
    {
        _serverTCPPort = port;
        _numConnSlots = connSlots;
        _enableWebSockets = wsEnable;
        _enableFileServer = enableFileServer;
        _taskCore = taskCore;
        _taskPriority = taskPriority;
        _taskStackSize = taskStackSize;
        _sendBufferMaxLen = sendBufferMaxLen;
        _restAPIChannelID = restAPIChannelID;
        _404PageSource = p404PageSource ? p404PageSource : "";
        _mimeTypes = pMimeTypes ? pMimeTypes : "";
        String headersStr;
        for (auto& header : stdRespHeaders)
        {
            headersStr += header;
            headersStr += "\r\n";
        }
        _stdRespHeaders = headersStr;
    }

    // TCP port of server
    int _serverTCPPort = DEFAULT_HTTP_PORT;

    // Number of connection slots
    uint32_t _numConnSlots = DEFAULT_CONN_SLOTS;

    // Enable websockets
    bool _enableWebSockets = DEFAULT_ENABLE_WEBSOCKETS;

    // Enable file server
    bool _enableFileServer = DEFAULT_ENABLE_FILE_SERVER;

    // Task settings
    uint32_t _taskCore = DEFAULT_TASK_CORE;
    uint32_t _taskPriority = DEFAULT_TASK_PRIORITY;
    uint32_t _taskStackSize = DEFAULT_TASK_STACK_BYTES;

    // Max length of send buffer
    uint32_t _sendBufferMaxLen = DEFAULT_SEND_BUFFER_MAX_LEN;

    // Channel ID for REST API
    uint32_t _restAPIChannelID = UINT32_MAX;

    // Standard response headers - added to every response
    String _stdRespHeaders;

    // 404 page source - note that this MUST be either NULL or a pointer to a string that is
    // valid for the lifetime of the program
    String _404PageSource;

    // MIME types for file serving - this must be a string in the form ext1=type1,ext2=type2,..
    // or NULL for the default
    String _mimeTypes;
};
