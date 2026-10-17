# FRAG-Emu project notes

FRAG ("Faithfully Runs Ancient Games") runs Windows 3.1/95/98 games on modern
systems with a DOSBox-like experience. It is built on Wine; FRAG's value is the
per-game packaging and 9x-specific fixes around it. Games only, not general apps.

## Decisions

- **Approach:** API reimplementation via Wine, not full-PC emulation. Fall back to
  86Box/DOSBox-X with a real Win98 install for titles that can't work this way.
- **Hosts:** Linux x86-64 first. Windows/macOS hosts and ARM (Box64/FEX) only later.
- **Language:** C++17 for the launcher, C for Windows test programs. CMake + Ninja.
- **License:** GPL-3.0-or-later. Wine-derived code keeps its LGPL-2.1+ headers.
  The owner can submit their own Wine fixes upstream under LGPL separately.
- **No game data in the repo**, ever. `.gitignore` blocks disc images and `games/`.

## Layout

- `src/config.*` — `frag.conf` parser (`[game]`, `[wine]`, `[drive X]`, `[dlls]`).
- `src/wine.*` — prefix creation, drive symlinks, registry (version + drive types), launching.
- `src/disc.*` — CD images: ISO9660/Joliet reader for .iso, .cue/.bin, raw .bin/.img.
- `src/midi.*` — starts/stops FluidSynth around a game run.
- `src/display.*` — restores the host screen mode (xrandr) after a game.
- `helpers/` — FRAG's own Windows helper programs, built into `build/dlls`:
  `fragdesk.exe` (virtual desktop probe), `fragstart.exe` + `fragfix.dll` (game
  launcher with in-process fixes).
- `src/main.cpp` — CLI: `frag run`, `frag prefix`, `--version`.
- `third_party/` — Wine-derived Windows DLLs FRAG builds and installs into prefixes.
- `tests/win32`, `tests/win16` — tiny Windows programs built with MinGW and Open Watcom.
  They check Windows version, drive mapping, the message loop and installer-style
  codec registration. `tests/run_test.sh`
  runs them through `frag` (under `xvfb-run` without a display).

## Build and test

