#
# rp2040-console: the scaffolding a terminal game needs to run on RP2040.
#
# Included from a game's CMakeLists, after project() and the SDK import:
#
#   cmake_minimum_required(VERSION 3.13)
#   list(APPEND CMAKE_MODULE_PATH "$ENV{GAME_CONSOLE}/cmake")
#   project(mygame C CXX ASM)
#   include(rp2040-console)
#   rp2040_console_game(
#       NAME        mygame
#       SOURCES     ${GAME_SOURCES}
#       ENV         TERM=xterm GAMEPATH=/
#       DATA_FILES  help credits
#       EMPTY_FILES scores
#       DATA_DIRS   save
#   )
#
# What that buys, and why each part is here:
#
#   Console      USB CDC stdio, with the UART as a wired alternative.
#   Startup      main() is wrapped, so the console, environment, clock and
#                volume are up before the game's first instruction, without
#                a single line of setup code in the game.
#   Files        FatFs over a FAT volume in the Pico's own flash, with an
#                SPI SD card as the fallback, and the POSIX file calls on
#                top of them.
#   Terminal     termcap and termios surfaces, since there is no /etc/termcap
#                and no line discipline.
#   libc         the syscall surface newlib expects a kernel to fill, plus
#                entropy from the RP2040's own noise source.
#   Exit         quitting reboots the board instead of leaving it dark.
#
# Everything the engine needs to know about a particular game is an argument
# above. Nothing in the engine refers to any specific game.
#

