/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebHandlerRestAPI.h"

#define DEBUG_WEB_HANDLER_REST_API
#define DEBUG_WEB_HANDLER_REST_API_DETAIL

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

bool RaftWebHandlerRestAPI::handleRequest(struct mg_connection *c, int ev, void *ev_data)
{
#ifdef DEBUG_WEB_HANDLER_REST_API_DETAIL
    if (ev != MG_EV_POLL)
    {
        LOG_I(MODULE_PREFIX, "handleRequest ev %d", ev);
    }    
#endif

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
    if (reqStr.startsWith(_restAPIPrefix))
    {
        // Remove prefix on test string
        String endpointName = reqStr.substring(_restAPIPrefix.length());
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

    // Check if handling chunked data
    bool lastChunkReceived = false;
    if (ev == MG_EV_HTTP_CHUNK)
    {
        // Get offset (this is cleared when connection accepted and when HTTP_MSG received)
        const uint8_t* pData = (uint8_t*)c->data + RAFT_MG_HTTP_DATA_OFFSET_POS;
        uint32_t contentPos = Raft::getBEUint32AndInc(pData);

        // Get content-length if present
        uint32_t contentLength = 0;
        struct mg_str* cl = mg_http_get_header(hm, "Content-Length");
        if (cl)
        {
            contentLength = atol(cl->ptr);
        }

        // Extract information from multipart message
        struct mg_http_part part;
        size_t ofs = 0;
        while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) != 0)
        {

#ifdef DEBUG_WEB_HANDLER_REST_API_DETAIL                
            // Debug
            LOG_I(MODULE_PREFIX, "handleRequest chunked data nameLen %d filenameLen %d bodyLen %d contentPos %d contentLen %d ofs %d",
                        part.name.len, part.filename.len, part.body.len, contentPos, contentLength, ofs);
#endif                

            // // Prepare block
            String filename = String(part.filename.ptr, part.filename.len);
            // FileStreamBlock fileStreamBlock(
            //                 filename.c_str(),
            //                 contentLength, 
            //                 contentPos, 
            //                 part.body.ptr, 
            //                 part.body.len, 
            //                 isFinalPart, 
            //                 formInfo._crc16, 
            //                 formInfo._crc16Valid,
            //                 formInfo._fileLenBytes, 
            //                 formInfo._fileLenValid, 
            //                 contentPos==0);
            // // Check for callback
            // if (_endpoint.restApiFnChunk)
            //     _endpoint.restApiFnChunk(_requestStr, fileStreamBlock, _apiSourceInfo);
            // }

            // Update content pos
            contentPos += part.body.len;
        }

        // Update multipart offset (set to 0 if we have finished)
        Raft::setBEUint32((uint8_t*)c->data, RAFT_MG_HTTP_DATA_OFFSET_POS, 
                        hm->chunk.len == 0 ? 0 : contentPos);

        // String name = String(part.name.ptr, part.name.len);
        // String filename = String(part.filename.ptr, part.filename.len);
        // LOG_I(MODULE_PREFIX, "handleRequest chunked data name %s filename %s bodyLen %d", 
        //             name.c_str(), filename.c_str(), part.body.len);

//         // Prepare to send to chunk callback
//         FileStreamBlock fileStreamBlock(formInfo._fileName.c_str(), 
//                         _headerExtract.contentLength, contentPos, 
//                         pBuf, bufLen, isFinalPart, formInfo._crc16, formInfo._crc16Valid,
//                         formInfo._fileLenBytes, formInfo._fileLenValid, contentPos==0);
//         // Check for callback
//         if (_endpoint.restApiFnChunk)
//             _endpoint.restApiFnChunk(_requestStr, fileStreamBlock, _apiSourceInfo);                
//             // Get the chunk data
//             FileStreamBlock fileStreamBlock;
//             endpoint.restApiFnChunk(reqStr, 
//         }
// } && mg_http_match_uri(hm, "/api/upload")) {
    // MG_INFO(("Got chunk len %lu", (unsigned long) hm->chunk.len));
    // MG_INFO(("Query string: [%.*s]", (int) hm->query.len, hm->query.ptr));
    // MG_INFO(("Chunk data:\n%.*s", (int) hm->chunk.len, hm->chunk.ptr));
        mg_http_delete_chunk(c, hm);
        if (hm->chunk.len == 0) {
            MG_INFO(("Last chunk received"));
            // mg_http_reply(c, 200, "", "ok (chunked)\n");
            lastChunkReceived = true;
        }
// }

// if (ev == MG_EV_HTTP_CHUNK)
// {
//     // Mongoose http message
//     struct mg_http_message *hm = (struct mg_http_message *) ev_data;

//     // Get request string and method string
//     String reqStr = String(hm->uri.ptr, hm->uri.len);
//     String methodStr = String(hm->method.ptr, hm->method.len);

//     String queryStr = String(hm->query.ptr, hm->query.len);

//     LOG_I(MODULE_PREFIX, "handleRequest chunked data %s method %s query %s chunkLen %d", 
//                 reqStr.c_str(), methodStr.c_str(), queryStr.c_str(), hm->chunk.len);
// }

// // Upload info
// FileStreamBlock fileStreamBlock(formInfo._fileName.c_str(), 
//                 _headerExtract.contentLength, contentPos, 
//                 pBuf, bufLen, isFinalPart, formInfo._crc16, formInfo._crc16Valid,
//                 formInfo._fileLenBytes, formInfo._fileLenValid, contentPos==0);
// // Check for callback
// if (_endpoint.restApiFnChunk)
//     _endpoint.restApiFnChunk(_requestStr, fileStreamBlock, _apiSourceInfo);
    }

    // Handle regular messages and end of multipart
    if ((ev == MG_EV_HTTP_MSG) || lastChunkReceived)
    {
        // Clear multipart offset (in case multiple files are uploaded)
        Raft::setBEUint32((uint8_t*)c->data, RAFT_MG_HTTP_DATA_OFFSET_POS, 0);

        // Check endpoint callback
        String respStr;
        if (endpoint.restApiFn)
        {
            // Call endpoint
            endpoint.restApiFn(reqStr, respStr, _webServerSettings._restAPIChannelID);

            // Debug
#ifdef DEBUG_WEB_HANDLER_REST_API
            LOG_I(MODULE_PREFIX, "handleRequest respStr %s", respStr.c_str());
#endif

        // Send start of response
        mg_printf(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n");

        // Add standard headers
        for (auto& header : _standardHeaders)
        {
            mg_printf(c, "%s: %s\r\n", header.name.c_str(), header.value.c_str());
        }

        // End of header
        mg_printf(c, "\r\n");

        // Send response
        mg_http_write_chunk(c, respStr.c_str(), respStr.length());
        mg_http_printf_chunk(c, "");
        }
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

#endif
