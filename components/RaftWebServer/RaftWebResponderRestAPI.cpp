/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebResponderRestAPI.h"
#include "Logger.h"
#include "FileStreamBlock.h"
#include "APISourceInfo.h"

// #define DEBUG_RESPONDER_REST_API
// #define DEBUG_RESPONDER_REST_API_NON_MULTIPART_DATA
// #define DEBUG_RESPONDER_REST_API_MULTIPART_DATA
// #define DEBUG_MULTIPART_EVENTS
// #define DEBUG_MULTIPART_HEADERS
// #define DEBUG_MULTIPART_DATA
// #define DEBUG_RESPONDER_API_START_END

// #define DEBUG_RESPONDER_REST_API_DEFERRED

// Responders which currently exist
std::vector<RaftWebResponderRestAPI*> RaftWebResponderRestAPI::_liveResponders;

#if defined(DEBUG_RESPONDER_REST_API_DEFERRED) || defined(DEBUG_RESPONDER_REST_API) || defined(DEBUG_RESPONDER_REST_API_NON_MULTIPART_DATA) || defined(DEBUG_RESPONDER_REST_API_MULTIPART_DATA) || defined(DEBUG_MULTIPART_EVENTS) || defined(DEBUG_RESPONDER_REST_API) || defined(DEBUG_MULTIPART_DATA) || defined(DEBUG_RESPONDER_API_START_END)
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
    _numBytesReceived = 0;
#ifdef APPLY_MIN_GAP_BETWEEN_API_CALLS_MS    
    _lastFileReqMs = 0;
