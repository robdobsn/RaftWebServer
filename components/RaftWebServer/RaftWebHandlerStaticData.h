/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebHandler.h"
#include <Logger.h>
#include <RaftWebRequestHeader.h>
#include <RaftWebResponderData.h>

// #define DEBUG_STATIC_DATA_HANDLER

class RaftWebHandlerStaticData : public RaftWebHandler
{
public:
    // Note that the pointer passed-in is owned externally - so must stay valid for lifetime of server
    RaftWebHandlerStaticData(const char* pBaseURI, const uint8_t* pData, 
                    uint32_t dataLen, const char* pMIMEType,
                    const char* pDefaultPath)
    {
        // Default path (for response to /)
        _defaultPath = pDefaultPath;
        if (!_defaultPath.startsWith("/"))
            _defaultPath = "/" + _defaultPath;

        // Handle URI and base folder
        if (pBaseURI)
            _baseURI = pBaseURI;
        if (pData)
            _pData = pData;
        _dataLen = dataLen;
        _mimeType = pMIMEType;

        // Ensure paths and URLs have a leading /
        if (_baseURI.length() == 0 || _baseURI[0] != '/')
            _baseURI = "/" + _baseURI;

        // Remove trailing / unless only /
        if (_baseURI.endsWith("/"))
            _baseURI.remove(_baseURI.length()-1);
    }
    virtual ~RaftWebHandlerStaticData()
    {
    }
    virtual const char* getName() const override
    {
        return "HandlerStaticData";
    }
    virtual String getBaseURL() const override
    {
        return _baseURI;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Get a responder if we can handle this request
    // NOTE: this returns a new object or NULL
    // NOTE: if a new object is returned the caller is responsible for deleting it when appropriate
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, 
                RaftHttpStatusCode &statusCode) override final
    {
        // Debug
#ifdef DEBUG_STATIC_DATA_HANDLER
        LOG_I("WebStatData", "getNewResponder reqURL %s baseURI %s", 
                    requestHeader.URL.c_str(), _baseURI.c_str());
#endif

        // Must be a GET
        if (requestHeader.extract.method != WEB_METHOD_GET)
            return NULL;

        // Check the URL is valid
        if (!(requestHeader.URL.equals(_baseURI) || requestHeader.URL.startsWith(_baseURI + "/") || 
                ((requestHeader.URL.equals("/")) && _baseURI.equalsIgnoreCase(_defaultPath))))
        {
#ifdef DEBUG_STATIC_DATA_HANDLER
            LOG_W("WebStatData", "getNewResponder no-match uri %s base %s default %s", 
                    requestHeader.URL.c_str(), _baseURI.c_str(), _defaultPath.c_str());
#endif
            return NULL;
        }

        // Check that the connection type is HTTP
        if (requestHeader.reqConnType != REQ_CONN_TYPE_HTTP)
            return NULL;

        // Looks like we can handle this so create a new responder object
        RaftWebResponder* pResponder = new RaftWebResponderData(_pData, _dataLen, 
                    _mimeType.c_str(), this, params);

        // Debug
#ifdef DEBUG_STATIC_DATA_HANDLER
        LOG_W("WebStatData", "getNewResponder constructed new responder %lx uri %s base %s", 
                    (unsigned long)pResponder, requestHeader.URL.c_str(), _baseURI.c_str());
#endif

        // Return new responder - caller must clean up by deleting object when no longer needed
        statusCode = HTTP_STATUS_OK;
        return pResponder;

    }
#endif
    virtual bool isFileHandler() const override final
    {
        return true;
    }
private:
    // URI
    String _baseURI;

    // Default path (for response to /)
    String _defaultPath;

    // Type
    String _mimeType;

    // Data pointer
    const uint8_t* _pData;
    uint32_t _dataLen;
};
