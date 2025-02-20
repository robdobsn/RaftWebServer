/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftWebMultipart.h"
#include "RaftUtils.h"
#include "RaftJson.h"

// #define WARN_ON_MULTIPART_ERRORS
// #define DEBUG_MULTIPART_RECEIVE_ASCII_ONLY
// #define DEBUG_MULTIPART_PAYLOAD
// #define DEBUG_MULTIPART_BOUNDARY
// #define DEBUG_CONTENT_DISPOSITION

#if defined(DEBUG_MULTIPART_PAYLOAD) || defined(DEBUG_MULTIPART_BOUNDARY) || defined(DEBUG_MULTIPART_RECEIVE_ASCII_ONLY) || defined(WARN_ON_MULTIPART_ERRORS)
static const char *MODULE_PREFIX = "RaftMultipart";
#endif

const bool RaftWebMultipart::IS_VALID_TCHAR[] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    // CTRL chars
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,    // CTRL chars
    0,1,0,1,1,1,1,1,0,0,1,1,0,1,1,0,    // Punctuation
    1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,    // Digits
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,    // Upper case
    1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,    // Upper case
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,    // Lower case
    1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,0,    // Lower case
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor/Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebMultipart::RaftWebMultipart()
{
    clearCallbacks();
    clear();
}

RaftWebMultipart::RaftWebMultipart(const String &boundary)
{
    clearCallbacks();
    setBoundary(boundary);
}

