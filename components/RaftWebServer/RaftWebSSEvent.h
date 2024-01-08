/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"

// SSEvent for tx queue
class RaftWebSSEvent
{
public:
    RaftWebSSEvent()
    {
    }
    RaftWebSSEvent(const char* eventContent, const char* eventGroup)
    {
        _content = eventContent;
        _group = eventGroup;
    }
    const String& getContent()
    {
        return _content;
    }
    const String& getGroup()
    {
        return _group;
    }

private:
    String _content;
    String _group;
};