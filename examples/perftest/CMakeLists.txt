# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

set(SUPPORTED_TARGETS esp32)

add_compile_definitions(FEATURE_WEB_SERVER_USE_ESP_IDF)

set(FS_IMAGE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/fsimage)
message(STATUS "FS_IMAGE_PATH: ${FS_IMAGE_PATH}")

# Fetch the RaftCore library
FetchContent_Declare(
  raftcore
  GIT_REPOSITORY https://github.com/robdobsn/RaftCore.git
  GIT_TAG        v0.4.11
)

# Populate first so we have a folder that we can add in EXTRA_COMPONENT_DIRS
message(STATUS "Fetching dependencies...")
FetchContent_Populate(raftcore)

# Fetch raftcore
# get_filename_component(raftwebserver_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. REALPATH)
# set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftcore_SOURCE_DIR} ${raftwebserver_SOURCE_DIR})
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftcore_SOURCE_DIR} "../../components")
FetchContent_MakeAvailable(raftcore)

# Configure ESP-IDF project
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project name
project(RaftWebServerExample DEPENDS raftcore)
