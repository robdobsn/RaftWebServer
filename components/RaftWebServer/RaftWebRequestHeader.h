/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <RaftArduino.h>
#include <RaftJson.h>
#include "RaftWebInterface.h"

class RaftWebRequestHeaderExtract
{
public:
    RaftWebRequestHeaderExtract()
    {
        clear();
    }

    void clear()
    {
        method = WEB_METHOD_NONE;
        host.clear();
        contentType.clear();
        multipartBoundary.clear();
        authorization.clear();
        isMultipart = false;
        isDigest = false;
        contentLength = 0;
    }

    // Request method
    RaftWebServerMethod method;

    // Host - from Host header
    String host;

    // Content type
    String contentType;

    // Multipart boundary
    String multipartBoundary;

    // Is multipart message
    bool isMultipart;

    // Content length
    uint32_t contentLength;

    // Authorization
    String authorization;
    bool isDigest;
};

// Web request header info
class RaftWebRequestHeader
{
public:
    RaftWebRequestHeader()
    {
        clear();
    }

    void clear()
    {
        gotFirstLine = false;
        isComplete = false;
        URIAndParams.clear();
        URL.clear();
        params.clear();
        versStr.clear();
        nameValues.clear();
        nameValues.reserve(MAX_WEB_HEADERS/2);
        isContinue = false;
        reqConnType = REQ_CONN_TYPE_HTTP;
        extract.clear();
    }

    // Got first line (which contains request)
    bool gotFirstLine;

    // Is complete
    bool isComplete;

    // URI (inc params)
    String URIAndParams;

    // URL (excluding params)
    String URL;

    // Params
    String params;

    // Version
    String versStr;

    // Header name/value pairs
    static const uint32_t MAX_WEB_HEADERS = 20;
    std::vector<RaftJson::NameValuePair> nameValues;

    // Header extract
    RaftWebRequestHeaderExtract extract;

    // Continue required
    bool isContinue;

    // Requested connection type
    RaftWebReqConnectionType reqConnType;

    // WebSocket info
    String webSocketKey;
    String webSocketVersion;

};
