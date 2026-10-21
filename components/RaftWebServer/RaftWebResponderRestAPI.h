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
#include "ArduinoTime.h"
#include "RaftUtils.h"
#include "RaftWebResponder.h"
#include "RaftWebRequestParams.h"
#include "RaftWebConnection.h"
#include "RaftWebMultipart.h"
#include "APISourceInfo.h"
#include <vector>

// #define APPLY_MIN_GAP_BETWEEN_API_CALLS_MS 200

class RaftWebHandler;

class RaftWebResponderRestAPI : public RaftWebResponder
{
public:
    RaftWebResponderRestAPI(const RaftWebServerRestEndpoint& endpoint, RaftWebHandler* pWebHandler, 
                        const RaftWebRequestParams& params, String& reqStr, 
                        const RaftWebRequestHeaderExtract& headerExtract,
                        uint32_t channelID);
    virtual ~RaftWebResponderRestAPI();

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
        return "API";
    }

    // Ready to receive data
    virtual bool readyToReceiveData() override final;

    // Service (handles GET requests so that their response can be deferred)
    virtual void loop() override final;

    // Response availability (false while a deferred response is pending)
    virtual bool responseAvailable() override final;
    virtual bool isStdHeaderRequired() override final;

    // Waiting for a deferred response (the connection isn't idle meanwhile)
    virtual bool isAwaitingResponse() override final
    {
        return _deferPending;
    }

    // Complete a deferred response (main task only)
    // @return true if a responder with a pending deferred response for this token was found
    static bool completeDeferredResponse(uint32_t deferToken, const String& respStr);

private:
    // Endpoint
    RaftWebServerRestEndpoint _endpoint;

    // Handler
    RaftWebHandler* _pWebHandler;

    // Params
    RaftWebRequestParams _reqParams;

    // Extract from header
    RaftWebRequestHeaderExtract _headerExtract;

    // Vars
    bool _endpointCalled;

    // Deferred response
    bool _deferPending = false;
    uint32_t _deferToken = 0;

    // Responders which currently exist (so a deferred response is only delivered to a responder which still
    // exists - the client may have disconnected and the responder been deleted)
    static std::vector<RaftWebResponderRestAPI*> _liveResponders;
    String _requestStr;
    String _respStr;
    uint32_t _respStrPos;
    uint32_t _sendStartMs;
    static const uint32_t SEND_DATA_OVERALL_TIMEOUT_MS = 1 * 60 * 1000;

    // Data received
    uint32_t _numBytesReceived;

    // Multipart parser
    RaftWebMultipart _multipartParser;

    // API source
    APISourceInfo _apiSourceInfo;

    // Throttle back API requests
#ifdef APPLY_MIN_GAP_BETWEEN_API_CALLS_MS    
    uint32_t _lastFileReqMs;
#endif

    // Helpers
    void multipartOnEvent(void* pCtx, RaftMultipartEvent event, const uint8_t *pBuf, uint32_t pos);
    RaftRetCode multipartOnData(void* pCtx, const uint8_t *pBuf, uint32_t len, RaftMultipartForm& formInfo, 
                uint32_t contentPos, bool isFinalPart);
    void multipartOnHeaderNameValue(void* pCtx, const String& name, const String& val);
    void callEndpoint(bool allowDefer);
};