get_filename_component(RP2040_CONSOLE_PATH "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(RP2040_CONSOLE_SRC     "${RP2040_CONSOLE_PATH}/src")
set(RP2040_CONSOLE_INCLUDE "${RP2040_CONSOLE_PATH}/include")
include("${CMAKE_CURRENT_LIST_DIR}/host-tools.cmake")

# --- before pico_sdk_init() -------------------------------------------
#
# Keep .text in flash. Turning this off is the whole reason a game bigger
# than SRAM can run here: the RP2040 memory-maps flash at 0x10000000 and
# executes through the XIP cache, so code and constant tables cost no RAM
# at all. Copying to RAM duplicates every byte of it for a speed-up a
# terminal game cannot notice, and the duplicate is what would not fit.
#
if(NOT DEFINED PICO_COPY_TO_RAM)
    set(PICO_COPY_TO_RAM 0)
endif()

#
# Do NOT disable the default alarm pool.
#
# pico_stdio_usb schedules tud_task() through add_alarm_in_us(). With
# PICO_TIME_DEFAULT_ALARM_POOL_DISABLED the alarm is never added, tud_task()
# never runs, and the board enumerates as a USB device with no serial port
# on it. The pool costs a small fixed amount of SRAM.
#

pico_sdk_init()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

#
# The behaviour this engine relies on is the SDK's, not its own: which libc
# functions get --wrap'ed onto the stdio drivers (putchar, puts, printf,
# vprintf - but not fputc or fputs, which is why those are supplied here),
# whether stdio_init_all() is called on the application's behalf, and which
# linker symbol sizes the heap. All four were true of 2.3.1. Say so loudly
# rather than letting a future version fail quietly.
#
if(NOT PICO_SDK_VERSION_STRING VERSION_EQUAL "2.3.1")
    message(WARNING
        "rp2040-console was validated against SDK 2.3.1, building against "
        "${PICO_SDK_VERSION_STRING}. Re-check the stdio --wrap set, the "
        "alarm pool and the heap symbol before trusting the image.")
endif()

#
# FatFs, taken from the copy the SDK already vendors with TinyUSB. It is
# BSD-licensed and it is exactly the code not to hand-roll: a buggy FAT
# implementation corrupts save files. Override FATFS_DIR to point
# somewhere else if the SDK ever stops vendoring it.
#
set(FATFS_DIR
        "${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source"
        CACHE PATH "FatFs source directory")

if(NOT EXISTS "${FATFS_DIR}/ff.c")
    message(FATAL_ERROR "No FatFs at ${FATFS_DIR}/ff.c")
endif()

#
# FatFs is configured by the ffconf.h vendored next to it, and it already
# suits this use: writable (FF_FS_READONLY 0), no on-device formatting
# (FF_USE_MKFS 0, the volume is built on the host by mkvolume.sh where
# fsck can check it), long filenames with ffunicode.c, 512-byte sectors to
# match the flash driver, no RTC, and more than one drive slot although
# only the current drive is ever used.
#
# Passing -D overrides for these does not work and is not attempted: ffconf.h
# defines each with an unconditional #define, so the file wins over the
# command line and the compiler only warns about the redefinition. Edit
# ffconf.h if a game genuinely needs different settings.
#

option(RP2040_CONSOLE_IO_TRACE
        "Trace file opens and block I/O to the console (noisy during play)" OFF)
option(RP2040_CONSOLE_UART_STDIN
        "Also read input from UART0, for when USB will not come up" OFF)
option(RP2040_CONSOLE_CLOCK_PROMPT
        "Ask for the date and time at startup (Enter accepts the default)" ON)

#
# How long to wait for someone to open the serial port before printing anything,
# and whether an open port requires the host to assert DTR. Both exist because
# stdio_usb discards output, and does not read input, while it believes no
# terminal is attached; see console_wait_for_terminal().
#
set(RP2040_CONSOLE_CONNECT_WAIT_MS "0" CACHE STRING
        [==[Milliseconds to wait for a terminal to open the port before
        starting; 0 (the default) waits until one does]==])

option(RP2040_CONSOLE_DTR_GATE
        "Only do console I/O while the host asserts DTR (off: always allowed)" OFF)

#
# The onboard LED, which reports how far the boot got on a board that prints
# nothing. There is no way to find the LED at run time, so it is a build choice:
# GP25 on a Raspberry Pi Pico, GP24 on a Waveshare RP2040-Zero, GP22 on a
# Pimoroni Pico Plus 2. See src/diag.c.
#
set(RP2040_CONSOLE_LED "25" CACHE STRING
        [==[GPIO(s) of the onboard LED, comma separated; empty disables it]==])

#
# Defaults for the values a game may override per target. The flash layout
# is here rather than buried in the driver because it has to be agreed
# between two things that never talk to each other: the program, which reads
# the volume, and mkvolume.sh, which writes it.
#
set(RP2040_CONSOLE_FS_OFFSET "0x180000" CACHE STRING
        "Flash offset of the FAT volume")
set(RP2040_CONSOLE_FS_SIZE "524288" CACHE STRING
        "FAT volume size in bytes")
set(RP2040_CONSOLE_COLS "80" CACHE STRING
        "Terminal columns reported to the game")
set(RP2040_CONSOLE_ROWS "24" CACHE STRING
        "Terminal rows reported to the game")

# ------------------------------------------------------------------------

#
# Declare a game to build against the engine.
#
#   NAME          target and output name
#   ARGV0         what argv[0] holds (default: NAME)
#   SOURCES       the game's own sources
#   GENERATED     files generated into the build tree, produced by a custom
#                 command in the game's own CMakeLists; passed straight to
#                 add_executable so the command runs
#   INCLUDE_DIRS  where the game's headers live
#   ENV           NAME=VALUE entries for the game's environment
#   ROOT          directory DATA_FILES are found in
#   DATA_FILES    files to place on the volume
#   EMPTY_FILES   empty files to create (score files and lock files a game
#                 expects an installation to have provided and never makes)
#   DATA_DIRS     directories to create on the volume
#   VOLUME_NAME   FAT volume label
#   FS_OFFSET     flash offset of the volume      (default 0x180000)
#   FS_SIZE       volume size in bytes            (default 524288)
#   HEAP_END      top of the heap; see below      (default: SDK's)
#   COLS ROWS     terminal size to report         (default 80 24)
#   LISTDIR       volume directory listed at startup (default: none)
#   LEGACY_C      quiet the compiler about pre-ANSI idioms
#   RENAMES       name=replacement, for a name in the program that clashes with
#                 something else in the link; see the prefix header
#
function(rp2040_console_game)
    set(options LEGACY_C LEGACY_TIME32 SOFT_REBOOT)
    set(oneValueArgs
            NAME
            ROOT
            ARGV0
            VOLUME_NAME
            FS_OFFSET
            FS_SIZE
            HEAP_END
            FLASH_SIZE
            STACK_RESERVE
            COLS
            ROWS
            LISTDIR)
    set(multiValueArgs
            SOURCES
            GENERATED
            INCLUDE_DIRS
            DEFINES
            ENV
            RENAMES
            SYSTEM_HEADERS
            DATA_FILES
            EMPTY_FILES
            DATA_DIRS)

    cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}"
            ${ARGN})

    if(NOT CG_NAME)
        message(FATAL_ERROR "rp2040_console_game() requires NAME")
    endif()

    if(NOT CG_ARGV0)
        set(CG_ARGV0 ${CG_NAME})
    endif()

    foreach(v FS_OFFSET FS_SIZE COLS ROWS)
        if("${CG_${v}}" STREQUAL "")
            set(CG_${v} ${RP2040_CONSOLE_${v}})
        endif()
    endforeach()

    # Each target owns its generated manifest and prefix headers.
    set(_config_dir "${CMAKE_CURRENT_BINARY_DIR}/${CG_NAME}-console")
    file(MAKE_DIRECTORY "${_config_dir}")
    if("${CG_FLASH_SIZE}" STREQUAL "")
        if(PICO_FLASH_SIZE_BYTES)
            set(CG_FLASH_SIZE "${PICO_FLASH_SIZE_BYTES}")
        elseif(PICO_DEFAULT_FLASH_SIZE_BYTES)
            set(CG_FLASH_SIZE "${PICO_DEFAULT_FLASH_SIZE_BYTES}")
        else()
            set(CG_FLASH_SIZE "2 * 1024 * 1024")
        endif()
    endif()
    math(EXPR CG_FLASH_SIZE "${CG_FLASH_SIZE}")
    math(EXPR _fs_end "${CG_FS_OFFSET} + ${CG_FS_SIZE}")
    math(EXPR _fs_offset "${CG_FS_OFFSET}")
    math(EXPR _fs_size "${CG_FS_SIZE}")
    math(EXPR _offset_remainder "${_fs_offset} % 4096")
    math(EXPR _size_remainder "${_fs_size} % 4096")
    if(_fs_offset LESS_EQUAL 0 OR _fs_size LESS_EQUAL 0 OR
       _fs_end GREATER CG_FLASH_SIZE OR _offset_remainder OR _size_remainder)
        message(FATAL_ERROR "${CG_NAME}: FAT region must fit flash and use positive 4 KB-aligned offset/size")
    endif()
    if(NOT CG_STACK_RESERVE)
        set(CG_STACK_RESERVE 0)
    endif()
    set(_system_headers "/* Native declarations, before application renames. */\n")
    foreach(header IN LISTS CG_SYSTEM_HEADERS)
        if(NOT header MATCHES "^[A-Za-z0-9_./+-]+$")
            message(FATAL_ERROR "Invalid SYSTEM_HEADERS entry '${header}'")
        endif()
        string(APPEND _system_headers "#include <${header}>\n")
    endforeach()
    file(WRITE "${_config_dir}/rp2040-console-system.h" "${_system_headers}")

    # --- the game's environment ----------------------------------------
    #
    # Turned into a C array included by env.c, rather than into a pile of
    # -D definitions: values contain slashes and equals signs, and a
    # generated file does not have to survive quoting twice.
    #
    set(_env_table "")
    foreach(entry IN LISTS CG_ENV)
        string(FIND "${entry}" "=" _eq)
        if(_eq LESS 1)
            message(FATAL_ERROR "ENV entry '${entry}' is not NAME=VALUE")
        endif()
        string(SUBSTRING "${entry}" 0 ${_eq} _name)
        math(EXPR _start "${_eq} + 1")
        string(SUBSTRING "${entry}" ${_start} -1 _value)
        string(APPEND _env_table "    { \"${_name}\", \"${_value}\" },\n")
    endforeach()

    file(WRITE "${_config_dir}/rp2040_console_env.inc"
            "/* Generated from the game's manifest. Do not edit. */\n"
            "${_env_table}")

    # --- names that clash ---------------------------------------------
    #
    # Which names clash is a fact about the program rather than about this
    # platform - a one-argument itoa(), or a panic() of one's own, are things a
    # particular program chose - so the program says so here and the prefix
    # header includes whatever comes out. That keeps the engine out of the
    # program's vocabulary.
    #
    # Written at configure time rather than by a build step: the file is
    # included, not compiled, and nothing lists it as a source, so a build-time
    # dependency would not be ordered on the first build in a clean tree.
    #
    set(_renames "")
    foreach(entry IN LISTS CG_RENAMES)
        string(FIND "${entry}" "=" _eq)
        if(_eq LESS 1)
            message(FATAL_ERROR "RENAMES entry '${entry}' is not name=replacement")
        endif()
        string(SUBSTRING "${entry}" 0 ${_eq} _from)
        math(EXPR _start "${_eq} + 1")
        string(SUBSTRING "${entry}" ${_start} -1 _to)
        string(APPEND _renames "#define ${_from} ${_to}\n")
    endforeach()

    file(WRITE "${_config_dir}/rp2040-console-renames.h"
            "/* Generated from the game's manifest. Do not edit.\n"
            " *\n"
            " * Included by rp2040-console.h after the C library headers have been\n"
            " * read, so that a rename reaches the program's own declarations and\n"
            " * calls without also rewriting a libc declaration of the same name.\n"
            " */\n"
            "${_renames}")

    # --- the executable -------------------------------------------------
    #
    add_executable(${CG_NAME}
            "${RP2040_CONSOLE_SRC}/engine_main.c"
            "${RP2040_CONSOLE_SRC}/probes.c"
            "${RP2040_CONSOLE_SRC}/restart.c"
            "${RP2040_CONSOLE_SRC}/clock.c"
            "${RP2040_CONSOLE_SRC}/boot.c"
            "${RP2040_CONSOLE_SRC}/env.c"
            "${RP2040_CONSOLE_SRC}/entropy.c"
            "${RP2040_CONSOLE_SRC}/syscalls.c"
            "${RP2040_CONSOLE_SRC}/heap.c"
            "${RP2040_CONSOLE_SRC}/time32.c"
            "${RP2040_CONSOLE_SRC}/fileio.c"
            "${RP2040_CONSOLE_SRC}/storage.c"
            "${RP2040_CONSOLE_SRC}/flash_fs.c"
            "${RP2040_CONSOLE_SRC}/sd_card.c"
            "${RP2040_CONSOLE_SRC}/termcap_shim.c"
            "${RP2040_CONSOLE_SRC}/termios_emul.c"
            "${RP2040_CONSOLE_SRC}/console.c"
            "${RP2040_CONSOLE_SRC}/diag.c"
            "${RP2040_CONSOLE_SRC}/mtime.c"
            "${FATFS_DIR}/ff.c"
            "${FATFS_DIR}/ffunicode.c"
            "${FATFS_DIR}/ffsystem.c"
            ${CG_SOURCES}
            ${CG_GENERATED}
            )

    target_include_directories(${CG_NAME} PRIVATE
            "${_config_dir}"
            "${CMAKE_CURRENT_BINARY_DIR}"
            )

    # FatFs headers first: ff.h and the game's own <sys/stat.h> disagree
    # about a thing or two and FatFs has to win in its own drivers.
    #
    # The engine's include directory comes first. It holds the headers this
    # toolchain does not have at all - <termcap.h>, <curses.h>, <sys/ioctl.h>,
    # and the <sys/termios.h> that newlib's <termios.h> is a one-line include of
    # - plus nothing that shadows a working header, which is deliberate: the
    # moment this directory shadows a real newlib header, bugs stop being about
    # the port and start being about which of two headers won.
    #
    target_include_directories(${CG_NAME} BEFORE PRIVATE
            "${RP2040_CONSOLE_INCLUDE}"
            "${RP2040_CONSOLE_SRC}"
            ${FATFS_DIR}
            )

    # Prepended to the game's own translation units, and to them alone. This is
    # where the port's preprocessing-level fixes go - the getchar() translation
    # and the handful of names that clash with another implementation - so that
    # a game's sources and headers never have to mention the platform.
    #
    # Per source file rather than a target option, because this is not the tidy
    # choice it looks like: the Pico SDK arranges for its own sources to be
    # compiled into the executable target, so a target-wide -include rewrites
    # the SDK as well. That is not harmless - the SDK defines a panic() with a
    # different signature from a game's, and renaming both to the same name
    # turns two functions into a duplicate symbol. An assembler source handed
    # <stdio.h> fails too, and fails somewhere that points nowhere near the
    # cause.
    #
    set(_program_sources ${CG_SOURCES} ${CG_GENERATED})
    set(_program_options -include "${RP2040_CONSOLE_INCLUDE}/rp2040-console.h")
    if(CG_SOFT_REBOOT)
        list(APPEND _program_options -fno-common)
    endif()
    foreach(dir IN LISTS CG_INCLUDE_DIRS)
        get_filename_component(dir "${dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        # Quoted game headers win over compatibility headers with the same name.
        # Angle-bracket system headers continue to use the platform ABI first.
        list(APPEND _program_options -iquote "${dir}" -I "${dir}")
    endforeach()
    foreach(src IN LISTS _program_sources)
        if(src MATCHES "[.](c|C|cc|cpp|cxx)$")
            foreach(option IN LISTS _program_options)
                set_property(SOURCE "${src}" APPEND PROPERTY COMPILE_OPTIONS
                        "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,${CG_NAME}>:${option}>")
            endforeach()
            if(CG_LEGACY_TIME32)
                set_property(SOURCE "${src}" APPEND PROPERTY COMPILE_DEFINITIONS
                        "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,${CG_NAME}>:RP2040_CONSOLE_LEGACY_TIME32=1>")
            endif()
        endif()
    endforeach()

    if(CG_SOFT_REBOOT)
        if(NOT PICO_PLATFORM STREQUAL "rp2040" OR PICO_COPY_TO_RAM)
            message(FATAL_ERROR "SOFT_REBOOT requires RP2040 with XIP (PICO_COPY_TO_RAM=0)")
        endif()
        set(_declared_sources "")
        foreach(src IN LISTS _program_sources)
            get_source_file_property(_language "${src}" LANGUAGE)
            if(_language AND NOT _language STREQUAL "C")
                message(FATAL_ERROR "SOFT_REBOOT only supports C translation units: ${src} (${_language})")
            endif()
            if(NOT src MATCHES "[.](c|h|inc)$")
                message(FATAL_ERROR "SOFT_REBOOT requires declared C sources/headers; unsupported source: ${src}")
            endif()
            get_filename_component(src "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            string(APPEND _declared_sources "${src}\n")
        endforeach()
        foreach(rename IN LISTS CG_RENAMES)
            if(rename MATCHES "^(exit|_exit|_Exit|atexit)=")
                message(FATAL_ERROR "SOFT_REBOOT owns the exit/atexit names; remove ${rename}")
            endif()
        endforeach()
        file(WRITE "${_config_dir}/program-sources.txt" "${_declared_sources}")
        execute_process(COMMAND "${Python3_EXECUTABLE}"
                "${RP2040_CONSOLE_PATH}/tools/configure-soft-reboot.py"
                --sdk "${PICO_SDK_PATH}" --output "${_config_dir}/section_default_data.incl"
                RESULT_VARIABLE _linker_result)
        if(NOT _linker_result EQUAL 0)
            message(FATAL_ERROR "Could not configure SOFT_REBOOT linker ranges")
        endif()
        pico_add_linker_script_override_path(${CG_NAME} "${_config_dir}" FILES section_default_data.incl)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${RP2040_CONSOLE_PATH}/tools/configure-soft-reboot.py"
                "${PICO_SDK_PATH}/src/rp2_common/pico_standard_link/script_include/section_default_data.incl")
        set(_launcher "${Python3_EXECUTABLE}" "${RP2040_CONSOLE_PATH}/tools/tag-program-state.py"
                --sources "${_config_dir}/program-sources.txt" --objcopy "${CMAKE_OBJCOPY}"
                --objdump "${CMAKE_OBJDUMP}" --)
        if(CMAKE_C_COMPILER_LAUNCHER)
            list(APPEND _launcher ${CMAKE_C_COMPILER_LAUNCHER})
        endif()
        set_property(TARGET ${CG_NAME} PROPERTY C_COMPILER_LAUNCHER ${_launcher})
        target_sources(${CG_NAME} PRIVATE
                "${RP2040_CONSOLE_SRC}/soft_reboot.c"
                "${RP2040_CONSOLE_SRC}/soft_reboot_libc.c"
                "${RP2040_CONSOLE_SRC}/soft_reboot_platform.c")
        target_compile_definitions(${CG_NAME} PRIVATE RP2040_CONSOLE_SOFT_REBOOT=1)
        foreach(allocator _malloc_r _calloc_r _realloc_r _free_r _tzset_r _tzset_unlocked_r _setlocale_r)
            target_link_options(${CG_NAME} PRIVATE "-Wl,--wrap=${allocator}")
        endforeach()
        # Changes to the launcher must also recompile program objects.
        foreach(src IN LISTS _program_sources)
            if(src MATCHES "[.]c$")
                set_property(SOURCE "${src}" APPEND PROPERTY OBJECT_DEPENDS
                        "${RP2040_CONSOLE_PATH}/tools/tag-program-state.py")
            endif()
        endforeach()
    endif()

    target_compile_definitions(${CG_NAME} PRIVATE
            PICO_NO_BINARY_INFO=1
            RP2040_CONSOLE=1
            RP2040_CONSOLE_FS_OFFSET=${CG_FS_OFFSET}
            RP2040_CONSOLE_FS_SIZE=${CG_FS_SIZE}
            RP2040_CONSOLE_COLS=${CG_COLS}
            RP2040_CONSOLE_ROWS=${CG_ROWS}
            # What argv[0] says. A program uses it to name itself in messages
            # and to stat itself to date its own save files, so it has to be a
            # plausible name rather than the empty string the runtime would
            # otherwise leave behind.
            RP2040_CONSOLE_ARGV0="${CG_ARGV0}"
            RP2040_CONSOLE_LED="${RP2040_CONSOLE_LED}"
            RP2040_CONSOLE_CONNECT_WAIT_MS=${RP2040_CONSOLE_CONNECT_WAIT_MS}
            )

    #
    # Whether the console requires the host to assert DTR.
    #
    # With the gate in place, stdio_usb drops output and ignores input
    # whenever it believes no terminal has the port open - and a terminal
    # opened a moment late then finds a board that prints nothing and answers
    # nothing. With the gate off, I/O happens whether or not DTR is up.
    #
    # This reaches stdio_usb.c because the SDK's pico_stdio_usb is an INTERFACE
    # target that hands its sources to whatever links it, so they are compiled
    # here, with the definitions given here.
    #
    if(NOT RP2040_CONSOLE_DTR_GATE)
        target_compile_definitions(${CG_NAME} PRIVATE
                PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1)
    endif()

    #
    # Anything else a program wants defined. The list should be short: a define
    # the program needs in order to compile at all is usually a header the
    # toolchain is missing, which belongs in the engine's include directory
    # where the compiler can find it, rather than a symbol the program then has
    # to test for in its own sources.
    #
    if(CG_DEFINES)
        target_compile_definitions(${CG_NAME} PRIVATE ${CG_DEFINES})
    endif()

    if(RP2040_CONSOLE_IO_TRACE)
        target_compile_definitions(${CG_NAME} PRIVATE RP2040_CONSOLE_IO_TRACE=1)
    endif()

    if(RP2040_CONSOLE_UART_STDIN)
        target_compile_definitions(${CG_NAME} PRIVATE RP2040_CONSOLE_UART_STDIN=1)
    endif()

    if(CG_LISTDIR)
        #
        # A directory the app wants listed at startup (NetHack's saved games).
        # Compiled in only when the manifest names one, since the idea of
        # "saves" is the app's, not the engine's.
        #
        target_compile_definitions(${CG_NAME} PRIVATE
                "RP2040_CONSOLE_LISTDIR=\"${CG_LISTDIR}\"")
    endif()

    if(NOT RP2040_CONSOLE_CLOCK_PROMPT)
        target_compile_definitions(${CG_NAME} PRIVATE RP2040_CONSOLE_CLOCK_NO_PROMPT=1)
    endif()

    if(CG_LEGACY_C)
        #
        # A game written before ANSI C was settled has idioms modern gcc
        # treats as errors or drown in warnings: implicit int, int used as
        # a pointer, calls through mismatched function types. They are the
        # program's style, not the engine's business, so opting in quiets
        # the compiler for the whole target rather than for individual
        # files.
        #
        target_compile_options(${CG_NAME} PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-Wno-int-conversion>
                $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-int>
                $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>
                -Wno-unused-variable
                -Wno-unused-but-set-variable
                -Wno-parentheses
                -Wno-misleading-indentation
                -Wno-format
                -Wno-stringop-truncation
                -Wno-sign-compare
                -Wno-unused-function
                -Wno-array-bounds
                -Wno-restrict
                )
    endif()

    #
    # The engine owns startup, and does it by being the only main() in the
    # link: the prefix header renames the program's own main to
    # rp2040_console_game_main, which the engine's main() calls once the
    # console, the environment, the clock and the volume are up. A program
    # therefore needs no platform-specific initialisation call of its own, and
    # cannot forget one.
    #
    # The obvious way to do this is -Wl,--wrap=main, and it does not work here.
    # --wrap redirects a reference only where that reference would otherwise be
    # resolved out of an archive; a symbol defined by an object file handed to
    # the linker directly is used as-is. The C library's putchar and printf
    # qualify, which is why the SDK's own wrapping of them works. A game's
    # main.c does not: it is one of the objects in the link, so the C runtime's
    # call to main reaches it untouched, __wrap_main is included and never
    # called, and the program runs with no console and no filesystem. Nothing
    # complains. Renaming at preprocessing time is both the reliable version of
    # the same idea and the version that fails loudly, because a program with no
    # main to rename leaves an undefined reference to rp2040_console_game_main.
    #
    # Worth knowing that the Pico SDK's crt0 reaches main through a literal pool
    # (ldr r1, =main; blx r1) rather than a direct bl, so even a working wrap
    # would be doing something subtler than it looks.
    #

    #
    # The console's buffering policy is enforced with --wrap, and here --wrap does
    # work: setbuf() and setvbuf() are C library symbols, so a reference to one is
    # precisely the kind that would be resolved out of an archive. See console.c
    # for why a program is not allowed to buffer the console: with two writers,
    # only one of which goes through stdio, buffering reorders the output.
    #
    target_link_options(${CG_NAME} PRIVATE
            "-Wl,--wrap=setvbuf"
            "-Wl,--wrap=setbuf"
            )

    if(CG_HEAP_END)
        #
        # This SDK's linker script grows .heap from the end of .bss to the
        # top of RAM and reads only HEAP_LIMIT, so leaving it alone means
        # the heap swallows everything above .bss and the stack keeps its
        # small default. Setting the top of the heap explicitly is how the
        # two are sized deliberately: heap below, stack above the static
        # data. A game that needs more heap raises it and watches what is
        # left.
        #
        target_link_options(${CG_NAME} PRIVATE "-Wl,--defsym=HEAP_LIMIT=${CG_HEAP_END}")
    endif()

    target_link_libraries(${CG_NAME}
            pico_stdlib
            pico_rand
            hardware_flash
            hardware_watchdog
            hardware_spi
            )

    # The legacy difftime adapter uses real double arithmetic. Unreferenced
    # routines are still discarded; native terminal builds retain no-float.
    pico_set_float_implementation(${CG_NAME} none)
    if(CG_LEGACY_TIME32)
        pico_set_double_implementation(${CG_NAME} pico)
    else()
        pico_set_double_implementation(${CG_NAME} none)
    endif()

    #
    # These set target properties, so the target must already exist, and the
    # explicit 0 on the UART matters: pico_enable_stdio_uart(<t> 1) sets the
    # *cache* variable PICO_STDIO_UART, and deleting that call later does not
    # unset it. The cached value survives, the SDK's UART driver stays linked
    # and registered on stdin, and a build that reads as USB-only goes on
    # reading a GPIO that is floating - which delivers garbage at the baud
    # rate forever, and a program reading stdin consumes it as input.
    # Passing 0 overwrites the stale entry.
    #
    pico_enable_stdio_usb(${CG_NAME} 1)
    pico_enable_stdio_uart(${CG_NAME} 0)

    pico_override_flash_size(${CG_NAME} ${CG_FLASH_SIZE})
    # Force the SDK's PROVIDE symbol into the symbol table for validation.
    target_link_options(${CG_NAME} PRIVATE "-Wl,--undefined=__flash_binary_end")
    pico_add_extra_outputs(${CG_NAME})
    add_custom_command(TARGET ${CG_NAME} POST_BUILD
            COMMAND "${Python3_EXECUTABLE}" "${RP2040_CONSOLE_PATH}/tools/check-memory.py"
                    --elf "$<TARGET_FILE:${CG_NAME}>" --nm "${CMAKE_NM}"
                    --flash-size "${CG_FLASH_SIZE}" --fs-offset "${CG_FS_OFFSET}"
                    --fs-size "${CG_FS_SIZE}" --stack-reserve "${CG_STACK_RESERVE}"
                    --copy-to-ram "$<BOOL:${PICO_COPY_TO_RAM}>"
            VERBATIM)
    set_property(TARGET ${CG_NAME} APPEND PROPERTY LINK_DEPENDS
            "${RP2040_CONSOLE_PATH}/tools/check-memory.py")

    # --- the volume ------------------------------------------------------
    #
    # Written out as a manifest that mkvolume.sh reads, so the files a game
    # needs are declared once, here, and the shell script does not have to
    # know what game it is packaging.
    #
    if(CG_ROOT)
        set(_root "${CG_ROOT}")
    else()
        set(_root "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    if(NOT CG_VOLUME_NAME)
        # 8 characters, because that is what a FAT volume label holds and a
        # truncated one is confusing to read back on a host.
        string(SUBSTRING "${CG_NAME}_______" 0 8 _label)
        string(TOUPPER "${_label}" CG_VOLUME_NAME)
    endif()

    set(_manifest "# Generated by rp2040-console from ${CG_NAME}'s manifest.\n")
    foreach(path IN LISTS CG_DATA_FILES CG_EMPTY_FILES CG_DATA_DIRS ITEMS "${_root}")
        if(path MATCHES "[|\t\r\n#]")
            message(FATAL_ERROR "Volume paths cannot contain tabs, newlines, '#' or '|': ${path}")
        endif()
    endforeach()
    foreach(path IN LISTS CG_DATA_FILES CG_EMPTY_FILES CG_DATA_DIRS)
        if(path MATCHES "^/|(^|/)[.][.](/|$)")
            message(FATAL_ERROR "Volume destinations must be relative without '..': ${path}")
        endif()
    endforeach()
    string(APPEND _manifest "label ${CG_VOLUME_NAME}\n")
    string(APPEND _manifest "offset ${CG_FS_OFFSET}\n")
    string(APPEND _manifest "size ${CG_FS_SIZE}\n"
                        "flash_size ${CG_FLASH_SIZE}\n")
    foreach(f IN LISTS CG_DATA_FILES)
        string(APPEND _manifest "file\t${_root}/${f}\t${f}\n")
    endforeach()
    foreach(f IN LISTS CG_EMPTY_FILES)
        string(APPEND _manifest "empty\t${f}\n")
    endforeach()
    foreach(d IN LISTS CG_DATA_DIRS)
        string(APPEND _manifest "dir\t${d}\n")
    endforeach()

    set(_volume_manifest "${CMAKE_BINARY_DIR}/${CG_NAME}-volume.txt")
    file(WRITE "${_volume_manifest}" "${_manifest}")

    #
    # `cmake --build . --target volume` builds the FAT image and its UF2
    # next to the program's UF2.
    #
    #
    # Called through sh rather than cmake -P: building a volume needs
    # mkfs.vfat and the mtools, so this is a POSIX-host job already and a
    # second layer of script would only add quoting. mkvolume.sh finds
    # picotool itself; set PICOTOOL to tell it where.
    #
    add_custom_target(${CG_NAME}-volume
            COMMAND sh "${RP2040_CONSOLE_PATH}/tools/mkvolume.sh"
                    --manifest "${_volume_manifest}"
                    --out "${CMAKE_BINARY_DIR}"
            DEPENDS ${CG_NAME}
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT "Building the FAT volume for ${CG_NAME}"
            )
    if(NOT TARGET volume)
        add_custom_target(volume)
    endif()
    add_dependencies(volume ${CG_NAME}-volume)
endfunction()
