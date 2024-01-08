/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include "RaftWebHandler.h"
#include "Logger.h"
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include "mongoose.h"
#else
#include "RaftWebRequestHeader.h"
#include "RaftWebResponderSSEvents.h"
#endif

class RaftWebHandlerSSEvents : public RaftWebHandler
{
public:
    RaftWebHandlerSSEvents(const String& eventsPath, RaftWebSSEventsCB eventCallback)
            : _eventCallback(eventCallback)
    {
        _eventsPath = eventsPath;
    }
    virtual ~RaftWebHandlerSSEvents()
    {
    }
    virtual const char* getName() const override
    {
        return "HandlerSSEvents";
    }
#if !defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, 
                RaftHttpStatusCode &statusCode) override final
    {
        // LOG_W("RaftWebHandlerSSEvents", "getNewResponder %s connType %d method %d", 
        //             requestHeader.URL, requestHeader.reqConnType, requestHeader.extract.method);

        // Check if event-stream
        if (requestHeader.reqConnType != REQ_CONN_TYPE_EVENT)
            return NULL;

        // Check for correct prefix
        if (!requestHeader.URL.startsWith(_eventsPath))
        {
            LOG_W("WebHandlerSSEvents", "getNewResponder unmatched url req %s != expected %s", requestHeader.URL.c_str(), _eventsPath.c_str());
            return NULL;
        }

        // We can handle this so create a new responder object
        RaftWebResponder* pResponder = new RaftWebResponderSSEvents(this, params, requestHeader.URL, 
                    _eventCallback, webServerSettings);

        // Debug
        // LOG_W("WebHandlerSSEvents", "getNewResponder constructed new responder %lx uri %s", (unsigned long)pResponder, requestHeader.URL.c_str());

        // Return new responder - caller must clean up by deleting object when no longer needed
        statusCode = HTTP_STATUS_OK;
        return pResponder;
    }
#endif

private:
    String _eventsPath;
    RaftWebSSEventsCB _eventCallback;
};
