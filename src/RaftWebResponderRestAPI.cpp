/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebResponderRestAPI.h"
#include <Logger.h>
#include <FileStreamBlock.h>
#include <APISourceInfo.h>

// #define DEBUG_RESPONDER_REST_API
// #define DEBUG_RESPONDER_REST_API_NON_MULTIPART_DATA
// #define DEBUG_RESPONDER_REST_API_MULTIPART_DATA
// #define DEBUG_MULTIPART_EVENTS
// #define DEBUG_MULTIPART_HEADERS
// #define DEBUG_MULTIPART_DATA
// #define DEBUG_RESPONDER_API_START_END

#if defined(DEBUG_RESPONDER_REST_API) || defined(DEBUG_RESPONDER_REST_API_NON_MULTIPART_DATA) || defined(DEBUG_RESPONDER_REST_API_MULTIPART_DATA) || defined(DEBUG_MULTIPART_EVENTS) || defined(DEBUG_RESPONDER_REST_API) || defined(DEBUG_MULTIPART_DATA) || defined(DEBUG_RESPONDER_API_START_END)
static const char *MODULE_PREFIX = "RaftWebRespREST";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebResponderRestAPI::RaftWebResponderRestAPI(const RaftWebServerRestEndpoint& endpoint, RaftWebHandler* pWebHandler, 
                    const RaftWebRequestParams& params, String& reqStr, 
                    const RaftWebRequestHeaderExtract& headerExtract,
                    uint32_t channelID)
    : _reqParams(params), _apiSourceInfo(channelID)
{
    _endpoint = endpoint;
    _pWebHandler = pWebHandler;
    _endpointCalled = false;
    _requestStr = reqStr;
    _headerExtract = headerExtract;
    _respStrPos = 0;
    _sendStartMs = millis();
#ifdef APPLY_MIN_GAP_BETWEEN_API_CALLS_MS    
    _lastFileReqMs = 0;
#endif

    // Hook up callbacks
    _multipartParser.onEvent = std::bind(&RaftWebResponderRestAPI::multipartOnEvent, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _multipartParser.onData = std::bind(&RaftWebResponderRestAPI::multipartOnData, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, 
            std::placeholders::_5);
    _multipartParser.onHeaderNameValue = std::bind(&RaftWebResponderRestAPI::multipartOnHeaderNameValue, this, 
            std::placeholders::_1, std::placeholders::_2);

    // Check if multipart
    if (_headerExtract.isMultipart)
    {
        _multipartParser.setBoundary(_headerExtract.multipartBoundary);
    }

#ifdef DEBUG_RESPONDER_REST_API
    LOG_I(MODULE_PREFIX, "constr new responder %d reqStr %s", (uint32_t)this, 
                    reqStr.c_str());
#endif
}

RaftWebResponderRestAPI::~RaftWebResponderRestAPI()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle inbound data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderRestAPI::handleData(const uint8_t* pBuf, uint32_t dataLen)
{
    // Record data received so we know when to respond
    uint32_t curBufPos = _numBytesReceived;
    _numBytesReceived += dataLen;

    // Handle data which may be multipart
    if (_headerExtract.isMultipart)
    {
#ifdef DEBUG_RESPONDER_REST_API_MULTIPART_DATA
        LOG_I(MODULE_PREFIX, "handleData multipart len %d", dataLen);
#endif
        _multipartParser.handleData(pBuf, dataLen);
#ifdef DEBUG_RESPONDER_REST_API_MULTIPART_DATA
        LOG_I(MODULE_PREFIX, "handleData multipart finished bytesRx %d contentLen %d", 
                    _numBytesReceived, _headerExtract.contentLength);
#endif
    }
    else
    {
#ifdef DEBUG_RESPONDER_REST_API_NON_MULTIPART_DATA
        LOG_I(MODULE_PREFIX, "handleData curPos %d bufLen %d totalLen %d", curBufPos, dataLen, _headerExtract.contentLength);
#endif
        // Send as the body
        if (_endpoint.restApiFnBody)
            _endpoint.restApiFnBody(_requestStr, pBuf, dataLen, curBufPos, _headerExtract.contentLength, _apiSourceInfo);
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start responding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderRestAPI::startResponding(RaftWebConnection& request)
{
    _isActive = true;
    _endpointCalled = false;
    _numBytesReceived = 0;
    _respStrPos = 0;
    _sendStartMs = millis();

#ifdef DEBUG_RESPONDER_REST_API
    LOG_I(MODULE_PREFIX, "startResponding isActive %d", _isActive);
#endif
    return _isActive;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get response next
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebResponderRestAPI::getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen)
{
    // Check if all data received
    if (_numBytesReceived != _headerExtract.contentLength)
    {
#ifdef DEBUG_RESPONDER_REST_API
        LOG_I(MODULE_PREFIX, "getResponseNext not all data rx numRx %d contentLen %d", 
                    _numBytesReceived, _headerExtract.contentLength);
#endif
        return 0;
    }

#ifdef DEBUG_RESPONDER_REST_API
    LOG_I(MODULE_PREFIX, "getResponseNext maxRespLen %d endpointCalled %d isActive %d", 
                    bufMaxLen, _endpointCalled, _isActive);
#endif

    // Check if we need to call API
    uint32_t respLen = 0;
    if (!_endpointCalled)
    {
        // Call endpoint
        if (_endpoint.restApiFn)
            _endpoint.restApiFn(_requestStr, _respStr, _apiSourceInfo);

        // Endpoint done
        _endpointCalled = true;
    }

    // Check how much of buffer to send
    uint32_t respRemain = _respStr.length() - _respStrPos;
    respLen = bufMaxLen > respRemain ? respRemain : bufMaxLen;

    // Prep buffer
    pBuf = (uint8_t*) (_respStr.c_str() + _respStrPos);

#ifdef DEBUG_RESPONDER_API_START_END
    LOG_I(MODULE_PREFIX, "getResponseNext API totalLen %d sending %d fromPos %d URL %s",
                _respStr.length(), respLen, _respStrPos, _requestStr.c_str());
#endif

    // Update position
    _respStrPos += respLen;
    if (_respStrPos >= _respStr.length())
    {
        _isActive = false;
#ifdef DEBUG_RESPONDER_API_START_END
        LOG_I(MODULE_PREFIX, "getResponseNext endOfFile sent final chunk ok");
#endif
    }

#ifdef DEBUG_RESPONDER_REST_API
    LOG_I(MODULE_PREFIX, "getResponseNext respLen %d isActive %d", respLen, _isActive);
#endif
    return respLen;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get content type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* RaftWebResponderRestAPI::getContentType()
{
    return "text/json";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Leave connection open
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderRestAPI::leaveConnOpen()
{
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if ready for data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderRestAPI::readyForData()
{
#ifdef APPLY_MIN_GAP_BETWEEN_API_CALLS_MS
    if (!Raft::isTimeout(millis(), _lastFileReqMs, APPLY_MIN_GAP_BETWEEN_API_CALLS_MS))
        return false;
    _lastFileReqMs = millis();
    LOG_I(MODULE_PREFIX, "readyForData time %d", _lastFileReqMs);
#endif

    // Check if endpoint specifies a ready function
    if (_endpoint.restApiFnIsReady)
        return _endpoint.restApiFnIsReady(_apiSourceInfo);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks on multipart parser
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebResponderRestAPI::multipartOnEvent(RaftMultipartEvent event, const uint8_t *pBuf, uint32_t pos)
{
#ifdef DEBUG_MULTIPART_EVENTS
    LOG_W(MODULE_PREFIX, "multipartEvent event %s (%d) pos %d", RaftWebMultipart::getEventText(event), event, pos);
#endif
}

void RaftWebResponderRestAPI::multipartOnData(const uint8_t *pBuf, uint32_t bufLen, RaftMultipartForm& formInfo, 
                    uint32_t contentPos, bool isFinalPart)
{
#ifdef DEBUG_MULTIPART_DATA
    LOG_W(MODULE_PREFIX, "multipartData len %d filename %s contentPos %d isFinal %d", 
                bufLen, formInfo._fileName.c_str(), contentPos, isFinalPart);
#endif
    // Upload info
    FileStreamBlock fileStreamBlock(formInfo._fileName.c_str(), 
                    _headerExtract.contentLength, contentPos, 
                    pBuf, bufLen, isFinalPart, formInfo._crc16, formInfo._crc16Valid,
                    formInfo._fileLenBytes, formInfo._fileLenValid, contentPos==0);
    // Check for callback
    if (_endpoint.restApiFnChunk)
        _endpoint.restApiFnChunk(_requestStr, fileStreamBlock, _apiSourceInfo);
}

void RaftWebResponderRestAPI::multipartOnHeaderNameValue(const String& name, const String& val)
{
#ifdef DEBUG_MULTIPART_HEADERS
    LOG_W(MODULE_PREFIX, "multipartHeaderNameValue %s = %s", name.c_str(), val.c_str());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get content length (or -1 if not known)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int RaftWebResponderRestAPI::getContentLength()
{
    if (!_endpointCalled)
    {
        // Call endpoint
        if (_endpoint.restApiFn)
            _endpoint.restApiFn(_requestStr, _respStr, _apiSourceInfo);

        // Endpoint done
        _endpointCalled = true;
    }
    return _respStr.length();
}
