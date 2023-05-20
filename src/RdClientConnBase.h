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
#include <SpiramAwareAllocator.h>
#include <vector>

enum ClientConnRslt
{
    CLIENT_CONN_RSLT_OK,
    CLIENT_CONN_RSLT_ERROR,
    CLIENT_CONN_RSLT_CONN_CLOSED,
};

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
    virtual RdWebConnSendRetVal write(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs) = 0;

    // Setup
    virtual void setup(bool blocking) = 0;

    // Data access
    virtual ClientConnRslt getDataStart(std::vector<uint8_t, SpiramAwareAllocator<uint8_t>>& dataBuf) = 0;
    virtual void getDataEnd() = 0;
};
