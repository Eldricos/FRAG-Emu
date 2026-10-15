// SPDX-License-Identifier: GPL-3.0-or-later
// Per-game configuration (frag.conf) loading.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace frag {

namespace fs = std::filesystem;

// One mounted Windows drive, e.g. D: pointing at a folder with the CD contents.
struct Drive {
    char letter = 0;     // upper-case 'D'
    fs::path path;       // absolute host path
    std::string type;    // "hd" or "cdrom"
    std::string label;   // optional volume label
};

struct GameConfig {
    fs::path file;                     // the frag.conf this was loaded from
    std::string name;                  // [game] name, also names the prefix
    std::string exe;                   // [game] exe, Windows path (D:\SETUP.EXE) or host path
    std::vector<std::string> args;     // [game] args, split on whitespace
    std::string windows_version = "win98";  // [wine] version: win31, win95, win98, winme, ...
    std::string wine_binary;           // [wine] binary, empty = "wine" from PATH or $FRAG_WINE
    bool host_drive = false;           // [wine] host_drive: show the host filesystem as Z:
    std::vector<Drive> drives;         // [drive X] sections
    std::map<std::string, std::string> dll_overrides;  // [dlls] name = native,builtin
    std::string midi_synth = "auto";   // [sound] midi_synth: auto (start FluidSynth if no synth runs) or off
    fs::path soundfont;                // [sound] soundfont, empty = first General MIDI SoundFont found
};

// Loads a frag.conf file. `path` may be the file or a directory containing frag.conf.
// Throws std::runtime_error with a line-numbered message on errors.
GameConfig load_config(const fs::path& path);

}  // namespace frag
