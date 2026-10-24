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
#include "SpiramAwareAllocator.h"
#include <memory>
#include <atomic>

// Maximum size of a chunk of a file read and sent in one go (override in the build if required)
#ifndef RAFT_WEB_FILE_CHUNK_MAX_BYTES
#define RAFT_WEB_FILE_CHUNK_MAX_BYTES 4096
#endif

class RaftWebHandler;
class RaftWebRequestHeader;

class RaftWebResponderFile : public RaftWebResponder
{
public:
    RaftWebResponderFile(const String& filePath, RaftWebHandler* pWebHandler, const RaftWebRequestParams& params,
                    const RaftWebRequestHeader& requestHeader, uint32_t maxSendSize);
    virtual ~RaftWebResponderFile();

    // Handle inbound data
    virtual bool handleInboundData(const uint8_t* pBuf, uint32_t dataLen) override final;

    // Start responding
    virtual bool startResponding(RaftWebConnection& request) override final;

    // Get response next
    virtual uint32_t getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen) override final;

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
    RaftWebRequestParams _reqParams;
    uint32_t _fileLength = 0;
    uint32_t _fileSendStartMs = 0;

    // The file is read on the file system worker, a chunk ahead of the one being sent. The responder and the read job
    // share this (the connection can close while a read is in progress). The job writes the chunk then sets
    // chunkReady; getResponseNext() takes it (swapping buffers with _lastChunkData, which holds the chunk being sent)
    struct ReadState
    {
        FileSystemChunker fileChunker;
        SpiramAwareUint8Vector chunkData;
        uint32_t readLen = 0;
        bool isFinalChunk = false;
        bool readOk = false;
        std::atomic<bool> readInProgress{false};
        std::atomic<bool> chunkReady{false};
    };
    std::shared_ptr<ReadState> _pReadState;
    SpiramAwareUint8Vector _lastChunkData;
    bool startRead(uint32_t readLen);

    // Heap to leave free (for the network stack) when allocating the chunk buffer and minimum chunk size
    static const uint32_t CHUNK_HEAP_MARGIN_BYTES = 8192;
    static const uint32_t CHUNK_MIN_BYTES = 512;
    static const uint32_t SEND_DATA_OVERALL_TIMEOUT_MS = 5 * 60 * 1000;
    bool _isFinalChunk = false;
};
