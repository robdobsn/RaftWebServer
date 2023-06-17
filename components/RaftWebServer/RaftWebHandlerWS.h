/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftWebServer
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftWebHandler.h"
#include <Logger.h>
#include <RaftWebRequestHeader.h>
#include <RaftWebResponderWS.h>
#include <ConfigBase.h>
#include <vector>

// #define DEBUG_WEB_HANDLER_WS_NEW_RESPONDER

class RaftWebHandlerWS : public RaftWebHandler
{
public:
    RaftWebHandlerWS(const ConfigBase& config,
            RaftWebSocketCanAcceptCB canAcceptRxMsgCB, RaftWebSocketMsgCB rxMsgCB)
            : _canAcceptRxMsgCB(canAcceptRxMsgCB), _rxMsgCB(rxMsgCB)
    {
        // Store config
        _wsConfig = config;

        // Setup channelIDs mapping
        uint32_t maxConn = _wsConfig.getLong("maxConn", 1);
        _channelIDUsage.clear();
        _channelIDUsage.resize(maxConn);
    }
    virtual ~RaftWebHandlerWS()
    {
    }
    virtual bool isWebSocketHandler() const override final
    {
        return true;
    }
    virtual const char* getName() const override
    {
        return "HandlerWS";
    }
    virtual String getBaseURL() const override
    {
        return _wsConfig.getString("pfix", "ws");
    }
#if defined(FEATURE_WEB_SERVER_USE_ORIGINAL)
    virtual RaftWebResponder* getNewResponder(const RaftWebRequestHeader& requestHeader, 
                const RaftWebRequestParams& params, 
                RaftHttpStatusCode &statusCode
                ) override final
    {
        // Check if websocket request
        if (requestHeader.reqConnType != REQ_CONN_TYPE_WEBSOCKET)
            return NULL;

        String wsPath = _wsConfig.getString("pfix", "ws");
        wsPath = wsPath.startsWith("/") ? wsPath : "/" + wsPath;

        // LOG_I("WebHandlerWS", "getNewResponder: req %s this prefix %s", requestHeader.URIAndParams.c_str(), wsPath.c_str());

        // Check for WS prefix
        if (!requestHeader.URL.startsWith(wsPath))
        {
            // LOG_W("WebHandlerWS", "getNewResponder unmatched ws req %s != expected %s", 
            //                 requestHeader.URL.c_str(), wsPath.c_str());
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
            LOG_W("WebHandlerWS", "getNewResponder pfix %s no free connections", wsPath.c_str());
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
#ifdef DEBUG_WEB_HANDLER_WS_NEW_RESPONDER
        LOG_I("WebHandlerWS", "getNewResponder constructed new responder %lx channelID %d uri %s", 
                        (unsigned long)pResponder, channelID, requestHeader.URL.c_str());
#endif
        // Return new responder - caller must clean up by deleting object when no longer needed
        return pResponder;
    }
#endif

    // Setup websocket channel ID
    void setupWebSocketChannelID(uint32_t wsConnIdx, uint32_t chanID)
    {
        // Check valid
        if (wsConnIdx >= _channelIDUsage.size())
            return;
        _channelIDUsage[wsConnIdx].channelID = chanID;
        _channelIDUsage[wsConnIdx].isUsed = false;
    }

    void responderDelete(RaftWebResponderWS* pResponder)
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
                    LOG_I("WebHandlerWS", "responderDelete deleted responder %p channelID %d OK", pResponder, channelID);
                    return;
                }
            }
            // Debug
            LOG_W("WebHandlerWS", "responderDelete %p channelID %d NOT FOUND", pResponder, channelID);
        }
        else
        {
            LOG_W("WebHandlerWS", "responderDelete responder %p channelID %d not available", pResponder, channelID);
        }
    }

private:
    // Config
    ConfigBase _wsConfig;

    // WS interface functions
    RaftWebSocketCanAcceptCB _canAcceptRxMsgCB;
    RaftWebSocketMsgCB _rxMsgCB;

    // Web socket protocol channelIDs
    class ChannelIDUsage
    {
    public:
        uint32_t channelID = UINT32_MAX;
        bool isUsed = false;
    };
    std::vector<ChannelIDUsage> _channelIDUsage;
};
