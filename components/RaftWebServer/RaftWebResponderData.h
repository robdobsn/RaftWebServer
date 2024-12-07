/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "RaftWebResponder.h"
#include "RaftWebRequestParams.h"

// #define DEBUG_STATIC_DATA_RESPONDER

class RaftWebHandler;

class RaftWebResponderData : public RaftWebResponder
{
public:
    RaftWebResponderData( const uint8_t* pData, uint32_t dataLen, const char* pMIMEType,
                RaftWebHandler* pWebHandler, const RaftWebRequestParams& params)
            : _reqParams(params)
    {
        _pWebHandler = pWebHandler;
        _mimeType = pMIMEType;
        _curDataPos = 0;
        _dataLength = dataLen;
        _pData = pData;
        _fileSendStartMs = millis();
    }

    virtual ~RaftWebResponderData()
    {
    }

    // Handle inbound data
    virtual bool handleInboundData(const SpiramAwareUint8Vector& data) override final
    {
        return true;
    }

    // Start responding
    virtual bool startResponding(RaftWebConnection& request) override final
    {
        _isActive = true;
        _curDataPos = 0;
        _fileSendStartMs = millis();
        return _isActive;
    }

    /// @brief Get next response data
    /// @param maxLen Maximum length to return
    /// @return Response data
    virtual SpiramAwareUint8Vector getResponseNext(uint32_t bufMaxLen) override final
    {
        uint32_t lenToCopy = _dataLength - _curDataPos;
        if (lenToCopy > bufMaxLen)
            lenToCopy = bufMaxLen;
        if (!_isActive || (lenToCopy == 0))
        {
#ifdef DEBUG_STATIC_DATA_RESPONDER
            LOG_I("WebRespData", "getResponseNext NOTHING TO RETURN");
#endif
            _isActive = false;
            return SpiramAwareUint8Vector();
        }
#ifdef DEBUG_STATIC_DATA_RESPONDER
        LOG_I("WebRespData", "getResponseNext pos %d totalLen %d lenToCopy %d isActive %d ptr %x", 
                    _curDataPos, _dataLength, lenToCopy, _isActive, _pData);
#endif
        SpiramAwareUint8Vector respData(_pData + _curDataPos, lenToCopy);
        _curDataPos += lenToCopy;
        if (_curDataPos >= _dataLength)
        {
            _isActive = false;
        }

#ifdef DEBUG_STATIC_DATA_RESPONDER
        LOG_I("WebRespData", "getResponseNext returning %d curPos %d isActive %d", 
                    lenToCopy, _curDataPos, _isActive);
#endif
        return respData;
    }

    // Get content type
    virtual const char* getContentType() override final
    {
        return _mimeType.c_str();
    }

    // Get content length (or -1 if not known)
    virtual int getContentLength() override final
    {
        return _dataLength;
    }

    // Leave connection open
    virtual bool leaveConnOpen() override final
    {
        return false;
    }

    // Get responder type
    virtual const char* getResponderType() override final
    {
        return "DATA";
    }

private:
    RaftWebHandler* _pWebHandler;
    RaftWebRequestParams _reqParams;
    const uint8_t* _pData;
    uint32_t _dataLength;
    uint32_t _curDataPos;
    String _mimeType;
    uint32_t _fileSendStartMs;
    static const uint32_t SEND_DATA_OVERALL_TIMEOUT_MS = 5 * 60 * 1000;
};
