/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "stdint.h"
#include "stddef.h"
#include <functional>
extern "C"
{
#include "lwip/err.h"
}

class FileStreamBlock;
class String;
class APISourceInfo;

// Web methods
enum RaftWebServerMethod
{
    WEB_METHOD_NONE, 
    WEB_METHOD_GET, 
    WEB_METHOD_POST, 
    WEB_METHOD_DELETE,
    WEB_METHOD_PUT, 
    WEB_METHOD_PATCH, 
    WEB_METHOD_HEAD, 
    WEB_METHOD_OPTIONS
};

// Web connection type
enum RaftWebReqConnectionType
{
    REQ_CONN_TYPE_NONE = -1,
    REQ_CONN_TYPE_HTTP,
    REQ_CONN_TYPE_WEBSOCKET,
    REQ_CONN_TYPE_EVENT,
    REQ_CONN_TYPE_MAX
};

// HTTP Status codes
enum RaftHttpStatusCode
{
    // Informational - interim responses
	HTTP_STATUS_CONTINUE                      = 100,
	HTTP_STATUS_SWITCHING_PROTOCOLS           = 101,
	HTTP_STATUS_PROCESSING                    = 102,
	HTTP_STATUS_EARLYHINTS                    = 103,
   
    // Success   
	HTTP_STATUS_OK                            = 200,
	HTTP_STATUS_CREATED                       = 201,
	HTTP_STATUS_ACCEPTED                      = 202,
	HTTP_STATUS_NONAUTHORITATIVEINFORMATION   = 203,
	HTTP_STATUS_NOCONTENT                     = 204,
	HTTP_STATUS_RESETCONTENT                  = 205,
	HTTP_STATUS_PARTIALCONTENT                = 206,
	HTTP_STATUS_MULTISTATUS                   = 207,
	HTTP_STATUS_ALREADYREPORTED               = 208,
	HTTP_STATUS_IMUSED                        = 226,
   
	// Redirection   
	HTTP_STATUS_MULTIPLECHOICES               = 300,
	HTTP_STATUS_MOVEDPERMANENTLY              = 301,
	HTTP_STATUS_FOUND                         = 302,
	HTTP_STATUS_SEEOTHER                      = 303,
	HTTP_STATUS_NOTMODIFIED                   = 304,
	HTTP_STATUS_USEPROXY                      = 305,
	                                                
	HTTP_STATUS_TEMPORARYREDIRECT             = 307,
	HTTP_STATUS_PERMANENTREDIRECT             = 308,
   
	// Client error   
	HTTP_STATUS_BADREQUEST                    = 400,
	HTTP_STATUS_UNAUTHORIZED                  = 401,
	HTTP_STATUS_PAYMENTREQUIRED               = 402,
	HTTP_STATUS_FORBIDDEN                     = 403,
	HTTP_STATUS_NOTFOUND                      = 404,
	HTTP_STATUS_METHODNOTALLOWED              = 405,
	HTTP_STATUS_NOTACCEPTABLE                 = 406,
	HTTP_STATUS_PROXYAUTHENTICATIONREQUIRED   = 407,
	HTTP_STATUS_REQUESTTIMEOUT                = 408,
	HTTP_STATUS_CONFLICT                      = 409,
	HTTP_STATUS_GONE                          = 410,
	HTTP_STATUS_LENGTHREQUIRED                = 411,
	HTTP_STATUS_PRECONDITIONFAILED            = 412,
	HTTP_STATUS_PAYLOADTOOLARGE               = 413,
	HTTP_STATUS_URITOOLONG                    = 414,
	HTTP_STATUS_UNSUPPORTEDMEDIATYPE          = 415,
	HTTP_STATUS_RANGENOTSATISFIABLE           = 416,
	HTTP_STATUS_EXPECTATIONFAILED             = 417,
	HTTP_STATUS_IMATEAPOT                     = 418,
	HTTP_STATUS_UNPROCESSABLEENTITY           = 422,
	HTTP_STATUS_LOCKED                        = 423,
	HTTP_STATUS_FAILEDDEPENDENCY              = 424,
	HTTP_STATUS_UPGRADEREQUIRED               = 426,
	HTTP_STATUS_PRECONDITIONREQUIRED          = 428,
	HTTP_STATUS_TOOMANYREQUESTS               = 429,
	HTTP_STATUS_REQUESTHEADERFIELDSTOOLARGE   = 431,
	HTTP_STATUS_UNAVAILABLEFORLEGALREASONS    = 451,

    // Server error
	HTTP_STATUS_INTERNALSERVERERROR           = 500,
	HTTP_STATUS_NOTIMPLEMENTED                = 501,
	HTTP_STATUS_BADGATEWAY                    = 502,
	HTTP_STATUS_SERVICEUNAVAILABLE            = 503,
	HTTP_STATUS_GATEWAYTIMEOUT                = 504,
	HTTP_STATUS_HTTPVERSIONNOTSUPPORTED       = 505,
	HTTP_STATUS_VARIANTALSONEGOTIATES         = 506,
	HTTP_STATUS_INSUFFICIENTSTORAGE           = 507,
	HTTP_STATUS_LOOPDETECTED                  = 508,
	HTTP_STATUS_NOTEXTENDED                   = 510,
	HTTP_STATUS_NETWORKAUTHENTICATIONREQUIRED = 511,

    // Max
	HTTP_STATUS_MAX = 1023
};

class RaftWebInterface
{
public:
    // Get Web Method Str
    static const char* getHTTPMethodStr(RaftWebServerMethod method);

    // Get conn type string
    static const char* getReqConnTypeStr(RaftWebReqConnectionType reqConnType);

    // ESP IDF error codes
    static const char* espIdfErrToStr(err_t err);

    // HTTP status codes
    static const char* getHTTPStatusStr(RaftHttpStatusCode status);
};

// Endpoint functions
typedef std::function<void(String &reqStr, String &respStr, const APISourceInfo& sourceInfo)> RaftWebAPIFunction;
typedef std::function<void(String &reqStr, const uint8_t *pData, size_t len, size_t index, 
					size_t total, const APISourceInfo& sourceInfo)> RaftWebAPIFnBody;
typedef std::function<void(String &reqStr, FileStreamBlock& fileStreamBlock, const APISourceInfo& sourceInfo)> RaftWebAPIFnChunk;
typedef std::function<bool(const APISourceInfo& sourceInfo)> RaftWebAPIFnIsReady;

// REST API support
class RaftWebServerRestEndpoint
{
public:
    RaftWebServerRestEndpoint()
    {
        restApiFn = nullptr;
        restApiFnBody = nullptr;
        restApiFnChunk = nullptr;
		restApiFnIsReady = nullptr;
    }
    RaftWebAPIFunction restApiFn;
    RaftWebAPIFnBody restApiFnBody;
    RaftWebAPIFnChunk restApiFnChunk;
	RaftWebAPIFnIsReady restApiFnIsReady;
};

typedef std::function<bool(const char* url, RaftWebServerMethod method, RaftWebServerRestEndpoint& endpoint)> RaftWebAPIMatchEndpointCB;

// Websocket support
typedef std::function<bool(uint32_t channelID)> RaftWebSocketCanAcceptCB;
typedef std::function<void(uint32_t channelID, const uint8_t* pBuf, uint32_t bufLen)> RaftWebSocketMsgCB;

