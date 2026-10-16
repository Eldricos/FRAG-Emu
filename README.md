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
- To build: CMake 3.16+, a C++17 compiler, and Ninja
  (on Debian/Ubuntu: `sudo apt install build-essential cmake ninja-build`)
- For games' videos: 32-bit GStreamer decoders (on Debian/Ubuntu:
  `sudo apt install gstreamer1.0-libav:i386 gstreamer1.0-plugins-good:i386`);
  FRAG warns when they're missing
- For MIDI music: FluidSynth and a General MIDI SoundFont
  (on Debian/Ubuntu: `sudo apt install fluidsynth fluid-soundfont-gm`)
- Recommended: `i686-w64-mingw32-gcc` (`gcc-mingw-w64-i686` on Debian/Ubuntu) to
  build the Windows DLLs FRAG adds to games, such as a fixed MS ADPCM sound codec
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

Without Ninja, leave out `-G Ninja` and CMake uses Make instead. If a configure
fails, delete the `build` folder before retrying: CMake caches the failed
settings, and the same error repeats until the folder is removed.

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

A minimal config for a game copied into a folder called `game` next to `frag.conf`:

```ini
[game]
name = Example Game            # also names the game's prefix
exe  = D:\GAME.EXE             # the game's exe, on the drive below

[drive D]
path = game                    # folder with the game files, relative to this file
```

Or straight from a CD image, which needs no folder and no label setting:

```ini
[game]
name = Example Game
exe  = D:\SETUP.EXE

[drive D]
image = Example Game.cue       # .iso, .cue (with its .bin) or a raw .bin/.img
```

Leave out `exe` to start what the CD's `autorun.inf` names, as Windows does when
the disc is inserted (usually the game's install menu). If an `exe` can't be
found, FRAG lists the installers it sees on the game's drives.

Only `name` and `exe` are required (`exe` not with a CD that has `autorun.inf`). All other settings are optional; uncomment
them only when you need them:

```ini
[game]
# args = -nointro              # extra command-line arguments for the game

[wine]
# version = win95              # win31, win95, win98 or winme (default: win98)
# binary  = /path/to/wine      # a specific Wine build (default: wine on your PATH,
                               # or $FRAG_WINE if set)
# host_drive = yes             # show your whole Linux filesystem as Z: (default: no)
# video_memory = 128           # MB of video memory games are told about (default 128;
                               # Wine would ask the graphics driver, and some VMs say 1)
# desktop = 800x600            # run the game in a window of this size instead of
                               # full screen; it can't change your screen's mode
# safe_terminate = no          # turn off FRAG's TerminateThread fix (default: on;
                               # see "Frozen games" below)

[drive D]
# type  = cdrom                # hd (default) or cdrom; use cdrom for CD contents
                               # (images default to cdrom)
# label = GAMECD               # the CD's volume label, for games that check it
                               # (stored as .windows-label in the drive's folder;
                               # images default to their own label)
# serial = 1A2B3C4D            # the volume serial number (images: their own)

# [sound]
# midi_synth = auto            # auto (default): start FluidSynth if no MIDI synth runs; off
# soundfont  = /path/to/GM.sf2 # default: the first General MIDI SoundFont found

# [dlls]                       # Wine DLL overrides, e.g. to use a wrapper DLL
# ddraw = native,builtin       # placed next to the game's exe
```

Most games from this era refuse to install or start unless their files are on a
CD-ROM drive, often with the original disc's volume label. For a folder copied
from a game CD, set `type = cdrom` and `label` to the disc's label. On Linux,
`lsblk -o NAME,LABEL` shows the label while the disc is inserted.

FRAG prefers a game's own 1990s video codecs (Indeo, Cinepak, Microsoft Video 1)
over Wine's built-in ones, falling back to Wine's when the game has none. Set
e.g. `ir50_32 = builtin` under `[dlls]` to change that for one game.

Games stay self-contained, like in DOSBox: a game sees only C: (its own Wine
prefix) and the drives in its `frag.conf`. Wine's usual Z: drive, which shows your
whole Linux filesystem, is hidden because old games that scan every drive letter
for their CD can crash on it. Your computer's real CD/DVD and USB drives are
hidden too, and shortcuts made by installers are not added to your desktop's
application menu. Start games with `frag run`.

Windows paths are matched case-insensitively, like on Windows, so
`D:\SETUP.EXE` finds `setup.exe`. The game starts in its exe's directory.

## Screen modes

Full-screen games of this era switch your screen to their own resolution, often
640x480. When a game exits, even if it crashed, FRAG puts the screen back the way
it was (using `xrandr`). If a game freezes, press Ctrl+C in the terminal: FRAG
stops it (all of its Wine processes) and restores the screen. Press Ctrl+C a
second time to force it. To keep a game from
touching your screen at all, set `desktop = 800x600` (or another size) under
`[wine]`: the game then runs inside a window of that size, like in DOSBox.

## Frozen games

Games of the Windows 9x era often end their own threads with `TerminateThread`.
Windows lets such a thread finish what it is doing in the kernel first; Wine
kills it on the spot, and if it was inside Wine's window code at that moment, a
lock stays taken forever. The game then freezes, typically while loading or
leaving a level, and `frag` prints
`RtlpWaitForCriticalSection ... "loader_section" wait timed out`.
Games also end threads that are just starting or finishing, while Wine runs
every DLL's start-up or clean-up code for them under a lock; killed there, the
thread leaves that lock taken. FRAG starts games through its own
`fragstart.exe`, which loads `fragfix.dll` into them: their `TerminateThread`
waits until the thread is out of Wine's code and locks before ending it. `frag run --verbose` shows each call. Set
`[wine] safe_terminate = no` to start the game directly instead.

## CD images

A drive can be a CD image instead of a folder: `.iso`, a `.cue` sheet with its
`.bin` file(s), or a raw `.bin`/`.img`. The drive is then a CD-ROM with the disc's
own volume label and the serial number Windows would report for it, so games
that check for their CD find it. Long file names (Joliet) work as on Windows.

On the first run FRAG unpacks the image's files into `discs/` in FRAG's data
folder; later runs reuse them, and a changed image is unpacked again. This needs
as much free space as the image. Delete a folder there to reclaim the space.

CD audio tracks are not played yet; games that use them for music are silent.

## MIDI music

Many games of this era play their music as MIDI, which needs a synthesizer. If none
is running, `frag run` starts FluidSynth for the game and stops it when the game
exits. It plays through PulseAudio (or PipeWire), using the first General MIDI
SoundFont it finds, such as `/usr/share/sounds/sf2/FluidR3_GM.sf2`. FluidSynth's
messages go to `fluidsynth.log` in the game's prefix (`frag prefix <frag.conf>`
prints where that is). A synth you started yourself is used instead.

## Diagnosing sound

`build/tests/soundprobe.conf` runs a small test program (built when the MinGW
cross-compiler is installed) that lists the wave and MIDI devices Wine sees,
shows where MIDI music is routed, and plays a short tune and a beep:

```sh
frag run build/tests/soundprobe.conf
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). Code taken from Wine keeps its
original LGPL-2.1-or-later notices.
