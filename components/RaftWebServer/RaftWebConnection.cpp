/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <functional>
#include "Logger.h"
#include "RaftUtils.h"
#include "ArduinoTime.h"
#include "RaftJson.h"
#include "RaftWebConnection.h"
#include "RaftWebInterface.h"
#include "RaftWebHandler.h"
#include "RaftWebConnManager.h"
#include "RaftWebResponder.h"

static const char *MODULE_PREFIX = "RaftWebConn";

// Warn
#define WARN_WEB_CONN_ERROR_CLOSE
#define WARN_WEB_CONN_CANNOT_SEND
#define WARN_ON_PACKET_SEND_MISMATCH

// Debug
// #define DEBUG_WEB_REQUEST_HEADERS
// #define DEBUG_WEB_REQUEST_HEADER_DETAIL
// #define DEBUG_WEB_REQUEST_READ
// #define DEBUG_WEB_REQUEST_RESP
// #define DEBUG_WEB_REQUEST_READ_START_END
// #define DEBUG_RESPONDER_PROGRESS
// #define DEBUG_RESPONDER_PROGRESS_DETAIL
// #define DEBUG_RESPONDER_HEADER
// #define DEBUG_RESPONDER_HEADER_DETAIL
// #define DEBUG_RESPONDER_CONTENT_DETAIL
// #define DEBUG_RESPONDER_CREATE_DELETE
// #define DEBUG_WEB_SOCKET_SEND
// #define DEBUG_WEB_SSEVENT_SEND
// #define DEBUG_WEB_CONNECTION_DATA_PACKETS
// #define DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
// #define DEBUG_WEB_CONN_OPEN_CLOSE
// #define DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS 10
// #define DEBUG_WEB_CONN_SERVICE_SLOW_STR_DATA_1ST_LINE_DETAIL
// #define DEBUG_WEB_CONN_SERVICE_SLOW_STR_DATA_ALL_DETAIL
// #define DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS 10
// #define DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS 10
// #define DEBUG_RESPONDER_FAILURE

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
                uint32_t maxSendBufferBytes, uint32_t clearPendingDurationMs)
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
    _clearPendingDurationMs = clearPendingDurationMs;

    // Set non-blocking connection
    _pClientConn->setup(USE_BLOCKING_WEB_CONNECTIONS);

    // Debug
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
    LOG_I(MODULE_PREFIX, "setNewConn connId %d maxSendBytes %d clearPendingMs %d", 
            _pClientConn->getClientId(), maxSendBufferBytes, clearPendingDurationMs);
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
    if (_clearPendingDurationMs > 0)
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

