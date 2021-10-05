cmake_minimum_required(VERSION 3.0.0)

# If the toolchain is already defined, do not attempt to find it
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    # If the VCPKG_ROOT environment variable is not defined, try to automatically define it from AppData/home
    if(NOT DEFINED ENV{VCPKG_ROOT})
        if(WIN32)
            file(READ "$ENV{LOCALAPPDATA}/vcpkg/vcpkg.path.txt" VCPKG_ROOT)
        elseif(UNIX)
            file(READ "$ENV{HOME}/.vcpkg/vcpkg.path.txt" VCPKG_ROOT)
        endif()

        set(VCPKG_PATH "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    else()
        set(VCPKG_PATH "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    endif()
    
    if(EXISTS "${VCPKG_PATH}")
        message("vcpkg CMake toolchain was found at \"${VCPKG_PATH}\"")
        set(CMAKE_TOOLCHAIN_FILE "${VCPKG_PATH}")
    else()
        message(FATAL_ERROR "vcpkg could not be found")
    endif()
endif()

project(SlimeVR-OpenVR-Driver VERSION 0.2.0)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(DRIVER_NAME "SlimeVR")

include(CTest)
enable_testing()

# OpenVR
set(OPENVR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/openvr/headers")

set(SIZEOF_VOIDP ${CMAKE_SIZEOF_VOID_P})
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PROCESSOR_ARCH "64")
else()
    set(PROCESSOR_ARCH "32")
endif()

if(WIN32)
    set(PLATFORM_NAME "win")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_NAME "linux")
elseif(APPLE)
    set(PLATFORM_NAME "osx")
endif()

find_library(OPENVR_LIB openvr_api HINTS "${CMAKE_CURRENT_SOURCE_DIR}/libraries/openvr/lib/${PLATFORM_NAME}${PROCESSOR_ARCH}/" NO_DEFAULT_PATH )

# Protobuf
# Installation:
# Please refer to this readme to install protobuf in your system: https://github.com/protocolbuffers/protobuf/blob/master/src/README.md
# WARNING: CLang has an arror building protobuf messages, use MSVC 2019
INCLUDE(FindProtobuf)
find_package(protobuf CONFIG REQUIRED)
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/ProtobufMessages.proto")
SET_SOURCE_FILES_PROPERTIES(${PROTO_SRC} ${PROTO_INCL} PROPERTIES GENERATED TRUE)

# Project
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library("${PROJECT_NAME}" SHARED "${HEADERS}" "${SOURCES}" ${PROTO_HEADER} ${PROTO_SRC})
target_include_directories("${PROJECT_NAME}" PUBLIC "${OPENVR_INCLUDE_DIR}")
target_include_directories("${PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libraries/linalg")
target_include_directories("${PROJECT_NAME}" PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/")
target_link_libraries("${PROJECT_NAME}" PUBLIC "${OPENVR_LIB}" protobuf::libprotoc protobuf::libprotobuf protobuf::libprotobuf-lite)
set_property(TARGET "${PROJECT_NAME}" PROPERTY CXX_STANDARD 17)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# IDE Config
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Header Files" FILES ${HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${SOURCES})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

# Build
# Copy driver assets to output folder
add_custom_command(
    TARGET ${PROJECT_NAME}
    PRE_BUILD 
    COMMAND ${CMAKE_COMMAND} -E copy_directory 
    ${CMAKE_SOURCE_DIR}/driver/ 
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/driver
)

# Copy dll to output folder
add_custom_command(
    TARGET ${PROJECT_NAME} 
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
    $<TARGET_FILE:${PROJECT_NAME}>
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/driver/${DRIVER_NAME}/bin/${PLATFORM_NAME}${PROCESSOR_ARCH}/driver_${DRIVER_NAME}$<TARGET_FILE_SUFFIX:${PROJECT_NAME}>
)

# Copy libprotobuf dll to output folder
add_custom_command(
    TARGET ${PROJECT_NAME} 
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/libprotobuf.dll
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/driver/${DRIVER_NAME}/bin/${PLATFORM_NAME}${PROCESSOR_ARCH}/libprotobuf.dll
)
