/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include "RaftClientConnBase.h"

// Callback for new connection
typedef std::function<bool(RaftClientConnBase* pClientConn)> RaftWebNewConnCBType;

class RaftClientListener
{
public:
    RaftClientListener()
    {
        _handOffNewConnCB = nullptr;
    }

    void setHandOffNewConnCB(RaftWebNewConnCBType handOffNewConnCB)
    {
        _handOffNewConnCB = handOffNewConnCB;
    }
    void listenForClients(int port, uint32_t numConnSlots);

private:
    static const uint32_t WEB_SERVER_SOCKET_RETRY_DELAY_MS = 1000;
    RaftWebNewConnCBType _handOffNewConnCB;
};

