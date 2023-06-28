/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <RaftUtils.h>
#include "RaftWebHandlerRestAPI.h"

// #define WARN_ON_MULIPART_API_ERROR
// #define DEBUG_WEB_HANDLER_REST_API
// #define DEBUG_WEB_HANDLER_REST_API_DETAIL
// #define DEBUG_WEB_HANDLER_REST_API_RAW_BODY_INFO
// #define DEBUG_WEB_HANDLER_REST_API_RAW_BODY_VERBOSE
// #define DEBUG_WEB_HANDLER_REST_API_BODY_VERBOSE
// #define DEBUG_WEB_HANDLER_REST_API_CHUNK_VERBOSE
// #define DEBUG_WEB_HANDLER_REST_API_EVENT_VERBOSE
// #define DEBUG_WEB_HANDLER_REST_API_MATCH_DETAIL
// #define DEBUG_MULTIPART_DATA
// #define DEBUG_MULTIPART_EVENTS
// #define DEBUG_MULTIPART_HEADERS
#define DEBUG_WEB_HANDLER_STATE_MANAGEMENT

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <MongooseMultipartState.h>
#include <FileStreamBlock.h>
#include <RaftWebConnManager_mongoose.h>
#include <APISourceInfo.h>
#endif

#if defined(DEBUG_WEB_HANDLER_REST_API) || defined(WARN_ON_MULIPART_API_ERROR) || defined(DEBUG_WEB_HANDLER_STATE_MANAGEMENT)
static const char* MODULE_PREFIX = "RaftWebHandlerRestAPI";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle request (Original)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef FEATURE_WEB_SERVER_USE_MONGOOSE

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

