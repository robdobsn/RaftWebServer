/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "lwip/api.h"
#include <functional>
#include "RdClientConnBase.h"

// Callback for new connection
typedef std::function<bool(RdClientConnBase* pClientConn)> RdWebNewConnCBType;

class RdClientListener
{
public:
    RdClientListener()
    {
        _handOffNewConnCB = nullptr;
    }

    void setHandOffNewConnCB(RdWebNewConnCBType handOffNewConnCB)
    {
        _handOffNewConnCB = handOffNewConnCB;
    }
    void listenForClients(int port, uint32_t numConnSlots);

private:
    static const uint32_t WEB_SERVER_SOCKET_RETRY_DELAY_MS = 1000;
    RdWebNewConnCBType _handOffNewConnCB;
};

