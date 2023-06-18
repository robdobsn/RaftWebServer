cmake_minimum_required(VERSION 3.16)

# message(STATUS "==========================================================")

# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()

# message(STATUS "==========================================================")

idf_component_register(
  SRCS
    "RaftWebServer.cpp"
    "RaftWebHandlerRestAPI.cpp"
    "RaftWebHandlerStaticFiles.cpp"
    "RaftWebHandlerWS.cpp"
    "espidf/RaftWebConnManager_espidf.cpp"
    "mongoose/RaftWebConnManager_mongoose.cpp"
    "original/RaftClientConnNetconn.cpp"
    "original/RaftClientConnSockets.cpp"
    "original/RaftClientListener.cpp"
    "original/RaftWebConnection.cpp"
    "original/RaftWebConnManager_original.cpp"
    "original/RaftWebInterface.cpp"
    "original/RaftWebMultipart.cpp"
    "original/RaftWebResponderFile.cpp"
    "original/RaftWebResponderRestAPI.cpp"
    "original/RaftWebResponderWS.cpp"
    "original/RaftWebSocketLink.cpp"
  INCLUDE_DIRS
    "."
    "espidf"
    "mongoose"
    "original"
  REQUIRES
    raftcore-src
    mongoose-src
    mbedtls
    esp_http_server
)
