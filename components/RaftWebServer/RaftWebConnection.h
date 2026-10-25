/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "RaftWebConnDefs.h"
#include "RaftWebRequestParams.h"
#include "RaftWebRequestHeader.h"
#include "RaftClientConnBase.h"

// #define DEBUG_TRACE_HEAP_USAGE_WEB_CONN

class RaftWebHandler;
class RaftWebConnManager;
class RaftWebResponder;

class RaftWebConnection
{
public:
    RaftWebConnection();
    virtual ~RaftWebConnection();

    // Called frequently
    void loop();

    // Check if we can send
    RaftWebConnSendRetVal canSendOnConn();

    // Send on server-side events
    void sendOnSSEvents(const char* eventContent, const char* eventGroup);

    // Clear (closes connection if open)
    void clear();

    // Set a new connection
    bool setNewConn(RaftClientConnBase* pClientConn, RaftWebConnManager* pConnManager,
                uint32_t maxSendBufferBytes, uint32_t clearPendingDurationMs);

    // True if active
    bool isActive();

    // Get header
    RaftWebRequestHeader& getHeader()
    {
        return _header;
    }

    // Get responder
    RaftWebResponder* getResponder()
    {
        return _pResponder;
    }

private:
    // Connection manager
    RaftWebConnManager* _pConnManager;

    // Client connection
    RaftClientConnBase* _pClientConn;
    static const bool USE_BLOCKING_WEB_CONNECTIONS = false;

    // Header parse info
    String _parseHeaderStr;

    // Header contents
    RaftWebRequestHeader _header;

    // Responder
    RaftWebResponder* _pResponder;

    // Send headers if needed
    bool _isStdHeaderRequired;
    bool _sendSpecificHeaders;

    // Response code if no responder available
    RaftHttpStatusCode _httpResponseStatus;

    // Timeout timer
    static const uint32_t MAX_STD_CONN_DURATION_MS = 5 * 60 * 1000;
    // OTA flash erases can disable the cache and stall the web server task for
    // 6-8+ seconds. The idle timeout must comfortably exceed that worst case.
    static const uint32_t MAX_CONN_IDLE_DURATION_MS = 30 * 1000;
    static const uint32_t MAX_HEADER_SEND_RETRY_MS = 10;
    static const uint32_t MAX_CONTENT_SEND_RETRY_MS = 0;
    // A gap between service-loop iterations longer than this means the whole system
    // was blocked (e.g. a flash erase during OTA disables the cache and starves all
    // tasks). When this happens the inactivity (idle) timeout must not be applied as
    // the stall is the server's fault, not an idle client.
    static const uint32_t CONN_SERVICE_STALL_THRESHOLD_MS = 1000;
    uint32_t _timeoutStartMs;
    uint32_t _timeoutDurationMs;
    uint32_t _timeoutLastActivityMs;
    uint32_t _timeoutOnIdleDurationMs;
    uint32_t _lastLoopServiceMs = 0;
    bool _timeoutActive;

    // Responder/connection clear pending
    bool _isClearPending;
    uint32_t _clearPendingStartMs;
    uint32_t _clearPendingDurationMs = 0;

    // Max send buffer size
    uint32_t _maxSendBufferBytes;

    // Queued data to send, from _socketTxQueuedPos (data before it has been sent - it is only removed when more is
    // added, so sending from the queue doesn't move what is left each time)
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> _socketTxQueuedBuffer;
    uint32_t _socketTxQueuedPos = 0;
    uint32_t txQueuedLen() const
    {
        return _socketTxQueuedBuffer.size() - _socketTxQueuedPos;
    }
    void txQueueClear()
    {
        _socketTxQueuedBuffer.clear();
        _socketTxQueuedPos = 0;
    }

    // Heap to leave free (for the network stack) when growing the send buffer
    static const uint32_t SEND_BUFFER_HEAP_MARGIN_BYTES = 8192;

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
    bool responderHandleData(const uint8_t* pRxData, uint32_t dataLen, uint32_t& curBufPos, bool doRespond);

    // Set HTTP response status
    void setHTTPResponseStatus(RaftHttpStatusCode reponseCode);

    // Raw send on connection - used by websockets, etc
    RaftWebConnSendRetVal rawSendOnConn(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs);
    RaftWebConnSendRetVal rawSendOnConnTake(SpiramAwareUint8Vector& buf, uint32_t maxRetryMs);
    RaftWebConnSendRetVal rawSendOnConnImpl(const uint8_t* pBuf, uint32_t bufLen, uint32_t maxRetryMs,
                SpiramAwareUint8Vector* pTakeBuf);    

    // Header handling
    bool getStandardHeaders(String& headerStr);
    bool sendStandardHeaders();

    // Handle next chunk of response
    bool handleResponseChunk();

    // Handle sending queued data
    bool handleTxQueuedData();

    // Clear the responder and connection after send completion
    void clearAfterSendCompletion();
};
