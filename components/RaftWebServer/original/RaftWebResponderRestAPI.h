/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <RaftArduino.h>
#include <Logger.h>
#include <ArduinoTime.h>
#include <RaftUtils.h>
#include "RaftWebResponder.h"
#include <RaftWebRequestParams.h>
#include <RaftWebConnection.h>
#include "RaftWebMultipart.h"
#include "APISourceInfo.h"

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
};
