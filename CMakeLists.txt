cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(minihf)
target_sources(app PRIVATE src/main.c
                           src/modes/cw.c
                           src/modes/rtty.c
                           src/protocol/cobs.c
                           src/protocol/packet_parser.c
                           src/uart_handler.c
                           src/radio/radio_cmd.c
                           src/radio/radio.c)
target_include_directories(app PRIVATE ${CMAKE_SOURCE_DIR})
target_include_directories(app PRIVATE include)
add_subdirectory(drivers)
