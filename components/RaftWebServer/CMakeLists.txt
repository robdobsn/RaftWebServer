cmake_minimum_required(VERSION 3.16)

idf_component_register(
  SRCS
    "RaftClientConnESP8266.cpp"
    "RaftClientConnNetconn.cpp"
    "RaftClientConnSockets.cpp"
    "RaftClientListener.cpp"
    "RaftWebConnection.cpp"
    "RaftWebConnManager.cpp"
    "RaftWebConnManagerEspIdf.cpp"
    "RaftWebHandlerStaticFiles.cpp"
    "RaftWebInterface.cpp"
    "RaftWebMultipart.cpp"
    "RaftWebResponderFile.cpp"
    "RaftWebResponderRestAPI.cpp"
    "RaftWebResponderWS.cpp"
    "RaftWebServer.cpp"
    "RaftWebSocketLink.cpp"
  INCLUDE_DIRS
    "."
  REQUIRES
    raftcore-src
    mbedtls
    esp_http_server
)
