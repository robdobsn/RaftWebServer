/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebResponderFile.h"
#include "FileSystemWorker.h"
#include "Logger.h"
#include "FileSystemChunker.h"
#include "RaftWebRequestHeader.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

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
    _pReadState = std::make_shared<ReadState>();
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
    _connStatus = CONN_INACTIVE;
    if (gzipValid)
    {
        String gzipFilePath = filePath + ".gz";
        bool isActive = _pReadState->fileChunker.start(gzipFilePath, maxSendSize, false, false, true, false);
        if (isActive)
        {
            _connStatus = CONN_ACTIVE;
            addHeader("Content-Encoding", "gzip");
#ifdef DEBUG_RESPONDER_FILE
            LOG_I(MODULE_PREFIX, "constructor connId %d filePath %s",
                    _reqParams.connId, gzipFilePath.c_str());
#endif
        }
    }

    // Fallback to unzipped file if necessary
    if (_connStatus == CONN_INACTIVE)
    {
        bool isActive = _pReadState->fileChunker.start(filePath, maxSendSize, false, false, true, false);
        if (isActive)
        {
            _connStatus = CONN_ACTIVE;
#ifdef DEBUG_RESPONDER_FILE
            LOG_I(MODULE_PREFIX, "constructor connId %d filePath %s",
                    _reqParams.connId, filePath.c_str());
#endif
        }
    }

#ifdef WARN_RESPONDER_FILE
    if (_connStatus == CONN_INACTIVE)
    {
        LOG_W(MODULE_PREFIX, "constructor connId %d failed to start filepath %s",
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
    LOG_I(MODULE_PREFIX, "startResponding connId %d connStatus=%d filePath %s",
                _reqParams.connId, _connStatus, _filePath.c_str());
#endif
    _fileSendStartMs = millis();
    return _connStatus != CONN_INACTIVE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get response next
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebResponderFile::getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen)
{
#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
    uint64_t debugTotalStartUs = micros();
    uint64_t debugStartUs = micros();
#endif

    // Limit the chunk size - each chunk is read in one go (on the file system worker) and the chunk buffers and any
    // unsent data they leave queued are held per connection
    if (bufMaxLen > RAFT_WEB_FILE_CHUNK_MAX_BYTES)
        bufMaxLen = RAFT_WEB_FILE_CHUNK_MAX_BYTES;

    // A chunk read on the file system worker: send it, and read the next one
    ReadState& readState = *_pReadState;
    if (!readState.chunkReady.load())
    {
        // Nothing to send until the read in progress (or one started now) is done
        if (!readState.readInProgress.load() && !startRead(bufMaxLen))
            _connStatus = CONN_INACTIVE;
        return 0;
    }
    readState.chunkReady = false;
    if (!readState.readOk)
    {
        _connStatus = CONN_INACTIVE;
        _lastChunkData.clear();
        LOG_W(MODULE_PREFIX, "getResponseNext connId %d failed filePath %s", _reqParams.connId, _filePath.c_str());
        return 0;
    }
    _lastChunkData.swap(readState.chunkData);
    uint32_t readLen = readState.readLen;
    _isFinalChunk = readState.isFinalChunk;

#ifdef DEBUG_RESPONDER_FILE_CONTENTS
    LOG_I(MODULE_PREFIX, "getResponseNext connId %d newChunk len %d connStatus %d isFinalChunk %d filePath %s", 
                _reqParams.connId, readLen, _connStatus, _isFinalChunk, _filePath.c_str());
#endif
    pBuf = _lastChunkData.data();

    // Check if done, otherwise read ahead
    if (_isFinalChunk)
    {
        _connStatus = CONN_INACTIVE;
#ifdef DEBUG_RESPONDER_FILE_START_END
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d endOfFile sent final chunk ok filePath %s",
                _reqParams.connId, _filePath.c_str());
#endif
    }
    else if (!startRead(bufMaxLen))
    {
        // The next chunk can't be read (no memory) - this one is sent and the response ends early
        _connStatus = CONN_INACTIVE;
    }

#ifdef DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS
    uint32_t debugChunkHandleUs = micros() - debugStartUs;
    uint32_t debugTotalUs = micros() - debugTotalStartUs;
    if (debugTotalUs > DEBUG_RESPONDER_FILE_PERFORMANCE_THRESH_MS * 1000)
    {
        LOG_I(MODULE_PREFIX, "getResponseNext connId %d timing total %duS handleChunk %duS",
                _reqParams.connId, (int)debugTotalUs, (int)debugChunkHandleUs);
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
    return _pReadState->fileChunker.getFileLen();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Leave connection open
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderFile::leaveConnOpen()
{
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read the next chunk on the file system worker (or here if the worker can't take the job)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderFile::startRead(uint32_t readLen)
{
    // Limit the chunk to what can be allocated (a failed allocation aborts) leaving a margin for the network stack.
    // No read is in progress, so the read buffer isn't in use
    std::shared_ptr<ReadState> pReadState = _pReadState;
    if (readLen > pReadState->chunkData.capacity())
    {
        uint32_t largestBlock = SpiramAwareAllocator<uint8_t>::max_allocatable();
        uint32_t allocatable = largestBlock > CHUNK_HEAP_MARGIN_BYTES ? largestBlock - CHUNK_HEAP_MARGIN_BYTES : 0;
        if (readLen > allocatable)
            readLen = allocatable > pReadState->chunkData.capacity() ? allocatable : pReadState->chunkData.capacity();
        if (readLen < CHUNK_MIN_BYTES)
        {
            LOG_W(MODULE_PREFIX, "startRead connId %d insufficient memory (largest block %d) filePath %s", 
                        _reqParams.connId, (int)largestBlock, _filePath.c_str());
            return false;
        }
    }

    // Read on the file system worker (or here if the worker can't take the job)
    pReadState->readInProgress = true;
    auto readJob = [pReadState, readLen]() {
        pReadState->chunkData.resize(readLen);
        uint32_t chunkLen = 0;
        bool isFinalChunk = false;
        pReadState->readOk = pReadState->fileChunker.nextRead(pReadState->chunkData.data(), readLen, chunkLen, isFinalChunk);
        pReadState->chunkData.resize(chunkLen);
        pReadState->readLen = chunkLen;
        pReadState->isFinalChunk = isFinalChunk;
        pReadState->chunkReady = true;
        pReadState->readInProgress = false;
    };
    if (!fileSystemWorker.submit(readJob))
        readJob();
    return true;
}
