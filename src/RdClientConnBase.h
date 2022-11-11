/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "lwip/api.h"
#include "RdWebConnDefs.h"

class RdClientConnBase
{
public:

    // Virtual destructor
    virtual ~RdClientConnBase()
    {
    }

    // Connection is active
    virtual bool isActive()
    {
        return true;
    }

    // Current state
    virtual const char* getStateStr()
    {
        return "none";
    }

    // Client ID
    virtual uint32_t getClientId()
    {
        return 0;
    }

    // Write
    virtual RdWebConnSendRetVal write(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs);

    // Setup
    virtual void setup(bool blocking);

    // Data access
    virtual uint8_t* getDataStart(uint32_t& dataLen, bool& errorOccurred, bool& connClosed);
    virtual void getDataEnd();
};
