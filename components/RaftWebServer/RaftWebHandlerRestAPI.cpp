/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebHandlerRestAPI.h"

#define DEBUG_WEB_HANDLER_REST_API

static const char* MODULE_PREFIX = "RaftWebHandlerRestAPI";

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
    // Check valid
    if (!_matchEndpointCB)
        return false;

    // Check for HTTP message
    if (ev == MG_EV_HTTP_MSG) 
    {
        // Mongoose http message
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        // Get request string and method string
        String reqStr = String(hm->uri.ptr, hm->uri.len);
        String methodStr = String(hm->method.ptr, hm->method.len);

        // Check for API prefix
        RaftWebServerRestEndpoint endpoint;
        bool canHandle = false;
        if (reqStr.startsWith(_restAPIPrefix))
        {
            // Remove prefix on test string
            LOG_I(MODULE_PREFIX, "handleRequest reqStr %s prefix %s", reqStr.c_str(), _restAPIPrefix.c_str());
            String endpointName = reqStr.substring(_restAPIPrefix.length());
            if (_matchEndpointCB(endpointName.c_str(), convMongooseMethodToRaftMethod(methodStr), endpoint))
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

        // Check endpoint callback
        String respStr;
        if (endpoint.restApiFn)
        {
            // Call endpoint
            endpoint.restApiFn(reqStr, respStr, _webServerSettings._restAPIChannelID);

            // Debug
            LOG_I(MODULE_PREFIX, "handleRequest respStr %s", respStr.c_str());
        }

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

        // mg_http_reply(c, 200, "", "{\"result\": \"%.*s\"}\n", (int) hm->uri.len, hm->uri.ptr);
        // return _respStr.length();

    //     LOG_I(MODULE_PREFIX, "canHandle matching %s with req %.*s", _restAPIPrefix.c_str(), (int) hm->uri.len, hm->uri.ptr);

    //     if (mg_http_match_uri(hm, _restAPIPrefix.c_str())) 
    //     {
            
    //         // Debug
    //         LOG_I(MODULE_PREFIX, "canHandle matched %s req %.*s", _restAPIPrefix.c_str(), (int) hm->uri.len, hm->uri.ptr);

    // //         // // TODO - remove
    // //         // // Print some statistics about currently established connections
    // //         // mg_printf(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    // //         // mg_http_printf_chunk(c, "ID PROTO TYPE      LOCAL           REMOTE\n");
    // //         // for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
    // //         // mg_http_printf_chunk(c, "%-3lu %4s %s %M %M\n", t->id,
    // //         //                         t->is_udp ? "UDP" : "TCP",
    // //         //                         t->is_listening  ? "LISTENING"
    // //         //                         : t->is_accepted ? "ACCEPTED "
    // //         //                                         : "CONNECTED",
    // //         //                         mg_print_ip, &t->loc, mg_print_ip, &t->rem);
    // //         // }
    // //         // mg_http_printf_chunk(c, "");  // Don't forget the last empty chunk

    //         return true;
    //     }
        
    //     struct mg_http_serve_opts opts = 
    //     {
    //         .root_dir = _baseFolder.c_str(),
    //         .ssi_pattern = NULL,
    //         .extra_headers = NULL,
    //         .mime_types = NULL,
    //         .page404 = NULL,
    //         .fs = NULL,
    //     };
    //     mg_http_serve_dir(c, hm, &opts);

        // Handled ok
        return true;
    }
    return false;
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
