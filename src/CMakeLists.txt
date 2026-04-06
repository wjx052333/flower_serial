add_library(serial_core STATIC
    serial.c
    relay.c
    soil.c
    device.c
)

target_include_directories(serial_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(serial_core PRIVATE -Wall -Wextra -D_DEFAULT_SOURCE)
