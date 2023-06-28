# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

set(SUPPORTED_TARGETS esp32)

# Check if mongoose build
if (DEFINED USE_MONGOOSE)
  message(STATUS "============== BUILDING EXAMPLE USING MONGOOSE WEB SERVER ==============")
  add_compile_definitions(FEATURE_WEB_SERVER_USE_MONGOOSE)
else()
  message(STATUS "============== BUILDING EXAMPLE USING RAFT ORIGINAL WEB SERVER ==============")
endif()


set(FS_IMAGE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/fsimage)
message(STATUS "FS_IMAGE_PATH: ${FS_IMAGE_PATH}")

# Fetch the RaftCore library
FetchContent_Declare(
  raftcore
  GIT_REPOSITORY https://github.com/robdobsn/RaftCore.git
  GIT_TAG        v0.4.18
)

# Fetch the mongoose web library if required
if (DEFINED USE_MONGOOSE)
FetchContent_Declare(
  mongoose
  GIT_REPOSITORY https://github.com/cesanta/mongoose.git
  GIT_TAG        7.10
)
endif()

# Populate first so we have a folder that we can add in EXTRA_COMPONENT_DIRS
message(STATUS "Fetching dependencies...")
FetchContent_Populate(raftcore)

# Handle mongoose - fetch and then copy CMakeLists.txt to mongoose source directory
if (DEFINED USE_MONGOOSE)
FetchContent_Populate(mongoose)
message(STATUS "Copying mongoose CMakeLists.txt... from ${CMAKE_CURRENT_SOURCE_DIR}/mongoose/CMakeLists.txt to ${mongoose_SOURCE_DIR}/CMakeLists.txt")
configure_file(../../components/RaftWebServer/mongoose/CMakeLists.txt ${mongoose_SOURCE_DIR}/CMakeLists.txt COPYONLY)
endif()

# Fetch raftcore
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftcore_SOURCE_DIR} ${mongoose_SOURCE_DIR} "../../components")
FetchContent_MakeAvailable(raftcore)

# Make mongoose available
if (DEFINED USE_MONGOOSE)
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${mongoose_SOURCE_DIR})
FetchContent_MakeAvailable(mongoose)
set(MONGOOSE_NAME_ONLY_IF_DEFINED mongoose)
endif()

# Configure ESP-IDF project
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the project name
project(RaftWebServerExample DEPENDS raftcore ${MONGOOSE_NAME_ONLY_IF_DEFINED})
