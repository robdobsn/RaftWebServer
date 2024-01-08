/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string.h>
#include <stdint.h>
#include <vector>
#include "SpiramAwareAllocator.h"

// Buffer for tx queue
class RaftWebDataFrame
{
public:
    RaftWebDataFrame()
    {
    }
    RaftWebDataFrame(uint32_t channelID, const uint8_t* pBuf, uint32_t bufLen, uint32_t frameTimeMs)
    {
        frame.resize(bufLen);
        memcpy(frame.data(), pBuf, frame.size());
        _channelID = channelID;
        _frameTimeMs = frameTimeMs;
    }
    const uint8_t* getData()
    {
        return frame.data();
    }
    uint32_t getLen()
    {
        return frame.size();
    }
    uint32_t getChannelID()
    {
        return _channelID;
    }
    uint32_t getFrameTimeMs()
    {
        return _frameTimeMs;
    }
private:
    std::vector<uint8_t, SpiramAwareAllocator<uint8_t>> frame;
    uint32_t _channelID = UINT32_MAX;
    uint32_t _frameTimeMs = 0;
};
