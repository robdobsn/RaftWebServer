/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <functional>

// Callback function for any endpoint
enum RaftWebConnSendRetVal
{
    WEB_CONN_SEND_FAIL,
    WEB_CONN_SEND_OK,
    WEB_CONN_SEND_EAGAIN,
    WEB_CONN_SEND_TOO_LONG,
    WEB_CONN_SEND_FRAME_ERROR,
    WEB_CONN_NO_CONNECTION
};
class RaftWebConnDefs
{
public:
    static const char* getSendRetValStr(RaftWebConnSendRetVal retVal)
    {
        switch(retVal)
        {
            case WEB_CONN_SEND_OK: return "Ok";
            case WEB_CONN_SEND_EAGAIN: return "EAGAIN";
            case WEB_CONN_SEND_TOO_LONG: return "TooLong";
            case WEB_CONN_SEND_FRAME_ERROR: return "FrameError";
            case WEB_CONN_NO_CONNECTION: return "NoConn";
            default: return "Fail";
        }
    }
};

// Function to test if connection is ready to send
typedef std::function<RaftWebConnSendRetVal()> RaftWebConnReadyToSendFn;

// Function to send on a connection
typedef std::function<RaftWebConnSendRetVal(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxSendRetryMs)> RaftWebConnSendFn;

