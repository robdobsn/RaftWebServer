/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Logger.h"
#include "RaftWebHandler.h"
#include "RaftWebRequestHeader.h"
#include "RaftWebResponderRestAPI.h"

// #define DEBUG_WEB_HANDLER_REST_API

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RaftWebHandlerRestAPI
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class RaftWebHandlerRestAPI : public RaftWebHandler
{
public:

    /// @brief Constructor of REST API handler
    /// @param restAPIPrefix (eg "/api")
    /// @param matchEndpointCB (callback to match a uri and method to a handler)
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
    /// @brief Get name of handler
    /// @return Name of handler
    virtual const char* getName() const override
    {
        return "HandlerRESTAPI";
    }
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
            const RaftWebRequestParams& params, 
            RaftHttpStatusCode &statusCode) override final;

private:
    RaftWebAPIMatchEndpointCB _matchEndpointCB;
    String _restAPIPrefix;
};
