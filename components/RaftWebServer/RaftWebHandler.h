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

#ifdef FEATURE_WEB_SERVER_USE_ORIGINAL
class RaftWebRequestParams;
class RaftWebRequestHeader;
class RaftWebResponder;
#endif

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
#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params,
                RaftHttpStatusCode &statusCode)
    {
        return NULL;
    }
#elif defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
    virtual esp_err_t handleRequest(httpd_req_t *req)
    {
        return ESP_OK;
    }
#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    virtual bool handleRequest(struct mg_connection *c, int ev, void *ev_data)
    {
        return false;
    }
#endif
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
