/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdint.h>
#include <vector>
#include <algorithm>
#include "Logger.h"
#include "RaftWebSocketLink.h"
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "ArduinoTime.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#include "psa/crypto.h"
#else
#include "mbedtls/sha1.h"
#endif
#include "mbedtls/base64.h"
#include "RaftWebConnection.h"
#include "PlatformUtils.h"

#ifdef ESP_PLATFORM
#include "esp_system.h"
#else
#include <cstdlib>
#include <ctime>
// Portable random function for non-ESP platforms
static uint32_t portable_random() {
    static bool initialized = false;
    if (!initialized) {
        srand(time(nullptr));
        initialized = true;
    }
    return (rand() << 16) | rand();
}
#define esp_random portable_random
#endif

static const char *MODULE_PREFIX = "RaftWSLink";

// Warn
#define WARN_WEBSOCKET_EXCESS_DATA_AFTER_UPGRADE
#define WARN_WEBSOCKET_DATA_DISCARD_AS_EXCEEDS_MSG_SIZE
#define WARN_ON_WS_LINK_SEND_TOO_LONG

// Debug
// #define DEBUG_WEBSOCKET_CLOSE_COMMAND
// #define DEBUG_WEBSOCKET_LINK
// #define DEBUG_WEBSOCKET_LINK_SEND
// #define DEBUG_WEBSOCKET_LINK_EVENTS
// #define DEBUG_WEBSOCKET_PING_PONG
// #define DEBUG_WEBSOCKET_LINK_HEADER_DETAIL
// #define DEBUG_WEBSOCKET_LINK_DATA_STR
// #define DEBUG_WEBSOCKET_LINK_DATA_BINARY
// #define DEBUG_WEBSOCKET_SEND
// #define DEBUG_WEBSOCKET_DATA_BUFFERING
// #define DEBUG_WEBSOCKET_DATA_BUFFERING_CONTENT
// #define DEBUG_WEBSOCKET_DATA_PROCESSING
// #define DEBUG_WEBSOCKET_RX_DETAIL
// #define DEBUG_WEBSOCKET_TIME_SEND_MSG
// #define DEBUG_WEBSOCKET_HANDSHAKE
// #define DEBUG_WEBSOCKET_TX_AVAILABLE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebSocketLink::RaftWebSocketLink()
{
}

