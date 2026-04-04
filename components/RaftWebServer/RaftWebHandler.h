/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include "RaftArduino.h"
#include "RaftWebInterface.h"
#include "RaftWebServerSettings.h"
#include "RaftJson.h"

class RaftWebConnManager;
class RaftWebRequestParams;
class RaftWebRequestHeader;
class RaftWebResponder;

class RaftWebHandler
{
public:
    RaftWebHandler()
    {
    }
    virtual ~RaftWebHandler()
    {        
    }
    virtual const char* getName() const
    {
        return "HandlerBase";
    }
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params,
                RaftHttpStatusCode &statusCode)
    {
        return NULL;
    }
    virtual bool canSend(uint32_t channelID, bool& noConn)
    {
        noConn = false;
        return true;
    }
    virtual bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
    {
        return false;
    }
    virtual bool isFileHandler() const
    {
        return false;
    }
    virtual bool isWebSocketHandler() const
    {
        return false;
    }
    void setWebServerSettings(const RaftWebServerSettings& webServerSettings)
    {
        _webServerSettings = webServerSettings;
    }
    uint32_t getMaxResponseSize() const
    {
        return _webServerSettings.sendBufferMaxLen;
    }

    // Connection manager
    RaftWebConnManager* _pConnManager = nullptr;
    void setConnManager(RaftWebConnManager* pConnManager)
    {
        _pConnManager = pConnManager;
    }

protected:
    RaftWebServerSettings _defaultWebServerSettings;
    RaftWebServerSettings& _webServerSettings = _defaultWebServerSettings;

};
