/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "lwip/api.h"
#include <ArduinoOrAlt.h>

class RaftWebServerSettings
{
public:
    static const int DEFAULT_HTTP_PORT = 80;

    // Task settings
    static const int DEFAULT_TASK_CORE = 0;
    static const int DEFAULT_TASK_PRIORITY = 9;
    static const int DEFAULT_TASK_SIZE_BYTES = 3000;   

    // Send buffer max length
    static const int DEFAULT_SEND_BUFFER_MAX_LEN = 1000;

    RaftWebServerSettings()
    {
        _serverTCPPort = DEFAULT_HTTP_PORT;
        _numConnSlots = 6;
        _enableWebSockets = true;
        _enableFileServer = true;
        _taskCore = DEFAULT_TASK_CORE;
        _taskPriority = DEFAULT_TASK_PRIORITY;
        _taskStackSize = DEFAULT_TASK_SIZE_BYTES;
        _sendBufferMaxLen = DEFAULT_SEND_BUFFER_MAX_LEN;
        _restAPIChannelID = UINT32_MAX;
    }

    RaftWebServerSettings(int port, uint32_t connSlots, bool wsEnable, 
            bool enableFileServer, uint32_t taskCore,
            uint32_t taskPriority, uint32_t taskStackSize,
            uint32_t sendBufferMaxLen,
            uint32_t restAPIChannelID)
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
    }

    // TCP port of server
    int _serverTCPPort;

    // Number of connection slots
    uint32_t _numConnSlots;

    // Enable websockets
    bool _enableWebSockets;

    // Enable file server
    bool _enableFileServer;

    // Task settings
    uint32_t _taskCore;
    uint32_t _taskPriority;
    uint32_t _taskStackSize;

    // Max length of send buffer
    uint32_t _sendBufferMaxLen;

    // Channel ID for REST API
    uint32_t _restAPIChannelID;
};
