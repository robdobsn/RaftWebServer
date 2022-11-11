/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ArduinoOrAlt.h>
#include <Logger.h>
#include <ArduinoTime.h>
#include <RaftUtils.h>
#include "RdWebResponder.h"
#include <RdWebRequestParams.h>
#include <RdWebConnection.h>
#include "RdWebMultipart.h"
#include "APISourceInfo.h"

// #define APPLY_MIN_GAP_BETWEEN_API_CALLS_MS 200

class RdWebHandler;

class RdWebResponderRestAPI : public RdWebResponder
{
public:
    RdWebResponderRestAPI(const RdWebServerRestEndpoint& endpoint, RdWebHandler* pWebHandler, 
                        const RdWebRequestParams& params, String& reqStr, 
                        const RdWebRequestHeaderExtract& headerExtract,
                        uint32_t channelID);
    virtual ~RdWebResponderRestAPI();

    // Handle inbound data
    virtual bool handleData(const uint8_t* pBuf, uint32_t dataLen) override final;

    // Start responding
    virtual bool startResponding(RdWebConnection& request) override final;

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

    // Ready for data
    virtual bool readyForData() override final;

private:
    // Endpoint
    RdWebServerRestEndpoint _endpoint;

    // Handler
    RdWebHandler* _pWebHandler;

    // Params
    RdWebRequestParams _reqParams;

    // Extract from header
    RdWebRequestHeaderExtract _headerExtract;

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
    RdWebMultipart _multipartParser;

    // API source
    APISourceInfo _apiSourceInfo;

    // Throttle back API requests
#ifdef APPLY_MIN_GAP_BETWEEN_API_CALLS_MS    
    uint32_t _lastFileReqMs;
#endif

    // Helpers
    void multipartOnEvent(RdMultipartEvent event, const uint8_t *pBuf, uint32_t pos);
    void multipartOnData(const uint8_t *pBuf, uint32_t len, RdMultipartForm& formInfo, 
                uint32_t contentPos, bool isFinalPart);
    void multipartOnHeaderNameValue(const String& name, const String& val);
};
