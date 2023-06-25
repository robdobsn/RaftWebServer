/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebHandlerRestAPI.h"

// #define DEBUG_WEB_HANDLER_REST_API
// #define DEBUG_WEB_HANDLER_REST_API_DETAIL
// #define DEBUG_WEB_HANDLER_REST_API_RAW_BODY_VERBOSE
// #define DEBUG_WEB_HANDLER_REST_API_BODY_VERBOSE
// #define DEBUG_WEB_HANDLER_REST_API_CHUNK_VERBOSE
// #define DEBUG_WEB_HANDLER_REST_API_EVENT_VERBOSE

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <MongooseMultipartState.h>
#include <FileStreamBlock.h>
#include <RaftWebConnManager_mongoose.h>
#endif

#if defined(DEBUG_WEB_HANDLER_REST_API)
static const char* MODULE_PREFIX = "RaftWebHandlerRestAPI";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle request (Original)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef FEATURE_WEB_SERVER_USE_ORIGINAL

RaftWebResponder* RaftWebHandlerRestAPI::getNewResponder(const RaftWebRequestHeader& requestHeader, 
        const RaftWebRequestParams& params, 
        RaftHttpStatusCode &statusCode)
{
    // Check
    if (!_matchEndpointCB)
        return nullptr;

    // Debug
#ifdef DEBUG_WEB_HANDLER_REST_API
    uint64_t getResponderStartUs = micros();
#endif

    // Check for API prefix
    if (!requestHeader.URL.startsWith(_restAPIPrefix))
    {
#ifdef DEBUG_WEB_HANDLER_REST_API
        uint64_t getResponderEndUs = micros();
        LOG_W(MODULE_PREFIX, "getNewResponder no match with %s for %s took %lldus", 
                    _restAPIPrefix.c_str(), requestHeader.URL.c_str(), getResponderEndUs-getResponderStartUs);
#endif
        return nullptr;
    }

    // Remove prefix on test string
    String reqStr = requestHeader.URIAndParams.substring(_restAPIPrefix.length());
    RaftWebServerRestEndpoint endpoint;
    if (!_matchEndpointCB(reqStr.c_str(), requestHeader.extract.method, endpoint))
    {
#ifdef DEBUG_WEB_HANDLER_REST_API
        uint64_t getResponderEndUs = micros();
        LOG_W(MODULE_PREFIX, "getNewResponder no matching endpoint found %s took %lld", 
                    requestHeader.URL.c_str(), getResponderEndUs-getResponderStartUs);
#endif
        return nullptr;
    }
    // Looks like we can handle this so create a new responder object
    RaftWebResponder* pResponder = new RaftWebResponderRestAPI(endpoint, this, params, 
                    reqStr, requestHeader.extract, 
                    _webServerSettings._restAPIChannelID);

    // Debug
#ifdef DEBUG_WEB_HANDLER_REST_API
    uint64_t getResponderEndUs = micros();
    LOG_I(MODULE_PREFIX, "getNewResponder constructed new responder %lx uri %s took %lld", 
                (unsigned long)pResponder, requestHeader.URL.c_str(), getResponderEndUs-getResponderStartUs);
#endif

    // Return new responder - caller must clean up by deleting object when no longer needed
    statusCode = HTTP_STATUS_OK;
    return pResponder;
}

#endif // FEATURE_WEB_SERVER_USE_ORIGINAL

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mongoose handlers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

