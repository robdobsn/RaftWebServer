/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebConnection.h"
#include "RaftWebInterface.h"
#include "RaftWebHandler.h"
#include "RaftWebConnManager.h"
#include "RaftWebResponder.h"
#include <Logger.h>
#include <RaftUtils.h>
#include <ArduinoTime.h>
#include <RdJson.h>
#include <functional>

static const char *MODULE_PREFIX = "RaftWebConn";

// Warn
#define WARN_WEB_CONN_ERROR_CLOSE

// Debug
// #define DEBUG_WEB_REQUEST_HEADERS
// #define DEBUG_WEB_REQUEST_HEADER_DETAIL
// #define DEBUG_WEB_REQUEST_READ
// #define DEBUG_WEB_REQUEST_RESP
// #define DEBUG_WEB_REQUEST_READ_START_END
// #define DEBUG_RESPONDER_PROGRESS
// #define DEBUG_RESPONDER_PROGRESS_DETAIL
// #define DEBUG_RESPONDER_HEADER_DETAIL
// #define DEBUG_RESPONDER_CONTENT_DETAIL
// #define DEBUG_RESPONDER_CREATE_DELETE
// #define DEBUG_WEB_SOCKET_SEND
// #define DEBUG_WEB_SSEVENT_SEND
// #define DEBUG_WEB_CONNECTION_DATA_PACKETS
// #define DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
// #define DEBUG_WEB_CONN_OPEN_CLOSE
// #define DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS 50
// #define DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS 50
// #define DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS 50

