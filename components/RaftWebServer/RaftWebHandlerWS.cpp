/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftWebHandlerWS.h"
#include <RaftUtils.h>

#define WARN_WS_SEND_APP_DATA_FAIL

#define DEBUG_WEB_HANDLER_WS
#define DEBUG_WS_SEND_APP_DATA

static const char* MODULE_PREFIX = "RaftWebHandlerWS";

#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)

RaftWebResponder* RaftWebHandlerWS::getNewResponder(const RaftWebRequestHeader& requestHeader, 
            const RaftWebRequestParams& params, 
            RaftHttpStatusCode &statusCode
            )
{
    // Check if websocket request
    if (requestHeader.reqConnType != REQ_CONN_TYPE_WEBSOCKET)
        return NULL;

    String wsPath = _wsConfig.getString("pfix", "ws");
    wsPath = wsPath.startsWith("/") ? wsPath : "/" + wsPath;

#ifdef DEBUG_WEB_HANDLER_WS
    LOG_I(MODULE_PREFIX, "getNewResponder: req %s this prefix %s", requestHeader.URIAndParams.c_str(), wsPath.c_str());
#endif

    // Check for WS prefix
    if (!requestHeader.URL.startsWith(wsPath))
    {
#ifdef DEBUG_WEB_HANDLER_WS        
        LOG_I(MODULE_PREFIX, "getNewResponder unmatched ws req %s != expected %s", 
                        requestHeader.URL.c_str(), wsPath.c_str());
#endif
        // We don't change the status code here as we didn't find a match
        return NULL;
    }

    // Check limits on connections
    uint32_t wsConnIdxAvailable = UINT32_MAX;
    for (uint32_t wsConnIdx = 0; wsConnIdx < _channelIDUsage.size(); wsConnIdx++)
    {
        if (!_channelIDUsage[wsConnIdx].isUsed)
        {
            wsConnIdxAvailable = wsConnIdx;
            break;
        }
    }
    if (wsConnIdxAvailable == UINT32_MAX)
    {
        statusCode = HTTP_STATUS_SERVICEUNAVAILABLE;
        LOG_W(MODULE_PREFIX, "getNewResponder pfix %s no free connections", wsPath.c_str());
        return NULL;
    }

    // Looks like we can handle this so create a new responder object
    uint32_t channelID = _channelIDUsage[wsConnIdxAvailable].channelID;
    RaftWebResponder* pResponder = new RaftWebResponderWS(this, params, requestHeader.URL, 
                _canAcceptRxMsgCB, _rxMsgCB, 
                channelID,
                _wsConfig.getLong("pktMaxBytes", 1000),
                _wsConfig.getLong("txQueueMax", 10),
                _wsConfig.getLong("pingMs", 2000),
                _wsConfig.getLong("noPongMs", 5000),
                _wsConfig.getString("content", "binary")
                );

    if (pResponder)
    {
        statusCode = HTTP_STATUS_OK;
        _channelIDUsage[wsConnIdxAvailable].isUsed = true;
    }

    // Debug
#ifdef DEBUG_WEB_HANDLER_WS
    LOG_I(MODULE_PREFIX, "getNewResponder constructed new responder %lx channelID %d uri %s", 
                    (unsigned long)pResponder, channelID, requestHeader.URL.c_str());
#endif
    // Return new responder - caller must clean up by deleting object when no longer needed
    return pResponder;
}

void RaftWebHandlerWS::responderDelete(RaftWebResponderWS* pResponder)
{
    // Get the channelID
    uint32_t channelID = UINT32_MAX;
    if (pResponder->getChannelID(channelID))
    {
        // Find the channelID slot
        for (auto &channelIDUsage : _channelIDUsage)
        {
            if (channelIDUsage.isUsed && (channelIDUsage.channelID == channelID))
            {
                channelIDUsage.isUsed = false;
                // Debug
                LOG_I(MODULE_PREFIX, "responderDelete deleted responder %p channelID %d OK", pResponder, channelID);
                return;
            }
        }
        // Debug
        LOG_W(MODULE_PREFIX, "responderDelete %p channelID %d NOT FOUND", pResponder, channelID);
    }
    else
    {
        LOG_W(MODULE_PREFIX, "responderDelete responder %p channelID %d not available", pResponder, channelID);
    }
}