bool RaftWebHandlerRestAPI::handleRequest(struct mg_connection *pConn, int ev, void *ev_data)
{
#ifdef DEBUG_WEB_HANDLER_REST_API_EVENT_VERBOSE
    if (ev != MG_EV_POLL)
    {
        LOG_I(MODULE_PREFIX, "handleRequest ev %s", RaftWebConnManager_mongoose::mongooseEventToString(ev));
    }    
#endif

    // Check if connection contains multipart state pointer
    // If so delete it and clear the data - this assumes that nothing else uses the
    // section of the connection data field used for multipart state
    // Return false so other handlers get a chance to see the close event
    if (ev == MG_EV_CLOSE)
    {
        multipartStateCleanup(pConn);
        return false;
    }

    // Check valid
    if (!_matchEndpointCB)
        return false;

    // Check for HTTP message
    if ((ev != MG_EV_HTTP_MSG) && (ev != MG_EV_HTTP_CHUNK))
        return false;

    // Mongoose http message
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;

    // Get request string and method string
    String reqStr = String(hm->uri.ptr, hm->uri.len);
    String methodStr = String(hm->method.ptr, hm->method.len);

    // Check for API prefix
    RaftWebServerRestEndpoint endpoint;
    bool canHandle = false;
    RaftWebServerMethod method = convMongooseMethodToRaftMethod(methodStr);
    String endpointName = reqStr;
    if (reqStr.startsWith(_restAPIPrefix))
    {
        // Endpoint name is after prefix
        endpointName = reqStr.substring(_restAPIPrefix.length());
#ifdef DEBUG_WEB_HANDLER_REST_API_DETAIL
        LOG_I(MODULE_PREFIX, "handleRequest testing endpointName %s method %s for match", 
                    endpointName.c_str(), methodStr.c_str());
#endif
        if (_matchEndpointCB(endpointName.c_str(), method, endpoint))
        {
#ifdef DEBUG_WEB_HANDLER_REST_API
            LOG_I(MODULE_PREFIX, "handleRequest MATCHED endpoint %s method %s", 
                    reqStr.c_str(), methodStr.c_str());
#endif
            canHandle = true;
        }
    }

    // Check if we can handle this request
    if (!canHandle)
        return false;

    // Get multipart state pointer
    // This is a pointer to an object created when a multipart session is ongoing
    // The pointer is stored in the connection state data (c->data)
    // The connection state data must be initialised to nullptr on CONNECT or ACCEPT
    MongooseMultipartState* pMultipartState = multipartStateGetPtr(pConn);

    // Check if handling chunked data
    bool lastChunkReceived = false;
    if (ev == MG_EV_HTTP_CHUNK)
    {
        if (!pMultipartState)
        {
            // No multipart state
#ifdef DEBUG_WEB_HANDLER_REST_API
            LOG_I(MODULE_PREFIX, "handleRequest no connection state - so creating");
#endif

            // Create new connection state
            pMultipartState = new MongooseMultipartState();

            // Store in connection state data
            multipartStateSetPtr(pConn, pMultipartState);
        }
        if (!pMultipartState)
        {
            return false;
        }

        // Get content position from conn state
        uint32_t contentPos = pMultipartState->contentPos;

        // Get content-length if present
        struct mg_str* cl = mg_http_get_header(hm, "Content-Length");
        uint32_t contentLength = cl ? atol(cl->ptr) : 0;

        // Check if content is chunked
        struct mg_str *te = mg_http_get_header(hm, "Transfer-Encoding");
        bool isChunked = te ? mg_vcasecmp(te, "chunked") == 0 : false;

#ifdef DEBUG_WEB_HANDLER_REST_API_RAW_BODY_VERBOSE
        {
            const int DEBUG_MAX_STRING_LENGTH = 500;
            String hmBody(hm->body.ptr, hm->body.len > DEBUG_MAX_STRING_LENGTH ? DEBUG_MAX_STRING_LENGTH : hm->body.len);
            if (hm->body.len > DEBUG_MAX_STRING_LENGTH)
                hmBody += "...";
            LOG_I(MODULE_PREFIX, "handleRequest raw %s data body\n----------\n%s\n----------\n", 
                                isChunked ? "CHUNKED" : "NOT CHUNKED",
                                hmBody.c_str());
        }
#endif

        // Check if chunked
        if (isChunked)
        {

            // Extract information from multipart message
            struct mg_http_part part;
            size_t ofs = 0;
            while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) != 0)
            {
                // Extract filename if present
                if (part.filename.len > 0)
                {
                    String fileName = String(part.filename.ptr, part.filename.len);
                    if (fileName != pMultipartState->fileName)
                    {
    #ifdef DEBUG_WEB_HANDLER_REST_API
                        // Debug
                        LOG_I(MODULE_PREFIX, "handleRequest multipart data new filename %s", fileName.c_str());
    #endif

                        contentPos = 0;
                    }
                    pMultipartState->fileName = fileName;
                }

    #ifdef DEBUG_WEB_HANDLER_REST_API
                // Debug
                LOG_I(MODULE_PREFIX, "handleRequest multipart data nameLen %d filenameLen %d bodyLen %d contentPos %d contentLen %d ofs %d",
                            part.name.len, part.filename.len, part.body.len, contentPos, contentLength, ofs);
    #endif

    #ifdef DEBUG_WEB_HANDLER_REST_API_DETAIL
                if (part.body.len < 50)
                {
                    LOG_I(MODULE_PREFIX, "handleRequest multipart data body %s", String(part.body.ptr, part.body.len).c_str());
                }
    #endif

                // Check we have a filename
                if (pMultipartState->fileName.length() != 0)
                {
    #ifdef DEBUG_WEB_HANDLER_REST_API
                    LOG_I(MODULE_PREFIX, "handleRequest multipart sending to API");
    #endif
                    // Prepare block
                    FileStreamBlock fileStreamBlock(
                        pMultipartState->fileName.c_str(),
                        contentLength, 
                        contentPos, 
                        (uint8_t*)part.body.ptr, 
                        part.body.len,
                        false,
                        0, 
                        false, 
                        0, 
                        false, 
                        contentPos==0);
                    // Check for callback
                    APISourceInfo apiSourceInfo(_webServerSettings._restAPIChannelID);
                    if (endpoint.restApiFnChunk) 
                        endpoint.restApiFnChunk(endpointName, fileStreamBlock, apiSourceInfo);
                }

                // Update content pos
                contentPos += part.body.len;
            }

            // Delete the mulipart chunk now that we have processed it
            mg_http_delete_chunk(pConn, hm);

            // Update multipart state
            pMultipartState->contentPos = contentPos;

            // Check for last chunk
            if (hm->chunk.len == 0) {

    #ifdef DEBUG_WEB_HANDLER_REST_API
                // Debug
                LOG_I(MODULE_PREFIX, "handleRequest multipart data last chunk");
    #endif

                // Prepare final block
                FileStreamBlock fileStreamBlock(
                    pMultipartState->fileName.c_str(),
                    contentLength, 
                    contentPos, 
                    nullptr, 
                    0,
                    false,
                    0, 
                    false, 
                    0, 
                    false, 
                    false);
                // Check for callback
                APISourceInfo apiSourceInfo(_webServerSettings._restAPIChannelID);
                if (endpoint.restApiFnChunk)
                    endpoint.restApiFnChunk(endpointName, fileStreamBlock, apiSourceInfo);

                // Last chunk received
                lastChunkReceived = true;
                multipartStateCleanup(pConn);
            }
        }

        // Not chunked - send content to body function
        else
        {
            // Call body method with contents
            APISourceInfo apiSourceInfo(_webServerSettings._restAPIChannelID);
            if (endpoint.restApiFnBody)
                endpoint.restApiFnBody(endpointName, (uint8_t*)hm->body.ptr, hm->body.len, 
                            contentPos, contentLength, apiSourceInfo);

            // Debug
#ifdef DEBUG_WEB_HANDLER_REST_API_BODY_VERBOSE
            {
                const int DEBUG_MAX_STRING_LENGTH = 500;
                String hmBody(hm->body.ptr, hm->body.len > DEBUG_MAX_STRING_LENGTH ? DEBUG_MAX_STRING_LENGTH : hm->body.len);
                if (hm->body.len > DEBUG_MAX_STRING_LENGTH)
                    hmBody += "...";
                LOG_I(MODULE_PREFIX, "handleRequest body API call contentPos %d contentLen %d channelID %d\n----------\n%s\n----------\n", 
                                contentPos, contentLength, _webServerSettings._restAPIChannelID,
                                hmBody.c_str());
            }
#endif
        }
    }

    // Handle regular messages and end of multipart
    if ((ev == MG_EV_HTTP_MSG) || lastChunkReceived)
    {
        // Clear multipart offset (in case multiple files are uploaded)
        Raft::setBEUint32((uint8_t*)pConn->data, RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_POS, 0);

#ifdef DEBUG_WEB_HANDLER_REST_API_RAW_BODY_VERBOSE
        {
            const int DEBUG_MAX_STRING_LENGTH = 500;
            String hmBody(hm->body.ptr, hm->body.len > DEBUG_MAX_STRING_LENGTH ? DEBUG_MAX_STRING_LENGTH : hm->body.len);
            if (hm->body.len > DEBUG_MAX_STRING_LENGTH)
                hmBody += "...";
            LOG_I(MODULE_PREFIX, "handleRequest raw body\n----------\n%s\n----------\n", hmBody.c_str());
        }
#endif

        // Check endpoint callback
        String respStr;
        if (endpoint.restApiFn)
        {
            // Call endpoint
            endpoint.restApiFn(endpointName, respStr, _webServerSettings._restAPIChannelID);

            // Debug
#ifdef DEBUG_WEB_HANDLER_REST_API
            LOG_I(MODULE_PREFIX, "handleRequest respStr %s", respStr.c_str());
#endif
        }

        // Response
        mg_http_reply(pConn, 200, _webServerSettings._stdRespHeaders.c_str(), "%s", respStr.c_str());

        // Cleanup multipart state
        multipartStateCleanup(pConn);
    }

    // Handled ok
    return true;
}

