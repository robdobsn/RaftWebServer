/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "Logger.h"
#include "ArduinoTime.h"
#include "RaftWebResponder.h"
#include "RaftWebRequestParams.h"
#include "FileSystemChunker.h"

class RaftWebHandler;
class RaftWebRequestHeader;

class RaftWebResponderFile : public RaftWebResponder
{
public:
    RaftWebResponderFile(const String& filePath, RaftWebHandler* pWebHandler, const RaftWebRequestParams& params,
                    const RaftWebRequestHeader& requestHeader, uint32_t maxSendSize);
    virtual ~RaftWebResponderFile();

    // Handle inbound data
    virtual bool handleInboundData(const SpiramAwareUint8Vector& data) override final;

    // Start responding
    virtual bool startResponding(RaftWebConnection& request) override final;

    /// @brief Get next response data
    /// @param maxLen Maximum length to return
    /// @return Response data
    virtual SpiramAwareUint8Vector getResponseNext(uint32_t bufMaxLen) override final;

    // Get content type
    virtual const char* getContentType() override final;

    // Get content length (or -1 if not known)
    virtual int getContentLength() override final;

    // Leave connection open
    virtual bool leaveConnOpen() override final;

    // Get responder type
    virtual const char* getResponderType() override final
    {
        return "FILE";
    }

private:
    String _filePath;
    RaftWebHandler* _pWebHandler = nullptr;
    FileSystemChunker _fileChunker;
    RaftWebRequestParams _reqParams;
    uint32_t _fileLength = 0;
    uint32_t _fileSendStartMs = 0;
    static const uint32_t SEND_DATA_OVERALL_TIMEOUT_MS = 5 * 60 * 1000;
    bool _isFinalChunk = false;
};
