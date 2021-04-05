cmake_minimum_required(VERSION 3.0.0)
project(SlimeVR-OpenVR-Driver VERSION 0.1.0)

include(CTest)
enable_testing()

file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library("${PROJECT_NAME}" SHARED "${HEADERS}" "${SOURCES}")

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
