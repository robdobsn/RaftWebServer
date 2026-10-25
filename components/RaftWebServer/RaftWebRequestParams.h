/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <cstdint>
#include "RaftJson.h"
#include "RaftWebConnDefs.h"

class RaftWebRequestParams
{
public:
    RaftWebRequestParams(RaftWebConnReadyToSendFn webConnReadyToSend, RaftWebConnSendFn webConnRawSend, uint32_t connId) 
    {
        _webConnReadyToSend = webConnReadyToSend;
        _webConnRawSend = webConnRawSend;
        this->connId = connId;
    }
    RaftWebConnSendFn getWebConnRawSend() const
    {
        return _webConnRawSend;
    }
    RaftWebConnReadyToSendFn getWebConnReadyToSend() const
    {
        return _webConnReadyToSend;
    }

    // Send that may take the buffer (see RaftWebConnSendTakeFn) - not set for every connection
    void setWebConnRawSendTake(RaftWebConnSendTakeFn webConnRawSendTake)
    {
        _webConnRawSendTake = webConnRawSendTake;
    }
    RaftWebConnSendTakeFn getWebConnRawSendTake() const
    {
        return _webConnRawSendTake;
    }

    // Connection ID (for debugging)
    uint32_t connId = 0;
    
private:
    RaftWebConnSendFn _webConnRawSend;
    RaftWebConnSendTakeFn _webConnRawSendTake = nullptr;
    RaftWebConnReadyToSendFn _webConnReadyToSend;
};
