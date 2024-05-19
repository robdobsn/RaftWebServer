/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <list>
#include "RaftArduino.h"
#include "RaftJson.h"
#include "RaftWebConnDefs.h"

class RaftWebConnection;

class RaftWebResponder
{
public:
    RaftWebResponder()
    {
        _isActive = false;
    }
    virtual ~RaftWebResponder()
    {
    }
    virtual bool isActive()
    {
        return _isActive;
    }

    virtual void loop()
    {
    }
    
    // Handle inbound data
    virtual bool handleInboundData(const uint8_t* pBuf, uint32_t dataLen)
    {
        return false;
    }

    // Start responding
    virtual bool startResponding(RaftWebConnection& request)
    {
        return false;
    }

    // Check if any reponse data is available
    virtual bool responseAvailable()
    {
        return _isActive;
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
    std::list<RaftJson::NameValuePair>* getHeaders()
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

    // Ready to send data
    virtual bool isReadyToSend()
    {
        return true;
    }

    // Encode and send data
    virtual bool encodeAndSendData(const uint8_t* pBuf, uint32_t bufLen)
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

    // Ready to receive data
    virtual bool readyToReceiveData()
    {
        return true;
    }

protected:
    // Is Active
    bool _isActive;

private:
    // Additional headers to send
    std::list<RaftJson::NameValuePair> _headers;

};
