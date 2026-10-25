cmake_minimum_required(VERSION 3.20)

project(libqjsonrpc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_library(qjsonrpc
    src/error.h
    src/error.cpp
    src/json.h
    src/jsonrpc.h
    src/jsonrpc.cpp
    src/log.h
    src/log.cpp
    src/qt_tcp_transport.h
    src/qt_tcp_transport.cpp
    src/session.h
    src/session.cpp
    src/stdio_transport.h
    src/stdio_transport.cpp
    src/transport.h
)

target_include_directories(qjsonrpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qjsonrpc PUBLIC Qt6::Core Qt6::Network)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
