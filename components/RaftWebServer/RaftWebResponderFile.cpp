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
// Handle inbound data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderFile::handleInboundData(const uint8_t* pBuf, uint32_t dataLen)
{
#ifdef DEBUG_RESPONDER_FILE
    LOG_I(MODULE_PREFIX, "handleInboundData connId %d len %d filePath %s", 
                _reqParams.connId, dataLen, _filePath.c_str());
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start responding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Get response next
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebResponderFile::getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen)
{
#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
    uint32_t debugGetRespNextStartMs = millis();
    uint32_t debugStartMs = millis();
    uint32_t debugChunkDataMs = 0;
    uint32_t debugNextReadMs = 0;
    uint32_t debugChunkHandleMs = 0;
#endif

    uint32_t readLen = 0;
    _lastChunkData.resize(bufMaxLen);
#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
    debugChunkDataMs = millis() - debugGetRespNextStartMs;
    debugStartMs = millis();
#endif
    if (!_fileChunker.nextRead(_lastChunkData.data(), bufMaxLen, readLen, _isFinalChunk))
    {
#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
        debugNextReadMs = millis() - debugStartMs;
        debugStartMs = millis();
#endif
        _isActive = false;
        _lastChunkData.clear();
        LOG_W(MODULE_PREFIX, "getResponseNext connId %d failed filePath %s", _reqParams.connId, _filePath.c_str());
        return 0;
    }
    
#ifdef DEBUG_RESPONDER_FILE_CONTENTS
    LOG_I(MODULE_PREFIX, "getResponseNext connId %d newChunk len %d isActive %d isFinalChunk %d filePos %d filePath %s", 
                _reqParams.connId, readLen, _isActive, _isFinalChunk, _fileChunker.getFilePos(), _filePath.c_str());
#endif
    _lastChunkData.resize(readLen);
    pBuf = _lastChunkData.data();

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
    debugChunkHandleMs = millis() - debugStartMs;
    if (millis() - debugGetRespNextStartMs > DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS)
    {
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d timing initClr %dms getNext %dms handleChunk %dms",
                _reqParams.connId, debugChunkDataMs, debugNextReadMs, debugChunkHandleMs);
    }
#endif

    return readLen;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get content type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Get content length (or -1 if not known)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int RaftWebResponderFile::getContentLength()
{
    return _fileChunker.getFileLen();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Leave connection open
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderFile::leaveConnOpen()
{
    return false;
}
