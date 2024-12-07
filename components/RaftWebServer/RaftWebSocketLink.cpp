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
#include "esp_system.h"

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

void RaftWebSocketLink::handleRxData(const SpiramAwareUint8Vector& msg)
{
    static const uint8_t UPGRADE_REQ_TEXT[] = "Upgrade: websocket\r\n";
    static const uint8_t UPGRADE_REQ_KEY[] = "Sec-WebSocket-Key: ";
    static const uint8_t HTTP_EOL_STR[] = "\r\n";
    uint32_t bufPos = 0;
    // If we don't yet have an upgrade request
    if (!_upgradeReqReceived)
    {
        // Check for header
        if (Raft::findInBuf(msg, bufPos, UPGRADE_REQ_TEXT, sizeof(UPGRADE_REQ_TEXT)) < 0)
            return;

        // Check for upgrade key
        int keyPos = Raft::findInBuf(msg, bufPos, UPGRADE_REQ_KEY, sizeof(UPGRADE_REQ_KEY));
        if (keyPos < 0)
            return;
        keyPos += sizeof(UPGRADE_REQ_TEXT);

        // Find key length
        int keyLen = Raft::findInBuf(msg, keyPos, HTTP_EOL_STR, sizeof(HTTP_EOL_STR));
        if (keyLen < 0)
            return;

        // Extract key
        _wsKey = String(msg.data() + keyPos, keyLen);
        _upgradeReqReceived = true;

        // Continue with any excess data
        if (keyPos + keyLen >= msg.size())
            return;
        bufPos = keyPos + keyLen;

#ifdef WARN_WEBSOCKET_EXCESS_DATA_AFTER_UPGRADE
        LOG_W(MODULE_PREFIX, "handleRxData excessDataAfter ws upgrade len %d", msg.size() - bufPos);
#endif
    }

    // Check if any data already received
    if (_rxDataToProcess.size() > 0)
    {
        if (_rxDataToProcess.size() + (msg.size() - bufPos) > MAX_WS_MESSAGE_SIZE + 50)
        {
#ifdef WARN_WEBSOCKET_DATA_DISCARD_AS_EXCEEDS_MSG_SIZE
        LOG_W(MODULE_PREFIX, "handleRxData discard as exceeds max stashed %d len %d max %d", 
                _rxDataToProcess.size(), msg.size() - bufPos, MAX_WS_MESSAGE_SIZE);
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
            _rxDataToProcess.insert(_rxDataToProcess.end(), msg.data() + bufPos, msg.data() + msg.size());

#ifdef DEBUG_WEBSOCKET_DATA_BUFFERING_CONTENT
            String dataToProc;
            Raft::getHexStr(_rxDataToProcess.data(), 
                    _rxDataToProcess.size() < MAX_DEBUG_BIN_HEX_LEN ? _rxDataToProcess.size() : MAX_DEBUG_BIN_HEX_LEN, 
                    dataToProc);
            LOG_I(MODULE_PREFIX, "handleRxData agg dataToProc len %d data %s%s", 
                    _rxDataToProcess.size(), dataToProc.c_str(),
                    _rxDataToProcess.size() < MAX_DEBUG_BIN_HEX_LEN ? "" : "...");
#endif
        }
    }

    // Handle packets
    while(bufPos < msg.size())
    {
        int32_t dataConsumed = handleRxPacketData(msg, bufPos);
#ifdef DEBUG_WEBSOCKET_DATA_PROCESSING
        LOG_I(MODULE_PREFIX, "handleRxData consumed %d remainLen %d", dataConsumed, msg.size() - bufPos);
#endif
        if (dataConsumed == 0)
        {
            // Store residual data (needs to use temp store as it may be copying part of itself)
            _rxDataToProcess.assign(msg.data() + bufPos, msg.data() + msg.size());
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
        if (dataConsumed >= msg.size() - bufPos)
        {
            // Clear any residual data
            _rxDataToProcess.clear();
            break;
        }
        bufPos += dataConsumed;
#ifdef DEBUG_WEBSOCKET_DATA_BUFFERING_CONTENT
        String nextDataStr;
        Raft::getHexStr(msg, pBuf, bufLen < MAX_DEBUG_BIN_HEX_LEN ? bufLen : MAX_DEBUG_BIN_HEX_LEN, nextDataStr);
        LOG_I(MODULE_PREFIX, "handleRxData nextData remainLen %d data %s%s", msg.size()-bufPos, nextDataStr.c_str(),
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
/// @brief Get data to tx
/// @param maxLen Maximum length to return
SpiramAwareUint8Vector RaftWebSocketLink::getTxData(uint32_t maxLen)
{
    // Check if upgrade received but no response yet sent
    if (!_upgradeRespSent && _upgradeReqReceived)
    {
        // No longer need to send
        _upgradeRespSent = true;

        // Make sure we don't PING too early
        _pingTimeLastMs = millis();

        // Form the upgrade response
        return formUpgradeResponse(_wsKey, _wsVersion, maxLen);
    }
    return SpiramAwareUint8Vector();
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
    SpiramAwareUint8Vector frameBuffer(frameLen);

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
        uint32_t maskKey = esp_random();
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
        LOG_W(MODULE_PREFIX, "sendMsg something awry with frameLen %d + %d != %d", pos, bufLen, frameBuffer.size());
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
        sendRetc = _rawConnSendFn(frameBuffer, MAX_WS_SEND_RETRY_MS);

#ifdef DEBUG_WEBSOCKET_LINK_SEND
    uint64_t timeTakenUs = micros() - timeNowUs;
    LOG_I(MODULE_PREFIX, "sendMsg result %s send %d bytes took %dus", 
            RaftWebConnDefs::getSendRetValStr(sendRetc), 
            frameBuffer.size(), (int)timeTakenUs);
#endif

    return sendRetc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Form response to upgrade connection
/// @param wsKey Key from request
/// @param wsVersion Version from request
/// @param bufMaxLen Maximum length of buffer
/// @return Response
SpiramAwareUint8Vector RaftWebSocketLink::formUpgradeResponse(const String &wsKey, const String &wsVersion, uint32_t bufMaxLen)
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
    return SpiramAwareUint8Vector(respStr.c_str(), respStr.c_str() + respStr.length());
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

uint32_t RaftWebSocketLink::handleRxPacketData(const SpiramAwareUint8Vector& msg, uint32_t bufPos)
{
    // Extract header
    extractWSHeaderInfo(msg, bufPos);

#ifdef DEBUG_WEBSOCKET_RX_DETAIL
    String outStr;
    Raft::getHexStr(msg, outStr, "", bufPos, 30);
    LOG_I(MODULE_PREFIX, "handleRxPacketData header len %d dataPos %d bufLen %d data %s", 
                (int)_wsHeader.len, _wsHeader.dataPos, msg.size()-bufPos, outStr.c_str());
#endif

    if (_wsHeader.dataPos + _wsHeader.len > msg.size()-bufPos)
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
            uint32_t copyLen = (msg.size()-bufPos) - _wsHeader.dataPos;
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
            _callbackData.insert(_callbackData.end(), msg.data() + bufPos + _wsHeader.dataPos, msg.data() + bufPos + _wsHeader.dataPos + copyLen);
            if (_wsHeader.fin)
                callbackEventCode = _wsHeader.firstFrameOpcode == WEBSOCKET_OPCODE_TEXT ? WEBSOCKET_EVENT_TEXT : WEBSOCKET_EVENT_BINARY;
            break;
        }
        case WEBSOCKET_OPCODE_PING:
        {
            callbackEventCode = WEBSOCKET_EVENT_PING;
            uint32_t copyLen = (msg.size()-bufPos) - _wsHeader.dataPos;
            if (copyLen > MAX_WS_MESSAGE_SIZE)
                break;

            // Send PONG
            sendMsg(WEBSOCKET_OPCODE_PONG, msg.data() + bufPos + _wsHeader.dataPos, _wsHeader.len);

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

uint32_t RaftWebSocketLink::extractWSHeaderInfo(const SpiramAwareUint8Vector& msg, uint32_t bufPos)
{
    // Extract header
    uint32_t blockLen = _wsHeader.extract(msg, bufPos);

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
