# =============================================================================
# Luniris LBridge Server
# =============================================================================
# Server for Raspberry Pi (Linux ARM64 only)
# - LBridge server for external clients
# - gRPC client to communicate with local Luniris API
# - Fully static build
#
# Can be built standalone or as part of the parent project
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# Only define project if built standalone
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(luniris_api_lbridge_server VERSION 1.0.0 LANGUAGES C CXX)
    set(LUNIRIS_SERVER_STANDALONE ON)
else()
    set(LUNIRIS_SERVER_STANDALONE OFF)
endif()

# =============================================================================
# Platform check
# =============================================================================
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Luniris server only supports Linux (current: ${CMAKE_SYSTEM_NAME})")
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
    message(WARNING "Luniris server is designed for ARM64 (current: ${CMAKE_SYSTEM_PROCESSOR})")
endif()

# =============================================================================
# Options
# =============================================================================
option(LUNIRIS_ENABLE_SECURE "Enable LBridge encryption (ChaCha20-Poly1305)" ON)
option(LUNIRIS_STATIC_BUILD "Build fully static executable" OFF)
option(LUNIRIS_ENABLE_LBRIDGE_LOG "Enable LBridge logging (all levels)" OFF)

# Propagate encryption option to LBridge
set(LBRIDGE_ENABLE_SECURE ${LUNIRIS_ENABLE_SECURE} CACHE BOOL "" FORCE)

# =============================================================================
# Static build configuration
# =============================================================================
if(LUNIRIS_STATIC_BUILD)
    message(STATUS "=== FULL STATIC BUILD ENABLED ===")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