#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
#include "esp_heap_trace.h"
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnection::RaftWebConnection()
{
    // Responder
    _pResponder = nullptr;
    _pClientConn = nullptr;
    
    // Clear
    clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnection::~RaftWebConnection()
{
    // Check if there is a responder to clean up
    if (_pResponder)
    {
#ifdef DEBUG_RESPONDER_CREATE_DELETE
        LOG_W(MODULE_PREFIX, "destructor deleting _pResponder %d", (uint32_t)_pResponder);
#endif
        delete _pResponder;
    }

    // Check if there is a client to clean up
    if (_pClientConn)
    {
#ifdef DEBUG_RESPONDER_CREATE_DELETE
        LOG_W(MODULE_PREFIX, "destructor deleting _pClientConn %d", _pClientConn->getClientId());
#endif
        delete _pClientConn;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set new connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::setNewConn(RaftClientConnBase* pClientConn, RaftWebConnManager* pConnManager,
                uint32_t maxSendBufferBytes)
{
    // Error check - there should not be a current client otherwise there's been a mistake!
    if (_pClientConn != nullptr)
    {
        // Caller should only use this method if connection is inactive
        LOG_E(MODULE_PREFIX, "setNewConn existing connection active %d", _pClientConn->getClientId());
        return false;
    }

    // Clear first
    clear();

    // New connection
    _pClientConn = pClientConn;
    _pConnManager = pConnManager;
    _timeoutStartMs = millis();
    _timeoutLastActivityMs = millis();
    _timeoutActive = true;
    _timeoutDurationMs = MAX_STD_CONN_DURATION_MS;
    _timeoutOnIdleDurationMs = MAX_CONN_IDLE_DURATION_MS;
    _maxSendBufferBytes = maxSendBufferBytes;

    // Set non-blocking connection
    _pClientConn->setup(USE_BLOCKING_WEB_CONNECTIONS);

    // Debug
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
    LOG_I(MODULE_PREFIX, "setNewConn connId %d", _pClientConn->getClientId());
#endif

    // Connection set
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnection::clear()
{
    // Delete responder if there is one
    if (_pResponder)
    {
#ifdef DEBUG_RESPONDER_CREATE_DELETE
        LOG_W(MODULE_PREFIX, "clear deleting _pResponder %d", (uint32_t)_pResponder);
#endif
        delete _pResponder;
        _pResponder = nullptr;
    }

    // Delete any client
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
    if (_pClientConn)
    {
        LOG_I(MODULE_PREFIX, "clear deleting clientConn %d", _pClientConn->getClientId());
    }
#endif
    delete _pClientConn;
    _pClientConn = nullptr;

    // Clear all fields
    _pConnManager = nullptr;
    _isStdHeaderRequired = true;
    _sendSpecificHeaders = true;
    _httpResponseStatus = HTTP_STATUS_OK;
    _timeoutStartMs = 0;
    _timeoutLastActivityMs = 0;
    _timeoutDurationMs = MAX_STD_CONN_DURATION_MS;
    _timeoutOnIdleDurationMs = MAX_CONN_IDLE_DURATION_MS;
    _timeoutActive = false;
    _isClearPending = false;
    _clearPendingStartMs = 0;
    _parseHeaderStr = "";
    _debugDataRxCount = 0;
    _maxSendBufferBytes = 0;
    _header.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set flags to indicate clear is pending
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnection::clearAfterSendCompletion()
{
    // Check if there is a timeout
    if (CONNECTION_CLEAR_PENDING_TIME_MS > 0)
    {
        // Set clear pending
        _isClearPending = true;
        _clearPendingStartMs = millis();
    }
    else
    {
        // Clear immediately
        clear();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check Active
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::isActive()
{
    return _pClientConn && _pClientConn->isActive();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send on connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::sendOnConn(const uint8_t* pBuf, uint32_t bufLen)
{
#ifdef DEBUG_WEB_SOCKET_SEND
    LOG_I(MODULE_PREFIX, "sendOnConnection len %d responder %d connId %d", bufLen, (uint32_t)_pResponder, 
                    _pClientConn ? _pClientConn->getClientId() : 0);
#endif

    // Send to responder
    if (_pResponder)
        return _pResponder->sendFrame(pBuf, bufLen);

    // Failure
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send server-side-event
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnection::sendOnSSEvents(const char* eventContent, const char* eventGroup)
{
#ifdef DEBUG_WEB_SSEVENT_SEND
    LOG_I(MODULE_PREFIX, "sendOnSSEvents eventGroup %s eventContent %s responder %d connId %d", 
                eventGroup, eventContent,
                (uint32_t)_pResponder, 
                _pClientConn ? _pClientConn->getClientId() : 0);
#endif

    // Send to responder
    if (_pResponder)
    {
        _pResponder->sendEvent(eventContent, eventGroup);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnection::service()
{
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugServiceStartMs = millis();
    uint32_t debugServiceRespTimeMs = 0;
    uint32_t debugServiceGetDataMs = 0;
    uint32_t debugServiceDataLen = 0;
    uint32_t debugDataResponderElapMs = 0;
    uint32_t debugDataEndElapMs = 0;
    uint32_t debugServiceConnHeaderMs = 0;
    uint32_t debugTimeOutHandlerElapMs = 0;
#endif

    // Check active
    if (!_pClientConn)
        return;

    // Handle any queued data
    handleTxQueuedData();

    // Check clear
    if (_isClearPending)
    {
        // Check for timeout
        if (Raft::isTimeout(millis(), _clearPendingStartMs, CONNECTION_CLEAR_PENDING_TIME_MS))
        {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
            LOG_I(MODULE_PREFIX, "service conn cleared - clear was pending");
#endif
            clear();
        }
        return;
    }

    // Check timeout
    if (_timeoutActive && (Raft::isTimeout(millis(), _timeoutStartMs, _timeoutDurationMs) ||
                    Raft::isTimeout(millis(), _timeoutLastActivityMs, _timeoutOnIdleDurationMs)))
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugTimeOutHandlerStartMs = millis();
#endif
        LOG_W(MODULE_PREFIX, "service timeout on connection connId %d sinceStartMs %d sinceLastActivityMs %d", 
                _pClientConn->getClientId(), 
                Raft::timeElapsed(millis(), _timeoutStartMs),
                Raft::timeElapsed(millis(), _timeoutLastActivityMs));
        clear();

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugTimeOutHandlerElapMs = millis() - debugTimeOutHandlerStartMs;
#endif
        return;
    }

    // Service responder and check if ready for data, if there is no responder
    // then always ready as we're building the header, etc
    bool checkForNewData = true;
    if (_pResponder)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugRespHdlDataStartMs = millis();
#endif

        _pResponder->service();
        checkForNewData = _pResponder->readyForData();

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugServiceRespTimeMs = millis() - debugRespHdlDataStartMs;
#endif
    }

    // Check for new data if required
    bool closeRequired = false;
    bool dataAvailable = false;
    bool errorOccurred = false;
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> rxData;
    if (checkForNewData)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t getDataStartMs = millis();
#endif

        RaftClientConnRslt rxRslt = _pClientConn->getDataStart(rxData);
        dataAvailable = rxData.size() > 0;
        if (rxRslt == RaftClientConnRslt::CLIENT_CONN_RSLT_CONN_CLOSED)
            closeRequired = true;
        else if (rxRslt == RaftClientConnRslt::CLIENT_CONN_RSLT_ERROR)
            errorOccurred = true;

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugServiceGetDataMs = millis() - getDataStartMs;
        debugServiceDataLen = dataAvailable ? rxData.size() : 0;
#endif
    }

    // Check if data available
    if (dataAvailable)
    {
        // Update for timeout
        _timeoutLastActivityMs = millis();

        // Update stats
        _debugDataRxCount += rxData.size();
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "service got new data len %d rxTotal %d", rxData.size(), _debugDataRxCount);
#endif
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
        String debugStr;
        Utils::getHexStrFromBytes(pData, rxData.size(), debugStr);
        LOG_I(MODULE_PREFIX, "connId %d RX: %s", _pClientConn->getClientId(), debugStr.c_str());
#endif
    }

    // See if we are forming the header
    uint32_t bufPos = 0;
    if (dataAvailable && !_header.isComplete)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugConnHeaderStartMs = millis();
#endif

        if (!serviceConnHeader(rxData.data(), rxData.size(), bufPos))
        {
            LOG_W(MODULE_PREFIX, "service connHeader error closing connId %d", _pClientConn->getClientId());
            errorOccurred = true;
        }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugServiceConnHeaderMs = millis() - debugConnHeaderStartMs;
#endif
    }

    // Service response - may remain in this state for multiple service loops
    // (e.g. for file-transfer / web-sockets)
    if (_header.isComplete)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugResponderStartMs = millis();
#endif

        if (!responderHandleData(rxData.data(), rxData.size(), bufPos))
        {
#ifdef DEBUG_RESPONDER_PROGRESS
            LOG_I(MODULE_PREFIX, "service no longer sending so close connId %d", _pClientConn->getClientId());
#endif
            closeRequired = true;
        }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugDataResponderElapMs = millis() - debugResponderStartMs;
#endif
    }

    // If new data checking then end the data access
    if (checkForNewData)
    {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        uint32_t debugDataEndStartMs = millis();
#endif
        _pClientConn->getDataEnd();
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
        debugDataEndElapMs = millis() - debugDataEndStartMs;
#endif
    }

    // Check for error
    if (errorOccurred)
    {
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
        LOG_I(MODULE_PREFIX, "service conn closing ErrorOccurred connId %d", 
                _pClientConn->getClientId());
#endif

        // This closes any connection and clears status ready for a new one
        clear();

#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
        heap_trace_stop();
        heap_trace_dump();
#endif
    }

    // Check for close required
    else if (closeRequired)
    {
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
        LOG_I(MODULE_PREFIX, "service conn closeRequired connId %d", 
                _pClientConn->getClientId());
#endif

        // Clear connection after send completion
        clearAfterSendCompletion();

#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
        heap_trace_stop();
        heap_trace_dump();
#endif
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    if (millis() - debugServiceStartMs > DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS)
    {
        LOG_I(MODULE_PREFIX, "service SLOW total %ldms srvResp %dms dataStart %dms dataLen %d connHdr %dms datResp %dms dataEnd %dms timoHdlr %dms", 
                millis() - debugServiceStartMs,
                debugServiceRespTimeMs,
                debugServiceGetDataMs,
                debugServiceDataLen,
                debugServiceConnHeaderMs,
                debugDataResponderElapMs,
                debugDataEndElapMs,
                debugTimeOutHandlerElapMs);
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service connection header
// Returns false on header error
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::serviceConnHeader(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos)
{
    if (!_pConnManager)
        return false;

    // Check for data
    if (dataLen == 0)
        return true;

    // Debug
#ifdef DEBUG_WEB_REQUEST_HEADER_DETAIL
    {
        String rxStr;
        Raft::strFromBuffer(pRxData, dataLen, rxStr);
        LOG_I(MODULE_PREFIX, "req data:\n%s", rxStr.c_str());
    }
#endif

    // Handle data for header
#ifdef DEBUG_WEB_REQUEST_HEADERS
    uint64_t hhStUs = micros();
#endif
    bool headerOk = handleHeaderData(pRxData, dataLen, curBufPos);
#ifdef DEBUG_WEB_REQUEST_HEADERS
    uint64_t hhEnUs = micros();
#endif
    if (!headerOk)
    {
#ifdef DEBUG_WEB_REQUEST_HEADERS
        uint64_t srStUs = micros();
#endif
        setHTTPResponseStatus(HTTP_STATUS_BADREQUEST);
#ifdef DEBUG_WEB_REQUEST_HEADERS
        uint64_t srEnUs = micros();
        LOG_I(MODULE_PREFIX, "serviceConnHeader badResponse hh %lld sr %lld", hhEnUs-hhStUs, srEnUs-srStUs);
#endif
        return false;
    }

    // Check if header if now complete
    if (!_header.isComplete)
    {
#ifdef DEBUG_WEB_REQUEST_HEADERS
        LOG_I(MODULE_PREFIX, "serviceConnHeader incomplete hh %lld", hhEnUs-hhStUs);
#endif
        return true;
    }

    // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
    LOG_I(MODULE_PREFIX, "onRxData headersOK method %s fullURI %s contentType %s contentLength %d host %s isContinue %d isMutilpart %d multipartBoundary %s", 
            RaftWebInterface::getHTTPMethodStr(_header.extract.method), _header.URIAndParams.c_str(),
            _header.extract.contentType.c_str(), _header.extract.contentLength, _header.extract.host.c_str(), 
            _header.isContinue, _header.extract.isMultipart, _header.extract.multipartBoundary.c_str());
    LOG_I(MODULE_PREFIX, "onRxData headerExt auth %s isComplete %d isDigest %d reqConnType %d wsKey %s wsVers %s", 
            _header.extract.authorization.c_str(), _header.isComplete, _header.extract.isDigest, 
            _header.reqConnType, _header.webSocketKey.c_str(), _header.webSocketVersion.c_str());
#endif

    // Check for pre-flight request
    if (_header.extract.method == WEB_METHOD_OPTIONS)
    {
        // Send response
        setHTTPResponseStatus(HTTP_STATUS_NOCONTENT);
        return true;
    }

    // Now find a responder
    RaftHttpStatusCode statusCode = HTTP_STATUS_NOTFOUND;
    // Delete any existing responder - there shouldn't be one
    if (_pResponder)
    {
        LOG_W(MODULE_PREFIX, "onRxData unexpectedly deleting _pResponder %d", (uint32_t)_pResponder);
        delete _pResponder;
        _pResponder = nullptr;
    }

    // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
    uint64_t gpStUs = micros();
#endif

    // Get a responder (we are responsible for deletion)
    RaftWebRequestParams params(
                std::bind(&RaftWebConnection::rawSendOnConn, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    _pResponder = _pConnManager->getNewResponder(_header, params, statusCode);
#ifdef DEBUG_RESPONDER_CREATE_DELETE
    if (_pResponder) 
    {
        uint32_t channelID = 0;
        bool chanIdOk = _pResponder->getChannelID(channelID);
        LOG_I(MODULE_PREFIX, "serviceConnHeader new responder type %s chanID %d%s responder %x connId %d", 
                _pResponder->getResponderType(), 
                channelID, 
                chanIdOk ? "" : " (INVALID)", 
                (uint32_t)_pResponder,
                _pClientConn ? _pClientConn->getClientId() : -1);
    } 
    else 
    {
        LOG_W(MODULE_PREFIX, "serviceConnHeader failed create responder URI %s HTTP resp %d", _header.URIAndParams.c_str(), statusCode);
    }
#endif

#ifdef DEBUG_WEB_REQUEST_HEADERS
    uint64_t gpEnUs = micros();
    uint64_t ssStUs = micros();
#endif

    // Check we got a responder
    if (!_pResponder)
    {
        setHTTPResponseStatus(statusCode);
    }
    else
    {
        // Remove timeouts on long-running responders
        if (_pResponder->leaveConnOpen())
            _timeoutActive = false;

        // Start responder
        _pResponder->startResponding(*this);
    }

#ifdef DEBUG_WEB_REQUEST_HEADERS
    uint64_t ssEnUs = micros();
    LOG_I(MODULE_PREFIX, "serviceConnHeader ok handleHeaders %lldus getResponder %lldus startResponding %lldus", hhEnUs-hhStUs, gpEnUs-gpStUs, ssEnUs-ssStUs);
#endif

    // Ok
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send data to responder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::responderHandleData(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos)
{
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    uint32_t debugRespHdlDataStartMs = millis();
    uint32_t debugRespHdlDataHandleDataMs = 0;
    uint32_t debugResponderServiceElapMs = 0;
    uint32_t debugHandleRespElapMs = 0;
    uint32_t debugSendStdHdrElapMs = 0;
#endif

    // Hand any data (if there is any) to responder (if there is one)
    bool errorOccurred = false;
    if (_pResponder && (curBufPos < dataLen) && pRxData)
    {
        _pResponder->handleData(pRxData+curBufPos, dataLen-curBufPos);
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        debugRespHdlDataHandleDataMs = millis() - debugRespHdlDataStartMs;
#endif
    }

    // Service the responder (if there is one)
    if (_pResponder)
    {
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        uint32_t debugResponderServiceStartMs = millis();
#endif
        _pResponder->service();
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        debugResponderServiceElapMs = millis() - debugResponderServiceStartMs;
#endif
    }

    // Handle active responder responses
    if (_pResponder && _pResponder->isActive())
    {
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        uint32_t debugHandleRespStartMs = millis();
#endif
        // Handle next chunk of response
        errorOccurred = !handleResponseChunk();

        // Record time of activity for timeouts
        _timeoutLastActivityMs = millis();

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        debugHandleRespElapMs = millis() - debugHandleRespStartMs;
#endif
    }

    // Send the standard response and headers if required    
    else if (_isStdHeaderRequired && (!_pResponder || _pResponder->isStdHeaderRequired()))
    {
        // Send standard headers
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        uint32_t debugSendStdHdrStartMs = millis();
#endif

        errorOccurred = !sendStandardHeaders();

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
        debugSendStdHdrElapMs = millis() - debugSendStdHdrStartMs;
#endif

        // Done headers
        _isStdHeaderRequired = false;
    }

    // Debug
#ifdef DEBUG_RESPONDER_PROGRESS_DETAIL
    LOG_I(MODULE_PREFIX, "responderHandleData responder %s isActive %s errorOccurred %s connId %d", 
                _pResponder ? "YES" : "NO", 
                (_pResponder && _pResponder->isActive()) ? "YES" : "NO", 
                errorOccurred ? "YES" : "NO", 
                _pClientConn->getClientId());
#endif

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    if (millis() - debugRespHdlDataStartMs > DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS)
    {
        LOG_I(MODULE_PREFIX, "responderHandleData total %ldms handleData %dms servResp %dms hdlResp %dms stdHdr %dms", 
            millis() - debugRespHdlDataStartMs,
            debugRespHdlDataHandleDataMs, 
            debugResponderServiceElapMs,
            debugHandleRespElapMs,
            debugSendStdHdrElapMs);
    }
#endif

    // If no responder then that's it
    if (!_pResponder || errorOccurred)
        return false;

    // Return indication of more to come
    return _pResponder->isActive();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle header data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::handleHeaderData(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos)
{
    // Go through received data extracting headers
    uint32_t pos = 0;
    while (true)
    {
        // Find eol if there is one
        uint32_t lfFoundPos = 0;
        for (lfFoundPos = pos; lfFoundPos < dataLen; lfFoundPos++)
            if (pRxData[lfFoundPos] == '\n')
                break;

        // Extract string
        String newStr;
        Raft::strFromBuffer(pRxData + pos, lfFoundPos - pos, newStr);
        newStr.trim();

        // Add to parse header string
        _parseHeaderStr += newStr;

        // Move on
        pos = lfFoundPos + 1;

        // Check if we have found a full line
        if (lfFoundPos != dataLen)
        {
            // Parse header line
            if (!parseHeaderLine(_parseHeaderStr))
                return false;
            _parseHeaderStr = "";
        }

        // Check all done
        if ((pos >= dataLen) || (_header.isComplete))
            break;
    }
    curBufPos = pos;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse header line
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::parseHeaderLine(const String& line)
{
    // Headers
#ifdef DEBUG_WEB_REQUEST_HEADER_DETAIL
    LOG_I(MODULE_PREFIX, "header line len %d = %s", line.length(), line.c_str());
#endif

    // Check if we're looking at the request line
    if (!_header.gotFirstLine)
    {
        // Check blank request line
        if (line.length() == 0)
            return false;

        // Parse method, etc
        if (!parseRequestLine(line))
            return false;

        // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
        LOG_I(MODULE_PREFIX, "parseHeaderLine method %s URL %s params %s fullURI %s", 
                    RaftWebInterface::getHTTPMethodStr(_header.extract.method), _header.URL.c_str(), _header.params.c_str(),
                    _header.URIAndParams.c_str());
#endif

        // Next parsing headers
        _header.gotFirstLine = true;
        return true;
    }

    // Check if we've finished all lines
    if (line.length() == 0)
    {
        // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
        LOG_I(MODULE_PREFIX, "End of headers");
#endif

        // Check if continue required
        if (_header.isContinue)
        {
            const char response[] = "HTTP/1.1 100 Continue\r\n\r\n";
            if (rawSendOnConn((const uint8_t*) response, sizeof(response)-1, MAX_HEADER_SEND_RETRY_MS) != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
                return false;
        }

        // Header now complete
        _header.isComplete = true;
    }
    else
    {
        // Handle each line of header
        parseNameValueLine(line);
    }

    // Ok
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse first line of HTTP header
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::parseRequestLine(const String& reqLine)
{
    // Methods
    static const char* WEB_REQ_METHODS [] = { "GET", "POST", "DELETE", "PUT", "PATCH", "HEAD", "OPTIONS" };
    static const RaftWebServerMethod WEB_REQ_METHODS_ENUM [] = { WEB_METHOD_GET, WEB_METHOD_POST, WEB_METHOD_DELETE, 
                        WEB_METHOD_PUT, WEB_METHOD_PATCH, WEB_METHOD_HEAD, WEB_METHOD_OPTIONS };
    static const uint32_t WEB_REQ_METHODS_NUM = sizeof(WEB_REQ_METHODS) / sizeof(WEB_REQ_METHODS[0]);

    // Method
    int sepPos = reqLine.indexOf(' ');
    String method = reqLine.substring(0, sepPos);
    _header.extract.method = WEB_METHOD_NONE;
    for (uint32_t i = 0; i < WEB_REQ_METHODS_NUM; i++)
    {
        if (method.equalsIgnoreCase(WEB_REQ_METHODS[i]))
        {
            _header.extract.method = WEB_REQ_METHODS_ENUM[i];
            break;
        }
    }

    // Check valid
    if (_header.extract.method == WEB_METHOD_NONE)
        return false;

    // URI
    int sep2Pos = reqLine.indexOf(' ', sepPos+1);
    if (sep2Pos < 0)
        return false;
    _header.URIAndParams = decodeURL(reqLine.substring(sepPos+1, sep2Pos));

    // Split out params if present
    _header.URL = _header.URIAndParams;
    int paramPos = _header.URIAndParams.indexOf('?');
    _header.params = "";
    if (paramPos > 0)
    {
        _header.URL = _header.URIAndParams.substring(0, paramPos);
        _header.params = _header.URIAndParams.substring(paramPos+1);
    }

    // Remainder is the version string
    _header.versStr = reqLine.substring(sep2Pos+1);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse name/value pairs of HTTP header
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnection::parseNameValueLine(const String& reqLine)
{
    // Extract header name/value pairs
    int nameEndPos = reqLine.indexOf(':');
    if (nameEndPos < 0)
        return;

    // Parts
    String name = reqLine.substring(0, nameEndPos);
    String val = reqLine.substring(nameEndPos+2);

    // Store
    if (_header.nameValues.size() >= RaftWebRequestHeader::MAX_WEB_HEADERS)
        return;
    _header.nameValues.push_back({name, val});

    // Handle named headers
    // Parsing derived from AsyncWebServer menodev
    if (name.equalsIgnoreCase("Host"))
    {
        _header.extract.host = val;
    }
    else if (name.equalsIgnoreCase("Content-Type"))
    {
        _header.extract.contentType = val.substring(0, val.indexOf(';'));
        if (val.startsWith("multipart/"))
        {
            _header.extract.multipartBoundary = val.substring(val.indexOf('=') + 1);
            _header.extract.multipartBoundary.replace("\"", "");
            _header.extract.isMultipart = true;
        }
    }
    else if (name.equalsIgnoreCase("Content-Length"))
    {
        _header.extract.contentLength = atoi(val.c_str());
    }
    else if (name.equalsIgnoreCase("Expect") && val.equalsIgnoreCase("100-continue"))
    {
        _header.isContinue = true;
    }
    else if (name.equalsIgnoreCase("Authorization"))
    {
        if (val.length() > 5 && val.substring(0, 5).equalsIgnoreCase("Basic"))
        {
            _header.extract.authorization = val.substring(6);
        }
        else if (val.length() > 6 && val.substring(0, 6).equalsIgnoreCase("Digest"))
        {
            _header.extract.isDigest = true;
            _header.extract.authorization = val.substring(7);
        }
    }
    else if (name.equalsIgnoreCase("Upgrade") && val.equalsIgnoreCase("websocket"))
    {
        // WebSocket request can be uniquely identified by header: [Upgrade: websocket]
        _header.reqConnType = REQ_CONN_TYPE_WEBSOCKET;
    }
    else if (name.equalsIgnoreCase("Accept"))
    {
        String acceptLC = val;
        acceptLC.toLowerCase();
        if (acceptLC.indexOf("text/event-stream") >= 0)
        {
            // WebEvent request can be uniquely identified by header:  [Accept: text/event-stream]
            _header.reqConnType = REQ_CONN_TYPE_EVENT;
        }
    }
    else if (name.equalsIgnoreCase("Sec-WebSocket-Key"))
    {
        _header.webSocketKey = val;
    }
    else if (name.equalsIgnoreCase("Sec-WebSocket-Version"))
    {
        _header.webSocketVersion = val;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decode URL escaped string
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftWebConnection::decodeURL(const String &inURL) const
{
    // Go through handling encoding
    const char* pCh = inURL.c_str();
    String outURL;
    outURL.reserve(inURL.length());
    while (*pCh)
    {
        // Check for % escaping
        if ((*pCh == '%') && *(pCh+1) && *(pCh+2))
        {
            char newCh = Raft::getHexFromChar(*(pCh+1)) * 16 + Raft::getHexFromChar(*(pCh+2));
            outURL.concat(newCh);
            pCh += 3;
        }
        else
        {
            outURL.concat(*pCh == '+' ? ' ' : *pCh);
            pCh++;
        }
    }
    return outURL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set HTTP response status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebConnection::setHTTPResponseStatus(RaftHttpStatusCode responseCode)
{
#ifdef DEBUG_WEB_REQUEST_RESP
    LOG_I(MODULE_PREFIX, "Setting response code %s (%d)", RaftWebInterface::getHTTPStatusStr(responseCode), responseCode);
#endif
    _httpResponseStatus = responseCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raw send on connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnSendRetVal RaftWebConnection::rawSendOnConn(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs)
{
    // Check connection
    if (!_pClientConn)
    {
        LOG_W(MODULE_PREFIX, "rawSendOnConn conn is nullptr");
        return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

    // Check buffer
    if (!pBuf)
    {
        LOG_W(MODULE_PREFIX, "rawSendOnConn pBuf is nullptr");
        return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
    String debugStr;
    Raft::getHexStrFromBytes(pBuf, bufLen, debugStr);
    LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d TX: %s", _pClientConn->getClientId(), debugStr.c_str());
#endif

    // Handle any data waiting to be written
    if (!handleTxQueuedData())
    {
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d failed handleTxQueueData", _pClientConn->getClientId());
#endif
        return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

    // Check if data can be sent immediately
    if (_socketTxQueuedBuffer.size() == 0)
    {
        // Queue is currently empty so try to send
        RaftWebConnSendRetVal retVal = _pClientConn->write(pBuf, bufLen, maxRetryMs);
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d send len %d result %s", _pClientConn->getClientId(), bufLen, RaftWebConnDefs::getSendRetValStr(retVal));
#endif
        if (retVal != RaftWebConnSendRetVal::WEB_CONN_SEND_EAGAIN)
            return retVal;
    }

    // Check queue max size
    uint32_t curSize = _socketTxQueuedBuffer.size();
    if (curSize + bufLen > _maxSendBufferBytes)
    {
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d send buffer overflow was %d trying to add %d max %d", 
                    _pClientConn->getClientId(), curSize, bufLen, _maxSendBufferBytes);
#endif
        return RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    }

    // Append to buffer
    _socketTxQueuedBuffer.resize(_socketTxQueuedBuffer.size() + bufLen);
    memcpy(_socketTxQueuedBuffer.data() + curSize, pBuf, bufLen);

#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
    LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d data added %d to send buffer newLen %d", _pClientConn->getClientId(), bufLen, _socketTxQueuedBuffer.size());
#endif

    // Ok - the data will be sent later
    return RaftWebConnSendRetVal::WEB_CONN_SEND_EAGAIN;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send standard headers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::sendStandardHeaders()
{
    // Send the first line
    char respLine[200];
    snprintf(respLine, sizeof(respLine), "HTTP/1.1 %d %s\r\n", _httpResponseStatus, 
                RaftWebInterface::getHTTPStatusStr(_httpResponseStatus));
    if (rawSendOnConn((const uint8_t*)respLine, strnlen(respLine, sizeof(respLine)), MAX_HEADER_SEND_RETRY_MS) != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
        return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
    // Debug
    LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s connId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif

    // Send headers related to pre-flight checks
    if (_header.extract.method == WEB_METHOD_OPTIONS)
    {
        // Header strings
        static const char* preFlightRespHeaders = "Access-Control-Allow-Methods: GET,HEAD,PUT,PATCH,POST,DELETE\r\nAccess-Control-Allow-Headers: *\r\nVary: Access-Control-Request-Headers\r\nContent-Length: 0\r\n";
        if (rawSendOnConn((const uint8_t*)preFlightRespHeaders, strlen(preFlightRespHeaders), MAX_HEADER_SEND_RETRY_MS) != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
            return false;
    }

    // Send content type
    if (_pResponder && _pResponder->getContentType())
    {
        snprintf(respLine, sizeof(respLine), "Content-Type: %s\r\n", _pResponder->getContentType());
        if (rawSendOnConn((const uint8_t*)respLine, strnlen(respLine, sizeof(respLine)), MAX_HEADER_SEND_RETRY_MS) != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
            return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
        // Debug
        LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s connId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
    }

    // Send standard headers
    if (_pConnManager)
    {
        std::list<RdJson::NameValuePair>* pRespHeaders = _pConnManager->getStdResponseHeaders();
        for (RdJson::NameValuePair& nvPair : *pRespHeaders)
        {
            snprintf(respLine, sizeof(respLine), "%s: %s\r\n", nvPair.name.c_str(), nvPair.value.c_str());
            if (rawSendOnConn((const uint8_t*)respLine, strnlen(respLine, sizeof(respLine)), MAX_HEADER_SEND_RETRY_MS) != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
                return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
            // Debug
            LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s connId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
        }
    }

    // Send additional headers
    if (_pResponder)
    {
        std::list<RdJson::NameValuePair>* pRespHeaders = _pResponder->getHeaders();
        for (RdJson::NameValuePair& nvPair : *pRespHeaders)
        {
            snprintf(respLine, sizeof(respLine), "%s: %s\r\n", nvPair.name.c_str(), nvPair.value.c_str());
            if (rawSendOnConn((const uint8_t*)respLine, strnlen(respLine, sizeof(respLine)), MAX_HEADER_SEND_RETRY_MS) != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
                return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
            // Debug
            LOG_I(MODULE_PREFIX, "sendAdditionalHeaders sent %s connId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
        }
    }

    // Content length if required
    if (_pResponder)
    {
        int contentLength = _pResponder->getContentLength();
        if (contentLength >= 0)
        {
            snprintf(respLine, sizeof(respLine), "Content-Length: %d\r\n", contentLength);
            if (rawSendOnConn((const uint8_t*)respLine, strnlen(respLine, sizeof(respLine)), MAX_HEADER_SEND_RETRY_MS) != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
                return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
            // Debug
            LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s connId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
        }
    }

    // Check if connection needs closing
    if (!_pResponder || !_pResponder->leaveConnOpen())
    {
        snprintf(respLine, sizeof(respLine), "Connection: close\r\n");
        if (rawSendOnConn((const uint8_t*)respLine, strnlen(respLine, sizeof(respLine)), MAX_HEADER_SEND_RETRY_MS) != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
            return false;

#ifdef DEBUG_RESPONDER_HEADER_DETAIL
        // Debug
        LOG_I(MODULE_PREFIX, "sendStandardHeaders sent %s connId %d", respLine, _pClientConn ? _pClientConn->getClientId() : 0);
#endif
    }

    // Send end of header line
    return rawSendOnConn((const uint8_t*)"\r\n", 2, MAX_HEADER_SEND_RETRY_MS) == RaftWebConnSendRetVal::WEB_CONN_SEND_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle next chunk of response
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::handleResponseChunk()
{
#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
    uint32_t debugHdlRespChunkStartMs = millis();
    uint32_t debugTimingStartMs = millis();
    uint32_t debugSendHdrsMs = 0;
    uint32_t debugGetRespNextMs = 0;
    uint32_t debugRawSendOnConnMs = 0;
#endif

    // Check if standard reponse to be sent first
    if (_isStdHeaderRequired && _pResponder->isStdHeaderRequired())
    {
        // Send standard headers
        if (!sendStandardHeaders())
        {
        // Debug
#ifdef DEBUG_RESPONDER_CONTENT_DETAIL
            LOG_I(MODULE_PREFIX, "handleResponseChunk sendStandardHeaders failed connId %d", _pClientConn ? _pClientConn->getClientId() : 0);
#endif
            return false;
        }

        // Done headers
        _isStdHeaderRequired = false;
    }

#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
    debugSendHdrsMs = millis() - debugTimingStartMs;
    debugTimingStartMs = millis();
#endif

    // Check if data waiting to be sent
    if (_socketTxQueuedBuffer.size() == 0)
    {
        // Get next chunk of response
        uint8_t* pRespBuffer = nullptr;
        uint32_t respSize = _pResponder->getResponseNext(pRespBuffer, _maxSendBufferBytes);

#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
        debugGetRespNextMs = millis() - debugTimingStartMs;
        debugTimingStartMs = millis();
#endif

        // Check valid
        if (respSize != 0)
        {
            // Send
            RaftWebConnSendRetVal retVal = rawSendOnConn((const uint8_t*)pRespBuffer, respSize, MAX_CONTENT_SEND_RETRY_MS);

            // Debug
#ifdef DEBUG_RESPONDER_CONTENT_DETAIL
            LOG_I(MODULE_PREFIX, "handleResponseChunk writing %d retVal %s connId %d", 
                        respSize, RaftWebConnDefs::getSendRetValStr(retVal), _pClientConn ? _pClientConn->getClientId() : 0);
#endif

            // Handle failure
            if (retVal == RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL)
                return false;
        }
    }

#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
    if (millis() - debugHdlRespChunkStartMs > DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS)
    {
        debugRawSendOnConnMs = millis() - debugTimingStartMs;
        LOG_I(MODULE_PREFIX, "handleResponseChunk timing sendHeaders %dms getRespNext %dms sendOnConn %dms connId %d respType %s", 
                debugSendHdrsMs, debugGetRespNextMs, debugRawSendOnConnMs, 
                _pClientConn ? _pClientConn->getClientId() : 0,
                _pResponder ? _pResponder->getResponderType() : "");
    }
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle sending queued data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::handleTxQueuedData()
{
    // Check if data in the queue to send
    if (_socketTxQueuedBuffer.size() > 0)
    {
        // Try to send
        RaftWebConnSendRetVal retVal = _pClientConn->write(_socketTxQueuedBuffer.data(), _socketTxQueuedBuffer.size(), MAX_CONTENT_SEND_RETRY_MS);
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "handleTxQueuedData connId %d result %s", _pClientConn->getClientId(), RaftWebConnDefs::getSendRetValStr(retVal));
#endif
        if (retVal == RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL)
            return false;
        else if (retVal == RaftWebConnSendRetVal::WEB_CONN_SEND_EAGAIN)
            return true;
        
        // Sent ok so clear queue
        _socketTxQueuedBuffer.clear();
    }
    return true;
}