RaftWebSocketLink::~RaftWebSocketLink()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup the web socket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebSocketLink::setup(RaftWebSocketCB webSocketCB, RaftWebConnSendFn rawConnSendFn,
                uint32_t pingIntervalMs, bool roleIsServer, uint32_t disconnIfNoPongMs,
                bool isBinary)
{
#ifdef DEBUG_WEBSOCKET_HANDSHAKE
    LOG_I(MODULE_PREFIX, "setup roleIsServer=%d upgradeRespSent was=%d (resetting to false)",
            roleIsServer, _upgradeRespSent);
#endif
    
    _webSocketCB = webSocketCB;
    _rawConnSendFn = rawConnSendFn;
    _pingIntervalMs = pingIntervalMs;
    _pingTimeLastMs = 0;
    _pongRxLastMs = 0;
    _disconnIfNoPongMs = disconnIfNoPongMs;
    _maskSentData = !roleIsServer;
    _isActive = true;
    _defaultContentOpCode = isBinary ? WEBSOCKET_OPCODE_BINARY : WEBSOCKET_OPCODE_TEXT;
    // Reset upgrade response flag for new connection
    _upgradeRespSent = false;
    _upgradeReqReceived = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Make room in a receive buffer for newSize bytes - an allocation failure can't be detected afterwards (exceptions
// are disabled) and would crash, so returns false if there isn't the memory (the message is then dropped). The
// buffer grows geometrically (so a long message isn't copied again for every packet), up to the longest message,
// or to just what is needed when memory is short
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebSocketLink::reserveRxBuffer(SpiramAwareUint8Vector& buf, uint32_t newSize)
{
    if (newSize <= buf.capacity())
        return true;
    uint32_t maxAllocatable = SpiramAwareAllocator<uint8_t>::max_allocatable();
    uint32_t newCapacity = std::max(newSize, std::min((uint32_t)buf.capacity() * 2, _maxRxMsgLen + WEBSOCKET_MAX_HEADER_LEN));
    if (newCapacity + RX_BUFFER_HEAP_MARGIN_BYTES > maxAllocatable)
        newCapacity = newSize;
    if (newCapacity + RX_BUFFER_HEAP_MARGIN_BYTES > maxAllocatable)
        return false;
    buf.reserve(newCapacity);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service the web socket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebSocketLink::loop()
{
    static const uint8_t PING_MSG[] = "RAFT";
    // Handle ping / pong
    if (_upgradeRespSent && _pingIntervalMs != 0)
    {
        // Check if time to send ping
        if (Raft::isTimeout(millis(), _pingTimeLastMs, _pingIntervalMs))
        {
#ifdef DEBUG_WEBSOCKET_PING_PONG
            LOG_I(MODULE_PREFIX, "PING");
#endif
            sendMsg(WEBSOCKET_OPCODE_PING, PING_MSG, sizeof(PING_MSG));
            _pingTimeLastMs = millis();
        }

        // Check for disconnect on no pong - this intentionally only starts working after
        // a first pong has been received - this is because older martypy versions did not
        // correctly handle the pong response
        if ((_disconnIfNoPongMs != 0) && (_pongRxLastMs != 0) &&
                 Raft::isTimeout(millis(), _pongRxLastMs, _disconnIfNoPongMs))
        {
            if (!_warnNoPongShown)
            {
                LOG_W(MODULE_PREFIX, "loop - no PONG received for %dms (>%dms), link inactive",
                        (int)Raft::timeElapsed(millis(), (int)_pongRxLastMs),
                        _disconnIfNoPongMs);
                _warnNoPongShown = true;
            }
            _isActive = false;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Upgrade the link - explicitly assume request header received
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebSocketLink::upgradeReceived(const String &wsKey, const String &wsVersion)
{
    _upgradeReqReceived = true;
    _wsKey = wsKey;
    _wsVersion = wsVersion;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle incoming data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebSocketLink::handleRxData(const uint8_t *pBuf, uint32_t bufLen)
{
    static const uint8_t UPGRADE_REQ_TEXT[] = "Upgrade: websocket\r\n";
    static const uint8_t UPGRADE_REQ_KEY[] = "Sec-WebSocket-Key: ";
    static const uint8_t HTTP_EOL_STR[] = "\r\n";
    // If we don't yet have an upgrade request
    if (!_upgradeReqReceived)
    {
        // Check for header
        if (Raft::findInBuf(pBuf, bufLen, UPGRADE_REQ_TEXT, sizeof(UPGRADE_REQ_TEXT)) < 0)
            return;

        // Check for upgrade key
        int keyPos = Raft::findInBuf(pBuf, bufLen, UPGRADE_REQ_KEY, sizeof(UPGRADE_REQ_KEY));
        if (keyPos < 0)
            return;
        keyPos += sizeof(UPGRADE_REQ_TEXT);

        // Find key length
        int keyLen = Raft::findInBuf(pBuf + keyPos, bufLen - keyPos, HTTP_EOL_STR, sizeof(HTTP_EOL_STR));
        if (keyLen < 0)
            return;

        // Extract key
        _wsKey = String(pBuf + keyPos, keyLen);
        _upgradeReqReceived = true;

        // Continue with any excess data
        if (keyPos + keyLen >= bufLen)
            return;
        pBuf += keyPos + keyLen;
        bufLen -= (keyPos + keyLen);

#ifdef WARN_WEBSOCKET_EXCESS_DATA_AFTER_UPGRADE
        LOG_W(MODULE_PREFIX, "handleRxData excessDataAfter ws upgrade len %d", bufLen);
#endif
    }

    // Check if any data already received
    if (_rxDataToProcess.size() > 0)
    {
        // The data stashed is at most one frame (a longer frame closes the link) and what has arrived since
        if ((_rxDataToProcess.size() > _maxRxMsgLen + WEBSOCKET_MAX_HEADER_LEN) ||
                !reserveRxBuffer(_rxDataToProcess, _rxDataToProcess.size() + bufLen))
        {
#ifdef WARN_WEBSOCKET_DATA_DISCARD_AS_EXCEEDS_MSG_SIZE
        LOG_W(MODULE_PREFIX, "handleRxData discard as exceeds max stashed %d len %d max %d", 
                (int)_rxDataToProcess.size(), bufLen, _maxRxMsgLen);
#endif
            _rxDataToProcess.clear();
        }
        else
        {
#ifdef DEBUG_WEBSOCKET_DATA_BUFFERING
        LOG_I(MODULE_PREFIX, "handleRxData adding stashedLen %d len %d", 
                _rxDataToProcess.size(), bufLen);
#endif
#ifdef DEBUG_WEBSOCKET_DATA_BUFFERING_CONTENT
            String prevResidual;
            Raft::getHexStrFromBytes(_rxDataToProcess.data(), 
                    _rxDataToProcess.size() < MAX_DEBUG_BIN_HEX_LEN ? _rxDataToProcess.size() : MAX_DEBUG_BIN_HEX_LEN,
                    prevResidual);
            LOG_I(MODULE_PREFIX, "handleRxData prevResidual len %d data %s%s", 
                    _rxDataToProcess.size(), prevResidual.c_str(),
                    _rxDataToProcess.size() < MAX_DEBUG_BIN_HEX_LEN ? "" : "...");
#endif
            //_rxDataToProcess.insert(_rxDataToProcess.end(), pBuf, pBuf+bufLen);
            uint32_t curSize = _rxDataToProcess.size();
            _rxDataToProcess.resize(curSize + bufLen);
            memcpy(_rxDataToProcess.data()+curSize, pBuf, bufLen);
            pBuf = _rxDataToProcess.data();
            bufLen = _rxDataToProcess.size();
#ifdef DEBUG_WEBSOCKET_DATA_BUFFERING_CONTENT
            String dataToProc;
            Raft::getHexStrFromBytes(_rxDataToProcess.data(), 
                    _rxDataToProcess.size() < MAX_DEBUG_BIN_HEX_LEN ? _rxDataToProcess.size() : MAX_DEBUG_BIN_HEX_LEN, 
                    dataToProc);
            LOG_I(MODULE_PREFIX, "handleRxData agg dataToProc len %d data %s%s", 
                    _rxDataToProcess.size(), dataToProc.c_str(),
                    _rxDataToProcess.size() < MAX_DEBUG_BIN_HEX_LEN ? "" : "...");
#endif
        }
    }

    // Handle packets
    while(bufLen > 0)
    {
        int32_t dataConsumed = handleRxPacketData(pBuf, bufLen);
#ifdef DEBUG_WEBSOCKET_DATA_PROCESSING
        LOG_I(MODULE_PREFIX, "handleRxData consumed %d bufLen %d", dataConsumed, bufLen);
#endif
        if (dataConsumed == 0)
        {
            // Store residual data - if it is already in the stash, the data processed is removed from the front
            // (without copying it all to a temporary, which could be a large allocation)
            const uint8_t* pStash = _rxDataToProcess.data();
            if (!_rxDataToProcess.empty() && (pBuf >= pStash) && (pBuf < pStash + _rxDataToProcess.size()))
                _rxDataToProcess.erase(_rxDataToProcess.begin(), _rxDataToProcess.begin() + (pBuf - pStash));
            else
                _rxDataToProcess.assign(pBuf, pBuf+bufLen);
#ifdef DEBUG_WEBSOCKET_DATA_BUFFERING
            LOG_I(MODULE_PREFIX, "handleRxData storing residual now stashed %d", _rxDataToProcess.size());
#endif
#ifdef DEBUG_WEBSOCKET_DATA_BUFFERING_CONTENT
            String residualFinalStr;
            Raft::getHexStrFromBytes(_rxDataToProcess.data(), 
                    _rxDataToProcess.size() < MAX_DEBUG_BIN_HEX_LEN ? _rxDataToProcess.size() : MAX_DEBUG_BIN_HEX_LEN,
                    residualFinalStr);
            LOG_I(MODULE_PREFIX, "handleRxData residual len %d data %s%s", 
                    _rxDataToProcess.size(), residualFinalStr.c_str(),
                    _rxDataToProcess.size() < MAX_DEBUG_BIN_HEX_LEN ? "" : "...");
#endif
            break;
        }
        if (dataConsumed >= bufLen)
        {
            // Clear any residual data
            _rxDataToProcess.clear();
            break;
        }
        pBuf += dataConsumed;
        bufLen -= dataConsumed;
#ifdef DEBUG_WEBSOCKET_DATA_BUFFERING_CONTENT
        String nextDataStr;
        Raft::getHexStrFromBytes(pBuf, bufLen < MAX_DEBUG_BIN_HEX_LEN ? bufLen : MAX_DEBUG_BIN_HEX_LEN, nextDataStr);
        LOG_I(MODULE_PREFIX, "handleRxData nextData len %d data %s%s", bufLen, nextDataStr.c_str(),
                bufLen < MAX_DEBUG_BIN_HEX_LEN ? "" : "...");
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if tx data is available
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebSocketLink::isTxDataAvailable()
{
    bool result = _upgradeReqReceived && !_upgradeRespSent;
#ifdef DEBUG_WEBSOCKET_TX_AVAILABLE
    LOG_I(MODULE_PREFIX, "isTxDataAvailable reqReceived=%d respSent=%d result=%d",
            _upgradeReqReceived, _upgradeRespSent, result);
#endif
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get data to tx
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebSocketLink::getTxData(uint8_t*& pBuf, uint32_t bufMaxLen)
{
#ifdef DEBUG_WEBSOCKET_HANDSHAKE
    // Debug logging for troubleshooting reconnection issues
    LOG_I(MODULE_PREFIX, "getTxData upgradeRespSent=%d upgradeReqReceived=%d",
            _upgradeRespSent, _upgradeReqReceived);
#endif
    
    // Check if upgrade received but no response yet sent
    if (!_upgradeRespSent && _upgradeReqReceived)
    {
        // No longer need to send
        _upgradeRespSent = true;

        // Make sure we don't PING too early
        _pingTimeLastMs = millis();

        // Form the upgrade response
        _wsUpgradeResponse = formUpgradeResponse(_wsKey, _wsVersion, bufMaxLen);
#ifdef DEBUG_WEBSOCKET_HANDSHAKE
        LOG_I(MODULE_PREFIX, "getTxData sending upgrade response length=%d", _wsUpgradeResponse.length());
#endif
        pBuf = (uint8_t*)_wsUpgradeResponse.c_str();
        return _wsUpgradeResponse.length();
    }

    // Other comms on the link is done through _rawConnSendFn
    _wsUpgradeResponse.clear();
#ifdef DEBUG_WEBSOCKET_HANDSHAKE
    LOG_I(MODULE_PREFIX, "getTxData no data to send (upgrade already sent or not received)");
#endif
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnSendRetVal RaftWebSocketLink::sendMsg(RaftWebSocketOpCodes opCode, const uint8_t *pBuf, uint32_t bufLen)
{
#ifdef DEBUG_WEBSOCKET_SEND
    // Log all sends to debug reconnection issue
    static const char* opCodeNames[] = {"CONT", "TEXT", "BIN", "?", "?", "?", "?", "?", "CLOSE", "PING", "PONG"};
    const char* opName = (opCode < 11) ? opCodeNames[opCode] : "UNKNOWN";
    LOG_I(MODULE_PREFIX, "sendMsg opCode=%s(%d) len=%d upgradeRespSent=%d", opName, opCode, bufLen, _upgradeRespSent);
#endif

#ifdef DEBUG_WEBSOCKET_TIME_SEND_MSG
    static uint64_t framePrepareUs = 0;
    static uint64_t rawConnSendUs = 0;
    static uint32_t lastReportMs = 0;
    static uint32_t callCount = 0;
    uint64_t startUs = micros();
#endif
    // Get length of frame
    uint32_t frameLen = bufLen + 2;
    uint32_t hdrLenCode = bufLen;
    if (bufLen > 125)
    {
        frameLen += 2;
        hdrLenCode = 126;
    }
    if (bufLen > 65535)
    {
        frameLen += 6;
        hdrLenCode = 127;
    }
    if (_maskSentData)
        frameLen += 4;

    // Check valid
    if (frameLen >= MAX_WS_MESSAGE_SIZE)
    {
#ifdef WARN_ON_WS_LINK_SEND_TOO_LONG
        LOG_W(MODULE_PREFIX, "sendMsg too long %d > %d (bufLen %d)", frameLen, MAX_WS_MESSAGE_SIZE, bufLen);
#endif
        return WEB_CONN_SEND_TOO_LONG;
    }

    // Header (at most 2 + 8 length + 4 mask bytes)
    uint8_t header[2 + 8 + WSHeaderInfo::WEB_SOCKET_MASK_KEY_BYTES];
    header[0] = 0x80 | opCode;
    header[1] = (_maskSentData ? 0x80 : 0) | hdrLenCode;

    // Length
    uint32_t pos = 2;
    if (hdrLenCode == 126)
    {
        header[pos++] = bufLen / 256;
        header[pos++] = bufLen % 256;
    }
    else if (hdrLenCode == 127)
    {
        for (int i = 0; i < 4; i++)
            header[pos++] = 0;
        for (int i = 3; i >= 0; i--)
            header[pos++] = (bufLen >> (i * 8)) & 0xff;
    }

    // Generate a random mask if required
    uint8_t maskBytes[WSHeaderInfo::WEB_SOCKET_MASK_KEY_BYTES] = {0, 0, 0, 0};
    if (_maskSentData)
    {
        uint32_t maskKey = platform_random();
        if (maskKey == 0)
            maskKey = 0x55555555;
        for (int i = 0; i < WSHeaderInfo::WEB_SOCKET_MASK_KEY_BYTES; i++)
        {
            maskBytes[i] = (maskKey >> ((3 - i) * 8)) & 0xff;
            header[pos++] = maskBytes[i];
        }
    }

    // Sanity check
    if (pos + bufLen != frameLen)
    {
        LOG_W(MODULE_PREFIX, "sendMsg something awry with frameLen %d + %d != %d", 
                    (int)pos, (int)bufLen, (int)frameLen);
        return WEB_CONN_SEND_FRAME_ERROR;
    }

    // Unmasked data (sent by a server) is sent as it is, after the header - without copying it into a frame first.
    // If the connection can't take all of it, what is left is queued in order by the connection, and if it can't be
    // queued the send fails and the connection is closed, so a frame is never sent in part
#ifdef DEBUG_WEBSOCKET_LINK_SEND
    uint64_t timeNowUs = micros();
#endif
    RaftWebConnSendRetVal sendRetc = RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    if (!_maskSentData)
    {
#ifdef DEBUG_WEBSOCKET_TIME_SEND_MSG
        framePrepareUs += (micros() - startUs);
        startUs = micros();
#endif
        if (_rawConnSendFn)
        {
            sendRetc = _rawConnSendFn(header, pos, MAX_WS_SEND_RETRY_MS);
            if ((sendRetc == RaftWebConnSendRetVal::WEB_CONN_SEND_OK) && (bufLen > 0))
            {
                sendRetc = _rawConnSendFn(pBuf, bufLen, MAX_WS_SEND_RETRY_MS);

                // The header has gone, so the frame must be completed or the connection closed
                if (sendRetc != RaftWebConnSendRetVal::WEB_CONN_SEND_OK)
                    sendRetc = RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
            }
        }
    }
    else
    {
        // Masked data (sent by a client): header then data, appended to reserved space (it isn't zero-filled first,
        // so large messages are written once rather than twice)
        std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> frameBuffer;
        frameBuffer.reserve(frameLen);
        frameBuffer.insert(frameBuffer.end(), header, header + pos);
        frameBuffer.insert(frameBuffer.end(), pBuf, pBuf + bufLen);
        for (uint32_t i = 0; i < bufLen; i++)
            frameBuffer[pos + i] ^= maskBytes[i % WSHeaderInfo::WEB_SOCKET_MASK_KEY_BYTES];

#ifdef DEBUG_WEBSOCKET_TIME_SEND_MSG
        framePrepareUs += (micros() - startUs);
        startUs = micros();
#endif
        if (_rawConnSendFn)
            sendRetc = _rawConnSendFn(frameBuffer.data(), frameBuffer.size(), MAX_WS_SEND_RETRY_MS);
    }

#ifdef DEBUG_WEBSOCKET_TIME_SEND_MSG
    rawConnSendUs += (micros() - startUs);
    callCount++;

    // Report every 5 seconds
    uint32_t nowMs = millis();
    if (nowMs - lastReportMs > 5000)
    {
        LOG_I(MODULE_PREFIX, "sendMsg timing (us): framePrepare=%dus rawConnSend=%dus calls=%d avgRawSend=%dus",
            (int)framePrepareUs, (int)rawConnSendUs, callCount,
            callCount > 0 ? (int)(rawConnSendUs / callCount) : 0);
        framePrepareUs = 0;
        rawConnSendUs = 0;
        callCount = 0;
        lastReportMs = nowMs;
    }
#endif

#ifdef DEBUG_WEBSOCKET_LINK_SEND
    uint64_t timeTakenUs = micros() - timeNowUs;
    LOG_I(MODULE_PREFIX, "sendMsg result %s send %d bytes took %dus", 
            RaftWebConnDefs::getSendRetValStr(sendRetc), 
            frameLen, (int)timeTakenUs);
#endif

    return sendRetc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Form response to upgrade connection
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftWebSocketLink::formUpgradeResponse(const String &wsKey, const String &wsVersion, uint32_t bufMaxLen)
{
    // Response with magic code
    String respStr =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        genMagicResponse(wsKey, wsVersion) + 
        "\r\n\r\n";

    // Debug
#ifdef DEBUG_WEBSOCKET_LINK
    LOG_I(MODULE_PREFIX, "formUpgradeResponse resp %s", respStr.c_str());
#endif

    // Return
    return respStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Gen the hash required for response
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftWebSocketLink::genMagicResponse(const String &wsKey, const String &wsVersion)
{
    // Standard hash
    static const char WEB_SOCKET_HASH[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // Concatenated key
    String concatKey = wsKey + WEB_SOCKET_HASH;

    // Use MBED TLS to perform SHA1 step
    static const uint32_t SHA1_RESULT_LEN = 20;
    uint8_t sha1Result[SHA1_RESULT_LEN];
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    size_t hashLen = 0;
    psa_hash_compute(PSA_ALG_SHA_1, (const uint8_t *)concatKey.c_str(), concatKey.length(),
                     sha1Result, SHA1_RESULT_LEN, &hashLen);
#else
    mbedtls_sha1((uint8_t *)concatKey.c_str(), concatKey.length(), sha1Result);
#endif

    // Base64 result can't be larger than 2x the input
    char base64Result[SHA1_RESULT_LEN * 2];
    size_t outputLen = 0;
    mbedtls_base64_encode((uint8_t *)base64Result, sizeof(base64Result), &outputLen, sha1Result, SHA1_RESULT_LEN);

    // Terminate the string and return
    if (outputLen >= sizeof(base64Result))
        return "";
    base64Result[outputLen] = '\0';
    return base64Result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle received packet data
// Returns amount of data consumed (0 if header is not complete yet or not enough data for entire block)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebSocketLink::handleRxPacketData(const uint8_t *pBuf, uint32_t bufLen)
{
    // Nothing more is taken once the link is closed
    if (!_isActive)
        return bufLen;

    // Extract header
    extractWSHeaderInfo(pBuf, bufLen);
#ifdef DEBUG_WEBSOCKET_RX_DETAIL
    String outStr;
    Raft::getHexStrFromBytes(pBuf, bufLen, outStr);
    LOG_I(MODULE_PREFIX, "handleRxPacketData header len %lld dataPos %d bufLen %d data %s", 
                _wsHeader.len, _wsHeader.dataPos, bufLen, outStr.c_str());
#endif
    bool isControlFrame = (_wsHeader.opcode & 0x08) != 0;

    // A data frame longer than the largest message can't be taken, and the stream can't be followed past it
    // without storing it, so close the link (status 1009 - message too big)
    if ((_wsHeader.dataPos > 0) && !isControlFrame && (_wsHeader.len > _maxRxMsgLen))
    {
        LOG_W(MODULE_PREFIX, "handleRxPacketData frame len %lld > max %d - closing", _wsHeader.len, _maxRxMsgLen);
        uint8_t respCode[2] = {0x03, 0xf1};
        sendMsg(WEBSOCKET_OPCODE_CLOSE, respCode, sizeof(respCode));
        _isActive = false;
        _callbackData.clear();
        if (_webSocketCB)
            _webSocketCB(WEBSOCKET_EVENT_DISCONNECT_INTERNAL, nullptr, 0);
        return bufLen;
    }

    // Wait for the whole frame
    if ((_wsHeader.dataPos == 0) || (_wsHeader.dataPos + _wsHeader.len > bufLen))
        return 0;
    uint32_t frameLen = _wsHeader.len;
    uint32_t consumed = _wsHeader.dataPos + frameLen;

    // Data frames of a message being dropped (too big) are skipped up to its final frame - control frames can come
    // between them and are still handled
    if (_wsHeader.ignoreUntilFinal && !isControlFrame)
    {
        if (_wsHeader.fin)
            _wsHeader.ignoreUntilFinal = false;
        return consumed;
    }

    // Handle payload
    RaftWebSocketEventCode callbackEventCode = WEBSOCKET_EVENT_NONE;
    const uint8_t* pCallbackData = nullptr;
    uint32_t callbackDataLen = 0;
    uint8_t controlPayload[WEBSOCKET_MAX_CONTROL_PAYLOAD_LEN];
    switch (_wsHeader.opcode)
    {
        case WEBSOCKET_OPCODE_CONTINUE:
        case WEBSOCKET_OPCODE_BINARY:
        case WEBSOCKET_OPCODE_TEXT:
        {
            // A continuation adds to the message so far; any other data frame starts a new message
            uint32_t curBufSize = 0;
            if (_wsHeader.opcode == WEBSOCKET_OPCODE_CONTINUE)
                curBufSize = _callbackData.size();
            else
                _callbackData.clear();

            // Check we don't try to store too much
            bool tooLong = curBufSize + frameLen > _maxRxMsgLen;
            if (tooLong || !reserveRxBuffer(_callbackData, curBufSize + frameLen))
            {
                if (tooLong)
                {
                    LOG_W(MODULE_PREFIX, "handleRxPacketData msg > max %d", _maxRxMsgLen);
                }
                else
                {
                    LOG_W(MODULE_PREFIX, "handleRxPacketData msg len %d dropped - not enough memory (largest free block %d)",
                            curBufSize + frameLen, SpiramAwareAllocator<uint8_t>::max_allocatable());
                }
                _callbackData.clear();
                _wsHeader.ignoreUntilFinal = !_wsHeader.fin;
                return consumed;
            }

            // Add the frame's data to the message (each frame has its own mask key)
            _callbackData.resize(curBufSize + frameLen);
            unmaskData(pBuf + _wsHeader.dataPos, _callbackData.data() + curBufSize, frameLen);
            if (_wsHeader.fin)
            {
                callbackEventCode = _wsHeader.firstFrameOpcode == WEBSOCKET_OPCODE_TEXT ? WEBSOCKET_EVENT_TEXT : WEBSOCKET_EVENT_BINARY;
                pCallbackData = _callbackData.data();
                callbackDataLen = _callbackData.size();
            }
            break;
        }
        case WEBSOCKET_OPCODE_PING:
        {
            // Control frame payloads are at most 125 bytes
            if (frameLen > sizeof(controlPayload))
                break;
            unmaskData(pBuf + _wsHeader.dataPos, controlPayload, frameLen);
            callbackEventCode = WEBSOCKET_EVENT_PING;
            pCallbackData = controlPayload;
            callbackDataLen = frameLen;

            // Send PONG
            sendMsg(WEBSOCKET_OPCODE_PONG, controlPayload, frameLen);

#ifdef DEBUG_WEBSOCKET_PING_PONG
            LOG_I(MODULE_PREFIX, "handleRxPacketData Rx PING Tx PONG %d", frameLen);
#endif

            break;
        }
        case WEBSOCKET_OPCODE_PONG:
        {
            callbackEventCode = WEBSOCKET_EVENT_PONG;
            _pongRxLastMs = millis();
            _warnNoPongShown = false;

#ifdef DEBUG_WEBSOCKET_PING_PONG
            LOG_I(MODULE_PREFIX, "handleRxPacketData PONG");
#endif
            break;
        }
        case WEBSOCKET_OPCODE_CLOSE:
        {
            // Send CLOSE in response
            uint8_t respCode[2] = {0x03, 0xe8};
            sendMsg(WEBSOCKET_OPCODE_CLOSE, respCode, sizeof(respCode));
            callbackEventCode = WEBSOCKET_EVENT_DISCONNECT_EXTERNAL;
            _isActive = false;
            _callbackData.clear();
#ifdef DEBUG_WEBSOCKET_CLOSE_COMMAND
            LOG_W(MODULE_PREFIX, "handleRxPacketData rx CLOSE - now INACTIVE");
#endif
            break;
        }
    }

    // Check if we should do the callback
    if (callbackEventCode != WEBSOCKET_EVENT_NONE)
    {
#ifdef DEBUG_WEBSOCKET_LINK_EVENTS
        // Debug
        LOG_I(MODULE_PREFIX, "handleRxPacketData callback eventCode %s len %d", getEventStr(callbackEventCode), callbackDataLen);
#endif

        // Callback
        if (_webSocketCB)
        {
#ifdef DEBUG_WEBSOCKET_LINK_DATA_STR
            String cbStr((const char*)pCallbackData, 
                        callbackDataLen < MAX_DEBUG_TEXT_STR_LEN ? callbackDataLen : MAX_DEBUG_TEXT_STR_LEN);
            LOG_I(MODULE_PREFIX, "handleRxPacketData %s%s", cbStr.c_str(),
                        callbackDataLen < MAX_DEBUG_TEXT_STR_LEN ? "" : " ...");
#endif
#ifdef DEBUG_WEBSOCKET_LINK_DATA_BINARY
            if (pCallbackData)
                Raft::logHexBuf(pCallbackData, 
                            callbackDataLen < MAX_DEBUG_BIN_HEX_LEN ? callbackDataLen : MAX_DEBUG_BIN_HEX_LEN, 
                            MODULE_PREFIX, "handleRxPacketData");
#endif
            // Perform callback
            _webSocketCB(callbackEventCode, pCallbackData, callbackDataLen);
        }

        // Clear a completed message (control frames can come between the frames of a message, which is kept)
        if (pCallbackData == _callbackData.data())
            _callbackData.clear();
    }
    return consumed;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Extract header information
// Returns block len (or 0 if not enough data yet to read header)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebSocketLink::extractWSHeaderInfo(const uint8_t *pBuf, uint32_t bufLen)
{
    // Extract header
    uint32_t blockLen = _wsHeader.extract(pBuf, bufLen);

    // Debug
#ifdef DEBUG_WEBSOCKET_LINK_HEADER_DETAIL
    LOG_I(MODULE_PREFIX, "extractWSHeaderInfo fin %d opcode %d mask %d maskKey %02x%02x%02x%02x len %lld dataPos %d",
          _wsHeader.fin, _wsHeader.opcode, _wsHeader.mask,
          _wsHeader.maskKey[0], _wsHeader.maskKey[1], _wsHeader.maskKey[2], _wsHeader.maskKey[3],
          _wsHeader.len, _wsHeader.dataPos);
#endif
    return blockLen;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unmask
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebSocketLink::unmaskData(const uint8_t* pSrc, uint8_t* pDest, uint32_t len)
{
    if (!_wsHeader.mask)
    {
        memcpy(pDest, pSrc, len);
        return;
    }
    for (uint32_t i = 0; i < len; i++)
        pDest[i] = pSrc[i] ^ _wsHeader.maskKey[i % WSHeaderInfo::WEB_SOCKET_MASK_KEY_BYTES];
}
