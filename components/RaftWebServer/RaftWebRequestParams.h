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
    RaftWebRequestParams(RaftWebConnSendFn webConnRawSend)
    {
        _webConnRawSend = webConnRawSend;
    }
    RaftWebConnSendFn getWebConnRawSend() const
    {
        return _webConnRawSend;
    }
    
private:
    RaftWebConnSendFn _webConnRawSend;
};
