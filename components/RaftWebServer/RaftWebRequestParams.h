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

    // Connection ID (for debugging)
    uint32_t connId = 0;
    
private:
    RaftWebConnSendFn _webConnRawSend;
    RaftWebConnReadyToSendFn _webConnReadyToSend;
};
