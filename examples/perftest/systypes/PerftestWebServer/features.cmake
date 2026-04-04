set(IDF_TARGET "esp32s3")
set(RAFT_COMPONENTS
    RaftCore@main
    RaftSysMods@main
    RaftWebServer@main
)
set(FS_TYPE "spiffs")
set(FS_IMAGE_PATH "../PerftestWebServer/FSImage")
