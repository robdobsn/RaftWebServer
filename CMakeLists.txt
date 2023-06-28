cmake_minimum_required(VERSION 3.16)

# message(STATUS "==========================================================")

# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()

# message(STATUS "==========================================================")

set(RAFT_COMPONENT_EXTRA_PATH /components/RaftWebServer/)
set(RAFT_WEBSERVER_REQUIRES raftcore-src mbedtls esp_http_server)
set(RAFT_WEBSERVER_SOURCES 
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebServer.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebHandlerRestAPI.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebHandlerStaticFiles.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebHandlerWS.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebMultipart.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebInterface.cpp
)
set(RAFT_WEBSERVER_INCLUDES ".")

# Check if mongoose component is present
set(TEST_VAR_1 ${BUILD_COMPONENT_DIRS})
string(REGEX MATCH "mongoose" TEST_VAR_2 ${TEST_VAR_1})
if(NOT "${TEST_VAR_2}" STREQUAL "")
  set(RAFT_WEBSERVER_REQUIRES ${RAFT_WEBSERVER_REQUIRES} mongoose-src)
  set(RAFT_WEBSERVER_SOURCES ${RAFT_WEBSERVER_SOURCES} 
    ${RAFT_COMPONENT_EXTRA_PATH}mongoose/RaftWebConnManager_mongoose.cpp
  )
  set(RAFT_WEBSERVER_INCLUDES ${RAFT_WEBSERVER_INCLUDES} "mongoose")
else()
  set(RAFT_WEBSERVER_SOURCES ${RAFT_WEBSERVER_SOURCES} 
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftClientConnNetconn.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftClientConnSockets.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftClientListener.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftWebConnection.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftWebConnManager_original.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftWebResponderFile.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftWebResponderRestAPI.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftWebResponderWS.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}original/RaftWebSocketLink.cpp
  )
  set(RAFT_WEBSERVER_INCLUDES ${RAFT_WEBSERVER_INCLUDES} "original")
endif()

idf_component_register(
  SRCS
    ${RAFT_WEBSERVER_SOURCES}
  INCLUDE_DIRS
    ${RAFT_WEBSERVER_INCLUDES}
  REQUIRES
    ${RAFT_WEBSERVER_REQUIRES}
)
