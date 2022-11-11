/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include <WString.h>
#include <RdJson.h>
#include <RdWebConnDefs.h>

class RdWebConnection;

class RdWebResponder
{
public:
    RdWebResponder()
    {
        _isActive = false;
    }
    virtual ~RdWebResponder()
    {
    }
    virtual bool isActive()
    {
        return _isActive;
    }

    virtual void service()
    {
    }
    
    // Handle inbound data
    virtual bool handleData(const uint8_t* pBuf, uint32_t dataLen)
    {
        return false;
    }

    // Start responding
    virtual bool startResponding(RdWebConnection& request)
    {
        return false;
    }

    // Get response next
    virtual uint32_t getResponseNext(uint8_t*& pBuf, uint32_t bufMaxLen)
    {
        return 0;
    }

    // Non-virtual methods
    void addHeader(String name, String value)
    {
        _headers.push_back({name, value});
    }

    // Get headers
    std::list<RdJson::NameValuePair>* getHeaders()
    {
        return &_headers;
    }

    // Get content type
    virtual const char* getContentType()
    {
        return NULL;
    }

    // Get content length (or -1 if not known)
    virtual int getContentLength()
    {
        return -1;
    }

    // Leave connection open
    virtual bool leaveConnOpen()
    {
        return false;
    }

    // Send standard headers
    virtual bool isStdHeaderRequired()
    {
        return true;
    }

    // Send a frame of data
    virtual bool sendFrame(const uint8_t* pBuf, uint32_t bufLen)
    {
        return false;
    }

    // Send event content and group
    virtual void sendEvent(const char* eventContent, const char* eventGroup)
    {
    }

    // Get responder type
    virtual const char* getResponderType()
    {
        return "NONE";
    }

    // Get channelID for responder
    virtual bool getChannelID(uint32_t& channelID)
    {
        return false;
    }

    // Ready for data
    virtual bool readyForData()
    {
        return true;
    }

protected:
    // Is Active
    bool _isActive;

private:
    // Additional headers to send
    std::list<RdJson::NameValuePair> _headers;

};
