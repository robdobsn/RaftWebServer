/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ArduinoOrAlt.h>
#include <stdint.h>

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
};
