/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebHandlerStaticFiles.h"
#include "RaftWebConnection.h"
#include "RaftWebResponder.h"
#include "RaftWebResponderFile.h"
#include <Logger.h>
#include <FileSystem.h>
#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <mongoose.h>
#endif

// #define DEBUG_STATIC_FILE_HANDLER

#if defined(DEBUG_STATIC_FILE_HANDLER)
static const char* MODULE_PREFIX = "RaftWebHdlrStaticFiles";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebHandlerStaticFiles::RaftWebHandlerStaticFiles(const char* pServePaths, const char* pCacheControl)
{
    // Handle URI and base folder
    if (pServePaths)
        _servePaths = pServePaths;
    if (pCacheControl)
        _cacheControl = pCacheControl;

#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)

    // Split the base folders by comma
    String baseFolders = _servePaths;
    int pos = 0;
    while (pos < baseFolders.length())
    {
        // Find next comma
        int nextPos = baseFolders.indexOf(',', pos);
        if (nextPos < 0)
            nextPos = baseFolders.length();

        // Get the folder
        String folder = baseFolders.substring(pos, nextPos);

        // Check if there is a = in the folder
        int eqPos = folder.indexOf('=');
        if (eqPos > 0)
        {
            // Get the URI and folder
            String uri = folder.substring(0, eqPos);
            String path = folder.substring(eqPos+1);

            // Ensure paths and URLs have a leading /
            if (uri.length() == 0 || uri[0] != '/')
                uri = "/" + uri;
            if (path.length() == 0 || path[0] != '/')
                path = "/" + path;

            // Remove trailing /
            if (uri.endsWith("/"))
                uri.remove(uri.length()-1);
            if (path.endsWith("/"))
                path.remove(path.length()-1);

            // Add to vector
            RdJson::NameValuePair nvPair(uri, path);
            _servedPathPairs.push_back(nvPair);
        }
        else
        {
            // Ensure paths and URLs have a leading /
            if (folder.length() == 0 || folder[0] != '/')
                folder = "/" + folder;

            // Remove trailing /
            if (folder.endsWith("/"))
                folder.remove(folder.length()-1);

            // Add to vector
            RdJson::NameValuePair nvPair("/", folder);
            _servedPathPairs.push_back(nvPair);
        }

        // Next
        pos = nextPos + 1;
    }

    // Debug show name-value pairs
#ifdef DEBUG_STATIC_FILE_HANDLER
    for (auto& nvPair : _servedPathPairs)
    {
        LOG_I(MODULE_PREFIX, "servedPathPairs uri %s path %s", nvPair.name.c_str(), nvPair.value.c_str());
    }
#endif

#endif

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebHandlerStaticFiles::~RaftWebHandlerStaticFiles()
{        
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// getName of the handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* RaftWebHandlerStaticFiles::getName() const
{
    return "HandlerStaticFiles";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get a responder if we can handle this request (ORIGINAL)
// NOTE: this returns a new object or NULL
// NOTE: if a new object is returned the caller is responsible for deleting it when appropriate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)

RaftWebResponder* RaftWebHandlerStaticFiles::getNewResponder(const RaftWebRequestHeader& requestHeader, 
            const RaftWebRequestParams& params,
            RaftHttpStatusCode &statusCode)
{
    // Debug
#ifdef DEBUG_STATIC_FILE_HANDLER
    uint64_t getResponderStartUs = micros();
    LOG_I(MODULE_PREFIX, "getNewResponder reqURL %s paths %s", requestHeader.URL.c_str(), _servePaths.c_str());    
#endif

    // Must be a GET
    if (requestHeader.extract.method != WEB_METHOD_GET)
        return nullptr;

    // Check that the connection type is HTTP
    if (requestHeader.reqConnType != REQ_CONN_TYPE_HTTP)
        return NULL;

    // Check the URL is valid
    RdJson::NameValuePair longestMatchedPath;
    for (auto& servePath : _servedPathPairs)
    {
        // Check if the path matches
        if (requestHeader.URL.startsWith(servePath.name))
        {
            if (servePath.name.length() > longestMatchedPath.name.length())
                longestMatchedPath = servePath;
        }
    }

    // Check if we found a match
    if (longestMatchedPath.name.length() == 0)
    {
#ifdef DEBUG_STATIC_FILE_HANDLER
        LOG_I(MODULE_PREFIX, "getNewResponder failed no match uri %s", requestHeader.URL.c_str());
#endif
        return nullptr;
    }

    // Get the file path
    String filePath = longestMatchedPath.value + "/";
    if (requestHeader.URL == "/")
    {
        filePath += "index.html";
    }
    else
    {
        String reqFileElem = requestHeader.URL.substring(longestMatchedPath.name.length());
        if (reqFileElem.startsWith("/"))
            reqFileElem.remove(0, 1);
        filePath += reqFileElem;
    }

    // Debug
#ifdef DEBUG_STATIC_FILE_HANDLER
    LOG_I(MODULE_PREFIX, "getNewResponder req %s filePath %s longestMatch name %s value %s", 
                requestHeader.URL.c_str(),
                filePath.c_str(), 
                longestMatchedPath.name.c_str(), longestMatchedPath.value.c_str());
#endif

    // Create responder
    RaftWebResponder* pResponder = new RaftWebResponderFile(filePath, this, params, 
            requestHeader, _webServerSettings._sendBufferMaxLen);

    // Check valid
    if (!pResponder)
        return nullptr;

    // Check active (otherwise file didn't exist, etc)
    if (!pResponder->isActive())
    {
        delete pResponder;
#ifdef DEBUG_STATIC_FILE_HANDLER
    uint64_t getResponderEndUs = micros();
    LOG_I(MODULE_PREFIX, "canHandle failed new responder (file not found?) uri %s took %lld", 
                requestHeader.URL.c_str(), getResponderEndUs-getResponderStartUs);
#endif
        return nullptr;
    }

    // Debug
#ifdef DEBUG_STATIC_FILE_HANDLER
    uint64_t getResponderEndUs = micros();
    LOG_I(MODULE_PREFIX, "canHandle constructed new responder %lx uri %s took %lld", 
                (unsigned long)pResponder, requestHeader.URL.c_str(), getResponderEndUs-getResponderStartUs);
#endif

    // Return new responder - caller must clean up by deleting object when no longer needed
    statusCode = HTTP_STATUS_OK;
    return pResponder;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get content type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftWebHandlerStaticFiles::getContentType(const String& filePath) const
{
    // Iterate MIME types str
    const char* pCurMime = _webServerSettings._pMimeTypes ? _webServerSettings._pMimeTypes : _mimeTypesStr;
    while (pCurMime != NULL)
    {
        // Get extension
        const char* pExt = pCurMime;
        pCurMime = strchr(pCurMime, ',');
        if (pCurMime != NULL)
        {
            // Get content type
            String contentExtAndType = String(pExt, pCurMime - pExt);
            String ext = contentExtAndType.substring(0, contentExtAndType.indexOf('='));
            String contentType = contentExtAndType.substring(contentExtAndType.indexOf('=') + 1);
            if (filePath.endsWith(ext))
                return contentType;
            pCurMime++;
        }
    }
    return "text/plain";
}

#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mongoose handlers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

bool RaftWebHandlerStaticFiles::handleRequest(struct mg_connection *pConn, int ev, void *ev_data)
{
    // Check event
    if (ev == MG_EV_HTTP_MSG) 
    {
        // Update extra headers string
        _extraHeadersStr = "";
        for (auto& header : _standardHeaders)
        {
            _extraHeadersStr += header.name + ": " + header.value + "\r\n";
        }

        // Extract message
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        struct mg_http_serve_opts opts = 
        {
            .root_dir = _servePaths.c_str(),
            .ssi_pattern = NULL,
            .extra_headers = _extraHeadersStr.c_str(),
            .mime_types = _webServerSettings._pMimeTypes ? _webServerSettings._pMimeTypes : _mimeTypesStr,
            .page404 = _webServerSettings._p404PageSource,
            .fs = NULL,
        };
        mg_http_serve_dir(pConn, hm, &opts);

#ifdef DEBUG_STATIC_FILE_HANDLER
        // Debug
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
        struct mg_http_message tmp = {0};
#pragma GCC diagnostic pop
        mg_http_parse((char *) pConn->send.buf, c->send.len, &tmp);
        struct mg_str unknown = mg_str_n("?", 1), *cl;
        cl = mg_http_get_header(&tmp, "Content-Length");
        if (cl == NULL) cl = &unknown;
        MG_INFO(("%.*s %.*s %.*s %.*s", (int) hm->method.len, hm->method.ptr,
                (int) hm->uri.len, hm->uri.ptr, (int) tmp.uri.len, tmp.uri.ptr,
                (int) cl->len, cl->ptr));
#endif
        return true;
    }
    return false;
}

#endif

