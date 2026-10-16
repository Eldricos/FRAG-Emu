# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(frag VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_executable(frag
  src/main.cpp
  src/config.cpp
  src/disc.cpp
  src/display.cpp
  src/midi.cpp
  src/wine.cpp
)
target_compile_definitions(frag PRIVATE FRAG_VERSION="${PROJECT_VERSION}")
target_compile_options(frag PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS frag RUNTIME DESTINATION bin)

# Windows DLLs FRAG installs into game prefixes, built next to the frag binary
# (build/dlls) and installed to share/frag/dlls. They need the MinGW cross-compiler;
# without it frag still works, minus these fixes.
find_program(MINGW32_CC i686-w64-mingw32-gcc)
if(MINGW32_CC)
  set(MSADP32_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wine-msadp32)
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/dlls/msadp32.acm
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dlls
    COMMAND ${MINGW32_CC} -O2 -Wall -shared -I${MSADP32_SRC} -o ${CMAKE_BINARY_DIR}/dlls/msadp32.acm
            ${MSADP32_SRC}/msadp32.c ${MSADP32_SRC}/msadp32.def -lwinmm
    DEPENDS ${MSADP32_SRC}/msadp32.c ${MSADP32_SRC}/msadp32.def ${MSADP32_SRC}/msacmdrv.h
            ${MSADP32_SRC}/wine/debug.h
    COMMENT "Building msadp32.acm (MS ADPCM codec with Windows names)")
  add_custom_target(msadp32 ALL DEPENDS ${CMAKE_BINARY_DIR}/dlls/msadp32.acm)
  # Helper programs FRAG runs inside prefixes.
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/dlls/fragdesk.exe
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dlls
    COMMAND ${MINGW32_CC} -O2 -Wall -mwindows -o ${CMAKE_BINARY_DIR}/dlls/fragdesk.exe
            ${CMAKE_CURRENT_SOURCE_DIR}/helpers/fragdesk.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/helpers/fragdesk.c
    COMMENT "Building fragdesk.exe")
  add_custom_target(fragdesk ALL DEPENDS ${CMAKE_BINARY_DIR}/dlls/fragdesk.exe)
  # fragstart.exe starts games with fragfix.dll (in-process fixes) loaded.
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/dlls/fragstart.exe ${CMAKE_BINARY_DIR}/dlls/fragfix.dll
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dlls
    COMMAND ${MINGW32_CC} -O2 -Wall -o ${CMAKE_BINARY_DIR}/dlls/fragstart.exe
            ${CMAKE_CURRENT_SOURCE_DIR}/helpers/fragstart.c
    COMMAND ${MINGW32_CC} -O2 -Wall -shared -o ${CMAKE_BINARY_DIR}/dlls/fragfix.dll
            ${CMAKE_CURRENT_SOURCE_DIR}/helpers/fragfix.c -lntdll
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/helpers/fragstart.c ${CMAKE_CURRENT_SOURCE_DIR}/helpers/fragfix.c
    COMMENT "Building fragstart.exe and fragfix.dll")
  add_custom_target(fragstart ALL DEPENDS ${CMAKE_BINARY_DIR}/dlls/fragstart.exe)
  install(FILES ${CMAKE_BINARY_DIR}/dlls/msadp32.acm ${CMAKE_BINARY_DIR}/dlls/fragdesk.exe
                ${CMAKE_BINARY_DIR}/dlls/fragstart.exe ${CMAKE_BINARY_DIR}/dlls/fragfix.dll
          DESTINATION share/frag/dlls)
else()
  message(WARNING "i686-w64-mingw32-gcc not found: building without msadp32.acm "
                  "(games that look for the MS ADPCM codec by name will have no sound)")
endif()

option(FRAG_BUILD_TESTS "Build the Windows test programs and register CTest tests" ON)
if(FRAG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
