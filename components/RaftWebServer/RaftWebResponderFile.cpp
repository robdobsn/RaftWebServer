/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebResponderFile.h"
#include "Logger.h"
#include "FileSystemChunker.h"
#include "RaftWebRequestHeader.h"

static const char *MODULE_PREFIX = "RaftWebRespFile";

// Warn
#define WARN_RESPONDER_FILE

// Debug
// #define DEBUG_RESPONDER_FILE
// #define DEBUG_RESPONDER_FILE_CONTENTS
// #define DEBUG_RESPONDER_FILE_START_END
// #define DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS 0

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebResponderFile::RaftWebResponderFile(const String& filePath, RaftWebHandler* pWebHandler, 
                const RaftWebRequestParams& params, const RaftWebRequestHeader& requestHeader,
                uint32_t maxSendSize)
    : _reqParams(params)
{
    _filePath = filePath;
    _pWebHandler = pWebHandler;
    _fileSendStartMs = millis();
 
    // Check if gzip is valid
    bool gzipValid = false;
    for (const RaftJson::NameValuePair& hdr : requestHeader.nameValues)
    {
        // LOG_I(MODULE_PREFIX, "constr hdr %s : %s", hdr.name.c_str(), hdr.value.c_str());
        if (hdr.name.equalsIgnoreCase("Accept-Encoding") && (hdr.value.indexOf("gzip") >= 0))
        {
            gzipValid = true;
            break;
        }
    }

    // If gzip valid try that first
    _isActive = false;
    if (gzipValid)
    {
        String gzipFilePath = filePath + ".gz";
        _isActive = _fileChunker.start(gzipFilePath, maxSendSize, false, false, true, false);
        if (_isActive)
        {
            addHeader("Content-Encoding", "gzip");
#ifdef DEBUG_RESPONDER_FILE
            LOG_I(MODULE_PREFIX, "constructor connId %d filePath %s",
                    _reqParams.connId, gzipFilePath.c_str());
#endif
        }
    }

    // Fallback to unzipped file if necessary
    if (!_isActive)
    {
        _isActive = _fileChunker.start(filePath, maxSendSize, false, false, true, false);
#ifdef DEBUG_RESPONDER_FILE
        if (_isActive)
        {
            LOG_I(MODULE_PREFIX, "constructor connId %d filePath %s",
                    _reqParams.connId, filePath.c_str());
        }
#endif
    }

#ifdef WARN_RESPONDER_FILE
    if (!_isActive)
    {
        LOG_E(MODULE_PREFIX, "constructor connId %d failed to start filepath %s",
                    _reqParams.connId, filePath.c_str());
    }
#endif

}

RaftWebResponderFile::~RaftWebResponderFile()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle inbound data
/// @param data Data
bool RaftWebResponderFile::handleInboundData(const SpiramAwareUint8Vector& data)
{
#ifdef DEBUG_RESPONDER_FILE
    LOG_I(MODULE_PREFIX, "handleInboundData connId %d len %d filePath %s", 
                _reqParams.connId, data.size(), _filePath.c_str());
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Start responding
/// @param request Request
/// @return true if responding
bool RaftWebResponderFile::startResponding(RaftWebConnection& request)
{
#ifdef DEBUG_RESPONDER_FILE_START_END
    LOG_I(MODULE_PREFIX, "startResponding connId %d isActive %d filePath %s",
                _reqParams.connId, _isActive, _filePath.c_str());
#endif
    _fileSendStartMs = millis();
    return _isActive;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get next response data
/// @param maxLen Maximum length to return
/// @return Response data
SpiramAwareUint8Vector RaftWebResponderFile::getResponseNext(uint32_t maxLen)
{
#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
    uint64_t debugTotalStartUs = micros();
    uint64_t debugStartUs = micros();
#endif

    uint32_t readLen = 0;
#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
    uint32_t debugChunkDataUs = micros() - debugStartUs;
    debugStartUs = micros();
#endif
    SpiramAwareUint8Vector nextData = _fileChunker.nextRead(maxLen, _isFinalChunk);
    if (nextData.size() == 0)
    {
        _isActive = false;
        LOG_W(MODULE_PREFIX, "getResponseNext connId %d failed filePath %s", _reqParams.connId, _filePath.c_str());
        return nextData;
    }

#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
    uint32_t debugNextReadUs = micros() - debugStartUs;
    debugStartUs = micros();
#endif

#ifdef DEBUG_RESPONDER_FILE_CONTENTS
    LOG_I(MODULE_PREFIX, "getResponseNext connId %d newChunk len %d isActive %d isFinalChunk %d filePos %d filePath %s", 
                _reqParams.connId, nextData.size(), _isActive, _isFinalChunk, _fileChunker.getFilePos(), _filePath.c_str());
#endif

    // Check if done
    if (_isFinalChunk)
    {
        _isActive = false;
#ifdef DEBUG_RESPONDER_FILE_START_END
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d endOfFile sent final chunk ok filePath %s",
                _reqParams.connId, _filePath.c_str());
#endif
    }

#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
    uint32_t debugChunkHandleUs = micros() - debugStartUs;
    uint32_t debugTotalUs = micros() - debugTotalStartUs;
    if (debugTotalUs > DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS * 1000)
    {
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d timing total %duS initClr %duS getNext %duS handleChunk %duS",
                _reqParams.connId, (int)debugTotalUs, (int)debugChunkDataUs, (int)debugNextReadUs, (int)debugChunkHandleUs);
    }
#endif

    return nextData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get content type string
/// @return Content type
const char* RaftWebResponderFile::getContentType()
{
    if (_filePath.endsWith(".html"))
        return "text/html";
    else if (_filePath.endsWith(".htm"))
        return "text/html";
    else if (_filePath.endsWith(".css"))
        return "text/css";
    else if (_filePath.endsWith(".json"))
        return "text/json";
    else if (_filePath.endsWith(".js"))
        return "application/javascript";
    else if (_filePath.endsWith(".png"))
        return "image/png";
    else if (_filePath.endsWith(".gif"))
        return "image/gif";
    else if (_filePath.endsWith(".jpg"))
        return "image/jpeg";
    else if (_filePath.endsWith(".ico"))
        return "image/x-icon";
    else if (_filePath.endsWith(".svg"))
        return "image/svg+xml";
    else if (_filePath.endsWith(".eot"))
        return "font/eot";
    else if (_filePath.endsWith(".woff"))
        return "font/woff";
    else if (_filePath.endsWith(".woff2"))
        return "font/woff2";
    else if (_filePath.endsWith(".ttf"))
        return "font/ttf";
    else if (_filePath.endsWith(".xml"))
        return "text/xml";
    else if (_filePath.endsWith(".pdf"))
        return "application/pdf";
    else if (_filePath.endsWith(".zip"))
        return "application/zip";
    else if (_filePath.endsWith(".gz"))
        return "application/x-gzip";
    return "text/plain";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get content length
/// @return Content length (or -1 if not known)
int RaftWebResponderFile::getContentLength()
{
    return _fileChunker.getFileLen();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Leave connection open
bool RaftWebResponderFile::leaveConnOpen()
{
    return false;
}
