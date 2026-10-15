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

  # Games run from CD images (.iso, .cue/.bin), with the image's label and serial.
  add_test(NAME disc_image
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/disc_image_test.sh $<TARGET_FILE:frag>
                   ${TEST_CD}/hello32.exe ${CMAKE_CURRENT_BINARY_DIR}/disc-image)
  set_tests_properties(disc_image PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)

  # FluidSynth is started for a game's MIDI music and stopped afterwards.
  configure_file(conf/hello32.conf.in ${CMAKE_CURRENT_BINARY_DIR}/midi-synth/hello32.conf @ONLY)
  add_test(NAME midi_synth
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/midi_synth_test.sh $<TARGET_FILE:frag>
                   ${CMAKE_CURRENT_BINARY_DIR}/midi-synth/hello32.conf)
  set_tests_properties(midi_synth PROPERTIES TIMEOUT 600)

  # Windows 9x programs find the display card through HKEY_DYN_DATA (NFS3's 3dsetup).
  set(HW_CD ${CMAKE_CURRENT_BINARY_DIR}/hw-cd)
  file(MAKE_DIRECTORY ${HW_CD})
  add_custom_command(
    OUTPUT ${HW_CD}/WIN9X_HW.EXE
    COMMAND ${MINGW32_CC} -O2 -Wall -o ${HW_CD}/WIN9X_HW.EXE ${CMAKE_CURRENT_SOURCE_DIR}/win32/win9x_hardware.c
    DEPENDS win32/win9x_hardware.c
    COMMENT "Building Win32 test program win9x_hw.exe")
  add_custom_target(test_win9x_hardware ALL DEPENDS ${HW_CD}/WIN9X_HW.EXE)
  configure_file(conf/win9x_hardware.conf.in ${CMAKE_CURRENT_BINARY_DIR}/win9x_hardware.conf @ONLY)
  add_test(NAME win32_win9x_hardware
           COMMAND ${RUN_TEST} $<TARGET_FILE:frag> ${CMAKE_CURRENT_BINARY_DIR}/win9x_hardware.conf)
  set_tests_properties(win32_win9x_hardware PROPERTIES TIMEOUT 300)

  # [wine] desktop: games run in a window of the given size.
  add_custom_command(
    OUTPUT ${HW_CD}/SCREEN_SIZE.EXE
    COMMAND ${MINGW32_CC} -O2 -Wall -o ${HW_CD}/SCREEN_SIZE.EXE ${CMAKE_CURRENT_SOURCE_DIR}/win32/screen_size.c
    DEPENDS win32/screen_size.c
    COMMENT "Building Win32 test program screen_size.exe")
  add_custom_target(test_screen_size ALL DEPENDS ${HW_CD}/SCREEN_SIZE.EXE)
  configure_file(conf/desktop.conf.in ${CMAKE_CURRENT_BINARY_DIR}/desktop.conf @ONLY)
  add_test(NAME win32_desktop
           COMMAND ${RUN_TEST} $<TARGET_FILE:frag> ${CMAKE_CURRENT_BINARY_DIR}/desktop.conf)
  set_tests_properties(win32_desktop PROPERTIES TIMEOUT 300)

  # [wine] video_memory: the amount DirectDraw reports, whatever the GL driver says.
  add_custom_command(
    OUTPUT ${HW_CD}/VIDMEM.EXE
    COMMAND ${MINGW32_CC} -O2 -Wall -o ${HW_CD}/VIDMEM.EXE ${CMAKE_CURRENT_SOURCE_DIR}/win32/vidmem.c
            -lddraw -ldxguid
    DEPENDS win32/vidmem.c
    COMMENT "Building Win32 test program vidmem.exe")
  add_custom_target(test_vidmem ALL DEPENDS ${HW_CD}/VIDMEM.EXE)
  configure_file(conf/vidmem.conf.in ${CMAKE_CURRENT_BINARY_DIR}/vidmem.conf @ONLY)
  add_test(NAME win32_video_memory
           COMMAND ${RUN_TEST} $<TARGET_FILE:frag> ${CMAKE_CURRENT_BINARY_DIR}/vidmem.conf)
  set_tests_properties(win32_video_memory PROPERTIES TIMEOUT 300)

  # Games find the MS ADPCM codec by its Windows name; FRAG ships a renamed copy.
  set(ACM_CD ${CMAKE_CURRENT_BINARY_DIR}/acm-cd)
  file(MAKE_DIRECTORY ${ACM_CD})
  add_custom_command(
    OUTPUT ${ACM_CD}/ACM_ADPCM.EXE
    COMMAND ${MINGW32_CC} -O2 -Wall -o ${ACM_CD}/ACM_ADPCM.EXE
            ${CMAKE_CURRENT_SOURCE_DIR}/win32/acm_adpcm.c -lmsacm32 -lwinmm
    DEPENDS win32/acm_adpcm.c
    COMMENT "Building Win32 test program acm_adpcm.exe")
  add_custom_target(test_acm_adpcm ALL DEPENDS ${ACM_CD}/ACM_ADPCM.EXE)
  configure_file(conf/acm_adpcm.conf.in ${CMAKE_CURRENT_BINARY_DIR}/acm_adpcm.conf @ONLY)
  add_test(NAME win32_acm_adpcm
           COMMAND ${RUN_TEST} $<TARGET_FILE:frag> ${CMAKE_CURRENT_BINARY_DIR}/acm_adpcm.conf)
  set_tests_properties(win32_acm_adpcm PROPERTIES TIMEOUT 300)

  # Sound probe: a diagnostic users run on their own machine to see what games see.
  #   frag run build/tests/soundprobe.conf
  set(SOUNDPROBE_CD ${CMAKE_CURRENT_BINARY_DIR}/soundprobe-cd)
  file(MAKE_DIRECTORY ${SOUNDPROBE_CD})
  add_custom_command(
    OUTPUT ${SOUNDPROBE_CD}/SOUNDPROBE.EXE
    COMMAND ${MINGW32_CC} -O2 -Wall -o ${SOUNDPROBE_CD}/SOUNDPROBE.EXE
            ${CMAKE_CURRENT_SOURCE_DIR}/win32/soundprobe.c -lwinmm -ldsound -lmsacm32
    DEPENDS win32/soundprobe.c
    COMMENT "Building sound probe")
  add_custom_target(soundprobe ALL DEPENDS ${SOUNDPROBE_CD}/SOUNDPROBE.EXE)
  configure_file(conf/soundprobe.conf.in ${CMAKE_CURRENT_BINARY_DIR}/soundprobe.conf @ONLY)
  configure_file(conf/soundprobe-quiet.conf.in ${CMAKE_CURRENT_BINARY_DIR}/soundprobe-quiet.conf @ONLY)
  add_test(NAME win32_soundprobe_runs
           COMMAND ${RUN_TEST} $<TARGET_FILE:frag> ${CMAKE_CURRENT_BINARY_DIR}/soundprobe-quiet.conf)
  set_tests_properties(win32_soundprobe_runs PROPERTIES TIMEOUT 300)

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
