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
#include "RdWebInterface.h"

class RdWebRequest;
class RdWebRequestParams;
class RdWebRequestHeader;
class RdWebResponder;
class RaftWebServerSettings;

class RdWebHandler
{
public:
    RdWebHandler()
    {
    }
    virtual ~RdWebHandler()
    {        
    }
    virtual const char* getName()
    {
        return "HandlerBase";
    }
    virtual RdWebResponder* getNewResponder(const RdWebRequestHeader& requestHeader, 
                const RdWebRequestParams& params, const RaftWebServerSettings& webServerSettings,
                RdHttpStatusCode &statusCode)
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

