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
- `src/main.cpp` — CLI: `frag run`, `frag prefix`, `--version`.
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
- Default `WINEDEBUG` for games is `fixme-all`, so `err:` lines (e.g. which DLL is
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
- **Star Trek: Deep Space Nine: Dominion Wars (2001)** — installs; launch not yet tested.
  Only harmless installer noise (`CoReleaseMarshalData`, menubuilder icons).
- **Classic Board Games (Hasbro)** — installs; launch not yet tested. InstallShield
  with 16-bit bootstrap again (`Win16Mutex` timeout + ignored exceptions, harmless).

## Roadmap

1. ~~Skeleton: launcher, per-game prefix, drive mounting, Win16 + Win32 smoke tests.~~
2. Disc images: mount ISO/CUE (likely via fuseiso or our own ISO9660 reader), CD audio.
3. Graphics: bundle and configure cnc-ddraw, DXVK (D3D8/9), a Glide wrapper; per-game choice.
4. Game-focused tests: DirectDraw 8-bit palette mode, DirectSound, joystick, timing.
5. Compatibility database: known-good settings per game, shipped with FRAG.
6. Win9x-specific fixes in a Wine fork, driven by real games that break.
7. Handle bundled redistributable installers (DirectX, DirectX Media, Indeo) that
   overwrite Wine DLLs: detect and skip, or install into a throwaway layer.
