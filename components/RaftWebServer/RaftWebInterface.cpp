/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebInterface.h"

// Web Methods
const char* RaftWebInterface::getHTTPMethodStr(RaftWebServerMethod method)
{
    switch(method)
    {
        case WEB_METHOD_NONE: return "NONE";
        case WEB_METHOD_GET: return "GET";
        case WEB_METHOD_POST: return "POST";
        case WEB_METHOD_DELETE: return "DELETE";
        case WEB_METHOD_PUT: return "PUT";
        case WEB_METHOD_PATCH: return "PATCH";
        case WEB_METHOD_HEAD: return "HEAD";
        case WEB_METHOD_OPTIONS: return "OPTIONS";
    }
    return "NONE";
}

// Get conn type string
const char* RaftWebInterface::getReqConnTypeStr(RaftWebReqConnectionType reqConnType)
{
    switch(reqConnType)
    {
        default:
        case REQ_CONN_TYPE_NONE: return "NONE";
        case REQ_CONN_TYPE_HTTP: return "HTTP";
        case REQ_CONN_TYPE_WEBSOCKET: return "WEBSOCKET";
        case REQ_CONN_TYPE_EVENT: return "DELETE"; 
    }
    return "NONE";
}

// HTTP status codes
const char* RaftWebInterface::getHTTPStatusStr(RaftHttpStatusCode status)
{
    switch(status)
    {
        case HTTP_STATUS_CONTINUE: return "Continue";
        case HTTP_STATUS_SWITCHING_PROTOCOLS: return "Switching Protocols";
        case HTTP_STATUS_OK: return "OK";
        case HTTP_STATUS_NOCONTENT: return "No Content";
        case HTTP_STATUS_BADREQUEST: return "Bad Request";
        case HTTP_STATUS_FORBIDDEN: return "Forbidden";
        case HTTP_STATUS_NOTFOUND: return "Not Found";
        case HTTP_STATUS_METHODNOTALLOWED: return "Method Not Allowed";
        case HTTP_STATUS_REQUESTTIMEOUT: return "Request Time-out";
        case HTTP_STATUS_LENGTHREQUIRED: return "Length Required";
        case HTTP_STATUS_PAYLOADTOOLARGE: return "Request Entity Too Large";
        case HTTP_STATUS_URITOOLONG: return "Request-URI Too Large";
        case HTTP_STATUS_UNSUPPORTEDMEDIATYPE: return "Unsupported Media Type";
        case HTTP_STATUS_NOTIMPLEMENTED: return "Not Implemented";
        case HTTP_STATUS_SERVICEUNAVAILABLE: return "Service Unavailable";
        default: return "See W3 ORG";
    }
    return "";
}
