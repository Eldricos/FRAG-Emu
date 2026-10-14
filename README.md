# FRAG-Emu

**FRAG — Faithfully Runs Ancient Games.**

FRAG runs games made for Windows 3.1, 95 and 98 on modern systems, with the
convenience of DOSBox: point it at a folder or CD, give it a small config file,
and play. No Windows install or license is needed.

FRAG is built on [Wine](https://www.winehq.org/). FRAG's own job is everything
around it: one self-contained prefix per game, mounted game folders and CDs,
the right Windows version for each game, and (planned) graphics wrappers and
per-game compatibility fixes.

> **Status:** early prototype. The launcher works with test programs on Linux x86;
> real games have not been tested yet.

## Requirements

- Linux on x86-64 (other hosts may come later)
- Wine 9.0 or newer, including 32-bit support (on Debian/Ubuntu: `wine`, `wine32:i386`, `wine64`)
- To build: CMake 3.16+, a C++17 compiler
- Optional, for the test programs:
  - `i686-w64-mingw32-gcc` (Win32 tests; `gcc-mingw-w64-i686` on Debian/Ubuntu)
  - [Open Watcom v2](https://github.com/open-watcom/open-watcom-v2) (Win16 tests), with `WATCOM` set to its install directory
  - `xvfb-run` when running tests without a display

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Usage

Create a `frag.conf` next to your game (or anywhere) and run:

```sh
frag run path/to/frag.conf        # or the directory containing frag.conf
frag run frag.conf --verbose      # show the commands FRAG runs
frag run frag.conf --wine-debug +relay,+seh   # Wine debug channels for bug reports
frag prefix frag.conf             # print where this game's Wine prefix lives
```

Prefixes live in `$FRAG_HOME`, else `$XDG_DATA_HOME/frag`, else `~/.local/share/frag`.

### frag.conf

```ini
[game]
name = Example Game            # required; also names the game's prefix
exe  = D:\SETUP.EXE            # required; a Windows path on a mounted drive, or a host path
args = -windowed               # optional

[wine]
version = win98                # win31, win95, win98 or winme (default win98)
binary  = /opt/wine/bin/wine   # optional; also settable with $FRAG_WINE

[drive D]                      # any letter except C (C: is the prefix's own drive)
path  = cd                     # relative to this file
type  = cdrom                  # hd (default) or cdrom
label = EXAMPLE

[dlls]                         # optional Wine DLL overrides
ddraw = native,builtin
```

Windows paths are matched case-insensitively, like on Windows, so
`D:\SETUP.EXE` finds `setup.exe`. The game starts in its exe's directory.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). Code taken from Wine keeps its
original LGPL-2.1-or-later notices.
