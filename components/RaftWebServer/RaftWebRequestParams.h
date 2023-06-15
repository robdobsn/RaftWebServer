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
    RaftWebRequestParams(uint32_t maxSendSize, 
            std::list<RdJson::NameValuePair>* pResponseHeaders,
            RaftWebConnSendFn webConnRawSend)
    {
        _maxSendSize = maxSendSize;
        _pResponseHeaders = pResponseHeaders;
        _webConnRawSend = webConnRawSend;
    }
    uint32_t getMaxSendSize()
    {
        return _maxSendSize;
    }
    RaftWebConnSendFn getWebConnRawSend() const
    {
        return _webConnRawSend;
    }
    std::list<RdJson::NameValuePair>* getHeaders() const
    {
        return _pResponseHeaders;
    }
    
private:
    uint32_t _maxSendSize;
    std::list<RdJson::NameValuePair>* _pResponseHeaders;
    RaftWebConnSendFn _webConnRawSend;
};