RaftWebServerMethod RaftWebHandlerRestAPI::convMongooseMethodToRaftMethod(const String& mongooseMethod)
{
    if (mongooseMethod.equalsIgnoreCase("GET")) return WEB_METHOD_GET;
    if (mongooseMethod.equalsIgnoreCase("POST")) return WEB_METHOD_POST;
    if (mongooseMethod.equalsIgnoreCase("PUT")) return WEB_METHOD_PUT;
    if (mongooseMethod.equalsIgnoreCase("DELETE")) return WEB_METHOD_DELETE;
    if (mongooseMethod.equalsIgnoreCase("OPTIONS")) return WEB_METHOD_OPTIONS;
    return WEB_METHOD_GET;
}

void RaftWebHandlerRestAPI::multipartStateCleanup(struct mg_connection *pConn)
{
    // Get multipart state ptr
    MongooseMultipartState* pMultipartState = multipartStateGetPtr(pConn);

#ifdef DEBUG_WEB_HANDLER_REST_API
        // Debug
        LOG_I(MODULE_PREFIX, "multipartStateCleanup checking ptr %p", pMultipartState);
#endif

    // Check if we need to delete
    if (pMultipartState)
    {
        // Delete the multipart state
        delete pMultipartState;

#ifdef DEBUG_WEB_HANDLER_REST_API
        // Debug
        LOG_I(MODULE_PREFIX, "multipartStateCleanup DELETING multipart state info");
#endif

        // Set save ptr to null
        memset((void*)pConn->data, 0, RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_LEN);
    }
}

MongooseMultipartState* RaftWebHandlerRestAPI::multipartStateGetPtr(struct mg_connection *pConn)
{
    const uint8_t* pPtrToMultipartStatePtr = (uint8_t*)pConn->data + RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_POS;
    MongooseMultipartState* pMultipartState = nullptr;
    memcpy(&pMultipartState, pPtrToMultipartStatePtr, RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_LEN);
    return pMultipartState;
}

void RaftWebHandlerRestAPI::multipartStateSetPtr(struct mg_connection *pConn, MongooseMultipartState* pMultipartState)
{
    uint8_t* pPtrToMultipartStatePtr = (uint8_t*)pConn->data + RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_POS;
    memcpy(pPtrToMultipartStatePtr, &pMultipartState, RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_LEN);
}

#endif
