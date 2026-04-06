cmake_minimum_required(VERSION 3.10)
project(serial_monitor)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBDATACHANNEL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/libdatachannel)
add_subdirectory(${LIBDATACHANNEL_DIR} ${CMAKE_CURRENT_BINARY_DIR}/libdatachannel)

add_subdirectory(src)
add_subdirectory(app)
