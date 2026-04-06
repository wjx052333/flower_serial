add_executable(serial_monitor main.c)
target_link_libraries(serial_monitor PRIVATE serial_core)
target_compile_options(serial_monitor PRIVATE -Wall -Wextra -D_DEFAULT_SOURCE)

add_executable(serial_ws ws_server.cpp)
target_link_libraries(serial_ws PRIVATE serial_core datachannel)
target_compile_options(serial_ws PRIVATE -Wall -Wextra)

add_executable(ws_client ws_client.cpp)
target_link_libraries(ws_client PRIVATE datachannel)
target_compile_options(ws_client PRIVATE -Wall -Wextra)
