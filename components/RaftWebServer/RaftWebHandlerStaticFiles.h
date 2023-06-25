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

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
#include <mongoose.h>
#endif

class RaftWebRequestHeader;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Web handler for static files
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class RaftWebHandlerStaticFiles : public RaftWebHandler
{
public:
    /// @brief Constructor of static files handler
    /// @param pServePaths (comma separated and can include uri=path pairs separated by =)
    /// @param pCacheControl (eg "no-cache, no-store, must-revalidate")
    RaftWebHandlerStaticFiles(const char* pServePaths, const char* pCacheControl);
    virtual ~RaftWebHandlerStaticFiles();
    virtual const char* getName() const override;
#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, 
                RaftHttpStatusCode &statusCode) override final;
#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)
    virtual bool handleRequest(struct mg_connection *pConn, int ev, void *ev_data) override final;
#endif
    virtual bool isFileHandler() const override final
    {
        return true;
    }


private:
    // Served uri/folder pairs (comma separated and can include uri=path pairs separated by =)
    // If a uri is not specified then "/" is used
    String _servePaths;

    // Cache
    String _cacheControl;

#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)

    // Cache
    String _lastModifiedTimeStr;

    // GZip
    bool _gzipFirst;
    bool _gzipStats;

    // Served path pairs
    std::vector<RdJson::NameValuePair> _servedPathPairs;

    // Helpers
    String getContentType(const String& filePath) const;

#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

    // Extra headers as a string
    String _extraHeadersStr;

#endif

    // MIME types for files
    static constexpr const char* _mimeTypesStr =
        R"(.html=text/html,)"
        R"(.htm=text/html,)"
        R"(.css=text/css,)"
        R"(.json=text/json,)"
        R"(.js=application/javascript,)"
        R"(.png=image/png,)"
        R"(.gif=image/gif,)"
        R"(.jpg=image/jpeg,)"
        R"(.ico=image/x-icon,)"
        R"(.svg=image/svg+xml,)"
        R"(.eot=font/eot,)"
        R"(.woff=font/woff,)"
        R"(.woff2=font/woff2,)"
        R"(.ttf=font/ttf,)"
        R"(.otf=font/otf,)"
        R"(.wasm=application/wasm,)"
        R"(.map=application/json,)"
        R"(.txt=text/plain,)"
        R"(.xml=text/xml,)"
        R"(.pdf=application/pdf,)"
        R"(.zip=application/zip,)"
        R"(.gz=application/gzip,)"
        R"(.mp3=audio/mpeg,)"
        R"(.wav=audio/wav,)"
        R"(.mp4=video/mp4,)"
        R"(.avi=video/x-msvideo,)"
        R"(.mpeg=video/mpeg,)"
        R"(.mpg=video/mpeg,)"
        R"(.mov=video/quicktime,)"
        R"(.flv=video/x-flv,)"
        R"(.wmv=video/x-ms-wmv,)"
        R"(.webm=video/webm,)"
        R"(.mkv=video/x-matroska,)"
        R"(.m4v=video/x-m4v,)"
        R"(.m3u8=application/x-mpegURL,)"
        R"(.ts=video/MP2T,)"
        R"(.m3u=audio/x-mpegURL,)"
        R"(.aac=audio/aac,)"
        R"(.m4a=audio/x-m4a,)"
        R"(.flac=audio/flac,)"
        ;
};
