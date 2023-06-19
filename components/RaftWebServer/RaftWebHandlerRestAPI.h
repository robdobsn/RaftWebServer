/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <Logger.h>
#include "RaftWebHandler.h"
#include "RaftWebInterface.h"
#include "RaftWebRequestHeader.h"
#include "RaftWebResponderRestAPI.h"
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <mongoose.h>
class MongooseMultipartState;
#endif

// #define DEBUG_WEB_HANDLER_REST_API

class RaftWebHandlerRestAPI : public RaftWebHandler
{
public:
    RaftWebHandlerRestAPI(const String& restAPIPrefix, RaftWebAPIMatchEndpointCB matchEndpointCB)
    {
        _matchEndpointCB = matchEndpointCB;
        _restAPIPrefix = restAPIPrefix;
        if (!_restAPIPrefix.startsWith("/"))
            _restAPIPrefix = "/" + _restAPIPrefix;
    }
    virtual ~RaftWebHandlerRestAPI()
    {
    }
    virtual const char* getName() const override
    {
        return "HandlerRESTAPI";
    }
#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
            const RaftWebRequestParams& params, 
            RaftHttpStatusCode &statusCode) override final;
#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    virtual bool handleRequest(struct mg_connection *pConn, int ev, void *ev_data) override final;
#endif

private:
    RaftWebAPIMatchEndpointCB _matchEndpointCB;
    String _restAPIPrefix;

    // Helpers
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    RaftWebServerMethod convMongooseMethodToRaftMethod(const String& mongooseMethod);
    void multipartStateCleanup(struct mg_connection *pConn);
    MongooseMultipartState* multipartStateGetPtr(struct mg_connection *pConn);
    void multipartStateSetPtr(struct mg_connection *pConn, MongooseMultipartState* pMultipartState);
#endif
};
