cmake_minimum_required(VERSION 3.16)
project(Luniris_LBridgeAPI VERSION 1.0.0 LANGUAGES C CXX)

# =============================================================================
# Build options
# =============================================================================
option(LUNIRIS_BUILD_CLIENT "Build the LBridge client library" ON)
option(LUNIRIS_BUILD_SERVER "Build the LBridge server (requires gRPC)" OFF)
option(LUNIRIS_ENABLE_SECURE "Enable LBridge encryption (ChaCha20-Poly1305)" ON)

# =============================================================================
# Global settings
# =============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# =============================================================================
# LBridge submodule
# =============================================================================
set(LBRIDGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LBRIDGE_ENABLE_SECURE ${LUNIRIS_ENABLE_SECURE} CACHE BOOL "" FORCE)
add_subdirectory(external/LBridge)

# =============================================================================
# nanopb setup (for protobuf support)
# =============================================================================
set(NANOPB_SRC_ROOT_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/external/nanopb)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${NANOPB_SRC_ROOT_FOLDER}/extra)

# Check if protoc is available for .proto generation
find_program(PROTOBUF_PROTOC_EXECUTABLE protoc)

if(PROTOBUF_PROTOC_EXECUTABLE)
    # protoc found: use FindNanopb.cmake which provides both runtime lib and generation
    message(STATUS "Found protoc: ${PROTOBUF_PROTOC_EXECUTABLE}")
    find_package(Nanopb REQUIRED)
else()
    # protoc not found: create nanopb runtime library manually
    # .proto generation will not be available
    message(STATUS "protoc not found - .proto generation disabled")
    message(STATUS "Install protoc to enable .proto file compilation")

    set(NANOPB_SRCS
        ${NANOPB_SRC_ROOT_FOLDER}/pb_common.c
        ${NANOPB_SRC_ROOT_FOLDER}/pb_encode.c
        ${NANOPB_SRC_ROOT_FOLDER}/pb_decode.c
    )

    add_library(nanopb STATIC ${NANOPB_SRCS} "server/src/server_log.h" "server/src/server_log.cpp")
    target_include_directories(nanopb PUBLIC ${NANOPB_SRC_ROOT_FOLDER})
    target_compile_features(nanopb PUBLIC c_std_11)
endif()

# =============================================================================
# Client library
# =============================================================================
if(LUNIRIS_BUILD_CLIENT)
    add_subdirectory(client)
endif()

# =============================================================================
# Server executable (for Raspberry Pi)
# =============================================================================
if(LUNIRIS_BUILD_SERVER)
    add_subdirectory(server)
endif()

# =============================================================================
# Configuration summary
# =============================================================================
message(STATUS "")
message(STATUS "Luniris LBridge API Configuration:")
message(STATUS "  Build client:  ${LUNIRIS_BUILD_CLIENT}")
message(STATUS "  Build server:  ${LUNIRIS_BUILD_SERVER}")
message(STATUS "  Encryption:    ${LUNIRIS_ENABLE_SECURE}")
message(STATUS "  protoc:        ${PROTOBUF_PROTOC_EXECUTABLE}")
message(STATUS "")
