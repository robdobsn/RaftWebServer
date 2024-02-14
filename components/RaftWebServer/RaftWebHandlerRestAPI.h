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
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include "mongoose.h"
class MongooseMultipartState;
#include "RaftWebMultipart.h"
#else
#include "RaftWebRequestHeader.h"
#include "RaftWebResponderRestAPI.h"
#endif

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
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    /// @brief Handle request (mongoose)
    /// @param pConn Mongoose connection
    /// @param ev Mongoose event
    /// @param ev_data Mongoose event data (this is a pointer to the mongoose event data structure)
    virtual bool handleRequest(struct mg_connection *pConn, int ev, void *ev_data) override final;
#else
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
            const RaftWebRequestParams& params, 
            RaftHttpStatusCode &statusCode) override final;
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
    RaftRetCode handleFileUploadChunk(const String& endpointName,
                RaftWebServerRestEndpoint& endpoint,
                struct mg_connection *pConn, 
                struct mg_http_message *pHttpMessage,
                MongooseMultipartState* pMultipartState);
    RaftRetCode sendFileChunkToRestAPI(const String& endpointName,
            RaftWebServerRestEndpoint& endpoint, 
            const String& filename, uint32_t channelID,
            const uint8_t* pChunk, uint32_t chunkLen,
            MongooseMultipartState* pMultipartState);
    void debugMultipartChunk(const char* pPrefix,
                const char* pChunk, uint32_t chunkLen);

    // Multipart callbacks
    void multipartOnEvent(void* pCtx, 
                RaftMultipartEvent event, const uint8_t *pBuf, uint32_t pos);
    RaftRetCode multipartOnData(void* pCtx,
                const uint8_t *pBuf, uint32_t len, RaftMultipartForm& formInfo, 
                uint32_t contentPos, bool isFinalPart);
    void multipartOnHeaderNameValue(void* pCtx,
            const String& name, const String& val);    
#endif
};
