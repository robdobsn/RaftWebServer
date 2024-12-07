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
#include "RaftWebConnDefs.h"
#include "SpiramAwareAllocator.h"

enum RaftClientConnRslt
{
    CLIENT_CONN_RSLT_OK,
    CLIENT_CONN_RSLT_ERROR,
    CLIENT_CONN_RSLT_CONN_CLOSED,
};

class RaftClientConnBase
{
public:

    // Virtual destructor
    virtual ~RaftClientConnBase()
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

    // Check if sending is ok
    virtual RaftWebConnSendRetVal canSend() = 0;

    // Write
    virtual RaftWebConnSendRetVal sendDataBuffer(const SpiramAwareUint8Vector& buf, 
                uint32_t maxRetryMs, uint32_t& bytesWritten) = 0;

    // Setup
    virtual void setup(bool blocking) = 0;

    // Data access
    virtual RaftClientConnRslt getDataStart(SpiramAwareUint8Vector& dataBuf) = 0;
    virtual void getDataEnd() = 0;
};
