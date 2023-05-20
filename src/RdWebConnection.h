/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ArduinoOrAlt.h>
#include "RdWebConnDefs.h"
#include "RdWebRequestParams.h"
#include "RdWebRequestHeader.h"
#include "RdClientConnBase.h"

// #define DEBUG_TRACE_HEAP_USAGE_WEB_CONN

class RdWebHandler;
class RdWebConnManager;
class RdWebResponder;

class RdWebConnection
{
public:
    RdWebConnection();
    virtual ~RdWebConnection();

    // Called frequently
    void service();

    // Send on connection
    bool sendOnConn(const uint8_t* pBuf, uint32_t bufLen);

    // Send on server-side events
    void sendOnSSEvents(const char* eventContent, const char* eventGroup);

    // Clear (closes connection if open)
    void clear();

    // Set a new connection
    bool setNewConn(RdClientConnBase* pClientConn, RdWebConnManager* pConnManager,
                uint32_t maxSendBufferBytes);

    // True if active
    bool isActive();

    // Get header
    RdWebRequestHeader& getHeader()
    {
        return _header;
    }

    // Get responder
    RdWebResponder* getResponder()
    {
        return _pResponder;
    }

private:
    // Connection manager
    RdWebConnManager* _pConnManager;

    // Client connection
    RdClientConnBase* _pClientConn;
    static const bool USE_BLOCKING_WEB_CONNECTIONS = false;

    // Header parse info
    String _parseHeaderStr;

    // Header contents
    RdWebRequestHeader _header;

    // Responder
    RdWebResponder* _pResponder;

    // Send headers if needed
    bool _isStdHeaderRequired;
    bool _sendSpecificHeaders;

    // Response code if no responder available
    RdHttpStatusCode _httpResponseStatus;

    // Timeout timer
    static const uint32_t MAX_STD_CONN_DURATION_MS = 60 * 60 * 1000;
    static const uint32_t MAX_CONN_IDLE_DURATION_MS = 60 * 1000;
    static const uint32_t MAX_HEADER_SEND_RETRY_MS = 10;
    static const uint32_t MAX_CONTENT_SEND_RETRY_MS = 0;
    uint32_t _timeoutStartMs;
    uint32_t _timeoutDurationMs;
    uint32_t _timeoutLastActivityMs;
    uint32_t _timeoutOnIdleDurationMs;
    bool _timeoutActive;

    // Responder/connection clear pending
    bool _isClearPending;
    uint32_t _clearPendingStartMs;
    static const uint32_t CONNECTION_CLEAR_PENDING_TIME_MS = 0;

    // Max send buffer size
    uint32_t _maxSendBufferBytes;

    // Queued data to send
    std::vector<uint8_t> _socketTxQueuedBuffer;

    // Debug
    uint32_t _debugDataRxCount;

    // Handle header data
    bool handleHeaderData(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos);

    // Parse a line of header section (including request line)
    // Returns false on failure
    bool parseHeaderLine(const String& line);

    // Parse the first line of HTTP request
    // Returns false on failure
    bool parseRequestLine(const String& line);

    // Parse name/value pairs in header line
    void parseNameValueLine(const String& reqLine);

    // Decode URL
    String decodeURL(const String &inURL) const;

    // Select handler
    void selectHandler();

    // Service connection header
    bool serviceConnHeader(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos);

    // Send data to responder
    bool responderHandleData(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos);

    // Set HTTP response status
    void setHTTPResponseStatus(RdHttpStatusCode reponseCode);

    // Raw send on connection - used by websockets, etc
    RdWebConnSendRetVal rawSendOnConn(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs);    

    // Send standard headers
    bool sendStandardHeaders();

    // Handle next chunk of response
    bool handleResponseChunk();

    // Handle sending queued data
    bool handleTxQueuedData();

    // Clear the responder and connection after send completion
    void clearAfterSendCompletion();
};
