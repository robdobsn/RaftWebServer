idf_component_register(
                SRCS
                    "main.cpp"
                INCLUDE_DIRS
                    "."
                REQUIRES 
                    raftcore-src
                    RaftWebServer
)

# Create a SPIFFS image from the contents of the FS_IMAGE_PATH directory
# that fits the partition named 'spiffs'. FLASH_IN_PROJECT indicates that
# the generated image should be flashed when the entire project is flashed to
# the target with 'idf.py -p PORT flash'.
message(STATUS "Configuring SPIFFS file system")
spiffs_create_partition_image(spiffs ${FS_IMAGE_PATH} FLASH_IN_PROJECT)
