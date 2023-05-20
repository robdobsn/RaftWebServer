/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ArduinoOrAlt.h>
#include <list>
#include "RaftWebInterface.h"

class RaftWebRequest;
class RaftWebRequestParams;
class RaftWebRequestHeader;
class RaftWebResponder;
class RaftWebServerSettings;

class RaftWebHandler
{
public:
    RaftWebHandler()
    {
    }
    virtual ~RaftWebHandler()
    {        
    }
    virtual const char* getName()
    {
        return "HandlerBase";
    }
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, const RaftWebServerSettings& webServerSettings,
                RaftHttpStatusCode &statusCode)
    {
        return NULL;
    }
    virtual String getBaseURL()
    {
        return "<<NONE>>";
    }
    virtual bool isFileHandler()
    {
        return false;
    }
    virtual bool isWebSocketHandler()
    {
        return false;
    }
    
private:
};

