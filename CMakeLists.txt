cmake_minimum_required(VERSION 3.0.0)

#if (NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(WIN32)
        set(VCPKG_TARGET_TRIPLET "x64-windows-static-md")
    endif()
#endif()

set (CMAKE_CXX_STANDARD 20)

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
set(DRIVER_NAME "slimevr")

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

if(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
endif()

find_library(OPENVR_LIB openvr_api HINTS "${CMAKE_CURRENT_SOURCE_DIR}/libraries/openvr/lib/${PLATFORM_NAME}${PROCESSOR_ARCH}/" NO_DEFAULT_PATH )

# Protobuf
# Installation:
# Please refer to this readme to install protobuf in your system: https://github.com/protocolbuffers/protobuf/blob/master/src/README.md
# WARNING: CLang has an arror building protobuf messages, use MSVC 2019
set(protobuf_MODULE_COMPATIBLE ON CACHE BOOL "")
find_package(Protobuf CONFIG REQUIRED)
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/ProtobufMessages.proto")
SET_SOURCE_FILES_PROPERTIES(${PROTO_SRC} ${PROTO_INCL} PROPERTIES GENERATED TRUE)

find_package(simdjson CONFIG REQUIRED)

# libuv
find_package(libuv REQUIRED)
find_package(uvw REQUIRED)

# Catch2
find_package(Catch2 3 REQUIRED)

# Project
set(DEPS_INCLUDES
    "${OPENVR_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/libraries/linalg"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/"
)
set(DEPS_LIBS
    "${OPENVR_LIB}"
    protobuf::libprotoc
    protobuf::libprotobuf
    protobuf::libprotobuf-lite
    simdjson::simdjson
    $<IF:$<TARGET_EXISTS:uv_a>,uv_a,uv> # libuv
)

# compile into a static lib
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
add_library("${PROJECT_NAME}_static" STATIC ${SOURCES} ${PROTO_HEADER} ${PROTO_SRC})
target_link_libraries("${PROJECT_NAME}_static" PUBLIC ${DEPS_LIBS})
set_property(TARGET "${PROJECT_NAME}_static" PROPERTY CXX_STANDARD 17)
include_directories("${PROJECT_NAME}_static" PUBLIC ${DEPS_INCLUDES} ${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

# compile driver
file(GLOB_RECURSE DRIVER_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/DriverFactory.cpp")
add_library("${PROJECT_NAME}" SHARED ${DRIVER_MAIN} ${HEADERS} ${PROTO_HEADER})
target_link_libraries("${PROJECT_NAME}" PRIVATE "${PROJECT_NAME}_static")
set_property(TARGET "${PROJECT_NAME}" PROPERTY CXX_STANDARD 17)

# compile tests
function(build_tests target_name test_dir)
    file(GLOB TESTS "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}/*.hpp")
    file(GLOB TESTS_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/test/common/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/test/common/*.hpp")
    add_executable(${target_name} ${TESTS} ${TESTS_COMMON} ${HEADERS} ${PROTO_HEADER})
    target_link_libraries(${target_name} PUBLIC "${PROJECT_NAME}_static" Catch2::Catch2WithMain)
    set_property(TARGET ${target_name} PROPERTY CXX_STANDARD 17)
endfunction()
build_tests(tests "test")
build_tests(tests_integration "test/integration")
add_test(NAME tests COMMAND "tests")

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