# =============================================================================
# Standalone build: configure dependencies
# =============================================================================
if(LUNIRIS_SERVER_STANDALONE)
    message(STATUS "Building Luniris Server in standalone mode")

    # Global settings
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Output directories
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # Determine paths relative to this CMakeLists.txt
    get_filename_component(LUNIRIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

    # LBridge submodule
    set(LBRIDGE_SRC_DIR "${LUNIRIS_ROOT}/external/LBridge")
    if(EXISTS "${LBRIDGE_SRC_DIR}/CMakeLists.txt")
        set(LBRIDGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${LBRIDGE_SRC_DIR}" "${CMAKE_BINARY_DIR}/lbridge")
    else()
        message(FATAL_ERROR "LBridge not found at ${LBRIDGE_SRC_DIR}. Run: git submodule update --init")
    endif()

    # nanopb setup
    set(NANOPB_SRC_DIR "${LUNIRIS_ROOT}/external/nanopb")
    if(EXISTS "${NANOPB_SRC_DIR}/pb.h")
        set(NANOPB_SRCS
            ${NANOPB_SRC_DIR}/pb_common.c
            ${NANOPB_SRC_DIR}/pb_encode.c
            ${NANOPB_SRC_DIR}/pb_decode.c
        )
        add_library(nanopb STATIC ${NANOPB_SRCS})
        target_include_directories(nanopb PUBLIC ${NANOPB_SRC_DIR})
        target_compile_features(nanopb PUBLIC c_std_11)
    else()
        message(FATAL_ERROR "nanopb not found at ${NANOPB_SRC_DIR}. Run: git submodule update --init")
    endif()

    # Common headers directory
    set(LUNIRIS_COMMON_DIR "${LUNIRIS_ROOT}/common")
    set(LUNIRIS_CLIENT_SRC_DIR "${LUNIRIS_ROOT}/client/src")
else()
    # Built as part of parent project
    get_filename_component(LUNIRIS_COMMON_DIR "${CMAKE_SOURCE_DIR}/common" ABSOLUTE)
    get_filename_component(LUNIRIS_CLIENT_SRC_DIR "${CMAKE_SOURCE_DIR}/client/src" ABSOLUTE)
endif()

# =============================================================================
# Find gRPC and Protobuf (built from source with static libs)
# =============================================================================
find_package(absl REQUIRED CONFIG)
find_package(Protobuf REQUIRED CONFIG)
find_package(OpenSSL QUIET)
find_package(ZLIB REQUIRED)
# re2 - prefer our static build in /usr/local
find_package(re2 CONFIG PATHS /usr/local/lib/cmake/re2 NO_DEFAULT_PATH QUIET)
if(NOT re2_FOUND)
    find_package(re2 REQUIRED CONFIG)
endif()

# c-ares - try CONFIG first, fall back to pkg-config
find_package(c-ares CONFIG QUIET)
if(NOT c-ares_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CARES REQUIRED libcares)
    add_library(c-ares::cares INTERFACE IMPORTED)
    set_target_properties(c-ares::cares PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CARES_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${CARES_LINK_LIBRARIES}"
    )
endif()

message(STATUS "Found absl")
message(STATUS "Found Protobuf: ${Protobuf_VERSION}")

# gRPC - find libraries manually (cmake config is broken due to missing plugin targets)
find_library(GRPC_LIBRARY grpc PATHS /usr/local/lib REQUIRED)
find_library(GRPCPP_LIBRARY grpc++ PATHS /usr/local/lib REQUIRED)
find_library(GPR_LIBRARY gpr PATHS /usr/local/lib REQUIRED)
# Find all upb-related libraries (check both local and system paths)
find_library(UPB_LIBRARY upb PATHS /usr/local/lib /usr/lib REQUIRED)
find_library(UPB_TEXTFORMAT_LIBRARY upb_textformat_lib PATHS /usr/local/lib /usr/lib)
find_library(UPB_JSON_LIBRARY upb_json_lib PATHS /usr/local/lib /usr/lib)
find_library(UPB_COLLECTIONS_LIBRARY upb_collections_lib PATHS /usr/local/lib /usr/lib)
find_library(UTF8_RANGE_LIBRARY utf8_range PATHS /usr/local/lib /usr/lib)
find_library(UTF8_VALIDITY_LIBRARY utf8_validity PATHS /usr/local/lib /usr/lib)
# Try alternate names used in some gRPC versions
find_library(UPB_BASE_LIBRARY upb_base PATHS /usr/local/lib /usr/lib)
find_library(UPB_MEM_LIBRARY upb_mem PATHS /usr/local/lib /usr/lib)
find_library(UPB_MESSAGE_LIBRARY upb_message PATHS /usr/local/lib /usr/lib)
find_library(ADDRESS_SORTING_LIBRARY address_sorting PATHS /usr/local/lib REQUIRED)
find_path(GRPC_INCLUDE_DIR grpcpp/grpcpp.h PATHS /usr/local/include REQUIRED)

message(STATUS "Found gRPC: ${GRPCPP_LIBRARY}")
message(STATUS "Found gRPC include: ${GRPC_INCLUDE_DIR}")

# Find protoc and grpc_cpp_plugin (from Alpine packages)
find_program(PROTOC_EXECUTABLE protoc PATHS /usr/bin REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin PATHS /usr/bin REQUIRED)

message(STATUS "protoc: ${PROTOC_EXECUTABLE}")
message(STATUS "gRPC plugin: ${GRPC_CPP_PLUGIN}")

# =============================================================================
# Proto files for gRPC (Luniris local API)
# =============================================================================
set(LUNIRIS_GRPC_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(LUNIRIS_GRPC_PROTO_FILES
    ${LUNIRIS_GRPC_PROTO_DIR}/luniris_service.proto
)

# Output directory for generated files
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Generate gRPC/Protobuf sources
set(GRPC_GENERATED_SRCS "")
set(GRPC_GENERATED_HDRS "")

foreach(PROTO_FILE ${LUNIRIS_GRPC_PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

    set(PROTO_SRC "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.cc")
    set(PROTO_HDR "${PROTO_GEN_DIR}/${PROTO_NAME}.pb.h")
    set(GRPC_SRC "${PROTO_GEN_DIR}/${PROTO_NAME}.grpc.pb.cc")
    set(GRPC_HDR "${PROTO_GEN_DIR}/${PROTO_NAME}.grpc.pb.h")

    add_custom_command(
        OUTPUT ${PROTO_SRC} ${PROTO_HDR} ${GRPC_SRC} ${GRPC_HDR}
        COMMAND ${PROTOC_EXECUTABLE}
            --proto_path=${LUNIRIS_GRPC_PROTO_DIR}
            --cpp_out=${PROTO_GEN_DIR}
            --grpc_out=${PROTO_GEN_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating gRPC sources for ${PROTO_NAME}.proto"
    )

    list(APPEND GRPC_GENERATED_SRCS ${PROTO_SRC} ${GRPC_SRC})
    list(APPEND GRPC_GENERATED_HDRS ${PROTO_HDR} ${GRPC_HDR})
endforeach()

# =============================================================================
# Server sources
# =============================================================================
set(SERVER_SOURCES
    src/main.cpp
    src/luniris_lbridge_server.cpp
    src/grpc_client.cpp
    src/crash_handler.cpp
    src/server_log.cpp
    ${LUNIRIS_CLIENT_SRC_DIR}/luniris_messages.pb.c
    ${GRPC_GENERATED_SRCS}
)

set(SERVER_HEADERS
    src/luniris_lbridge_server.h
    src/grpc_client.h
    src/crash_handler.h
    src/server_log.h
    ${GRPC_GENERATED_HDRS}
)

# =============================================================================
# Server executable
# =============================================================================
add_executable(luniris_api_lbridge_server ${SERVER_SOURCES} ${SERVER_HEADERS})

# Include directories
target_include_directories(luniris_api_lbridge_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LUNIRIS_CLIENT_SRC_DIR}
    ${LUNIRIS_COMMON_DIR}
    ${PROTO_GEN_DIR}
    ${GRPC_INCLUDE_DIR}
)

# =============================================================================
# Link dependencies
# =============================================================================
if(LUNIRIS_STATIC_BUILD)
    # Full static linking
    target_link_libraries(luniris_api_lbridge_server PRIVATE
        lbridge
        nanopb
        ${GRPCPP_LIBRARY}
        ${GRPC_LIBRARY}
        ${GPR_LIBRARY}
        # upb libraries - high-level first, then base (static link order matters)
        ${UPB_TEXTFORMAT_LIBRARY}
        ${UPB_JSON_LIBRARY}
        ${UPB_COLLECTIONS_LIBRARY}
        ${UPB_LIBRARY}
        $<$<BOOL:${UPB_BASE_LIBRARY}>:${UPB_BASE_LIBRARY}>
        $<$<BOOL:${UPB_MEM_LIBRARY}>:${UPB_MEM_LIBRARY}>
        $<$<BOOL:${UPB_MESSAGE_LIBRARY}>:${UPB_MESSAGE_LIBRARY}>
        ${UTF8_RANGE_LIBRARY}
        ${UTF8_VALIDITY_LIBRARY}
        ${ADDRESS_SORTING_LIBRARY}
        protobuf::libprotobuf
        absl::base
        absl::synchronization
        absl::strings
        absl::status
        absl::statusor
        absl::time
        absl::log
        absl::flags
        absl::hash
        absl::random_random
        re2::re2
        $<$<TARGET_EXISTS:OpenSSL::SSL>:OpenSSL::SSL>
        $<$<TARGET_EXISTS:OpenSSL::Crypto>:OpenSSL::Crypto>
        ZLIB::ZLIB
        c-ares::cares
        pthread
        dl
    )
else()
    # Dynamic linking
    target_link_libraries(luniris_api_lbridge_server PRIVATE
        lbridge
        nanopb
        ${GRPCPP_LIBRARY}
        ${GRPC_LIBRARY}
        ${GPR_LIBRARY}
        protobuf::libprotobuf
        pthread
    )
endif()

# C++17 for gRPC
target_compile_features(luniris_api_lbridge_server PRIVATE cxx_std_17)

# Compiler options
target_compile_options(luniris_api_lbridge_server PRIVATE
    -Wall
    -Wextra
    -O2
    -funwind-tables
    -fasynchronous-unwind-tables
)

# LBridge logging
if(LUNIRIS_ENABLE_LBRIDGE_LOG)
    target_compile_definitions(luniris_api_lbridge_server PRIVATE LBRIDGE_ENABLE_LOG)
    target_compile_definitions(lbridge PRIVATE LBRIDGE_ENABLE_LOG)
endif()

# =============================================================================
# Installation
# =============================================================================
install(TARGETS luniris_api_lbridge_server
    RUNTIME DESTINATION bin
)

# =============================================================================
# Configuration summary
# =============================================================================
message(STATUS "")
message(STATUS "Luniris Server Configuration:")
message(STATUS "  Target platform:  Linux ARM64 (Raspberry Pi)")
message(STATUS "  Static build:     ${LUNIRIS_STATIC_BUILD}")
message(STATUS "  Encryption:       ${LUNIRIS_ENABLE_SECURE}")
message(STATUS "  LBridge logging:  ${LUNIRIS_ENABLE_LBRIDGE_LOG}")
message(STATUS "  gRPC plugin:      ${GRPC_CPP_PLUGIN}")
message(STATUS "  Protobuf:         ${Protobuf_VERSION}")
message(STATUS "")
