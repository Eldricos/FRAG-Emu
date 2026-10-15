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

- **Need for Speed III: Hot Pursuit (1998)** — in progress. Installer
  (`setup.exe` in a subfolder, lower case) showed "Could not get 'HardWareKey' value"
  from `3dsetup` (Wine bug 7115: Win9x mode, empty HKEY_DYN_DATA). FRAG now fills
  in a display device; awaiting retest on the owner's machine.

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
