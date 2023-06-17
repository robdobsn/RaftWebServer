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

// TODO
#include <esp_http_server.h>

// #define DEBUG_STATIC_FILE_HANDLER

#if defined(FEATURE_WEB_SERVER_USE_ESP_IDF)
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

const char* RaftWebHandlerStaticFiles::getName() const
{
    return "HandlerStaticFiles";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle request (ESP IDF)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(FEATURE_WEB_SERVER_USE_ESP_IDF)

esp_err_t RaftWebHandlerStaticFiles::handleRequest(httpd_req_t *req)
{
    String filePath = getFilePath(req->uri);

    /* Concatenate the requested file path */
    struct stat file_stat;
    if (stat(filePath.c_str(), &file_stat) == -1) {
        LOG_E(MODULE_PREFIX, "Failed to stat file : %s", filePath.c_str());
        /* If file doesn't exist respond with 404 Not Found */
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    FILE *fd = fopen(filePath.c_str(), "r");
    if (!fd) {
        LOG_E(MODULE_PREFIX, "Failed to read existing file : %s", filePath.c_str());
        /* If file exists but unable to open respond with 500 Server Error */
        httpd_resp_set_status(req, "500 Server Error");
        httpd_resp_sendstr(req, "Failed to read existing file!");
        return ESP_OK;
    }

#ifdef DEBUG_STATIC_FILE_HANDLER
    LOG_I(MODULE_PREFIX, "Sending file : %s (%ld bytes)...", filePath.c_str(), file_stat.st_size);
#endif

    // Set content type
    httpd_resp_set_type(req, getContentType(filePath));

    // Buffer
    std::vector<char, SpiramAwareAllocator<char>> dataBuffer;
    uint32_t chunkSize = getMaxResponseSize();
    dataBuffer.resize(chunkSize);
    uint32_t numBytesRead = 0;
    do 
    {
        // Read chunk
        numBytesRead = fread(dataBuffer.data(), 1, chunkSize, fd);

        // Send chunk
        if (httpd_resp_send_chunk(req, dataBuffer.data(), numBytesRead) != ESP_OK) 
        {
            fclose(fd);
            LOG_E(MODULE_PREFIX, "File sending failed!");
            // Abort sending file
            httpd_resp_sendstr_chunk(req, NULL);
            // Send error message with status code
            httpd_resp_set_status(req, "500 Server Error");
            httpd_resp_sendstr(req, "Failed to send file!");
            return ESP_OK;
        }

        /* Keep looping till the whole file is sent */
    } while (numBytesRead != 0);

    /* Close file after sending complete */
    fclose(fd);

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

#endif

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
    String filePath = getFilePath(requestHeader.URL);

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

#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mongoose handlers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

bool RaftWebHandlerStaticFiles::canHandle(struct mg_connection *c, int ev, void *ev_data)
{
    // TODO
    return true;
}

void RaftWebHandlerStaticFiles::handle(struct mg_connection *c, int ev, void *ev_data)
{
    // TODO
    if (ev == MG_EV_HTTP_MSG) 
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        struct mg_http_message tmp = {0};
        struct mg_str unknown = mg_str_n("?", 1), *cl;
        struct mg_http_serve_opts opts = 
        {
            .root_dir = _baseFolder.c_str(),
            .ssi_pattern = NULL,
            .extra_headers = NULL,
            .mime_types = NULL,
            .page404 = NULL,
            .fs = NULL,
        };
        mg_http_serve_dir(c, hm, &opts);

        // Debug
        mg_http_parse((char *) c->send.buf, c->send.len, &tmp);
        cl = mg_http_get_header(&tmp, "Content-Length");
        if (cl == NULL) cl = &unknown;
        MG_INFO(("%.*s %.*s %.*s %.*s", (int) hm->method.len, hm->method.ptr,
                (int) hm->uri.len, hm->uri.ptr, (int) tmp.uri.len, tmp.uri.ptr,
                (int) cl->len, cl->ptr));
    }
}

#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get file path
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftWebHandlerStaticFiles::getFilePath(const String& reqURL) const
{
    // Remove the base path from the URL
    String filePath;
    if (!reqURL.equals("/"))
        filePath = reqURL.substring(_baseURI.length());
    else
        filePath = "/" + _defaultPath;

    // Add on the file path
    return _baseFolder + filePath;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get content type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* RaftWebHandlerStaticFiles::getContentType(const String& filePath) const
{
    if (filePath.endsWith(".html"))
        return "text/html";
    else if (filePath.endsWith(".htm"))
        return "text/html";
    else if (filePath.endsWith(".css"))
        return "text/css";
    else if (filePath.endsWith(".json"))
        return "text/json";
    else if (filePath.endsWith(".js"))
        return "application/javascript";
    else if (filePath.endsWith(".png"))
        return "image/png";
    else if (filePath.endsWith(".gif"))
        return "image/gif";
    else if (filePath.endsWith(".jpg"))
        return "image/jpeg";
    else if (filePath.endsWith(".ico"))
        return "image/x-icon";
    else if (filePath.endsWith(".svg"))
        return "image/svg+xml";
    else if (filePath.endsWith(".eot"))
        return "font/eot";
    else if (filePath.endsWith(".woff"))
        return "font/woff";
    else if (filePath.endsWith(".woff2"))
        return "font/woff2";
    else if (filePath.endsWith(".ttf"))
        return "font/ttf";
    else if (filePath.endsWith(".xml"))
        return "text/xml";
    else if (filePath.endsWith(".pdf"))
        return "application/pdf";
    else if (filePath.endsWith(".zip"))
        return "application/zip";
    else if (filePath.endsWith(".gz"))
        return "application/x-gzip";
    return "text/plain";
}
