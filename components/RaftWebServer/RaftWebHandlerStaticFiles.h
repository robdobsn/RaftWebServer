/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef ESP8266

#include "RaftWebHandler.h"
#include <Logger.h>

class RaftWebRequest;
class RaftWebRequestHeader;

class RaftWebHandlerStaticFiles : public RaftWebHandler
{
public:
    RaftWebHandlerStaticFiles(const char* pBaseURI, const char* pBaseFolder, 
            const char* pCacheControl, const char* pDefaultPath);
    virtual ~RaftWebHandlerStaticFiles();
    virtual const char* getName() const override;
    virtual String getBaseURL() const override
    {
        return _baseURI;
    }
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, 
                RaftHttpStatusCode &statusCode) override final;
    virtual bool isFileHandler() const override final
    {
        return true;
    }

private:
    // URI
    String _baseURI;

    // Folder
    String _baseFolder;
    bool _isBaseReallyAFolder;

    // Default path (for response to /)
    String _defaultPath;

    // Cache
    String _cacheControl;
    String _lastModifiedTimeStr;

    // GZip
    bool _gzipFirst;
    bool _gzipStats;

    // Helpers
    String getFilePath(const RaftWebRequestHeader& header, bool defaultPath) const;

};

#endif
