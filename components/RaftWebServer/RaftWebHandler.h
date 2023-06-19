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
#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    virtual bool handleRequest(struct mg_connection *pConn, int ev, void *ev_data)
    {
        return false;
    }
    virtual bool canSend(uint32_t& channelID, bool& noConn)
    {
        noConn = true;
        return false;
    }
    virtual bool sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
    {
        return false;
    }
#endif
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

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    static const int RAFT_MG_HTTP_DATA_CHANNEL_ID_POS = 0;
    static const int RAFT_MG_HTTP_DATA_CHANNEL_ID_LEN = 4;
    static const int RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_POS = RAFT_MG_HTTP_DATA_CHANNEL_ID_POS + RAFT_MG_HTTP_DATA_CHANNEL_ID_LEN;
    static const int RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_LEN = sizeof(void*);
#endif
};
