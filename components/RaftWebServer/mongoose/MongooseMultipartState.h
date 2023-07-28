/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftArduino.h>
#include <stdint.h>
#include "RaftWebMultipart.h"

// #define DEBUG_MONGOOSE_CONN_STATE

class MongooseMultipartState
{
public:
    MongooseMultipartState()
    {
#ifdef DEBUG_MONGOOSE_CONN_STATE
        LOG_I("MongooseMultipartState", "CONSTRUCTED");
#endif
    }
    virtual ~MongooseMultipartState()
    {
#ifdef DEBUG_MONGOOSE_CONN_STATE
        LOG_I("MongooseMultipartState", "DESTROYED");
#endif
    }

    // Data position (offset when transferring multipart data)
    uint32_t contentPos = 0;

    // File name
    String fileName;

    // Chunk offset
    uint32_t chunkOffset = 0;

    // Content length
    uint32_t contentLength = 0;

    // Last block in section received
    bool lastBlock = false;

    // Multipart parser
    RaftWebMultipart multipartParser;

    // Matched endpoint
    RaftWebServerRestEndpoint endpoint;

    // Channel ID
    uint32_t channelID = 0;

    // Req str
    String reqStr;
};
