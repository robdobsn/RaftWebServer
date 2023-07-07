/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <RdJson.h>
#include <list>
#include "RaftWebConnDefs.h"

class RaftWebRequestParams
{
public:
    RaftWebRequestParams(RaftWebConnReadyToSendFn webConnReadyToSend, RaftWebConnSendFn webConnRawSend)
    {
        _webConnReadyToSend = webConnReadyToSend;
        _webConnRawSend = webConnRawSend;
    }
    RaftWebConnSendFn getWebConnRawSend() const
    {
        return _webConnRawSend;
    }
    RaftWebConnReadyToSendFn getWebConnReadyToSend() const
    {
        return _webConnReadyToSend;
    }
    
private:
    RaftWebConnSendFn _webConnRawSend;
    RaftWebConnReadyToSendFn _webConnReadyToSend;
};
