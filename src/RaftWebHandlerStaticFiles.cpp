/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ESP8266

#include "RaftWebHandlerStaticFiles.h"
#include "RaftWebConnection.h"
#include "RaftWebResponder.h"
#include "RaftWebResponderFile.h"
#include <Logger.h>
#include <FileSystem.h>

// #define DEBUG_STATIC_FILE_HANDLER

#ifdef DEBUG_STATIC_FILE_HANDLER
static const char* MODULE_PREFIX = "RaftWebHStatFile";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebHandlerStaticFiles::RaftWebHandlerStaticFiles(const char* pBaseURI, 
                const char* pBaseFolder, const char* pCacheControl,
                const char* pDefaultPath)
{
    // Default path (for response to /)
    _defaultPath = pDefaultPath;

    // Handle URI and base folder
    if (pBaseURI)
        _baseURI = pBaseURI;
    if (pBaseFolder)
        _baseFolder = pBaseFolder;
    if (pCacheControl)
        _cacheControl = pCacheControl;

    // Ensure paths and URLs have a leading /
    if (_baseURI.length() == 0 || _baseURI[0] != '/')
        _baseURI = "/" + _baseURI;
    if (_baseFolder.length() == 0 || _baseFolder[0] != '/')
        _baseFolder = "/" + _baseFolder;

    // Check if baseFolder is actually a folder
    _isBaseReallyAFolder = _baseFolder.endsWith("/");

    // Remove trailing /
    if (_baseURI.endsWith("/"))
        _baseURI.remove(_baseURI.length()-1); 
    if (_baseFolder.endsWith("/"))
        _baseFolder.remove(_baseFolder.length()-1); 
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

const char* RaftWebHandlerStaticFiles::getName()
{
    return "HandlerStaticFiles";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get a responder if we can handle this request
// NOTE: this returns a new object or NULL
// NOTE: if a new object is returned the caller is responsible for deleting it when appropriate
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebResponder* RaftWebHandlerStaticFiles::getNewResponder(const RaftWebRequestHeader& requestHeader, 
            const RaftWebRequestParams& params, const RaftWebServerSettings& webServerSettings,
            RaftHttpStatusCode &statusCode)
{
    // Debug
#ifdef DEBUG_STATIC_FILE_HANDLER
    uint64_t getResponderStartUs = micros();
    LOG_I(MODULE_PREFIX, "getNewResponder reqURL %s baseURI %s", requestHeader.URL.c_str(), _baseURI.c_str());    
#endif

    // Must be a GET
    if (requestHeader.extract.method != WEB_METHOD_GET)
        return NULL;

    // Check the URL is valid
    if (!requestHeader.URL.startsWith(_baseURI))
        return NULL;

    // Check that the connection type is HTTP
    if (requestHeader.reqConnType != REQ_CONN_TYPE_HTTP)
        return NULL;

    // Check if the path is just root
    String filePath = getFilePath(requestHeader, requestHeader.URL.equals("/"));

    // Create responder
    RaftWebResponder* pResponder = new RaftWebResponderFile(filePath, this, params, requestHeader);

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
// Get file path
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftWebHandlerStaticFiles::getFilePath(const RaftWebRequestHeader& header, bool defaultPath)
{
    // Remove the base path from the URL
    String filePath;
    if (!defaultPath)
        filePath = header.URL.substring(_baseURI.length());
    else
        filePath = "/" + _defaultPath;

    // Add on the file path
    return _baseFolder + filePath;
}

#endif