void RaftWebConnection::loop()
{
    // Check active
    if (!_pClientConn)
        return;

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint64_t debugServiceStartUs = micros();
    uint64_t debugHandleTxStartUs = micros();
#endif

    // Handle any queued data
    handleTxQueuedData();

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugHandleTxUs = micros() - debugHandleTxStartUs;
    uint64_t debugClearStartUs = micros();
#endif

    // Check clear
    if (_isClearPending)
    {
        // Check for timeout
        if (Raft::isTimeout(millis(), _clearPendingStartMs, _clearPendingDurationMs))
        {
#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
            LOG_I(MODULE_PREFIX, "loop conn %d clearing - clear was pending", _pClientConn->getClientId());
#endif
            clear();
        }
        return;
    }

    // Check timeout
    if (_timeoutActive && (Raft::isTimeout(millis(), _timeoutStartMs, _timeoutDurationMs) ||
                    Raft::isTimeout(millis(), _timeoutLastActivityMs, _timeoutOnIdleDurationMs)))
    {
        LOG_W(MODULE_PREFIX, "loop timeout on connection connId %d sinceStartMs %d sinceLastActivityMs %d", 
                _pClientConn->getClientId(), 
                Raft::timeElapsed(millis(), _timeoutStartMs),
                Raft::timeElapsed(millis(), _timeoutLastActivityMs));
        clear();
        return;
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugClearUs = micros() - debugClearStartUs;
    String responderStr = (_pResponder ? String(_pResponder->getResponderType()) + 
                        "(" + String((unsigned long)_pResponder, 16) + ")" : "NO_RESPONDER");
    uint64_t debugResponderStartUs = micros();
#endif

    // Service responder and check if ready for data, if there is no responder
    // then always ready as we're building the header, etc
    bool checkForNewData = true;
    if (_pResponder)
    {
        _pResponder->loop();
        checkForNewData = _pResponder->readyToReceiveData();
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugResponderUs = micros() - debugResponderStartUs;
    uint64_t debugGetDataStartUs = micros();
#endif
    // Check for new data if required
    bool closeRequired = false;
    bool dataAvailable = false;
    bool errorOccurred = false;
    SpiramAwareUint8Vector rxData;
    if (checkForNewData)
    {

        RaftClientConnRslt rxRslt = _pClientConn->getDataStart(rxData);
        dataAvailable = rxData.size() > 0;
        if (rxRslt == RaftClientConnRslt::CLIENT_CONN_RSLT_CONN_CLOSED)
        {
            closeRequired = true;
        }
        else if (rxRslt == RaftClientConnRslt::CLIENT_CONN_RSLT_ERROR)
            errorOccurred = true;
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugGetDataUs = micros() - debugGetDataStartUs;
    uint32_t debugGetDataLen = dataAvailable ? rxData.size() : 0;
    uint64_t debugUpdateRxStatsStartUs = micros();
#endif

    // Check if data available
    if (dataAvailable)
    {
        // Update for timeout
        _timeoutLastActivityMs = millis();

        // Update stats
        _debugDataRxCount += rxData.size();
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "loop connId %d got new data len %d rxTotal %d", 
                _pClientConn->getClientId(), rxData.size(), _debugDataRxCount);
#endif
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
        String debugStr;
        Raft::getHexStrFromBytes(rxData.data(), rxData.size(), debugStr);
        LOG_I(MODULE_PREFIX, "connId %d RX: %s", _pClientConn->getClientId(), debugStr.c_str());
#endif
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugUpdateRxStatsUs = micros() - debugUpdateRxStatsStartUs;
    uint64_t debugServiceHeaderStartUs = micros();
#endif

    // See if we are forming the header
    uint32_t bufPos = 0;
    bool headerWasComplete = _header.isComplete;
    if (dataAvailable && !_header.isComplete)
    {
        if (!serviceConnHeader(rxData.data(), rxData.size(), bufPos))
        {
            LOG_W(MODULE_PREFIX, "loop connId %d connHeader error closing", _pClientConn->getClientId());
            errorOccurred = true;
        }
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugServiceHeaderUs = micros() - debugServiceHeaderStartUs;
    uint64_t debugDataResponseStartUs = micros();
#endif
    
    // Service response - may remain in this state for multiple service loops
    // (e.g. for file-transfer / web-sockets)
    if (!responderHandleData(rxData, bufPos, headerWasComplete))
    {
#ifdef DEBUG_RESPONDER_PROGRESS
        LOG_I(MODULE_PREFIX, "loop connId %d no longer sending so close", _pClientConn->getClientId());
#endif
        closeRequired = true;
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugDataResponderElapUs = micros() - debugDataResponseStartUs;
    uint64_t debugNewDataEndStartUs = micros();
#endif

    // If new data checking then end the data access
    if (checkForNewData)
        _pClientConn->getDataEnd();

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugDataEndElapUs = micros() - debugNewDataEndStartUs;
    uint64_t debugErrorHandlerStartUs = micros();
#endif

    // Check for error
    if (errorOccurred)
    {
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
        LOG_I(MODULE_PREFIX, "loop connId %d conn closing ErrorOccurred", 
                _pClientConn->getClientId());
#endif

        // This closes any connection and clears status ready for a new one
        clear();

#ifdef DEBUG_TRACE_HEAP_USAGE_WEB_CONN
        heap_trace_stop();
        heap_trace_dump();
#endif
    }

#ifdef DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS
    uint32_t debugErrorHandlerElapUs = micros() - debugErrorHandlerStartUs;
    uint64_t debugCloseHandlerStartUs = micros();
#endif

    // Check for close required
    if (!errorOccurred && closeRequired)
    {
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
        LOG_I(MODULE_PREFIX, "loop connId %d conn closeRequired", 
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
    uint64_t timeNowUs = micros();
    uint32_t debugCloseHandlerElapUs = timeNowUs - debugCloseHandlerStartUs;
    uint32_t debugServiceElapsedUs = timeNowUs - debugServiceStartUs;
    if (debugServiceElapsedUs > DEBUG_WEB_CONN_SERVICE_TIME_THRESH_MS*1000)
    {
        LOG_I(MODULE_PREFIX, "loop connId %d %s SLOW total %dus dataLen %d tx %dus clear %dus responder %dus getData %dus updateRx %dus serviceHdr %dus dataResp %dus dataEnd %dus errorHdlr %dus closeHdlr %dus", 
                _pClientConn->getClientId(),
                responderStr.c_str(),
                debugServiceElapsedUs,
                debugGetDataLen,
                debugHandleTxUs,
                debugClearUs,
                debugResponderUs,
                debugGetDataUs,
                debugUpdateRxStatsUs,
                debugServiceHeaderUs,
                debugDataResponderElapUs,
                debugDataEndElapUs,
                debugErrorHandlerElapUs,
                debugCloseHandlerElapUs);
#ifdef DEBUG_WEB_CONN_SERVICE_SLOW_STR_DATA_1ST_LINE_DETAIL
        if (dataAvailable)
        {
            int idx = rxData.size() > 100 ? 100 : rxData.size();
            for (int i = 0; i < rxData.size(); i++)
            {
                if (rxData[i] == '\n')
                {
                    idx = i;
                    break;
                }
            }
            String str(rxData.data(), idx);
            LOG_I(MODULE_PREFIX, "loop connId %d %s", _pClientConn->getClientId(), str.c_str());
        }
#endif
#ifdef DEBUG_WEB_CONN_SERVICE_SLOW_STR_DATA_ALL_DETAIL
        if (dataAvailable)
        {
            String str(rxData.data(), rxData.size());
            LOG_I(MODULE_PREFIX, "loop connId %d %s", _pClientConn->getClientId(), str.c_str());
        }
#endif
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
        String rxStr(pRxData, dataLen);
        LOG_I(MODULE_PREFIX, "serviceConnHeader connId %d req data:\n%s",
                _pClientConn->getClientId(), rxStr.c_str());
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
        LOG_I(MODULE_PREFIX, "serviceConnHeader connId %d badResponse hh %lld sr %lld",
                _pClientConn->getClientId(), hhEnUs-hhStUs, srEnUs-srStUs);
#endif
        return false;
    }

    // Check if header if now complete
    if (!_header.isComplete)
    {
#ifdef DEBUG_WEB_REQUEST_HEADERS
        LOG_I(MODULE_PREFIX, "serviceConnHeader connId %d incomplete hh %lld", 
                _pClientConn->getClientId(), hhEnUs-hhStUs);
#endif
        return true;
    }

    // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
    LOG_I(MODULE_PREFIX, "onRxData connId %d headersOK method %s fullURI %s contentType %s contentLength %d host %s isContinue %d isMutilpart %d multipartBoundary %s", 
            _pClientConn->getClientId(),
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
        LOG_W(MODULE_PREFIX, "onRxData connId %d unexpectedly deleting _pResponder %d", 
                _pClientConn->getClientId(), (uint32_t)_pResponder);
        delete _pResponder;
        _pResponder = nullptr;
    }

    // Debug
#ifdef DEBUG_WEB_REQUEST_HEADERS
    uint64_t gpStUs = micros();
#endif

    // Get a responder (we are responsible for deletion)
    RaftWebRequestParams params(
                std::bind(&RaftWebConnection::canSendOnConn, this),
                std::bind(&RaftWebConnection::rawSendOnConn, this, std::placeholders::_1, std::placeholders::_2),
                _pClientConn->getClientId());
    _pResponder = _pConnManager->getNewResponder(_header, params, statusCode);
#ifdef DEBUG_RESPONDER_CREATE_DELETE
    if (_pResponder) 
    {
        uint32_t channelID = 0;
        bool chanIdSupported = _pResponder->getChannelID(channelID);
        String channelInfo = chanIdSupported ? String(channelID) : "N/A";
        LOG_I(MODULE_PREFIX, "serviceConnHeader connId %d new responder type %s chanID %s responder %x", 
                _pClientConn ? _pClientConn->getClientId() : -1,
                _pResponder->getResponderType(), 
                channelInfo.c_str(),
                (uint32_t)_pResponder);
    } 
    else 
    {
        LOG_W(MODULE_PREFIX, "serviceConnHeader failed create responder URI %s HTTP resp %d", 
                    _header.URIAndParams.c_str(), statusCode);
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

bool RaftWebConnection::responderHandleData(const SpiramAwareUint8Vector& rxData, uint32_t& curBufPos, bool doRespond)
{
#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    uint64_t debugRespHdlDataStartUs = micros();
    uint64_t debugHandleInboundDataStartUs = micros();
#endif

    // Hand any data (if there is any) to responder (if there is one)
    bool errorOccurred = false;
    if (_pResponder && (curBufPos < rxData.size()))
    {
        // Check if there is an offset
        if (curBufPos == 0)
        {
            _pResponder->handleInboundData(rxData);
        }
        else
        {
            SpiramAwareUint8Vector tmpRxData(rxData.data() + curBufPos, rxData.data() + rxData.size() - curBufPos);
            _pResponder->handleInboundData(tmpRxData);
        }
    }

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    uint32_t debugHandleInboundDataUs = micros() - debugHandleInboundDataStartUs;
    uint64_t debugResponderServiceStartUs = micros();
#endif

    // Service the responder (if there is one)
    if (_pResponder)
        _pResponder->loop();

    // If we've only just processed the header then we're not ready to send
    if (!doRespond)
        return true;

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    uint32_t debugResponderServiceElapUs = micros() - debugResponderServiceStartUs;
    uint64_t debugHandleRespStartUs = micros();
#endif

    // Handle active responder responses
    bool isActive = _pResponder && _pResponder->isActive();
    if (isActive)
    {
        // Handle next chunk of response
        errorOccurred = !handleResponseChunk();

        // Record time of activity for timeouts
        _timeoutLastActivityMs = millis();

    }

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    uint32_t debugHandleRespElapUs = micros() - debugHandleRespStartUs;
    uint64_t debugSendStdHdrStartUs = micros();
#endif

    // Send the standard response and headers if required    
    if (!isActive && _isStdHeaderRequired && (!_pResponder || _pResponder->isStdHeaderRequired()))
    {
        errorOccurred = !sendStandardHeaders();
        // Done headers
        _isStdHeaderRequired = false;
    }

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    uint32_t debugSendStdHdrElapUs = micros() - debugSendStdHdrStartUs;
    uint32_t debugRespHdlDataUs = micros() - debugRespHdlDataStartUs;
#endif

    // Debug
#ifdef DEBUG_RESPONDER_PROGRESS_DETAIL
    LOG_I(MODULE_PREFIX, "responderHandleData connId %d responder %s isActive %s errorOccurred %s", 
                _pClientConn->getClientId(),
                _pResponder ? "YES" : "NO", 
                (_pResponder && _pResponder->isActive()) ? "YES" : "NO", 
                errorOccurred ? "YES" : "NO");
#endif

#ifdef DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS
    if (debugRespHdlDataUs > DEBUG_WEB_RESPONDER_HDL_DATA_TIME_THRESH_MS)
    {
        LOG_I(MODULE_PREFIX, "responderHandleData total %dus handleInboundData %dus responderService %dus handleResp %dus sendStdHdr %dus", 
                debugRespHdlDataUs,
                debugHandleInboundDataUs,
                debugResponderServiceElapUs,
                debugHandleRespElapUs,
                debugSendStdHdrElapUs);
    }
#endif

    // If no responder then that's it
    if (!_pResponder || errorOccurred)
    {
#ifdef DEBUG_RESPONDER_FAILURE
        LOG_W(MODULE_PREFIX, "responderHandleData connId %d %s%s", 
                _pClientConn->getClientId(),
                _pResponder ? "" : "NO RESPONDER", 
                errorOccurred ? "ERROR OCCURRED" : "");
#endif
        return false;
    }

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
        String newStr(pRxData + pos, lfFoundPos - pos);
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
            const char* responseStr = "HTTP/1.1 100 Continue\r\n\r\n";
            const SpiramAwareUint8Vector response(    
                        reinterpret_cast<const uint8_t*>(responseStr),
                        reinterpret_cast<const uint8_t*>(responseStr) + strlen(responseStr)
                        );
            if (rawSendOnConn(response, MAX_HEADER_SEND_RETRY_MS) != WEB_CONN_SEND_OK)
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
// Check if we can send on the connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnSendRetVal RaftWebConnection::canSendOnConn()
{
    // Don't accept any more data while the buffer is not empty
    if (_socketTxQueuedBuffer.size() != 0)
    {
        return WEB_CONN_SEND_EAGAIN;
    }
    if (!_pClientConn)
    {
        return WEB_CONN_NO_CONNECTION;
    }
    return _pClientConn->canSend();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raw send on connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnSendRetVal RaftWebConnection::rawSendOnConn(const SpiramAwareUint8Vector& buf, uint32_t maxRetryMs)
{
    // Check connection
    if (!_pClientConn)
    {
#ifdef WARN_WEB_CONN_CANNOT_SEND
        LOG_W(MODULE_PREFIX, "rawSendOnConn conn is nullptr");
#endif
        return WEB_CONN_SEND_FAIL;
    }

    // Check if we can send
    RaftWebConnSendRetVal canSendRetVal = canSendOnConn();
    if (canSendRetVal != WEB_CONN_SEND_OK)
    {
#ifdef WARN_WEB_CONN_CANNOT_SEND
        LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d cannot send %s", 
            _pClientConn->getClientId(), RaftWebConnDefs::getSendRetValStr(canSendRetVal));
#endif
        return canSendRetVal;
    }

#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS_CONTENTS
    String debugStr;
    Raft::getHexStr(buf, debugStr);
    LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d TX: %s", _pClientConn->getClientId(), debugStr.c_str());
#endif

    // Handle any data waiting to be written
    if (!handleTxQueuedData())
    {
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d failed handleTxQueueData", _pClientConn->getClientId());
#endif
        return WEB_CONN_SEND_FAIL;
    }

    // Check if data can be sent immediately
    uint32_t bytesWritten = 0;
    RaftWebConnSendRetVal retVal = WEB_CONN_SEND_FAIL;
    if (_socketTxQueuedBuffer.size() == 0)
    {
        // Queue is currently empty so try to send
        retVal = _pClientConn->sendDataBuffer(buf, maxRetryMs, bytesWritten);
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d send len %d result %s bytesWritten %d", 
                    _pClientConn->getClientId(), buf.size(), RaftWebConnDefs::getSendRetValStr(retVal), bytesWritten);
#endif
        if ((retVal == WEB_CONN_SEND_OK) && (bytesWritten == buf.size()))
            return WEB_CONN_SEND_OK;
        if ((retVal != WEB_CONN_SEND_EAGAIN) && (retVal != WEB_CONN_SEND_OK))
            return retVal;
    }

    // Check queue max size
    if (bytesWritten > buf.size())
    {
#ifdef WARN_ON_PACKET_SEND_MISMATCH
        LOG_I(MODULE_PREFIX, "rawSendOnConn MISMATCH connId %d send len %d bytesWritten %d", 
                    _pClientConn->getClientId(), buf.size(), bytesWritten);
#endif
        return WEB_CONN_SEND_FAIL;
    }
    uint32_t curSize = _socketTxQueuedBuffer.size();
    if (curSize + buf.size() - bytesWritten > _maxSendBufferBytes)
    {
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d send buffer overflow was %d trying to add %d max %d", 
                    _pClientConn->getClientId(), curSize, buf.size()-bytesWritten, _maxSendBufferBytes);
#endif
        return WEB_CONN_SEND_FAIL;
    }

    // Append to buffer
    _socketTxQueuedBuffer.insert(_socketTxQueuedBuffer.end(), buf.begin() + bytesWritten, buf.end());

#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
    LOG_I(MODULE_PREFIX, "rawSendOnConn connId %d added %d bytes to send buffer newLen %d", 
                _pClientConn->getClientId(), buf.size()-bytesWritten, _socketTxQueuedBuffer.size());
#endif

    // The data has been queued for sending (WEB_CONN_SEND_OK)
    return WEB_CONN_SEND_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send standard headers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SpiramAwareUint8Vector RaftWebConnection::getStandardHeaders()
{
    SpiramAwareUint8Vector headers;
    const char* crlf = "\r\n";
    const char* colonSep = ": ";

    // Form the header
    String headerStr = "HTTP/1.1 " + String(_httpResponseStatus) + " " + RaftWebInterface::getHTTPStatusStr(_httpResponseStatus) + "\r\n";
    headers.insert(headers.end(), headerStr.c_str(), headerStr.c_str() + headerStr.length());

    // Add headers related to pre-flight checks
    if (_header.extract.method == WEB_METHOD_OPTIONS)
    {
        // Header strings
        static const char* preFlightRespHeaders = "Access-Control-Allow-Methods: GET,HEAD,PUT,PATCH,POST,DELETE\r\nAccess-Control-Allow-Headers: *\r\nVary: Access-Control-Request-Headers\r\nContent-Length: 0\r\n";
        headers.insert(headers.end(), preFlightRespHeaders, preFlightRespHeaders + strlen(preFlightRespHeaders));
    }

    // Add content type
    const char* pContentType = _pResponder->getContentType();
    if (_pResponder && pContentType)
    {
        const char* contentTypeStr = "Content-Type: ";
        headers.insert(headers.end(), contentTypeStr, contentTypeStr + strlen(contentTypeStr));
        headers.insert(headers.end(), pContentType, pContentType + strlen(pContentType));
        headers.insert(headers.end(), crlf, crlf + strlen(crlf));
    }

    // Add standard reponse headers
    if (_pConnManager)
    {
        const String& strRespHeaders = _pConnManager->getServerSettings().stdRespHeaders;
        headers.insert(headers.end(), strRespHeaders.c_str(), strRespHeaders.c_str() + strRespHeaders.length());
    }

    // Add additional headers
    if (_pResponder)
    {
        std::list<RaftJson::NameValuePair>* pRespHeaders = _pResponder->getHeaders();
        for (RaftJson::NameValuePair& nvPair : *pRespHeaders)
        {
            headers.insert(headers.end(), nvPair.name.c_str(), nvPair.name.c_str() + nvPair.name.length());
            headers.insert(headers.end(), colonSep, colonSep + strlen(colonSep));
            headers.insert(headers.end(), nvPair.value.c_str(), nvPair.value.c_str() + nvPair.value.length());
            headers.insert(headers.end(), crlf, crlf + strlen(crlf));
        }
    }

    // Add content length if required
    if (_pResponder)
    {
        int contentLength = _pResponder->getContentLength();
        if (contentLength >= 0)
        {
            String contentLengthStr = "Content-Length: " + String(contentLength) + crlf;
            headers.insert(headers.end(), contentLengthStr.c_str(), contentLengthStr.c_str() + contentLengthStr.length());
        }
    }

    // Check if connection needs closing
    if (!_pResponder || !_pResponder->leaveConnOpen())
    {
        const char* closeStr = "Connection: close\r\n";
        headers.insert(headers.end(), closeStr, closeStr + strlen(closeStr));
    }

    // End of headers
    headers.insert(headers.end(), crlf, crlf + strlen(crlf));

    // Ok
    return headers;
}

bool RaftWebConnection::sendStandardHeaders()
{
    // Get the headers
    SpiramAwareUint8Vector headers = getStandardHeaders();
    
    // Send the headers
    RaftWebConnSendRetVal rslt = rawSendOnConn(headers, MAX_HEADER_SEND_RETRY_MS);

    // Debug
#ifdef DEBUG_RESPONDER_HEADER
    LOG_I(MODULE_PREFIX, "sendStandardHeaders connId %d rslt %s len %d", 
                    _pClientConn ? _pClientConn->getClientId() : 0, 
                    RaftWebConnDefs::getSendRetValStr(rslt),
                    headers.size());
#endif
#ifdef DEBUG_RESPONDER_HEADER_DETAIL
    LOG_I(MODULE_PREFIX, "sendStandardHeaders connId %d rslt %s headers %s", 
                    _pClientConn ? _pClientConn->getClientId() : 0, 
                    RaftWebConnDefs::getSendRetValStr(rslt), 
                    headers.size());
#endif

    // Ok
    return rslt == WEB_CONN_SEND_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle next chunk of response
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebConnection::handleResponseChunk()
{
    // Check if there is anything to do
    if (!_pResponder || !((_isStdHeaderRequired && _pResponder->isStdHeaderRequired()) || _pResponder->responseAvailable()))
        return true;

#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
    uint64_t debugHdlRespChunkStartUs = micros();
    uint64_t debugCanSendOnConnStartUs = micros();
#endif

    // Check if the connection is busy
    RaftWebConnSendRetVal sendBusyRetVal = canSendOnConn();
    if (sendBusyRetVal == WEB_CONN_SEND_EAGAIN)
        return true;

    // Check if connection is closed
    if (sendBusyRetVal != WEB_CONN_SEND_OK)
    {
        // Debug
#ifdef DEBUG_WEB_CONN_OPEN_CLOSE
        LOG_I(MODULE_PREFIX, "handleResponseChunk conn closed connId %d", _pClientConn ? _pClientConn->getClientId() : 0);
#endif
        return false;
    }

#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
    uint32_t debugCanSendOnConnUs = micros() - debugCanSendOnConnStartUs;
    uint64_t debugSendStdHdrsStartUs = micros();
#endif

    // Check if standard reponse to be sent first
    if (_isStdHeaderRequired && _pResponder->isStdHeaderRequired())
    {
        // Send standard headers
        if (!sendStandardHeaders())
        {
        // Debug
#ifdef DEBUG_RESPONDER_HEADER
            LOG_I(MODULE_PREFIX, "handleResponseChunk sendStandardHeaders failed connId %d", _pClientConn ? _pClientConn->getClientId() : 0);
#endif
            return false;
        }

        // Done headers
        _isStdHeaderRequired = false;
    }

#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
    uint32_t debugSendStdHdrsUs = micros() - debugSendStdHdrsStartUs;
    uint64_t debugGetRespNextStartUs = micros();
    uint32_t debugGetRespNextUs = 0;
    uint32_t debugRawSendOnConnUs = 0;
#endif

    // Check if data waiting to be sent
    if (_socketTxQueuedBuffer.size() == 0)
    {
#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
        debugGetRespNextUs = micros() - debugGetRespNextStartUs;
        uint64_t debugRawSendOnConnStartUs = micros();
#endif

        // Get next chunk of response
        SpiramAwareUint8Vector respBuffer = _pResponder->getResponseNext(_maxSendBufferBytes);
        if (respBuffer.size() > 0)
        {

            // Send
            RaftWebConnSendRetVal retVal = rawSendOnConn(respBuffer, MAX_CONTENT_SEND_RETRY_MS);

            // Debug
#ifdef DEBUG_RESPONDER_CONTENT_DETAIL
            LOG_I(MODULE_PREFIX, "handleResponseChunk writing %d retVal %s connId %d", 
                        respSize, RaftWebConnDefs::getSendRetValStr(retVal), _pClientConn ? _pClientConn->getClientId() : 0);
#endif

            // Handle failure
            if (retVal != WEB_CONN_SEND_OK)
            {
#ifdef DEBUG_RESPONDER_FAILURE
                LOG_I(MODULE_PREFIX, "handleResponseChunk failed retVal %s connId %d", 
                        RaftWebConnDefs::getSendRetValStr(retVal), _pClientConn ? _pClientConn->getClientId() : 0);
#endif
                if (retVal != WEB_CONN_SEND_EAGAIN)
                    return false;
            }
        }

#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
        debugRawSendOnConnUs = micros() - debugRawSendOnConnStartUs;
#endif
    }

#ifdef DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS
    uint64_t timeNowUs = micros();
    uint32_t debugHdlRespChunkUs = timeNowUs - debugHdlRespChunkStartUs;
    if (debugHdlRespChunkUs > DEBUG_WEB_RESPONDER_HDL_CHUNK_THRESH_MS)
    {
        LOG_I(MODULE_PREFIX, "handleResponseChunk conn %d resp %s dataAvail %s hdrReqd %s total %dus canSendOnConn %dus sendStdHdrs %dus getRespNext %dus rawSendOnConn %dus",
                _pClientConn ? _pClientConn->getClientId() : 0,
                _pResponder ? _pResponder->getResponderType() : "",
                _pResponder ? (_pResponder->responseAvailable() ? "Y" : "N") : "N/A",
                _isStdHeaderRequired ? "Y" : "N",
                debugHdlRespChunkUs,
                debugCanSendOnConnUs,
                debugSendStdHdrsUs,
                debugGetRespNextUs,
                debugRawSendOnConnUs);
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
        uint32_t bytesWritten = 0;
        RaftWebConnSendRetVal retVal = _pClientConn->sendDataBuffer(_socketTxQueuedBuffer, 
                        MAX_CONTENT_SEND_RETRY_MS, bytesWritten);
        if (retVal == WEB_CONN_SEND_EAGAIN)
            return true;
#ifdef DEBUG_WEB_CONNECTION_DATA_PACKETS
        LOG_I(MODULE_PREFIX, "handleTxQueuedData connId %d result %s bytesWritten %d remaining %d", 
                        _pClientConn->getClientId(), RaftWebConnDefs::getSendRetValStr(retVal), bytesWritten, 
                        _socketTxQueuedBuffer.size()-bytesWritten);
#endif
        if (retVal == WEB_CONN_SEND_FAIL)
        {
            // Clear the send buffer
            _socketTxQueuedBuffer.clear();
            return false;
        }
        
        // Sent ok so clear items from the queue that were sent
        _socketTxQueuedBuffer.erase(_socketTxQueuedBuffer.begin(), _socketTxQueuedBuffer.begin() + bytesWritten);
    }
    return true;
}