```sh
WATCOM=/opt/watcom cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Cloud sessions need: `wine wine32:i386 wine64 gcc-mingw-w64-i686` (apt, after
`dpkg --add-architecture i386`) and Open Watcom from
`https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz`
extracted to `/opt/watcom`. First prefix creation takes ~20 s.

## Known behaviour

- Passing a Windows path (`D:\GAME.EXE`) to Wine makes the program see its own drive
  letter and a matching working directory; verified by `win32_hello`.
- Win16 programs run under Ubuntu's Wine 9.0 in this environment (`win16_hello`).
- `WINEPREFIX` must be absolute; `data_dir()` makes `FRAG_HOME` absolute.
- `WINEDEBUG` for games: `--wine-debug`, else `$WINEDEBUG` (honoured only since
  Oct 2026: earlier `WINEDEBUG=... frag run` traces were silently ignored, e.g. the
  empty NFS III relay traces), else `fixme-all`, so `err:` lines (e.g. which DLL is
  missing for c0000135) are shown.
- Video codecs (Indeo 3/4/5, Cinepak, MS Video 1) default to `native,builtin`.
  With Wine's builtin first, InstallShield fails with -119 registering a game's
  bundled `ir50_32.dll` (Wine's builtin has no DllRegisterServer). Found on the
  owner's first real game; covered by `win32_codec_register`.
  Some installers still keep Wine's codec (it has no version info) and then fail;
  workaround: delete `system32/ir50_32.dll` from the prefix before installing.
  TODO: per-game option to replace Wine's codecs.
- Host drives: Wine's mountmgr gets host CD/USB drives from UDisks over D-Bus and
  claims letters from D:. With no disc it deletes the `d:` folder link, so every
  run after the first failed with `wine: failed to open "D:\SETUP.EXE": c0000135`
  (that status means the exe file wasn't found). FRAG sets
  `DBUS_SYSTEM_BUS_ADDRESS` to a nonexistent socket for all Wine processes, removes
  `x::` device links and `MountedDevices` entries for its letters.
  `win32_stale_host_drive` checks the repair; the D-Bus part needs a real desktop to verify.
- Drive types must be written to the 64-bit registry view (`reg import ... /reg:64`);
  `wine regedit` is 32-bit and lands in Wow6432Node, where mountmgr ignores it.
  mountmgr reads them only at startup, so FRAG kills the prefix's wineserver after
  changing settings. Labels come from `.windows-label` in the drive folder.
  `win32_hello` checks CD-ROM type + label on a fresh prefix.

- winemenubuilder is disabled (`winemenubuilder.exe=d` in every Wine process, and
  its RunServices entry removed) so installers' shortcuts don't land in the host's
  application menu. Verified with a test program that saves a Start-menu `.lnk`.

- Z: (host filesystem) is hidden by default: `sync_drives` removes `dosdevices/z:`
  every run unless `[wine] host_drive = yes`. Classic Board Games scanned drive
  letters, hit Z:, overflowed a path buffer and crashed (return address became
  "11\\X"). Consequences: the settings .reg is imported from `C:\\windows\\` (host
  paths don't resolve without Z:), and a host-path `exe` is converted to its
  drive's Windows path (`to_windows_path`) or rejected. `win32_hello` checks Z: is hidden.

- FRAG's own DLLs: `third_party/` holds Wine-derived DLL sources that FRAG builds
  with MinGW into `build/dlls` (installed: `share/frag/dlls`; found via
  `/proc/self/exe`). `install_dlls` copies each into the prefix's `syswow64` (or
  `system32` in a 32-bit prefix) on every run, and `run()` gives it a `native,builtin`
  override. Currently only `msadp32.acm` (Wine's MS ADPCM codec reporting Windows'
  name "Microsoft ADPCM"), checked by `win32_acm_adpcm`. Without MinGW, frag warns and
  runs without them.

- MIDI synth (`src/midi.*`): unless `[sound] midi_synth = off`, `frag run` starts
  `fluidsynth -a pulseaudio -m alsa_seq <soundfont>` when no synth ("fluid",
  "timidity", "munt") is listed in `/proc/asound/seq/clients`, waits up to 5 s for it
  to appear there (Wine enumerates MIDI ports once, at winmm load), and stops it
  after the game. FluidSynth runs its shell on a pipe FRAG holds (O_CLOEXEC so Wine
  doesn't inherit it); closing the pipe makes it quit. `-i` without `-s` exits at
  once. Log: `<prefix>/fluidsynth.log`. `midi_synth` test uses a stand-in script and
  `FRAG_SEQ_CLIENTS`. Confirmed on the owner's machine with Classic Board Games: music
  plays and FluidSynth is gone after the game exits.

- CD images (`[drive X] image = ...`): `prepare_disc_drives` (in `frag run` only,
  before the prefix) unpacks the data track once into `<data_dir>/discs/<stem>-<hash
  of path+size+mtime>` (via `.partial` + rename) and sets the drive's path; type
  defaults to cdrom. Joliet tree and label when present (as Wine and Windows use
  for real discs), else ISO9660 with ";1" and trailing "." stripped. Files are
  0444 (CD files are read-only on Windows) with the record's date. Serial = byte
  sums of that descriptor in Win9x order (NT order for NT versions), written to
  `.windows-serial`, which Wine reads for folder drives. Raw sectors: sync pattern
  detection, MODE1 data at 16, MODE2 XA at 24. CUE: first non-AUDIO track, INDEX 01.
  Not yet: CD audio (Wine's mcicda needs real CD ioctls), multi-session, .mdf/.nrg/.ccd.
  `disc_image` test builds an ISO with genisoimage (skips if missing) and a
  .cue/.bin with python3.
  Confirmed on the owner's machine: Classic Board Games runs from a `dd`-made .iso
  with no label setting.

- Win9x hardware registry (win31/95/98/me only): Wine leaves
  `HKEY_DYN_DATA\Config Manager\Enum` empty (Wine bug 7115). FRAG adds one PCI
  display device (NVIDIA RIVA TNT): persistent `HKLM\Enum\PCI\...` (Class Display,
  Driver Display\0000) and `HKLM\System\CurrentControlSet\Services\Class\Display\0000`
  in frag-settings.reg, plus the DYN_DATA entry with HardWareKey. DYN_DATA lives only
  in the wineserver (gone ~1-2 s after the last process exits), so `run()` starts
  `wineserver -p30` and imports it with `regedit` (reg.exe doesn't know HKDD) right
  before every game. `win32_win9x_hardware` reads it the way NFS3's 3dsetup does.
- `exe` may be omitted when a CD drive has `autorun.inf` (its `open=` is run, with
  args). A missing exe's error lists setup/install/autorun.exe found up to 3 levels
  deep. Both covered by `disc_image`.

- Screen mode: `ScreenModeGuard` saves each connected output's mode from
  `xrandr --query` and resets changed ones after the game. Xvfb can't switch
  modes, so the restore itself is only verified on real desktops.
- Ctrl+C / SIGTERM / SIGHUP during a game: `interrupt_handler` counts them (a
  handler, not SIG_IGN, which Wine would inherit). A game frozen on the loader
  lock ignores Ctrl+C (Wine needs a new thread for it), so the first one makes
  `run()` run `wineserver -k` (not `-w`, which can wait forever) and wait up to
  5 s for the game; threads stuck on a lock may never notice the server is gone,
  so then (or on a second Ctrl+C) `kill_prefix_processes` SIGKILLs every process
  whose environ has this WINEPREFIX. Then the usual cleanup (screen mode,
  FluidSynth) runs. SIGPIPE gets a no-op handler so `frag ... | tee` survives tee
  dying first (couldn't reproduce the owner's failure that way, kept as guard).
  Before this, NFS III froze, xkill was needed and the host stayed at 640x480.
  `interrupt_frozen_game` freezes deadlock.exe on the loader lock (DllMain waits
  for a new thread) and checks frag stops it: plain, through tee, and with a
  wineserver wrapper that ignores -k (fails without the 5 s fallback).
- `[wine] desktop = WxH` (Wine virtual desktop). Registry alone (Explorer\Desktop +
  Desktops\Default) did NOT work in FRAG prefixes: every Wine process creates the
  "Default" desktop at startup if missing (win32u `winstation_init`, flags 0 =
  ordinary, screen-sized), and only explorer creates it virtual
  (DF_WINE_CREATE_DESKTOP). Stock prefixes win the race only because
  winemenubuilder runs at boot; FRAG disables it. So `start_desktop()` runs
  `wine explorer /desktop=Default,WxH` first, `AppDefaults\{start.exe,fragdesk.exe}
  \Explorer\Desktop=FragProbe` keeps the launcher and the probe off "Default",
  and `helpers/fragdesk.c` (OpenDesktop "Default" + SetThreadDesktop + metrics) is
  polled until ready. Explorer is killed after the game. `win32_desktop` test,
  5/5 runs pass (the race failed 2/3 without the start.exe key).
  `run()` restarts the prefix's wineserver first: a frozen earlier run (NFS III)
  left its processes and full-size desktop behind, so the desktop never appeared.

- Video memory: wined3d takes VRAM from the GL driver (WGL_WINE_query_renderer).
  VMware's SVGA3D reports 1 MB, so creating any primary surface failed with
  `err:d3d:resource_init Out of adapter memory.` / DDERR_OUTOFVIDEOMEMORY
  (0x8876017c). FRAG always writes `HKCU\Software\Wine\Direct3D\VideoMemorySize`
  (string, MB) from `[wine] video_memory`, default 128 (also keeps multi-GB cards
  from confusing old games; Wine itself caps at LONG_MAX for pre-Vista versions).
  ddraw's GetAvailableVidMem reports the total minus the screen's framebuffer.
  `win32_video_memory` checks it; with 4 MB it reproduces 0x8876017c exactly.

- Video playback (DirectShow/quartz) decodes through winegstreamer, i.e. the host's
  GStreamer for the game's bitness. 32-bit games need `gstreamer1.0-libav:i386`
  (Indeo, Cinepak...), not installed by default on Ubuntu; `run()` prints a note
  when `/usr/lib/i386-linux-gnu/gstreamer-1.0` (or Arch's `/usr/lib32/...`) lacks
  `libgstlibav.so`. Checked by hand here: note shown without, gone with the package.

- Windows 9x mode costs something: Wine's kernel32 loads krnl386.exe16 (the Win16
  subsystem and its Win16 lock) into every 32-bit program when the version is
  win95/98/me. That enables Wine bug 52511 (imm32 thread cleanup vs Win16 lock vs
  loader lock), a permanent freeze when a thread exits; NFS III hit it. NT versions
  don't load krnl386. Keep win98 for installers/games that check for 9x, but
  consider winxp for games that run on XP.

- TerminateThread (Wine 9.0 and 11.0): Wine kills the thread at once, even inside
  a syscall (`quit_handler` -> `abort_thread`), so a thread killed inside win32u
  holding `user_lock` leaves it taken forever. The next exiting thread then hangs
  in imm32's THREAD_DETACH (`NtUserDestroyInputContext` -> `get_user_handle_ptr`)
  holding the loader lock, and everything freezes. Windows defers the kill until
  the thread returns to user mode. Fix: `run()` starts games as
  `C:\\windows\\fragstart.exe <exe> <args>` (`helpers/fragstart.c`), which creates
  the game suspended and queues an APC `LoadLibraryA("fragfix.dll")` (Wine's
  start-up ends in alertable NtContinue, so it runs after DLL init, before the
  entry point; builtin kernel32 has the same address in every process).
  `helpers/fragfix.c` patches kernelbase/kernel32 TerminateThread with a jmp to a
  version that: (1) waits (up to 3 s, polling without suspending: suspending
  restarts the thread's Sleep, so per-ms suspends stalled it; handling sent
  messages) while the thread owns the loader lock (PEB+0xa0; owner == tid, or no
  owner but LockCount != -1) - games kill threads that are starting or exiting,
  i.e. running DLL notifications, which takes long in Wine; an exiting one usually
  just finishes; (2) kills a thread suspended in a wait syscall at once (EIP within
  32 bytes of NtWaitFor*/NtDelayExecution/NtUserMsgWait.../NtUserGetMessage...
  stubs), like Windows; moving it would let it die after being granted a critical
  section; (3) otherwise sets EIP to a stub that sets a flag (EBX) and spins in
  user mode, resumes it fully, and kills it from outside once there (100 ms max,
  then where it is). Wine applies a context set during a syscall only when the
  syscall returns. The stub first ended the thread itself (NtTerminateThread(-2)):
  after that, Wine 9's alloc_tls_slot looped forever in NtGetNextThread over two
  such threads when the game loaded a DLL with TLS. 32-bit PE only;
  `[wine] safe_terminate = no` turns it off; `FRAGFIX_LOG=1` (set by `--verbose`)
  logs calls. `win32_thread_kill`: without the fix the user lock leaks after 3-23
  kills; with it 100 kills, waiting, suspended and self-killing threads with the
  right exit codes, and threads killed while starting/exiting in a DLL whose
  notifications sleep 300 ms (`thread_kill_dll.c`) leave the loader lock free.
  NFS III (first version, stub ending the thread itself): log showed the game does
  call TerminateThread, and the loader lock was then "blocked by" exactly the
  thread just killed ("ended safely"), i.e. killed during its DLL notifications.
- After the game: `run()` waits while `prefix_programs()` finds Windows programs
  in the prefix (environ WINEPREFIX, argv[0] base name) other than Wine's services
  (services, winedevice, plugplay, explorer, rpcss, svchost, conhost, start, ...),
  e.g. an installer engine whose bootstrap exited; then `wineserver -k`. Without
  that, the services (and `wineserver -p30`) held the game's stdout for ~30 s, so
  `frag run ... | grep` kept the terminal (owner had to press Ctrl+C after NFS III).
  `game_leaves_program`: spawn.exe exits leaving a child for 3 s; frag must wait
  for it and return through a pipe within 20 s (37 s before, 5 s now).
- Wine version changes: `update_for_wine_version()` stores `wine --version` in
  `<prefix>/.frag-wine-version` and runs `wineboot -u` when it changes (not for a
  new prefix). A Wine 9.0 prefix copied for WineHQ Wine 11 wasn't auto-updated and
  aborted: "module not found for forward 'cryptbase.SystemFunction036'".
  `wine_version_update` uses a wrapper reporting another version.
- Crashes: FRAG sets `HKCU\Software\Wine\WineDbg\ShowCrashDialog=0`, so winedbg
  prints the register dump, backtrace and modules to the terminal instead of a
  dialog (NFS III's vanished before it could be read). Checked by hand with a
  thread that reads 0x190.

## Game notes

Real games tested on the owner's machine (Ubuntu, Wine 9.0). Feed these into the
compatibility database later.

- **LEGO Stunt Rally (2000)** — installer blocked; parked. Matches Wine bug 50218
  ("installer requests Indeo codec", unconfirmed, still present in Wine 7).
  - Needs `type = cdrom` (otherwise "insert the CD").
  - InstallShield 5 with a 16-bit bootstrap (`_isetup.exe`, `_SETUP.DLL`) -> 32-bit
    engine (`_INS*._MP`). Win16 support works; the engine raises repeated access
    violations in its window procs that Wine ignores.
  - Silently runs `D:\DxMedia\DXMEDIA.EXE` + `DXM6PTCH.EXE` (DirectX Media 6), which
    installs native danim, qcut, qdv, qdvd, dxtmsft(3), mpg2splt.ax, tm20dec.ax, LMRT
    over Wine's DLLs. Redistributable installers like this should probably be
    skipped or sandboxed by FRAG.
  - Indeo file group: loads native `Ivfsrc.ax` (Indeo source filter), unloads it at
    once, then fails with -119 (self-registration). No Wine err line; cause unknown.
    Wine's builtin ir50_32 lacks DllRegisterServer (fixed by native,builtin default),
    but this second failure remains.
  - Known later problems per WineHQ: black 3D scene (bug 25486), crash entering
    championship with winegstreamer (bug 48103).
- **Star Trek: Deep Space Nine: Dominion Wars (2001, US release)** — BLOCKED by
  copy protection; parked. Installs fine, then "no CD drive found". A relay trace
  shows ds9dw.exe reading itself and writing `SIntf16.dll`, `SIntf32.dll`,
  `SIntfNT.dll` to system32: the file set of newer SecuROM (old SecuROM used
  CMS16/CMS_95/CMS_NT). SecuROM checks physical disc properties, so a folder copy
  can never pass, and it generally fails under Wine even with the disc. Only an
  official unprotected exe (publisher patch / re-release) would help. Config used
  `type = cdrom`, label `Dominion_Wars` (from the disc).
- **Classic Board Games (Hasbro)** — WORKS, with sound and MIDI music. Installs exe to
  `C:\\Program Files (x86)\\Hasbro Interactive\\Classic Games\\ClassicBoard.exe`; uses
  Smacker video (smackw32), DirectDraw, WinG, DirectSound, MIDI via the winmm mapper.
  InstallShield with 16-bit bootstrap (`Win16Mutex` timeout + ignored exceptions, harmless).
  - Needs Z: hidden: with Z: visible it crashed (path buffer overflow while scanning drives).
  - "Could not initialize sound: (1)" and no music: the game enumerates ACM drivers and
    wants one named "Microsoft ADPCM"; Wine's msadp32 says "MS-ADPCM". Fixed by FRAG's
    renamed `msadp32.acm`; confirmed on the owner's machine (error gone, music plays).
    Found with a relay limited by `RelayInclude` (`winmm.*;dsound.*;msacm32.*;...`;
    a bare `winmm` is read as a function name) plus disassembly of the callback.
  - Music needs a MIDI synth; FRAG starts FluidSynth itself (confirmed working).
    Their prefix has `CurrentInstrument = FluidSynth`, but Wine names the port
    `FluidSynth - FluidSynth`; midimap falls back to device 0, harmless here.
  - Runs from an .iso of its disc (`[drive D] image = ...`, label/serial automatic).
  - Open: intro video glitches for a few seconds.

- **Need for Speed III: Hot Pursuit (1998)** — WORKS (WineHQ Wine 11.0, fresh
  prefix, win98, from an .iso; installer `D:\setup\english\setup.exe`). Played for
  a while with no noticeable issues; terminal returns by itself after quitting.
  Also confirmed on Ubuntu's Wine 9.0 with fragfix (no freeze). History below.
  - Installer (`setup.exe` in a subfolder, lower case) showed "Could not get
    'HardWareKey' value" from `3dsetup` (Wine bug 7115: Win9x mode, empty
    HKEY_DYN_DATA). FIXED by FRAG's Win9x hardware entries; confirmed (installs).
  - Game then says "The Microsoft DirectX 5 drivers must be installed for Need For
    Speed III to run!". Trace: reads its "D3D Device" setting, DirectDrawCreate
    succeeds, then wined3d logs "Out of adapter memory" twice, then the box. Same
    cause as LEGO Racers: VMware's GL driver reports 1 MB VRAM. FIXED by FRAG's
    VideoMemorySize default; confirmed (game loads, menus work).
  - Open: deadlock on the loader lock. Repeating "err:sync:RtlpWaitForCriticalSection
    section ... loader_section wait timed out in thread 01a8, blocked by 01a4".
    Seen while loading a race and when returning to the menu; also with
    `taskset -c 0`. Attaching winedbg (`bt all`) hangs too (needs the loader lock).
    Relay (`+relay,+loaddll`, RelayInclude `dsound.*`): eacsnd.dll's DirectSoundCreate
    calls all return (not from DllMain; eacsnd has no DllMain). 01a4 is a game thread
    (proc 004DF730) exiting: THREAD_DETACH runs through softtria, ws2_32, opengl32,
    ole32, combase, rpcrt4, then enters imm32 and never returns; 01a8 is a new thread
    stuck before its first THREAD_ATTACH. The game loads krnl386.exe16, system.drv16,
    comm.drv16 (16-bit thunks). Matches Wine bug 52511 / MR 1456 "imm32: Prevent
    deadlock on unload": imm32's thread cleanup vs krnl386's Win16 lock, taken in
    opposite order to the loader lock. imm32's DllMain THREAD_DETACH is unchanged in
    current master (`IMM_FreeThreadData`), so not fixed upstream.
    `[dlls] imm32 = d` does NOT work: comctl32 imports imm32, DINPUT imports
    comctl32, nfs3.exe imports DINPUT (c0000135). No game file imports any 16-bit
    function: Wine's kernel32 `process_attach` itself does
    `LoadLibraryA("krnl386.exe16")` for every non-Wine 32-bit exe when the version
    is Win9x (`GetVersion() & 0x80000000`). So the deadlock comes from FRAG's
    win98 default. `[wine] version = winxp` is worse: page fault reading 00000190 at
    004A3AEC (game code) at startup, Wine's crash dialog flashes, then the loader
    lock deadlock again (likely winedbg attaching). So NFS III wants 9x mode.
    win98 + `[dlls] krnl386.exe16 = d` (kernel32 then doesn't load the Win16
    subsystem; hello32 still reports Windows 98 and passes) did NOT help: same
    startup crash, still froze returning to the menu. So the crash is in 9x mode
    too. Hypothesis: XP run faulted reading 00000190 and the lock was held by
    thread 0190; Win9x thread IDs were pointers, Wine's are small numbers, so the
    game may dereference a thread ID; the crashed thread then holds the loader
    lock while winedbg tries to attach. With the crash dialog off, the startup
    crash is intermittent (one run none, one run X `BadWindow` on X_CreateWindow
    after `xrandr_get_screen_resources` failed). Full screen (no `desktop`)
    freezes the same way returning to the menu, with only the loader-lock message,
    so the virtual desktop isn't the cause. Since krnl386 off didn't help either,
    the Win16 lock (bug 52511) is probably not the other side; more likely imm32's
    THREAD_DETACH waits for another thread (e.g. a cross-thread window message)
    that itself waits for the loader lock. Last relay line per thread: 0168 ran
    DINPUT's WH_MOUSE_LL hook; 014c handled window messages to the end.
    `+imm,+win,+hook,+dinput` trace (freeze loading a race): the stuck exiting
    thread's last lines are imm32 DllMain reason 2, reason 3, then
    `NtUserDestroyInputContext (nil)`, which never returns: win32u's
    free_user_handle takes the user lock even for a null handle. So the other
    side holds win32u's user lock and waits for the loader lock. At that moment
    the window thread was calling DINPUT's WH_CBT hook (code 5, HCBT_ACTIVATE)
    and the DINPUT input thread its WH_MOUSE_LL hook. In Wine 9.0 dinput's
    cbt_hook_proc does SendMessageW(di_em_win, NOTIFY_FOREGROUND_LOST) to the
    input thread; current master handles foreground loss locally instead
    (`handle_foreground_lost`). Next: try a newer Wine (WineHQ wine-stable 11.0 in
    /opt/wine-stable, `[wine] binary = /opt/wine-stable/bin/wine`, on a copy of the
    prefix). Quitting from the menu also froze once.
    Wine 11.0: a copied 9.0 prefix failed `wineboot -u` (cryptbase forward), so a
    fresh prefix with the game reinstalled; still froze before a race, same
    message. Wine 11's dinput still SendMessages from its CBT hook, and imm32's
    THREAD_DETACH still takes win32u's user lock. New theory: a TerminateThread'd
    thread died holding the user lock (reproduced: leaks after a few kills).
    Relay traces of kernel32/kernelbase thread functions printed nothing
    (cause unknown). fragfix's log confirmed NFS III calls TerminateThread, and
    that it kills threads while they hold the loader lock (start/exit); see
    "TerminateThread" above. With the loader-lock-aware version: no more
    freezes (starts, races, menu, quit); log shows "ended in a wait", "ended by
    itself while waiting for the loader lock", "thread busy in Wine".
    (Vogons: 3dsetup offers only software rendering; game loads softtria.dll.)
- **LEGO Racers (1999)** — WORKS windowed (`[wine] desktop = WxH`); full screen
  BROKEN on both Ubuntu's Wine 9.0 and WineHQ Wine 11.0 (fresh prefix,
  reinstalled; FRAG waited for InstallShield's `_ins5576._mp`/`_isdel.exe`). Full
  screen, the game switches DirectDraw between exclusive 640x480 and normal around
  each intro video (quartz video windows are unowned top-level popups and are
  released properly). Afterwards the "LEGO Racers" X window stayed 1920x1080 and an
  unnamed classless 640x480 X window (likely winex11's GL client window, detached)
  sat at top level, so frames never reached the screen (stale video frame or the
  desktop showed). Not yet known whether `-novideo` full screen works. (A commit
  on Oct 17 wrongly recorded full screen as working on Wine 11 before it was tested.)
  Earlier history: Installer ran, but `frag run` didn't return
  afterwards (terminal had to be closed). Game: "Unable to set message drain"
  (DirectShow `IVideoWindow::put_MessageDrain`, i.e. the intro video's graph
  failed; Indeo video). Known workaround on Windows too: `args = -novideo`.
  With it the game starts but shows only a black screen and had to be killed,
  leaving the host at the game's resolution (now handled by ScreenModeGuard).
  In a 800x600 FRAG desktop it then said "Unable to create primary drawing surface
  error 8876017c" (DDERR_OUTOFVIDEOMEMORY) with "Out of adapter memory": the 1 MB
  VRAM report. FIXED by FRAG's VideoMemorySize default; confirmed (game loads).
  - Intro videos: Indeo 5 AVIs (`introcmp.avi` etc., 640x480, 15 fps, PCM audio).
    "Unable to set message drain" was missing 32-bit GStreamer libav; with
    `gstreamer1.0-libav:i386` (+ plugins-bad/ugly :i386) they play. Remaining
    console noise: GStreamer-Video-CRITICAL `gst_caps_is_fixed`, quartz
    DSoundRender/BaseMemAllocator/send_buffer errs (probably on skipping a video).
- Owner's machine: Ubuntu 24.04 (X11 session) in VMware Workstation 15.5.7 on
  Windows 10, RTX 2080. Guest GL: renderer "SVGA3D; build: RELEASE; LLVM;", OpenGL
  3.3 core, Mesa 25.2.8; vmwgfx reports 3D, dx, SM4_1. That is VMware's host-GPU
  path (software would be "llvmpipe" with GL 4.5); glxinfo's "Accelerated: no" and
  "Video memory: 1MB" are how Mesa's svga driver reports itself, not a fault. GL 3.3
  is enough for wined3d's Direct3D 1-9. (Workstation 17 would give GL 4.3.)

## Roadmap

1. ~~Skeleton: launcher, per-game prefix, drive mounting, Win16 + Win32 smoke tests.~~
2. Disc images: ~~.iso/.cue/.bin with label and serial~~ (unpacked, own reader).
   Still: CD audio tracks, other formats (.mdf/.nrg/.ccd), maybe FUSE instead of unpacking.
3. Graphics: bundle and configure cnc-ddraw, DXVK (D3D8/9), a Glide wrapper; per-game choice.
4. Game-focused tests: DirectDraw 8-bit palette mode, DirectSound, joystick, timing.
5. Compatibility database: known-good settings per game, shipped with FRAG.
6. Win9x-specific fixes in a Wine fork, driven by real games that break.
7. Handle bundled redistributable installers (DirectX, DirectX Media, Indeo) that
   overwrite Wine DLLs: detect and skip, or install into a throwaway layer.
8. MIDI: ~~start FluidSynth automatically~~ (needs the host's fluidsynth package).
   Later: bundle it, or MT-32 (Munt) for games that want one.
