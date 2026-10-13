cmake_minimum_required(VERSION 3.13)
include(../../pico_sdk_import.cmake)
project(console_integration C CXX ASM)
set(CMAKE_C_STANDARD 11)
include(../../cmake/rp2040-console.cmake)
rp2040_console_host_tool(NAME emit_program SOURCES emit.c HOST_CC /usr/bin/cc)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated.c"
    COMMAND ${emit_program_COMMAND} "${CMAKE_CURRENT_BINARY_DIR}/generated.c"
    DEPENDS ${emit_program_DEPENDS} VERBATIM)
rp2040_console_game(NAME generated_app SOURCES helper.c probe.c GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated.c"
    INCLUDE_DIRS app LEGACY_TIME32 RENAMES panic=game_panic SYSTEM_HEADERS hardware/watchdog.h
    ENV TERM=xterm EMPTY_FILES scores)
rp2040_console_game(NAME native_app SOURCES native.c probe.c ENV TERM=xterm)
rp2040_console_game(NAME soft_app SOURCES soft.c SOFT_REBOOT ENV TERM=xterm
    EMPTY_FILES session.txt HEAP_END 0x2003c000 STACK_RESERVE 24576)
# Regression: warning suppression must not be needed by the platform fixture.
target_compile_options(generated_app PRIVATE $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration> $<$<COMPILE_LANGUAGE:C>:-Werror=int-conversion>)

target_compile_options(soft_app PRIVATE $<$<COMPILE_LANGUAGE:C>:-UNDEBUG>)