RaftWebMultipart::~RaftWebMultipart()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear state
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebMultipart::clear()
{
    _parseState = RAFTMULTIPART_ERROR;
    _boundaryStr.clear();
    _boundaryBuf.clear();
    _contentPos = 0;
    _isFinalPart = false;
    _boundaryIdx = 0;
    _debugBytesHandled = 0;
    _headerFieldStartPos = INVALID_POS;
    _headerValueStartPos = INVALID_POS;
    _headerName.clear();
    _formInfo.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set multipart boundary
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebMultipart::setBoundary(const String &boundaryStr)
{
    // Clear state
    clear();

    // Add necessary chars to the boundary
    _boundaryStr = "\r\n--" + boundaryStr;

    // Index boundary for boyer-moore algorithm
    indexBoundary();

    // Buffer to handle boundary mismatches
    _boundaryBuf.resize(_boundaryStr.length() + 8);

    // Start of parsing
    _parseState = RAFTMULTIPART_START;

    // Debug
    _debugBytesHandled = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode RaftWebMultipart::handleData(const uint8_t *buffer, uint32_t bufLen)
{
    // Debug
    _debugBytesHandled += bufLen;

    // Check valid
    if (_parseState == RAFTMULTIPART_ERROR)
    {
#ifdef WARN_ON_MULTIPART_ERRORS
        LOG_W(MODULE_PREFIX, "hit an error previously - can't handle");
#endif
        return RAFT_INVALID_OPERATION;
    }

#ifdef DEBUG_MULTIPART_RECEIVE_ASCII_ONLY
    // Debug
    String inStr(buffer, bufLen);
    LOG_I(MODULE_PREFIX, "DATA START ---------------------------------------\n%s", inStr.c_str());
    LOG_I(MODULE_PREFIX, "DATA END ---------------------------------------");
#endif

    // Iterate bytes
    uint32_t bufPos = 0;
    if (_parseState != RAFTMULTIPART_PART_DATA)
    {
        while (bufPos < bufLen)
        {
            if (!processHeaderByte(buffer, bufPos, bufLen))
            {
                if (_parseState != RAFTMULTIPART_PART_DATA)
                    _parseState = RAFTMULTIPART_ERROR;
                bufPos++;
                break;
            }
            else
            {
                bufPos++;
            }
        }
    }

    // Process any payload
    if (_parseState == RAFTMULTIPART_PART_DATA)
        processPayload(buffer, bufPos, bufLen);

    if (_parseState == RAFTMULTIPART_ERROR) 
        return RAFT_OTHER_FAILURE;
    RaftRetCode result = _lastDataCallbackResult;
    _lastDataCallbackResult = RAFT_OK;
    return result;
}

bool RaftWebMultipart::processHeaderByte(const uint8_t *buffer, uint32_t bufPos, uint32_t bufLen)
{
    // Cur byte
    uint8_t curByte = buffer[bufPos];

    // Parser switch
    switch (_parseState)
    {
    case RAFTMULTIPART_ERROR:
    {
#ifdef WARN_ON_MULTIPART_ERRORS
        LOG_W(MODULE_PREFIX, "error detected at bufPos %d", bufPos);
#endif
        return false;
    }
    case RAFTMULTIPART_START:
    {
        _boundaryIdx = 0;
        _parseState = RAFTMULTIPART_START_BOUNDARY;
        [[fallthrough]];
    }
    case RAFTMULTIPART_START_BOUNDARY:
    {
        if (_boundaryIdx == _boundaryStr.length() - 2)
        {
            if (curByte != ASCII_CODE_CR)
            {
#ifdef WARN_ON_MULTIPART_ERRORS
                LOG_W(MODULE_PREFIX, "No CR after boundary pos %d ch %02x boundary %s boundaryLen %d",
                      _boundaryIdx, curByte, _boundaryStr.c_str(), _boundaryStr.length());
#endif
                return false;
            }
            _boundaryIdx++;
            break;
        }
        else if (_boundaryIdx - 1 == _boundaryStr.length() - 2)
        {
            if (curByte != ASCII_CODE_LF)
            {
#ifdef WARN_ON_MULTIPART_ERRORS
                LOG_W(MODULE_PREFIX, "No LF after CR bufPos %d", bufPos);
#endif
                return false;
            }
            _boundaryIdx = 0;
            stateCallback(RAFTMULTIPART_EVENT_PART_BEGIN, buffer, bufPos);
            _parseState = RAFTMULTIPART_HEADER_FIELD_START;
            break;
        }
        if (curByte != _boundaryStr[_boundaryIdx + 2])
        {
#ifdef WARN_ON_MULTIPART_ERRORS
            LOG_W(MODULE_PREFIX, "Boundary differs bufPOs %d", bufPos);
#endif
            return false;
        }
        _boundaryIdx++;
        break;
    }
    case RAFTMULTIPART_HEADER_FIELD_START:
    {
        _parseState = RAFTMULTIPART_HEADER_FIELD;
        _headerFieldStartPos = bufPos;
        _boundaryIdx = 0;
        [[fallthrough]];
    }
    case RAFTMULTIPART_HEADER_FIELD:
    {
        if (curByte == ASCII_CODE_CR)
        {
            _headerFieldStartPos = INVALID_POS;
            _parseState = RAFTMULTIPART_HEADERS_AWAIT_FINAL_LF;
            break;
        }

        if (curByte == ASCII_CODE_HYPHEN)
        {
            break;
        }

        if (curByte == ASCII_CODE_COLON)
        {
            if ((_headerFieldStartPos != INVALID_POS) && (_headerFieldStartPos < bufPos))
            {
                headerNameFound(buffer, _headerFieldStartPos, bufPos - _headerFieldStartPos);
            }
            else
            {
#ifdef WARN_ON_MULTIPART_ERRORS
                LOG_W(MODULE_PREFIX, "Empty header name found");
#endif
                return false;
            }
            _parseState = RAFTMULTIPART_HEADER_VALUE_START;
            break;
        }

        if ((curByte >= NUM_ASCII_VALS) || !IS_VALID_TCHAR[curByte])
        {
#ifdef WARN_ON_MULTIPART_ERRORS
            LOG_W(MODULE_PREFIX, "Header name not ascii");
#endif
            return false;
        }
        break;
    }
    case RAFTMULTIPART_HEADER_VALUE_START:
    {
        // Skip whitespace
        if (curByte == ASCII_CODE_SPACE)
        {
            break;
        }
        // Start of header value
        _headerValueStartPos = bufPos;
        _parseState = RAFTMULTIPART_HEADER_VALUE;
        [[fallthrough]];
    }
    case RAFTMULTIPART_HEADER_VALUE:
    {
        if (curByte == ASCII_CODE_CR)
        {
            if ((_headerValueStartPos != INVALID_POS) && (_headerValueStartPos < bufPos))
                headerValueFound(buffer, _headerValueStartPos, bufPos - _headerValueStartPos);
            _parseState = RAFTMULTIPART_HEADER_VALUE_GOT;
        }
        break;
    }
    case RAFTMULTIPART_HEADER_VALUE_GOT:
    {
        if (curByte != ASCII_CODE_LF)
        {
#ifdef WARN_ON_MULTIPART_ERRORS
            LOG_W(MODULE_PREFIX, "Header val LF expected after CR");
#endif
            return false;
        }

        _parseState = RAFTMULTIPART_HEADER_FIELD_START;
        break;
    }
    case RAFTMULTIPART_HEADERS_AWAIT_FINAL_LF:
    {
        if (curByte != ASCII_CODE_LF)
        {
#ifdef WARN_ON_MULTIPART_ERRORS
            LOG_W(MODULE_PREFIX, "Header end LF expected after CR");
#endif
            return false;
        }

        stateCallback(RAFTMULTIPART_EVENT_ALL_HEADERS_END, buffer, bufPos);
        _parseState = RAFTMULTIPART_PART_DATA;
        _contentPos = 0;
        _boundaryIdx = 0;
        _isFinalPart = false;
        return false;
    }
    default:
        break;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process payload
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebMultipart::processPayload(const uint8_t *buffer, uint32_t bufPos, uint32_t bufLen)
{
    // Process all data
    uint32_t payloadStartPos = bufPos;
    while (bufPos < bufLen)
    {
        // Check if we have already partially detected a boundary
        if (_boundaryIdx == 0)
        {
            // If not use boyer-moore algorithm to safely skip non-boundary data
            uint32_t boundaryLen = _boundaryStr.length();
            while (bufPos + boundaryLen < bufLen)
            {
                if (isBoundaryChar(buffer[bufPos + boundaryLen - 1]))
                    break;
                bufPos += boundaryLen;
            }
        }

        // Check for boundary
        uint8_t curByte = buffer[bufPos];
        if (_boundaryIdx != 0)
        {
            // Already in a possible boundary
            // Add current byte to buffer in case this isn't a real boundary
            if (_boundaryIdx < _boundaryBuf.size())
                _boundaryBuf[_boundaryIdx] = curByte;

            // Check if we are beyond the boundary - looking for indication of part-end
            if (((_boundaryIdx == _boundaryStr.length()) || 
                        (_boundaryIdx == _boundaryStr.length() + 1)) 
                 && (curByte == ASCII_CODE_HYPHEN))
            {
#ifdef DEBUG_MULTIPART_BOUNDARY
                LOG_W(MODULE_PREFIX, "Boundary hyphen bufPos %d boundaryIdx %d boundaryStrLen %d", 
                            bufPos, _boundaryIdx, _boundaryStr.length() + 1);
#endif
                if (_boundaryIdx == _boundaryStr.length() + 1)
                    _isFinalPart = true;
                _boundaryIdx++;
            }

            // Check for CR after the boundary
            else if (((_boundaryIdx == _boundaryStr.length()) ||
                        (_isFinalPart && (_boundaryIdx == _boundaryStr.length() + 2)))
                && (curByte == ASCII_CODE_CR))
            {
#ifdef DEBUG_MULTIPART_BOUNDARY
                LOG_W(MODULE_PREFIX, "Boundary CR %d %d", bufPos, _boundaryIdx);
#endif
                _boundaryIdx++;
            }

            // Check for LF after the CR after the boundary
            else if (((_boundaryIdx == _boundaryStr.length() + 1) ||
                        (_isFinalPart && (_boundaryIdx == _boundaryStr.length() + 3)))
                && (curByte == ASCII_CODE_LF))
            {
                // We have a complete boundary
#ifdef DEBUG_MULTIPART_BOUNDARY
                LOG_W(MODULE_PREFIX, "Boundary LF payloadStartPos %d bufPos %d boundaryIdx %d", 
                            payloadStartPos, bufPos, _boundaryIdx);
#endif
                // Send the data up to the start of the boundary (if there is any)
                int bufLen = bufPos - payloadStartPos - _boundaryIdx;
                if (bufLen < 0)
                    bufLen = 0;
                dataCallback(buffer, payloadStartPos, bufLen);
                payloadStartPos = bufPos + 1;
                _boundaryIdx = 0;
                _contentPos = 0;
            }

            // Check this is the next boundary char
            else if (curByte == _boundaryStr.charAt(_boundaryIdx))
            {
                // Bump
                _boundaryIdx++;
#ifdef DEBUG_MULTIPART_BOUNDARY
                LOG_W(MODULE_PREFIX, "Boundary IDX ++ %d %d", bufPos, _boundaryIdx);
#endif

                // Check for completed boundary
                if (_boundaryIdx == _boundaryStr.length())
                {
#ifdef DEBUG_MULTIPART_BOUNDARY
                    LOG_W(MODULE_PREFIX, "Boundary END FOUND AT %d", bufPos);
#endif
                }
            }
            else
            {
                // Check if this was at the start of a data part
                if (payloadStartPos + _boundaryIdx > bufPos)
                {
#ifdef DEBUG_MULTIPART_BOUNDARY
                    LOG_W(MODULE_PREFIX, "Mis-identified boundary crossing part %d byte %02x boundaryIdx %d payloadStartPos %d savedData %02x", 
                                bufPos, curByte, _boundaryIdx, payloadStartPos, _boundaryBuf.at(0));
#endif
                    // Failed to match boundary - the chars in the boundaryBuf were regular data
                    dataCallback(_boundaryBuf.data(), 0, _boundaryIdx);
                }
                else
                {
#ifdef DEBUG_MULTIPART_BOUNDARY
                    LOG_W(MODULE_PREFIX, "Mis-identified boundary within part %d byte %02x", bufPos, curByte);
#endif
                }
                _boundaryIdx = 0;
            }
        }

        // Check if at start of boundary
        if ((_boundaryIdx == 0) && (curByte == _boundaryStr.charAt(0)))
        {
            // Possible start of a boundary
            _boundaryBuf[0] = curByte;
            _boundaryIdx = 1;
#ifdef DEBUG_MULTIPART_BOUNDARY
            LOG_W(MODULE_PREFIX, "Boundary possible at %d byteVal %02x", bufPos, curByte);
#endif
        }

        // Move on
        bufPos++;

        // Check for end of data
        if (bufPos == bufLen)
        {
            // Send the data which doesn't involve a boundary
            if (bufLen > payloadStartPos + _boundaryIdx)
            {
#ifdef DEBUG_MULTIPART_PAYLOAD
                // Debug
                LOG_W(MODULE_PREFIX, "End of part data - data bufPos %d payloadStart %d boundaryIdx %d datalen %d bufLen %d", 
                            bufPos, payloadStartPos, _boundaryIdx, bufLen - payloadStartPos - _boundaryIdx, bufLen);
#endif
                // Send
                dataCallback(buffer, payloadStartPos, bufLen - payloadStartPos - _boundaryIdx);
            }
        }
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Header value handling
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebMultipart::headerNameFound(const uint8_t *pBuf, uint32_t pos, uint32_t len)
{
    if (len == 0)
        _headerName.clear();
    else
        _headerName = String(pBuf + pos, len);
}

void RaftWebMultipart::headerValueFound(const uint8_t *pBuf, uint32_t pos, uint32_t len)
{
    if ((len == 0) || (_headerName.length() == 0))
        return;
    String headerValue(pBuf + pos, len);

    // Parse header value to get content
    if (_headerName.equalsIgnoreCase("Content-Disposition"))
    {
        // Get data upto ;
        int semiPos = headerValue.indexOf(";");
        if (semiPos >= 0)
            _formInfo._contentDisp = headerValue.substring(0, semiPos);

        // Extract name - values from form data
        std::vector<RaftJson::NameValuePair> contentDispNameVals;
        RaftJson::extractNameValues(headerValue, "=", ";", NULL, contentDispNameVals);

        // Check for filename
        for (RaftJson::NameValuePair &nvp : contentDispNameVals)
        {
            if (nvp.name.equalsIgnoreCase("filename"))
            {
                _formInfo._fileName = nvp.value;
                _formInfo._fileName.replace("\"", "");
            }
            else if (nvp.name.equalsIgnoreCase("name"))
            {
                _formInfo._name = nvp.value;
                _formInfo._name.replace("\"", "");
            }
        }

#ifdef DEBUG_CONTENT_DISPOSITION
        String jsonStr = RaftJson::getJSONFromNVPairs(contentDispNameVals, true);
        LOG_I(MODULE_PREFIX, "Content-Disposition %s JSON %s name %s filename %s", 
                    _formInfo._contentDisp.c_str(), 
                    jsonStr.c_str(),
                    _formInfo._name.c_str(), _formInfo._fileName.c_str());
#endif        
    }
    else if (_headerName.equalsIgnoreCase("Content-Type"))
    {
        _formInfo._contentType = headerValue;
    }
    else if (_headerName.equalsIgnoreCase("FileLengthBytes"))
    {
        _formInfo._fileLenBytes = strtoul(headerValue.c_str(), NULL, 0);
        _formInfo._fileLenValid = true;
    }
    else if (_headerName.equalsIgnoreCase("CRC16"))
    {
        _formInfo._crc16 = strtoul(headerValue.c_str(), NULL, 0);
        _formInfo._crc16Valid = true;
    }

    // Callback on header info
    if (onHeaderNameValue)
        onHeaderNameValue(_pCtx, _headerName, headerValue);

    // Clear header info
    _headerName.clear();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State callback helper
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebMultipart::stateCallback(RaftMultipartEvent event, const uint8_t *buffer, uint32_t pos)
{
    if (onEvent != NULL)
        onEvent(_pCtx, event, buffer, pos);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data callback helper
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebMultipart::dataCallback(const uint8_t *pBuf, uint32_t pos, uint32_t bufLen)
{
    // Callback with data
    if (onData)
        _lastDataCallbackResult = onData(_pCtx, pBuf + pos, bufLen, _formInfo, _contentPos, _isFinalPart);

    // Move content pos on
    _contentPos += bufLen;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint8_t RaftWebMultipart::lower(uint8_t c) const
{
    return c | 0x20;
}

inline bool RaftWebMultipart::isBoundaryChar(uint8_t c) const
{
    return _boundaryCharMap[c];
}

bool RaftWebMultipart::isHeaderFieldCharacter(uint8_t c) const
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ASCII_CODE_HYPHEN;
}

void RaftWebMultipart::clearCallbacks()
{
    // Clear callbacks
    onEvent = NULL;
    onData = NULL;
    onHeaderNameValue = NULL;
}

void RaftWebMultipart::indexBoundary()
{
    // Clear boundary char map
    memset(_boundaryCharMap, 0, sizeof(_boundaryCharMap));

    // Set elem in boundary char map for each char appearing in _boundaryStr
    for (uint32_t i = 0; i < _boundaryStr.length(); i++)
    {
        _boundaryCharMap[(uint8_t)_boundaryStr.charAt(i)] = true;
    }
}

bool RaftWebMultipart::succeeded() const
{
    return _parseState == RAFTMULTIPART_END;
}

bool RaftWebMultipart::hasError() const
{
    return _parseState == RAFTMULTIPART_ERROR;
}

bool RaftWebMultipart::stopped() const
{
    return _parseState == RAFTMULTIPART_ERROR || _parseState == RAFTMULTIPART_END;
}