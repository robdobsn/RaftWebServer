/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftUtils.h"
#include "RaftWebResponderSSEvents.h"
#include "RaftWebConnection.h"
#include "RaftWebServerSettings.h"
#include "RaftWebConnDefs.h"

// #define DEBUG_RESPONDER_EVENTS
#define WARN_EVENTS_SEND_APP_DATA_FAIL

#if defined(DEBUG_RESPONDER_EVENTS) || defined(WARN_EVENTS_SEND_APP_DATA_FAIL)
static const char *MODULE_PREFIX = "RaftWebRespSSEvents";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftWebResponderSSEvents::RaftWebResponderSSEvents(RaftWebHandler *pWebHandler, const RaftWebRequestParams &params,
                                               const String &reqStr, RaftWebSSEventsCB eventsCallback,
                                               const RaftWebServerSettings &webServerSettings)
    : _reqParams(params), _eventsCB(eventsCallback)
{
    // Store socket info
    _pWebHandler = pWebHandler;
    _requestStr = reqStr;
    _isInitialResponse = true;
    _txQueue.setMaxLen(EVENT_TX_QUEUE_SIZE);
}

RaftWebResponderSSEvents::~RaftWebResponderSSEvents()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service - called frequently
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebResponderSSEvents::loop()
{
    // Check for data waiting to be sent
    RaftWebSSEvent event;
    if (_txQueue.get(event))
    {
#ifdef DEBUG_RESPONDER_EVENTS
        LOG_W(MODULE_PREFIX, "loop sendMsg group %s content %s", event.getGroup().c_str(), event.getContent().c_str());
#endif
        // Format message
        String outMsg = generateEventMessage(event.getContent(), event.getGroup(), time(NULL));
        RaftWebConnSendFn rawSendFn = _reqParams.getWebConnRawSend();
        if (rawSendFn)
        {
            bool rslt = rawSendFn((const uint8_t*)outMsg.c_str(), outMsg.length(), MAX_SSEVENT_SEND_RETRY_MS) == RaftWebConnSendRetVal::WEB_CONN_SEND_OK;
            if (!rslt)
                _isActive = false;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle inbound data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderSSEvents::handleInboundData(const SpiramAwareUint8Vector &data)
{
#ifdef DEBUG_RESPONDER_EVENTS
    String outStr(pBuf, dataLen);
    LOG_I(MODULE_PREFIX, "handleInboundData len %d %s", data.size(), outStr.c_str());
#endif
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start responding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderSSEvents::startResponding(RaftWebConnection &request)
{
    // Now active
    _isActive = true;
#ifdef DEBUG_RESPONDER_EVENTS
    LOG_I(MODULE_PREFIX, "startResponding isActive %d", _isActive);
#endif
    return _isActive;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if any reponse data is available
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderSSEvents::responseAvailable()
{
    // TODO - review this
    return _isActive && _isInitialResponse;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get next response data
/// @param maxLen Maximum length to return
/// @return Response data
SpiramAwareUint8Vector RaftWebResponderSSEvents::getResponseNext(uint32_t maxLen)
{
    // Response
    const char SSEVENT_RESPONSE[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Accept-Ranges: none\r\n\r\n";

    // Check if initial response
    if (_isInitialResponse)
    {
        // Response done
        _isInitialResponse = false;

        // Debug
#ifdef DEBUG_RESPONDER_EVENTS
        LOG_I(MODULE_PREFIX, "getResponseNext isActive %d", _isActive);
#endif

        // Return
        return SpiramAwareUint8Vector((const uint8_t *)SSEVENT_RESPONSE, (const uint8_t *)SSEVENT_RESPONSE+strlen(SSEVENT_RESPONSE));
    }

    SpiramAwareUint8Vector respData;
    // TODO - review this
    // // Get
    // uint32_t respLen = _webSocketLink.getTxData(pBuf, bufMaxLen);

    // Done response
#ifdef DEBUG_RESPONDER_EVENTS
    if (respData.size() > 0)
    {
        LOG_I(MODULE_PREFIX, "getResponseNext respLen %d isActive %d", respData.size(), _isActive);
    }
#endif
    return respData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get content type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char *RaftWebResponderSSEvents::getContentType()
{
    return "application/octet-stream";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Leave connection open
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftWebResponderSSEvents::leaveConnOpen()
{
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send event content and group
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftWebResponderSSEvents::sendEvent(const char* eventContent, const char* eventGroup)
{
    // Add to queue - don't block if full
    RaftWebSSEvent event(eventContent, eventGroup);
    bool putRslt = _txQueue.put(event);
    if (!putRslt)
    {
#ifdef WARN_EVENTS_SEND_APP_DATA_FAIL
        LOG_W(MODULE_PREFIX, "sendEvent failed group %s content %s", eventGroup, eventContent);
#endif
    }
    else
    {
#ifdef DEBUG_RESPONDER_EVENTS
        LOG_W(MODULE_PREFIX, "sendEvent ok group %s content %s", eventGroup, eventContent);
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Send a frame of data
// From ESPAsyncWebServer
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftWebResponderSSEvents::generateEventMessage(const String& msgStr, const String& eventStr, uint32_t id)
{
    String ev = "";

    if (id)
    {
        ev += "id: ";
        ev += String(id);
        ev += "\r\n";
    }

    if (pEvent != NULL)
    {
        ev += "event: ";
        ev += String(pEvent);
        ev += "\r\n";
    }

    char* pMsg = msgStr.c_str();
    size_t messageLen = msgStr.length();
    char *lineStart = pMsg;
    char *lineEnd;
    do
    {
        char *nextN = strchr(lineStart, '\n');
        char *nextR = strchr(lineStart, '\r');
        if (nextN == NULL && nextR == NULL)
        {
            size_t llen = (pMsg + messageLen) - lineStart;
            char *ldata = (char*)malloc(llen + 1);
            if (ldata != NULL)
            {
                memcpy(ldata, lineStart, llen);
                ldata[llen] = 0;
                ev += "data: ";
                ev += ldata;
                ev += "\r\n\r\n";
                free(ldata);
            }
            lineStart = pMsg + messageLen;
        }
        else
        {
            char *nextLine = NULL;
            if (nextN != NULL && nextR != NULL)
            {
                if (nextR < nextN)
                {
                    lineEnd = nextR;
                    if (nextN == (nextR + 1))
                        nextLine = nextN + 1;
                    else
                        nextLine = nextR + 1;
                }
                else
                {
                    lineEnd = nextN;
                    if (nextR == (nextN + 1))
                        nextLine = nextR + 1;
                    else
                        nextLine = nextN + 1;
                }
            }
            else if (nextN != NULL)
            {
                lineEnd = nextN;
                nextLine = nextN + 1;
            }
            else
            {
                lineEnd = nextR;
                nextLine = nextR + 1;
            }

            size_t llen = lineEnd - lineStart;
            char *ldata = (char *)malloc(llen + 1);
            if (ldata != NULL)
            {
                memcpy(ldata, lineStart, llen);
                ldata[llen] = 0;
                ev += "data: ";
                ev += ldata;
                ev += "\r\n";
                free(ldata);
            }
            lineStart = nextLine;
            if (lineStart == (pMsg + messageLen))
                ev += "\r\n";
        }
    } while (lineStart < (pMsg + messageLen));

    return ev;
}
