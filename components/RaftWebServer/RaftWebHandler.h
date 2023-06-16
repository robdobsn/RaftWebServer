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
#include <RaftWebInterface.h>
#include <RaftWebServerSettings.h>
#include <RdJson.h>

#ifdef FEATURE_WEB_SERVER_USE_ESP_IDF
#include <esp_http_server.h>
#endif

class RaftWebRequest;
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
    virtual String getBaseURL() const
    {
        return "<<NONE>>";
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
    void setStandardHeaders(const std::list<RdJson::NameValuePair>& headers)
    {
        _standardHeaders = headers;
    }
    uint32_t getMaxResponseSize() const
    {
        return _webServerSettings._sendBufferMaxLen;
    }
protected:
    RaftWebServerSettings _webServerSettings;
    std::list<RdJson::NameValuePair> _standardHeaders;
};