#elif defined(FEATURE_WEB_SERVER_USE_MONGOOSE)

bool RaftWebHandlerWS::handleRequest(struct mg_connection *c, int ev, void *ev_data)
{
    // Check for HTTP message
    if (ev == MG_EV_HTTP_MSG) 
    {
        // Mongoose http message
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;

        // Get request string and method string
        String reqStr = String(hm->uri.ptr, hm->uri.len);
        String methodStr = String(hm->method.ptr, hm->method.len);

        // Check for WS endpoint
        String wsPath = _wsConfig.getString("pfix", "ws");
        wsPath = wsPath.startsWith("/") ? wsPath : "/" + wsPath;        
        if (!reqStr.startsWith(wsPath))
            return false;

        // Check limits on connections
        uint32_t wsConnIdxAvailable = UINT32_MAX;
        for (uint32_t wsConnIdx = 0; wsConnIdx < _channelIDUsage.size(); wsConnIdx++)
        {
            if (!_channelIDUsage[wsConnIdx].isUsed)
            {
                wsConnIdxAvailable = wsConnIdx;
                break;
            }
        }
        if (wsConnIdxAvailable == UINT32_MAX)
        {
            mg_http_reply(c, HTTP_STATUS_SERVICEUNAVAILABLE, "", "");
            LOG_W(MODULE_PREFIX, "handleRequest pfix %s no free connections", wsPath.c_str());
            return true;
        }

        // Upgrade
        mg_ws_upgrade(c, hm, NULL);

        // Put the channel ID into the connection data field
        Raft::setBEUint32((uint8_t*)c->data, 0, _channelIDUsage[wsConnIdxAvailable].channelID);
        _channelIDUsage[wsConnIdxAvailable].isUsed = true;

        // Debug
        LOG_I(MODULE_PREFIX, "handleRequest WS conn upgraded ok reqStr %s connIdxAvail %d channelID %d", 
                        reqStr.c_str(), wsConnIdxAvailable, _channelIDUsage[wsConnIdxAvailable].channelID);

        // // Check endpoint callback
        // String respStr;
        // if (endpoint.restApiFn)
        // {
        //     // Call endpoint
        //     endpoint.restApiFn(reqStr, respStr, _webServerSettings._restAPIChannelID);

        //     // Debug
        //     LOG_I(MODULE_PREFIX, "handleRequest respStr %s", respStr.c_str());
        // }

        // // Send start of response
        // mg_printf(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n");

        // // Add standard headers
        // for (auto& header : _standardHeaders)
        // {
        //     mg_printf(c, "%s: %s\r\n", header.name.c_str(), header.value.c_str());
        // }

        // // End of header
        // mg_printf(c, "\r\n");

        // // Send response
        // mg_http_write_chunk(c, respStr.c_str(), respStr.length());
        // mg_http_printf_chunk(c, "");
        
        // mg_http_reply(c, 200, "", "{\"result\": \"%.*s\"}\n", (int) hm->uri.len, hm->uri.ptr);
        // return _respStr.length();

    //     LOG_I(MODULE_PREFIX, "canHandle matching %s with req %.*s", _restAPIPrefix.c_str(), (int) hm->uri.len, hm->uri.ptr);

    //     if (mg_http_match_uri(hm, _restAPIPrefix.c_str())) 
    //     {
            
    //         // Debug
    //         LOG_I(MODULE_PREFIX, "canHandle matched %s req %.*s", _restAPIPrefix.c_str(), (int) hm->uri.len, hm->uri.ptr);

    // //         // // TODO - remove
    // //         // // Print some statistics about currently established connections
    // //         // mg_printf(c, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    // //         // mg_http_printf_chunk(c, "ID PROTO TYPE      LOCAL           REMOTE\n");
    // //         // for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
    // //         // mg_http_printf_chunk(c, "%-3lu %4s %s %M %M\n", t->id,
    // //         //                         t->is_udp ? "UDP" : "TCP",
    // //         //                         t->is_listening  ? "LISTENING"
    // //         //                         : t->is_accepted ? "ACCEPTED "
    // //         //                                         : "CONNECTED",
    // //         //                         mg_print_ip, &t->loc, mg_print_ip, &t->rem);
    // //         // }
    // //         // mg_http_printf_chunk(c, "");  // Don't forget the last empty chunk

    //         return true;
    //     }
        
    //     struct mg_http_serve_opts opts = 
    //     {
    //         .root_dir = _baseFolder.c_str(),
    //         .ssi_pattern = NULL,
    //         .extra_headers = NULL,
    //         .mime_types = NULL,
    //         .page404 = NULL,
    //         .fs = NULL,
    //     };
    //     mg_http_serve_dir(c, hm, &opts);

        // Handled ok
        return true;
    }

    // Check for WS message
    if (ev == MG_EV_WS_MSG)
    {
        // WS message
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;

        // Get the channel ID from the connection data field
        const uint8_t* pData = (uint8_t*)c->data;
        uint32_t channelID = Raft::getBEUint32AndInc(pData);

        // Debug
        LOG_I(MODULE_PREFIX, "handleRequest WS channelID %d len %d", channelID, wm->data.len);

        // Check message callback
        if (_rxMsgCB)
        {
            // Call callback with the message
            _rxMsgCB(channelID, (const uint8_t*)wm->data.ptr, wm->data.len);
        }
        return true;
    }

    // Check for POLL event
    if (ev == MG_EV_POLL)
    {
        // Use poll event for sending
        // Check for data waiting to be sent
        RaftWebDataFrame frame;
        if (_txQueue.peek(frame))
        {
            // Get the channel ID from the connection data field
            const uint8_t* pData = (uint8_t*)c->data;
            uint32_t channelID = Raft::getBEUint32AndInc(pData);

            // Check the channel ID is for this channel
            if (channelID == frame.getChannelID())
            {

                // Send the message
                mg_ws_send(c, frame.getData(), frame.getLen(), WEBSOCKET_OP_BINARY);

                // Debug
#ifdef DEBUG_WS_SEND_APP_DATA
                LOG_I(MODULE_PREFIX, "sendMsg channelID %d len %d", channelID, frame.getLen());
#endif
                // Remove from queue
                _txQueue.get(frame);
            }
            else
            {
                // Check for timeout on message
                if (Raft::isTimeout(millis(), frame.getFrameTimeMs(), MAX_TIME_IN_QUEUE_MS))
                {
                    // Timeout - remove from queue
                    _txQueue.get(frame);
#ifdef WARN_WS_SEND_APP_DATA_FAIL                    
                    LOG_W(MODULE_PREFIX, "sendMsg timeout channelID %d len %d", channelID, frame.getLen());
#endif
                }
            }
        }
    }

    return false;
}

bool RaftWebHandlerWS::sendMsg(const uint8_t* pBuf, uint32_t bufLen, uint32_t channelID)
{
    // Add to queue - don't block if full
    RaftWebDataFrame frame(channelID, pBuf, bufLen, millis());
    bool putRslt = _txQueue.put(frame, MAX_WAIT_FOR_TX_QUEUE_MS);
    if (!putRslt)
    {
#ifdef WARN_WS_SEND_APP_DATA_FAIL
        LOG_W(MODULE_PREFIX, "sendMsg add to txQueue failed len %d count %d maxLen %d", bufLen, _txQueue.count(), _txQueue.maxLen());
#endif
        return false;
    }
#ifdef DEBUG_WS_SEND_APP_DATA
        LOG_I(MODULE_PREFIX, "sendMsg len %d", bufLen);
#endif
    return true;
}

#endif
