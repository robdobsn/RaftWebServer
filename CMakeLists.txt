cmake_minimum_required(VERSION 3.16)

# message(STATUS "==========================================================")

# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
#     message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()

# message(STATUS "==========================================================")

set(RAFT_COMPONENT_EXTRA_PATH "components/RaftWebServer/")
set(RAFT_WEBSERVER_REQUIRES RaftCore mbedtls esp_http_server)
set(RAFT_WEBSERVER_SOURCES 
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebServer.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebHandlerRestAPI.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebHandlerStaticFiles.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebHandlerWS.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebMultipart.cpp
    ${RAFT_COMPONENT_EXTRA_PATH}RaftWebInterface.cpp
)
set(RAFT_WEBSERVER_INCLUDES ${RAFT_COMPONENT_EXTRA_PATH})

# Check if mongoose component is present
set(TEST_MONGOOSE_FOLDER ${COMPONENT_DIR}/../mongoose-src)
get_filename_component(TEST_MONGOOSE_SRC ${TEST_MONGOOSE_FOLDER} REALPATH)
if (EXISTS "${TEST_MONGOOSE_SRC}")
    message(STATUS "Mongoose component found")
    set(RAFT_WEBSERVER_SOURCES ${RAFT_WEBSERVER_SOURCES} 
            ${RAFT_COMPONENT_EXTRA_PATH}mongoose/RaftWebConnManager_mongoose.cpp
    )
    set(RAFT_WEBSERVER_INCLUDES ${RAFT_WEBSERVER_INCLUDES} ${RAFT_COMPONENT_EXTRA_PATH}mongoose)
    set(RAFT_WEBSERVER_REQUIRES ${RAFT_WEBSERVER_REQUIRES} mongoose-src)
else()
    message(STATUS "Mongoose component NOT found")
    set(RAFT_WEBSERVER_SOURCES ${RAFT_WEBSERVER_SOURCES} 
            ${RAFT_COMPONENT_EXTRA_PATH}RaftClientConnNetconn.cpp
            ${RAFT_COMPONENT_EXTRA_PATH}RaftClientConnSockets.cpp
            ${RAFT_COMPONENT_EXTRA_PATH}RaftClientListener.cpp
            ${RAFT_COMPONENT_EXTRA_PATH}RaftWebConnection.cpp
            ${RAFT_COMPONENT_EXTRA_PATH}RaftWebConnManager.cpp
            ${RAFT_COMPONENT_EXTRA_PATH}RaftWebResponderFile.cpp
            ${RAFT_COMPONENT_EXTRA_PATH}RaftWebResponderRestAPI.cpp
            ${RAFT_COMPONENT_EXTRA_PATH}RaftWebResponderWS.cpp
            ${RAFT_COMPONENT_EXTRA_PATH}RaftWebSocketLink.cpp
    )
    set(RAFT_WEBSERVER_INCLUDES ${RAFT_WEBSERVER_INCLUDES} ${RAFT_COMPONENT_EXTRA_PATH})
endif()

idf_component_register(
  SRCS
    ${RAFT_WEBSERVER_SOURCES}
    "components/WebServer/WebServer.cpp"
  INCLUDE_DIRS
    ${RAFT_WEBSERVER_INCLUDES}
    "components/WebServer"
  REQUIRES
    ${RAFT_WEBSERVER_REQUIRES}
)
