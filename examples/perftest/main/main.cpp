/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @file		main
/// @brief		Example main file for the perftest example webserver
/// @details	Handles initialisation and startup of the system
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftCoreApp.h"
#include "RegisterSysMods.h"
#include "RegisterWebServer.h"
#include "RaftSysMod.h"
#include "RestAPIEndpointManager.h"
#include "CommsCoreIF.h"
#include "CommsChannelMsg.h"

static const char* MODULE_PREFIX = "Perftest";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PerftestSysMod - custom SysMod for perftest REST API endpoints and WebSocket testing
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class PerftestSysMod : public RaftSysMod
{
public:
    PerftestSysMod(const char* pModuleName, RaftJsonIF& sysConfig)
        : RaftSysMod(pModuleName, sysConfig)
    {
    }

    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new PerftestSysMod(pModuleName, sysConfig);
    }

    void addRestAPIEndpoints(RestAPIEndpointManager& endpointManager) override
    {
        // Add test GET endpoint
        endpointManager.addEndpoint("test",
                RestAPIEndpoint::ENDPOINT_CALLBACK,
                RestAPIEndpoint::ENDPOINT_GET,
                std::bind(&PerftestSysMod::apiTest, this,
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "test");

        // Add upload POST endpoint with file block handler
        endpointManager.addEndpoint("upload",
                RestAPIEndpoint::ENDPOINT_CALLBACK,
                RestAPIEndpoint::ENDPOINT_POST,
                std::bind(&PerftestSysMod::apiUploadComplete, this,
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                "Upload file",
                "application/json",
                nullptr,
                RestAPIEndpoint::ENDPOINT_CACHE_NEVER,
                nullptr,
                nullptr,
                std::bind(&PerftestSysMod::apiUploadBlock, this,
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void addCommsChannels(CommsCoreIF& commsCore) override
    {
        RaftSysMod::addCommsChannels(commsCore);
        _pCommsCore = &commsCore;
    }

    void loop() override
    {
        RaftSysMod::loop();

        // Send periodic messages on websocket channels
        if (_pCommsCore && (millis() - _lastWsSendMs > 200))
        {
            _lastWsSendMs = millis();
            for (uint32_t i = 0; i < NUM_WS_CHANNELS_TO_USE; i++)
            {
                // Build message
                String msgStr = "Hello from server ch " + String(i) + " count " + String(_wsMsgCtr++);

                // Send via comms core
                CommsChannelMsg msg;
                msg.setFromBuffer(_wsChannelIdBase + i, MSG_PROTOCOL_RAWCMDFRAME,
                        0, MSG_TYPE_PUBLISH,
                        (const uint8_t*)msgStr.c_str(), msgStr.length());
                _pCommsCore->outboundHandleMsg(msg);
            }
        }
    }

    void postSetup() override
    {
        // Test creating a small file and reading it back
        String testFileName = "/local/test.txt";
        FILE* testFile = fopen(testFileName.c_str(), "w+");
        if (testFile)
        {
            fprintf(testFile, "Hello world");
            fclose(testFile);
            testFile = fopen(testFileName.c_str(), "r");
            if (testFile)
            {
                char buf[100];
                fgets(buf, sizeof(buf), testFile);
                fclose(testFile);
                LOG_I(MODULE_PREFIX, "File test OK - read back '%s'", buf);
            }
            else
            {
                LOG_E(MODULE_PREFIX, "File test failed to read file");
            }
        }
        else
        {
            LOG_E(MODULE_PREFIX, "File test failed to create file");
        }
    }

private:
    // Comms core for WS message sending
    CommsCoreIF* _pCommsCore = nullptr;

    // WebSocket periodic send state
    uint32_t _lastWsSendMs = 0;
    uint32_t _wsMsgCtr = 0;
    static const uint32_t NUM_WS_CHANNELS_TO_USE = 2;
    static const uint32_t _wsChannelIdBase = 200;

    // File upload state
    FILE* _pUploadFile = nullptr;

    // REST API callbacks
    RaftRetCode apiTest(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
    {
        respStr = "Hello from testEndpointCallback";
        return RAFT_OK;
    }

    RaftRetCode apiUploadComplete(const String& reqStr, String& respStr, const APISourceInfo& sourceInfo)
    {
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true);
    }

    RaftRetCode apiUploadBlock(const String& req, FileStreamBlock& fileStreamBlock, const APISourceInfo& sourceInfo)
    {
        LOG_I(MODULE_PREFIX, "uploadFileBlock %s filename %s blockLen %d firstBlock %d finalBlock %d",
                req.c_str(), fileStreamBlock.filename, fileStreamBlock.blockLen,
                fileStreamBlock.firstBlock, fileStreamBlock.finalBlock);

        // Check for first block
        if (fileStreamBlock.firstBlock)
        {
            if (_pUploadFile)
            {
                LOG_E(MODULE_PREFIX, "uploadFileBlock file already open, closing");
                fclose(_pUploadFile);
                _pUploadFile = nullptr;
                return RaftRetCode::RAFT_BUSY;
            }
            String filename = String("/local/") + fileStreamBlock.filename;
            _pUploadFile = fopen(filename.c_str(), "w+");
            if (!_pUploadFile)
            {
                LOG_E(MODULE_PREFIX, "uploadFileBlock failed to open file %s", filename.c_str());
                return RaftRetCode::RAFT_CANNOT_START;
            }
            LOG_I(MODULE_PREFIX, "uploadFileBlock opened file %s", filename.c_str());
        }

        if (!_pUploadFile)
        {
            LOG_E(MODULE_PREFIX, "uploadFileBlock file not open");
            return RaftRetCode::RAFT_CANNOT_START;
        }

        // Seek to position
        if (fseek(_pUploadFile, fileStreamBlock.filePos, SEEK_SET) != 0)
        {
            LOG_E(MODULE_PREFIX, "uploadFileBlock failed to seek to position %d", fileStreamBlock.filePos);
            return RaftRetCode::RAFT_OTHER_FAILURE;
        }

        // Write data to file
        const uint8_t* pData = fileStreamBlock.pBlock;
        uint32_t dataPos = 0;
        const uint32_t WRITE_CHUNK_LEN = 1000;
        while (dataPos < fileStreamBlock.blockLen)
        {
            uint32_t writeLen = fileStreamBlock.blockLen - dataPos;
            if (writeLen > WRITE_CHUNK_LEN)
                writeLen = WRITE_CHUNK_LEN;
            if (fwrite(pData + dataPos, 1, writeLen, _pUploadFile) != writeLen)
            {
                LOG_E(MODULE_PREFIX, "uploadFileBlock failed to write len %d at %d - total blockLen %d",
                        writeLen, dataPos, fileStreamBlock.blockLen);
                return RaftRetCode::RAFT_OTHER_FAILURE;
            }
            dataPos += writeLen;
        }

        // Check for final block
        if (fileStreamBlock.finalBlock)
        {
            fclose(_pUploadFile);
            _pUploadFile = nullptr;
            LOG_I(MODULE_PREFIX, "uploadFileBlock closed file %s", fileStreamBlock.filename);
        }
        return RaftRetCode::RAFT_OK;
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create the app
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftCoreApp raftCoreApp;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Entry point
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" void app_main(void)
{
    // Register SysMods from RaftSysMods library
    RegisterSysMods::registerSysMods(raftCoreApp.getSysManager());

    // Register WebServer from RaftWebServer library
    RegisterSysMods::registerWebServer(raftCoreApp.getSysManager());

    // Register perftest SysMod
    raftCoreApp.registerSysMod("PerftestSysMod", PerftestSysMod::create, true);

    // Loop forever
    while (1)
    {
        // Loop the app
        raftCoreApp.loop();
    }
}
