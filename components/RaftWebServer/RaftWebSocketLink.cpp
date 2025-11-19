/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdint.h>
#include <vector>
#include "Logger.h"
#include "RaftWebSocketLink.h"
#include "RaftArduino.h"
#include "RaftUtils.h"
#include "ArduinoTime.h"
#include "mbedtls/sha1.h"
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
    _webSocketCB = webSocketCB;
    _rawConnSendFn = rawConnSendFn;
    _pingIntervalMs = pingIntervalMs;
    _pingTimeLastMs = 0;
    _pongRxLastMs = 0;
    _disconnIfNoPongMs = disconnIfNoPongMs;
    _maskSentData = !roleIsServer;
    _isActive = true;
    _defaultContentOpCode = isBinary ? WEBSOCKET_OPCODE_BINARY : WEBSOCKET_OPCODE_TEXT;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service the web socket
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebSocketLink::loop()
{
    static const uint8_t PING_MSG[] = "RIC";
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
        if (_rxDataToProcess.size() + bufLen > MAX_WS_MESSAGE_SIZE + 50)
        {
#ifdef WARN_WEBSOCKET_DATA_DISCARD_AS_EXCEEDS_MSG_SIZE
        LOG_W(MODULE_PREFIX, "handleRxData discard as exceeds max stashed %d len %d max %d", 
                (int)_rxDataToProcess.size(), bufLen, MAX_WS_MESSAGE_SIZE);
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
            // Store residual data (needs to use temp store as it may be copying part of itself)
            std::vector<uint8_t> tmpData;
            tmpData.assign(pBuf, pBuf+bufLen);
            _rxDataToProcess.assign(tmpData.data(), tmpData.data()+tmpData.size());
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
    return _upgradeReqReceived && !_upgradeRespSent;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get data to tx
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftWebSocketLink::getTxData(uint8_t*& pBuf, uint32_t bufMaxLen)
{
    // Check if upgrade received but no response yet sent
    if (!_upgradeRespSent && _upgradeReqReceived)
    {
        // No longer need to send
        _upgradeRespSent = true;

        // Make sure we don't PING too early
        _pingTimeLastMs = millis();

        // Form the upgrade response
        _wsUpgradeResponse = formUpgradeResponse(_wsKey, _wsVersion, bufMaxLen);
        pBuf = (uint8_t*)_wsUpgradeResponse.c_str();
        return _wsUpgradeResponse.length();
    }

    // Other comms on the link is done through _rawConnSendFn
    _wsUpgradeResponse.clear();
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send message
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebConnSendRetVal RaftWebSocketLink::sendMsg(RaftWebSocketOpCodes opCode, const uint8_t *pBuf, uint32_t bufLen)
{
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

    // Buffer
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> frameBuffer(frameLen);

    // Setup header
    frameBuffer[0] = 0x80 | opCode;
    frameBuffer[1] = (_maskSentData ? 0x80 : 0) | hdrLenCode;

    // Length
    uint32_t pos = 2;
    if (hdrLenCode == 126)
    {
        frameBuffer[pos++] = bufLen / 256;
        frameBuffer[pos++] = bufLen % 256;
    }
    else if (hdrLenCode == 127)
    {
        pos += 4;
        for (int i = 3; i >= 0; i--)
            frameBuffer[pos++] = (bufLen >> (i * 8)) & 0xff;
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
            frameBuffer[pos++] = maskBytes[i];
        }
    }

    // Sanity check
    if (pos + bufLen != frameBuffer.size())
    {
        LOG_W(MODULE_PREFIX, "sendMsg something awry with frameLen %d + %d != %d", 
                    (int)pos, (int)bufLen, (int)frameBuffer.size());
        return WEB_CONN_SEND_FRAME_ERROR;
    }

    // Copy the data
    memcpy(frameBuffer.data() + pos, pBuf, bufLen);

    // Mask the data
    if (_maskSentData)
    {
        for (uint32_t i = 0; i < bufLen; i++)
            frameBuffer[pos + i] ^= maskBytes[i % WSHeaderInfo::WEB_SOCKET_MASK_KEY_BYTES];
    }

    // Send
#ifdef DEBUG_WEBSOCKET_LINK_SEND
    uint64_t timeNowUs = micros();
#endif
    RaftWebConnSendRetVal sendRetc = RaftWebConnSendRetVal::WEB_CONN_SEND_FAIL;
    if (_rawConnSendFn)
        sendRetc = _rawConnSendFn(frameBuffer.data(), frameBuffer.size(), MAX_WS_SEND_RETRY_MS);

#ifdef DEBUG_WEBSOCKET_LINK_SEND
    uint64_t timeTakenUs = micros() - timeNowUs;
    LOG_I(MODULE_PREFIX, "sendMsg result %s send %d bytes took %dus", 
            RaftWebConnDefs::getSendRetValStr(sendRetc), 
            frameBuffer.size(), (int)timeTakenUs);
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
    mbedtls_sha1((uint8_t *)concatKey.c_str(), concatKey.length(), sha1Result);

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
    // Extract header
    extractWSHeaderInfo(pBuf, bufLen);
#ifdef DEBUG_WEBSOCKET_RX_DETAIL
    String outStr;
    Raft::getHexStrFromBytes(pBuf, bufLen, outStr);
    LOG_I(MODULE_PREFIX, "handleRxPacketData header len %lld dataPos %d bufLen %d data %s", 
                _wsHeader.len, _wsHeader.dataPos, bufLen, outStr.c_str());
#endif
    if (_wsHeader.dataPos + _wsHeader.len > bufLen)
        return 0;

    // Check if we are ignoring (because a frame was too big)
    if (_wsHeader.ignoreUntilFinal && _wsHeader.fin)
    {
        _wsHeader.ignoreUntilFinal = false;
        return _wsHeader.dataPos + _wsHeader.len;
    }

    // Handle payload
    RaftWebSocketEventCode callbackEventCode = WEBSOCKET_EVENT_NONE;
    switch (_wsHeader.opcode)
    {
        case WEBSOCKET_OPCODE_CONTINUE:
        case WEBSOCKET_OPCODE_BINARY:
        case WEBSOCKET_OPCODE_TEXT:
        {
            // Get length of data to copy
            uint32_t copyLen = bufLen - _wsHeader.dataPos;
            if (copyLen > _wsHeader.len)
                copyLen = _wsHeader.len;
            else
                _callbackData.clear();

            // Handle continuation
            uint32_t curBufSize = 0;
            if (_wsHeader.opcode == WEBSOCKET_OPCODE_CONTINUE)
                curBufSize = _callbackData.size();

            // Check we don't try to store too much
            if (curBufSize + copyLen > MAX_WS_MESSAGE_SIZE)
            {
                LOG_W(MODULE_PREFIX, "handleRxPacketData msg > max %d", MAX_WS_MESSAGE_SIZE);
                _callbackData.clear();
                _wsHeader.ignoreUntilFinal = true;
                return _wsHeader.dataPos + _wsHeader.len;             
            }
            // Add the data to any existing
            _callbackData.resize(curBufSize + copyLen);
            memcpy(_callbackData.data(), pBuf + _wsHeader.dataPos, copyLen);
            if (_wsHeader.fin)
                callbackEventCode = _wsHeader.firstFrameOpcode == WEBSOCKET_OPCODE_TEXT ? WEBSOCKET_EVENT_TEXT : WEBSOCKET_EVENT_BINARY;
            break;
        }
        case WEBSOCKET_OPCODE_PING:
        {
            callbackEventCode = WEBSOCKET_EVENT_PING;
            uint32_t copyLen = bufLen - _wsHeader.dataPos;
            if (copyLen > MAX_WS_MESSAGE_SIZE)
                break;

            // Send PONG
            sendMsg(WEBSOCKET_OPCODE_PONG, pBuf + _wsHeader.dataPos, _wsHeader.len);

#ifdef DEBUG_WEBSOCKET_PING_PONG
            LOG_I(MODULE_PREFIX, "handleRxPacketData Rx PING Tx PONG %lld", _wsHeader.len);
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
        LOG_I(MODULE_PREFIX, "handleRxPacketData callback eventCode %s len %d", getEventStr(callbackEventCode), _callbackData.size());
#endif

        // Callback
        if (_webSocketCB)
        {
            // Unmask the data
            unmaskData();

#ifdef DEBUG_WEBSOCKET_LINK_DATA_STR
            String cbStr(_callbackData.data(), 
                        _callbackData.size() < MAX_DEBUG_TEXT_STR_LEN ? _callbackData.size() : MAX_DEBUG_TEXT_STR_LEN);
            LOG_I(MODULE_PREFIX, "handleRxPacketData %s%s", cbStr.c_str(),
                        _callbackData.size() < MAX_DEBUG_TEXT_STR_LEN ? "" : " ...");
#endif
#ifdef DEBUG_WEBSOCKET_LINK_DATA_BINARY
            Raft::logHexBuf(_callbackData.data(), 
                        _callbackData.size() < MAX_DEBUG_BIN_HEX_LEN ? _callbackData.size() : MAX_DEBUG_BIN_HEX_LEN, 
                        MODULE_PREFIX, "handleRxPacketData");
#endif
            // Perform callback
            _webSocketCB(callbackEventCode, _callbackData.data(), _callbackData.size());
        }

        // Clear compiled data
        _callbackData.clear();
    }
    return _wsHeader.dataPos + _wsHeader.len;
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

void RaftWebSocketLink::unmaskData()
{
    if (_wsHeader.mask)
    {
        for (uint32_t i = 0; i < _callbackData.size(); i++)
        {
            _callbackData[i] ^= _wsHeader.maskKey[i % WSHeaderInfo::WEB_SOCKET_MASK_KEY_BYTES];
        }
    }
}
