/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "stdint.h"
#include <functional>

// Callback function for any endpoint
enum RdWebConnSendRetVal
{
    WEB_CONN_SEND_FAIL,
    WEB_CONN_SEND_OK,
    WEB_CONN_SEND_EAGAIN
};
class RdWebConnDefs
{
public:
    static const char* getSendRetValStr(RdWebConnSendRetVal retVal)
    {
        switch(retVal)
        {
            case WEB_CONN_SEND_OK: return "Ok";
            case WEB_CONN_SEND_EAGAIN: return "EAGAIN";
            default: return "Fail";
        }
    }
};

typedef std::function<RdWebConnSendRetVal(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxSendRetryMs)> RdWebConnSendFn;
