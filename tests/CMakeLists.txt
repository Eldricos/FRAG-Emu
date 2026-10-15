# SPDX-License-Identifier: GPL-3.0-or-later
# Builds tiny Windows test programs with cross-compilers and runs them through frag.
# Missing toolchains skip their tests instead of failing the build.

set(TEST_CD ${CMAKE_CURRENT_BINARY_DIR}/cd)
file(MAKE_DIRECTORY ${TEST_CD})
set(RUN_TEST ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)

find_program(MINGW32_CC i686-w64-mingw32-gcc)
if(MINGW32_CC)
  add_custom_command(
    OUTPUT ${TEST_CD}/hello32.exe
    COMMAND ${MINGW32_CC} -O2 -Wall -o ${TEST_CD}/hello32.exe ${CMAKE_CURRENT_SOURCE_DIR}/win32/hello32.c
    DEPENDS win32/hello32.c
    COMMENT "Building Win32 test program hello32.exe")
  add_custom_target(test_hello32 ALL DEPENDS ${TEST_CD}/hello32.exe)
  configure_file(conf/hello32.conf.in ${CMAKE_CURRENT_BINARY_DIR}/hello32.conf @ONLY)
  add_test(NAME win32_hello
           COMMAND ${RUN_TEST} $<TARGET_FILE:frag> ${CMAKE_CURRENT_BINARY_DIR}/hello32.conf)
  set_tests_properties(win32_hello PROPERTIES TIMEOUT 300)

  # A host CD drive that Wine assigned to D: must not hide the game's D:.
  configure_file(conf/stale_drive.conf.in ${CMAKE_CURRENT_BINARY_DIR}/stale-drive/hello32.conf @ONLY)
  add_test(NAME win32_stale_host_drive
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/stale_drive_test.sh $<TARGET_FILE:frag>
                   ${CMAKE_CURRENT_BINARY_DIR}/stale-drive/hello32.conf)
  set_tests_properties(win32_stale_host_drive PROPERTIES TIMEOUT 300)

  # Codec self-registration: a separate "CD" so the codec DLL can't shadow other tests.
  set(TEST_CODEC_CD ${CMAKE_CURRENT_BINARY_DIR}/codec-cd)
  file(MAKE_DIRECTORY ${TEST_CODEC_CD})
  add_custom_command(
    OUTPUT ${TEST_CODEC_CD}/IR50_32.DLL ${TEST_CODEC_CD}/CODEC_INSTALL.EXE
    COMMAND ${MINGW32_CC} -O2 -Wall -shared -Wl,--kill-at -o ${TEST_CODEC_CD}/IR50_32.DLL
            ${CMAKE_CURRENT_SOURCE_DIR}/win32/codec_dll.c
    COMMAND ${MINGW32_CC} -O2 -Wall -o ${TEST_CODEC_CD}/CODEC_INSTALL.EXE
            ${CMAKE_CURRENT_SOURCE_DIR}/win32/codec_install.c
    DEPENDS win32/codec_dll.c win32/codec_install.c
    COMMENT "Building codec registration test")
  add_custom_target(test_codec ALL DEPENDS ${TEST_CODEC_CD}/IR50_32.DLL ${TEST_CODEC_CD}/CODEC_INSTALL.EXE)
  configure_file(conf/codec.conf.in ${CMAKE_CURRENT_BINARY_DIR}/codec.conf @ONLY)
  add_test(NAME win32_codec_register
           COMMAND ${RUN_TEST} $<TARGET_FILE:frag> ${CMAKE_CURRENT_BINARY_DIR}/codec.conf)
  set_tests_properties(win32_codec_register PROPERTIES TIMEOUT 300)
else()
  message(STATUS "i686-w64-mingw32-gcc not found: Win32 tests disabled")
endif()

# Open Watcom: set WATCOM to its install directory (e.g. /opt/watcom).
set(WATCOM_DIR "$ENV{WATCOM}" CACHE PATH "Open Watcom install directory")
find_program(WATCOM_WCL wcl HINTS ${WATCOM_DIR}/binl64 ${WATCOM_DIR}/binl NO_DEFAULT_PATH)
if(WATCOM_WCL)
  add_custom_command(
    OUTPUT ${TEST_CD}/hello16.exe
    COMMAND ${CMAKE_COMMAND} -E env WATCOM=${WATCOM_DIR} "INCLUDE=${WATCOM_DIR}/h:${WATCOM_DIR}/h/win"
            "PATH=${WATCOM_DIR}/binl64:${WATCOM_DIR}/binl:$ENV{PATH}"
            ${WATCOM_WCL} -q -bt=windows -l=windows -zW -fe=${TEST_CD}/hello16.exe
            ${CMAKE_CURRENT_SOURCE_DIR}/win16/hello16.c
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS win16/hello16.c
    COMMENT "Building Win16 test program hello16.exe")
  add_custom_target(test_hello16 ALL DEPENDS ${TEST_CD}/hello16.exe)
  configure_file(conf/hello16.conf.in ${CMAKE_CURRENT_BINARY_DIR}/hello16.conf @ONLY)
  add_test(NAME win16_hello
           COMMAND ${RUN_TEST} $<TARGET_FILE:frag> ${CMAKE_CURRENT_BINARY_DIR}/hello16.conf
                   ${TEST_CD}/HELLO16.LOG)
  set_tests_properties(win16_hello PROPERTIES TIMEOUT 300)
else()
  message(STATUS "Open Watcom (wcl) not found: Win16 tests disabled; set WATCOM to enable")
endif()