#endif

    // Record as live
    _liveResponders.push_back(this);

    // Hook up callbacks
    _multipartParser.onEvent = std::bind(&RaftWebResponderRestAPI::multipartOnEvent, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);
    _multipartParser.onData = std::bind(&RaftWebResponderRestAPI::multipartOnData, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, 
            std::placeholders::_5, std::placeholders::_6);
    _multipartParser.onHeaderNameValue = std::bind(&RaftWebResponderRestAPI::multipartOnHeaderNameValue, this, 
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

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
    // No longer live - a deferred response completed after this point is discarded
    for (auto it = _liveResponders.begin(); it != _liveResponders.end(); ++it)
    {
        if (*it == this)
        {
            _liveResponders.erase(it);
            break;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service the responder
/// @note The endpoint is called here so that it can defer its response: GET requests before the response headers
///       are sent (the headers, which include the content length, are then sent when the response is complete),
///       other requests once the whole request body has been received (e.g. a file upload, whose reply waits until
///       the file is written)
void RaftWebResponderRestAPI::loop()
{
    if (_endpointCalled || (_connStatus != CONN_ACTIVE))
        return;
    if ((_headerExtract.method != WEB_METHOD_GET) && (_numBytesReceived != _headerExtract.contentLength))
        return;
    callEndpoint(true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Call the endpoint's main function
/// @param allowDefer true if the endpoint may defer its response
void RaftWebResponderRestAPI::callEndpoint(bool allowDefer)
{
    // Endpoint now called (whatever the outcome)
    _endpointCalled = true;

    // Deferral requires the hooks to the deferred response registry
    allowDefer = allowDefer && _endpoint.restApiDeferAllocToken && _endpoint.restApiDeferBind;
    if (allowDefer)
    {
        _apiSourceInfo.canDefer = true;
        _apiSourceInfo.deferToken = _endpoint.restApiDeferAllocToken();
    }

    // Call endpoint
    RaftRetCode retc = _endpoint.restApiFn ? _endpoint.restApiFn(_requestStr, _respStr, _apiSourceInfo) : RAFT_NOT_IMPLEMENTED;
    if (retc != RAFT_RESP_DEFERRED)
        return;

    // Response deferred - mark as pending before binding as binding delivers immediately if the endpoint has
    // already completed the request
    uint32_t deferToken = _apiSourceInfo.deferToken;
    _deferToken = deferToken;
    _deferPending = true;
    bool isBound = allowDefer && _endpoint.restApiDeferBind(deferToken, [deferToken](const String& respStr) {
                RaftWebResponderRestAPI::completeDeferredResponse(deferToken, respStr);
            });
    if (!isBound && _deferPending)
    {
        // The endpoint reported a deferred response without having deferred it - respond with an error now
        _deferPending = false;
        _respStr = R"({"req":")" + _requestStr + R"(","rslt":"fail","error":"deferFailed"})";
    }
#ifdef DEBUG_RESPONDER_REST_API_DEFERRED
    LOG_I(MODULE_PREFIX, "callEndpoint deferred token %d bound %d req %s", deferToken, isBound, _requestStr.c_str());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Complete a deferred response (main task only)
/// @param deferToken token of the request
/// @param respStr response
/// @return true if a responder with a pending deferred response for this token was found
bool RaftWebResponderRestAPI::completeDeferredResponse(uint32_t deferToken, const String& respStr)
{
    for (RaftWebResponderRestAPI* pResponder : _liveResponders)
    {
        if (pResponder->_deferPending && (pResponder->_deferToken == deferToken))
        {
            pResponder->_respStr = respStr;
            pResponder->_respStrPos = 0;
            pResponder->_deferPending = false;
#ifdef DEBUG_RESPONDER_REST_API_DEFERRED
            LOG_I(MODULE_PREFIX, "completeDeferredResponse token %d resp %s", deferToken, respStr.c_str());
#endif
            return true;
        }
    }
#ifdef DEBUG_RESPONDER_REST_API_DEFERRED
    LOG_I(MODULE_PREFIX, "completeDeferredResponse token %d no responder (client gone)", deferToken);
#endif
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if a response is available (not while a deferred response is pending)
bool RaftWebResponderRestAPI::responseAvailable()
{
    return (_connStatus == CONN_ACTIVE) && !_deferPending;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if the standard headers are required now (they are held back while a deferred response is pending
///        as they include the content length)
bool RaftWebResponderRestAPI::isStdHeaderRequired()
{
    return !_deferPending;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle inbound data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderRestAPI::handleInboundData(const uint8_t* pBuf, uint32_t dataLen)
{
    // Record data received so we know when to respond
    uint32_t curBufPos = _numBytesReceived;
    _numBytesReceived += dataLen;

    // Handle data which may be multipart
    if (_headerExtract.isMultipart)
    {
#ifdef DEBUG_RESPONDER_REST_API_MULTIPART_DATA
        LOG_I(MODULE_PREFIX, "handleInboundData multipart len %d", dataLen);
#endif
        _multipartParser.handleData(pBuf, dataLen);
#ifdef DEBUG_RESPONDER_REST_API_MULTIPART_DATA
        LOG_I(MODULE_PREFIX, "handleInboundData multipart finished bytesRx %d contentLen %d", 
                    _numBytesReceived, _headerExtract.contentLength);
#endif
    }
    else
    {
#ifdef DEBUG_RESPONDER_REST_API_NON_MULTIPART_DATA
        LOG_I(MODULE_PREFIX, "handleInboundData curPos %d bufLen %d totalLen %d", curBufPos, dataLen, _headerExtract.contentLength);
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
    _connStatus = CONN_ACTIVE;  // REST API connections are immediately active
    _endpointCalled = false;
    _numBytesReceived = 0;
    _respStrPos = 0;
    _sendStartMs = millis();

#ifdef DEBUG_RESPONDER_REST_API
    LOG_I(MODULE_PREFIX, "startResponding connStatus %d", _connStatus);
#endif
    return true;
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

    // Nothing to send while a deferred response is pending
    if (_deferPending)
        return 0;

    // Check if we need to call API
    uint32_t respLen = 0;
    if (!_endpointCalled)
        callEndpoint(false);

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
        _connStatus = CONN_INACTIVE;
#ifdef DEBUG_RESPONDER_API_START_END
        LOG_I(MODULE_PREFIX, "getResponseNext endOfFile sent final chunk ok");
#endif
    }

#ifdef DEBUG_RESPONDER_REST_API
    LOG_I(MODULE_PREFIX, "getResponseNext respLen %d connStatus %d", respLen, _connStatus);
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
// Check if ready to receive data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderRestAPI::readyToReceiveData()
{
    // While a deferred response is pending keep receiving so that a client disconnect is detected
    if (_deferPending)
        return true;

#ifdef APPLY_MIN_GAP_BETWEEN_API_CALLS_MS
    if (!Raft::isTimeout(millis(), _lastFileReqMs, APPLY_MIN_GAP_BETWEEN_API_CALLS_MS))
        return false;
    _lastFileReqMs = millis();
    LOG_I(MODULE_PREFIX, "readyToReceiveData time %d", _lastFileReqMs);
#endif

    // Check if endpoint specifies a ready function
    if (_endpoint.restApiFnIsReady)
        return _endpoint.restApiFnIsReady(_apiSourceInfo);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks on multipart parser
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebResponderRestAPI::multipartOnEvent(void* pCtx, RaftMultipartEvent event, const uint8_t *pBuf, uint32_t pos)
{
#ifdef DEBUG_MULTIPART_EVENTS
    LOG_W(MODULE_PREFIX, "multipartEvent event %s (%d) pos %d", RaftWebMultipart::getEventText(event), event, pos);
#endif
}

RaftRetCode RaftWebResponderRestAPI::multipartOnData(void* pCtx, const uint8_t *pBuf, uint32_t bufLen, RaftMultipartForm& formInfo, 
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
        return _endpoint.restApiFnChunk(_requestStr, fileStreamBlock, _apiSourceInfo);
    return RAFT_NOT_IMPLEMENTED;
}

void RaftWebResponderRestAPI::multipartOnHeaderNameValue(void* pCtx, const String& name, const String& val)
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
    // Check we are getting data
    if (_headerExtract.method != WEB_METHOD_GET)
        return -1;

    // Get length by calling API (normally already called from loop())
    if (!_endpointCalled)
        callEndpoint(false);
    return _respStr.length();
}
