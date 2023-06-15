cmake_minimum_required(VERSION 3.16)

idf_component_register(
  SRCS
    "components/RaftWebServer/RaftClientConnESP8266.cpp"
    "components/RaftWebServer/RaftClientConnNetconn.cpp"
    "components/RaftWebServer/RaftClientConnSockets.cpp"
    "components/RaftWebServer/RaftClientListener.cpp"
    "components/RaftWebServer/RaftWebConnection.cpp"
    "components/RaftWebServer/RaftWebConnManager.cpp"
    "components/RaftWebServer/RaftWebConnManagerEspIdf.cpp"
    "components/RaftWebServer/RaftWebHandlerStaticFiles.cpp"
    "components/RaftWebServer/RaftWebInterface.cpp"
    "components/RaftWebServer/RaftWebMultipart.cpp"
    "components/RaftWebServer/RaftWebResponderFile.cpp"
    "components/RaftWebServer/RaftWebResponderRestAPI.cpp"
    "components/RaftWebServer/RaftWebResponderWS.cpp"
    "components/RaftWebServer/RaftWebServer.cpp"
    "components/RaftWebServer/RaftWebSocketLink.cpp"
  INCLUDE_DIRS
    "components/RaftWebServer/."
  REQUIRES
    raftcore-src
    mbedtls
    esp_http_server
)