#endif // FEATURE_WEB_SERVER_USE_MONGOOSE

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

    // Check for HTTP message or chunk
    if ((ev != MG_EV_HTTP_MSG) && (ev != MG_EV_HTTP_CHUNK))
        return false;

    // Get mongoose http message
    struct mg_http_message *pHttpMsg = (struct mg_http_message *) ev_data;

    // Get request string and method string
    String reqStr = String(pHttpMsg->uri.ptr, pHttpMsg->uri.len);
    String methodStr = String(pHttpMsg->method.ptr, pHttpMsg->method.len);

    // Check for API prefix
    RaftWebServerRestEndpoint endpoint;
    bool canHandle = false;
    RaftWebServerMethod method = convMongooseMethodToRaftMethod(methodStr);
    String endpointName = reqStr;
    if (reqStr.startsWith(_restAPIPrefix))
    {
        // Endpoint name is after prefix
        endpointName = reqStr.substring(_restAPIPrefix.length());
#ifdef DEBUG_WEB_HANDLER_REST_API_MATCH_DETAIL
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

    // Check if handling a chunk - this is not just for "TransferEncoding: chunked" but any body data it seems
    RaftRetCode retCode = RAFT_RET_OK;
    if (ev == MG_EV_HTTP_CHUNK)
    {
        // Check if content is multipart-form-data
        struct mg_str *contentType = mg_http_get_header(pHttpMsg, "Content-Type");
        bool isMultipartForm = false;
        String multipartBoundary;
        if (contentType)
        {
            String contentTypeStr(contentType->ptr, contentType->len);
            // LOG_I(MODULE_PREFIX, "handleRequest content-type %s", contentTypeStr.c_str());
            isMultipartForm = contentTypeStr.startsWith("multipart/form-data");
            multipartBoundary = contentTypeStr.substring(contentTypeStr.indexOf("boundary=")+9);
            multipartBoundary = multipartBoundary.substring(0, multipartBoundary.indexOf("\r\n"));
        }

        // Check multipart state is valid
        if (!pMultipartState)
        {
            // No multipart state
#ifdef DEBUG_WEB_HANDLER_STATE_MANAGEMENT
            LOG_I(MODULE_PREFIX, "handleRequest no connection state - so creating - mem free %d",
                            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif

            // Create new connection state
            pMultipartState = new MongooseMultipartState();
            if (!pMultipartState)
                return false;

            // Store endpoint
            pMultipartState->endpoint = endpoint;

            // ChannelID
            pMultipartState->channelID = _webServerSettings._restAPIChannelID;

            // Request string
            pMultipartState->reqStr = reqStr;

            // Give context to multipart state
            pMultipartState->multipartParser.setContext(pMultipartState);

            // Hook up callbacks
            pMultipartState->multipartParser.onEvent = std::bind(&RaftWebHandlerRestAPI::multipartOnEvent, this, 
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
            pMultipartState->multipartParser.onData = std::bind(&RaftWebHandlerRestAPI::multipartOnData, this, 
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, 
                    std::placeholders::_5, std::placeholders::_6);
            pMultipartState->multipartParser.onHeaderNameValue = std::bind(&RaftWebHandlerRestAPI::multipartOnHeaderNameValue, this, 
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

            // Check if multipart
            if (isMultipartForm)
                pMultipartState->multipartParser.setBoundary(multipartBoundary);

            // Store in connection state data
            multipartStateSetPtr(pConn, pMultipartState);
        }

        // Get content-length if present
        struct mg_str* cl = mg_http_get_header(pHttpMsg, "Content-Length");
        pMultipartState->contentLength = cl ? atol(cl->ptr) : 0;

#ifdef DEBUG_WEB_HANDLER_REST_API_RAW_BODY_VERBOSE
        // Get the transfer coding
        struct mg_str *transferEncoding = mg_http_get_header(pHttpMsg, "Transfer-Encoding");
        bool isChunked = (transferEncoding != NULL) && (mg_vcasecmp(transferEncoding, "chunked") == 0);

        // Debug
        LOG_I(MODULE_PREFIX, "handleRequest EV_CHUNK %s %s contentPos %d", isMultipartForm ? "MULTIPART" : "NOT-MULTIPART",
                        isChunked ? "CHUNKED" : "NOT-CHUNKED", pMultipartState->contentPos);

        debugMultipartChunk("===CHUNK_CHUNK", pHttpMsg->chunk.ptr, pHttpMsg->chunk.len);
#endif

        // Assume that any multipart data is a file-upload
        if (isMultipartForm)
        {
            retCode = handleFileUploadChunk(endpointName, endpoint, pConn, pHttpMsg, pMultipartState);
        }

        // Not multipart - send content to body function
        else
        {
            // Call body method with contents
            APISourceInfo apiSourceInfo(_webServerSettings._restAPIChannelID);
            if (endpoint.restApiFnBody)
                retCode = endpoint.restApiFnBody(endpointName, (uint8_t*)pHttpMsg->body.ptr, pHttpMsg->body.len, 
                            0, pMultipartState->contentLength, apiSourceInfo);

            // Debug
#ifdef DEBUG_WEB_HANDLER_REST_API_BODY_VERBOSE
            {
                LOG_I(MODULE_PREFIX, "handleRequest body API call contentLen %d channelID %d result %s", 
                                pMultipartState->contentLength, _webServerSettings._restAPIChannelID, Raft::getRetCodeStr(retCode));
                debugMultipartChunk("===API_BODY", pHttpMsg->body.ptr, pHttpMsg->body.len);
            }
#endif
        }
    }

    // Handle regular messages and end of multipart
    if ((ev == MG_EV_HTTP_MSG) || pMultipartState->lastBlock || (retCode != RAFT_RET_OK))
    {
        // Clear multipart offset (in case multiple files are uploaded)
        Raft::setBEUint32((uint8_t*)pConn->data, RAFT_MG_HTTP_DATA_MULTIPART_STATE_PTR_POS, 0);
        pMultipartState->lastBlock = false;

#ifdef DEBUG_WEB_HANDLER_REST_API_RAW_BODY_VERBOSE
        if (ev == MG_EV_HTTP_MSG)
        {
            LOG_I(MODULE_PREFIX, "handleRequest raw body API call channelID %d", _webServerSettings._restAPIChannelID);
            debugMultipartChunk("===MSG_BODY", pHttpMsg->body.ptr, pHttpMsg->body.len);
            debugMultipartChunk("===MSG_CHUNK", pHttpMsg->chunk.ptr, pHttpMsg->chunk.len);
        }
        else if (retCode != RAFT_RET_OK)
        {
            LOG_I(MODULE_PREFIX, "handleRequest API call failed channelID %d retCode %s", 
                            _webServerSettings._restAPIChannelID, Raft::getRetCodeStr(retCode));
        }
        else
        {
            LOG_I(MODULE_PREFIX, "handleRequest multipart end OK channelID %d", _webServerSettings._restAPIChannelID);
        }
#endif

        // Check endpoint callback
        String respStr;
        if (endpoint.restApiFn)
        {
            // Call endpoint
            RaftRetCode retCodeEnd = endpoint.restApiFn(endpointName, respStr, _webServerSettings._restAPIChannelID);

            // Debug
#ifdef DEBUG_WEB_HANDLER_REST_API
            LOG_I(MODULE_PREFIX, "handleRequest respStr %s dataRetCode %s endRetCode %s ", 
                            respStr.c_str(), 
                            Raft::getRetCodeStr(retCode), 
                            Raft::getRetCodeStr(retCodeEnd));
#endif

            if ((retCodeEnd == RAFT_RET_OK) && (retCode != RAFT_RET_OK))
            {
                String respError = R"("error":")" + String(Raft::getRetCodeStr(retCode)) + R"(")";
                Raft::setJsonBoolResult(endpointName.c_str(), respStr, false, respError.c_str());
            }
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

#ifdef DEBUG_WEB_HANDLER_STATE_MANAGEMENT
        // Debug
        LOG_I(MODULE_PREFIX, "multipartStateCleanup checking ptr %p", pMultipartState);
#endif

    // Check if we need to delete
    if (pMultipartState)
    {
        // Delete the multipart state
        delete pMultipartState;

#ifdef DEBUG_WEB_HANDLER_STATE_MANAGEMENT
        // Debug
        LOG_I(MODULE_PREFIX, "multipartStateCleanup DELETING multipart state info - mem free %d",
                        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mongoose multipart handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode RaftWebHandlerRestAPI::handleFileUploadChunk(const String& endpointName,
                RaftWebServerRestEndpoint& endpoint,
                struct mg_connection *pConn, 
                struct mg_http_message *pHttpMsg,
                MongooseMultipartState* pMultipartState)
{
    // Use multipart parser
    RaftRetCode retCode = RAFT_RET_OK;
    if (pHttpMsg->chunk.ptr && (pHttpMsg->chunk.len > 0))
        retCode = pMultipartState->multipartParser.handleData((const uint8_t*)pHttpMsg->chunk.ptr, pHttpMsg->chunk.len);

    // Delete the mulipart chunk now that we have processed it
    mg_http_delete_chunk(pConn, pHttpMsg);

    // Result
    return retCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mongoose multipart handling - send file chunk to API
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode RaftWebHandlerRestAPI::sendFileChunkToRestAPI(const String& endpointName,
            RaftWebServerRestEndpoint& endpoint, 
            const String& filename, uint32_t channelID,
            const uint8_t* pChunk, uint32_t chunkLen,
            MongooseMultipartState* pMultipartState)
{
    FileStreamBlock fileStreamBlock(
        filename.c_str(),
        pMultipartState->contentLength, 
        pMultipartState->contentPos, 
        pChunk, 
        chunkLen,
        false,
        0, 
        false, 
        0, 
        false, 
        pMultipartState->contentPos==0);
    // Check for callback
    APISourceInfo apiSourceInfo(channelID);
    RaftRetCode retCode = RAFT_RET_INVALID_OBJECT;
    if (endpoint.restApiFnChunk)
    {
        retCode = endpoint.restApiFnChunk(endpointName, fileStreamBlock, apiSourceInfo);

        // Update content pos
        pMultipartState->contentPos += chunkLen;

        // Check ok
        if (retCode != RAFT_RET_OK)
        {
#ifdef WARN_ON_MULIPART_API_ERROR
            // Warn
            LOG_W(MODULE_PREFIX, "handleRequest multipart API error %d", retCode);
#endif
        }
    }
    return retCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks on multipart parser
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebHandlerRestAPI::multipartOnEvent(void* pCtx, 
                    RaftMultipartEvent event, const uint8_t *pBuf, uint32_t pos)
{
#ifdef DEBUG_MULTIPART_EVENTS
    LOG_W(MODULE_PREFIX, "multipartEvent event %s (%d) pos %d", RaftWebMultipart::getEventText(event), event, pos);
#endif
}

RaftRetCode RaftWebHandlerRestAPI::multipartOnData(void* pCtx,
                    const uint8_t *pBuf, uint32_t bufLen, RaftMultipartForm& formInfo, 
                    uint32_t contentPos, bool isFinalPart)
{
    // Get context
    MongooseMultipartState* pMultipartState = (MongooseMultipartState*)pCtx;
    if (!pMultipartState)
        return RAFT_RET_INVALID_OBJECT;

#ifdef DEBUG_MULTIPART_DATA
    LOG_W(MODULE_PREFIX, "multipartData len %d filename %s contentPos %d isFinal %d", 
                bufLen, formInfo._fileName.c_str(), contentPos, isFinalPart);
#endif
    // Upload info
    FileStreamBlock fileStreamBlock(formInfo._fileName.c_str(), 
                    pMultipartState->contentLength, contentPos, 
                    pBuf, bufLen, isFinalPart, formInfo._crc16, formInfo._crc16Valid,
                    formInfo._fileLenBytes, formInfo._fileLenValid, contentPos==0);

    // Set flag to indicate final part done
    pMultipartState->lastBlock = isFinalPart;

    // Check for callback
    APISourceInfo apiSourceInfo(pMultipartState->channelID);
    if (pMultipartState->endpoint.restApiFnChunk)
        return pMultipartState->endpoint.restApiFnChunk(pMultipartState->reqStr, fileStreamBlock, apiSourceInfo);

    // Not implemented
    return RAFT_RET_NOT_IMPLEMENTED;
}

void RaftWebHandlerRestAPI::multipartOnHeaderNameValue(void* pCtx,
                    const String& name, const String& val)
{
#ifdef DEBUG_MULTIPART_HEADERS
    LOG_W(MODULE_PREFIX, "multipartHeaderNameValue %s = %s", name.c_str(), val.c_str());
#endif
}

////////////////////////////////////////////////////////////////////////////////
/// Debug functions
////////////////////////////////////////////////////////////////////////////////

void RaftWebHandlerRestAPI::debugMultipartChunk(const char* pPrefix,
                const char* pChunk, uint32_t chunkLen)
{
#ifdef DEBUG_WEB_HANDLER_REST_API_DETAIL
    LOG_I(MODULE_PREFIX, "%s chunkLen %d", pPrefix, chunkLen);
#endif
}

#endif
