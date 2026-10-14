// SPDX-License-Identifier: GPL-3.0-or-later
// Wine prefix management and game launching.
#pragma once

#include "config.h"

#include <string>
#include <vector>

namespace frag {

struct RunOptions {
    bool verbose = false;
    std::string wine_debug = "-all";    // value for WINEDEBUG
    std::vector<std::string> extra_args;  // appended after the config's args
};

class WinePrefix {
public:
    explicit WinePrefix(const GameConfig& cfg, bool verbose);

    const fs::path& path() const { return path_; }

    // Creates the prefix if needed and applies drives, Windows version and drive types.
    // Safe to call on every launch.
    void prepare();

    // Resolves the config's exe to a host path, launches it and returns its exit code.
    int run(const RunOptions& opts);

    // Resolves a Windows path such as D:\INSTALL\SETUP.EXE to a host path,
    // matching each component case-insensitively like Windows does.
    fs::path to_host_path(const std::string& windows_path) const;

private:
    const GameConfig& cfg_;
    bool verbose_;
    std::string wine_;
    fs::path path_;

    int wine_cmd(const std::vector<std::string>& args, const std::string& debug = "-all") const;
    void sync_drives();
    void write_registry();
};

// Directory holding all FRAG data: $FRAG_HOME, else $XDG_DATA_HOME/frag, else ~/.local/share/frag.
fs::path data_dir();

}  // namespace frag
