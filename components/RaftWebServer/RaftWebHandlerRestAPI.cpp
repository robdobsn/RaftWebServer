/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftUtils.h"
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

#if defined(DEBUG_WEB_HANDLER_REST_API) || defined(WARN_ON_MULIPART_API_ERROR)
static const char* MODULE_PREFIX = "RaftWebHandlerRestAPI";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle request
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
                    _webServerSettings.restAPIChannelID);

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

