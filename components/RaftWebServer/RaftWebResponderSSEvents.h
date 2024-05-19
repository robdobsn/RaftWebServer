/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Logger.h"
#include "RaftArduino.h"
#include "RaftWebResponder.h"
#include "RaftWebRequestParams.h"
#include "RaftWebConnection.h"
#include "RaftWebSocketLink.h"
#include "RaftWebSSEvent.h"
#include "ThreadSafeQueue.h"

class RaftWebHandler;
class RaftWebServerSettings;
class ProtocolEndpointManager;

// Callback function for server-side-events
typedef std::function<void(const uint8_t* pBuf, uint32_t bufLen)> RaftWebSSEventsCB;

class RaftWebResponderSSEvents : public RaftWebResponder
{
public:
    RaftWebResponderSSEvents(RaftWebHandler* pWebHandler, const RaftWebRequestParams& params, 
                const String& reqStr, RaftWebSSEventsCB eventsCallback, 
                const RaftWebServerSettings& webServerSettings);
    virtual ~RaftWebResponderSSEvents();

    // Service - called frequently
    virtual void loop() override final;

    // Handle inbound data
    virtual bool handleInboundData(const uint8_t* pBuf, uint32_t dataLen) override final;

    // Start responding
    virtual bool startResponding(RaftWebConnection& request) override final;

    // Check if any reponse data is available
    virtual bool responseAvailable() override final;
    
    // Get response next
    virtual uint32_t getResponseNext(uint8_t* pBuf, uint32_t bufMaxLen) override final;

    // Get content type
    virtual const char* getContentType() override final;

    // Leave connection open
    virtual bool leaveConnOpen() override final;

    // Send standard headers
    virtual bool isStdHeaderRequired() override final
    {
        return false;
    }

    // Send event content and group
    virtual void sendEvent(const char* eventContent, const char* eventGroup);

    // Get responder type
    virtual const char* getResponderType() override final
    {
        return "SSEvents";
    }

private:
    // Handler
    RaftWebHandler* _pWebHandler;

    // Params
    RaftWebRequestParams _reqParams;

    // Events callback
    RaftWebSSEventsCB _eventsCB;

    // Vars
    String _requestStr;
    bool _isInitialResponse;

    // Queue for sending frames over the event channel
    static const uint32_t EVENT_TX_QUEUE_SIZE = 2;
    ThreadSafeQueue<RaftWebSSEvent> _txQueue;

    // Retry
    static const uint32_t MAX_SSEVENT_SEND_RETRY_MS = 1;

    // Generate event message
    String generateEventMessage(const String& msgStr, const String& eventStr, uint32_t id);
};
