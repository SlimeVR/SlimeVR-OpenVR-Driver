cmake_minimum_required(VERSION 3.19)

# PIE is required
include(CheckPIESupported)
check_pie_supported()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(SlimeVR-OpenVR-Driver VERSION 0.2.0)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(DRIVER_NAME "slimevr")

option(SLIMEVR_BUILD_TESTS "Build tests" OFF)

if (SLIMEVR_BUILD_TESTS)
    include(CTest)
    enable_testing()
endif()

set(OPENVR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/openvr/headers")

set(SOLARXR_PROTOCOL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libraries/solarxr-protocol")
set(FLATBUFFERS_GENERATED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/libraries/solarxr-protocol/protocol/cpp")

set(SIZEOF_VOIDP ${CMAKE_SIZEOF_VOID_P})
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PROCESSOR_ARCH "64")
else()
    set(PROCESSOR_ARCH "32")
endif()

if(WIN32)
    set(PLATFORM_NAME "win")
elseif(UNIX AND NOT APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(PLATFORM_NAME "linuxarm")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(PLATFORM_NAME "linux")
    else()
        message(FATAL_ERROR "Unsupported CPU architecture")
    endif()
elseif(APPLE)
    set(PLATFORM_NAME "osx")
endif()

include(cmake/CPM.cmake)

CPMAddPackage(URI "gh:simdjson/simdjson@4.6.4")
if (SLIMEVR_BUILD_TESTS)
    CPMAddPackage(URI "gh:catchorg/Catch2@3.15.0")
endif()

find_package(Git)

if (NOT GIT_DESC)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always
        OUTPUT_VARIABLE GIT_DESC
        ERROR_QUIET
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    string(STRIP "${GIT_DESC}" GIT_DESC)
else()
    message(STATUS "Using GIT_DESC ${GIT_DESC} from explicit build flag")
endif()

if (GIT_DESC)
    message(STATUS "GIT_DESC: ${GIT_DESC}")
else()
    message(FATAL_ERROR "Could not set GIT_DESC")
endif()

# Project
set(DEPS_INCLUDES
    "${OPENVR_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/libraries/linalg"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/"
)
set(DEPS_LIBS simdjson::simdjson)

configure_file(src/Consts.hpp.in src/Consts.hpp @ONLY)

# compile into a static lib
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
add_library("${PROJECT_NAME}_static" STATIC ${SOURCES} "${FLATBUFFERS_GENERATED_ROOT}/protocol.cpp")
target_link_libraries("${PROJECT_NAME}_static" PUBLIC ${DEPS_LIBS})
set_target_properties("${PROJECT_NAME}_static" PROPERTIES CXX_STANDARD 23)
target_include_directories("${PROJECT_NAME}_static" BEFORE PUBLIC "${SOLARXR_PROTOCOL_ROOT}/lib/flatbuffers/include" "${FLATBUFFERS_GENERATED_ROOT}/include")
target_include_directories("${PROJECT_NAME}_static" PUBLIC ${DEPS_INCLUDES} "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/src")
if(UNIX)
    target_compile_options("${PROJECT_NAME}_static" PRIVATE "-fPIC")
    target_link_libraries("${PROJECT_NAME}_static" PUBLIC atomic)
elseif(WIN32)
    target_link_libraries("${PROJECT_NAME}_static" PUBLIC ws2_32)
endif()

# compile driver
file(GLOB_RECURSE DRIVER_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/DriverFactory.cpp")
add_library("${PROJECT_NAME}" SHARED ${DRIVER_MAIN} ${HEADERS})
target_include_directories("${PROJECT_NAME}" PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries("${PROJECT_NAME}" PUBLIC "${PROJECT_NAME}_static")
set_target_properties("${PROJECT_NAME}" PROPERTIES CXX_STANDARD 23)

# compile tests
function(build_tests target_name test_dir executable_name)
    file(GLOB TESTS "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/${test_dir}/*.hpp")
    file(GLOB TESTS_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/test/common/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/test/common/*.hpp")
    add_executable(${target_name} ${TESTS} ${TESTS_COMMON} ${HEADERS})
    target_link_libraries(${target_name} PUBLIC "${PROJECT_NAME}_static" Catch2::Catch2WithMain)
    set_target_properties(${target_name} PROPERTIES CXX_STANDARD 23 OUTPUT_NAME "${executable_name}")
endfunction()
if (SLIMEVR_BUILD_TESTS)
    build_tests("${PROJECT_NAME}-Tests" "test" "tests")
    build_tests("${PROJECT_NAME}-Tests-Integration" "test/integration" "tests_integration")
    add_test(NAME "Driver tests" COMMAND "${PROJECT_NAME}-Tests")
endif()

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
