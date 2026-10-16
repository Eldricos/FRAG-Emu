// SPDX-License-Identifier: GPL-3.0-or-later
// Wine prefix management and game launching.
#pragma once

#include "config.h"

#include <string>
#include <sys/types.h>
#include <vector>

namespace frag {

struct RunOptions {
    bool verbose = false;
    std::string wine_debug = "fixme-all";  // WINEDEBUG: keep err/warn messages, hide fixme noise
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

    // The reverse: the Windows path of a host file on C: or a configured drive,
    // or an empty string if it is on none of them.
    std::string to_windows_path(const fs::path& host_path) const;

private:
    const GameConfig& cfg_;
    bool verbose_;
    std::string wine_;
    std::string wine_source_;  // where wine_ came from, for error messages
    fs::path path_;

    void check_wine() const;
    int wine_cmd(const std::vector<std::string>& args, const std::string& debug = "-all") const;
    void sync_drives();
    void install_dlls();
    void write_registry();
    void restart_wineserver() const;
    void load_win9x_hardware() const;
    pid_t start_desktop() const;
    std::string autorun_exe(std::vector<std::string>& args) const;
    std::string exe_not_found_hint() const;
};

// Signal handler for Ctrl+C, SIGTERM and SIGHUP while a game runs: makes
// WinePrefix::run stop the game's processes (see there).
void interrupt_handler(int);

// Directory holding all FRAG data: $FRAG_HOME, else $XDG_DATA_HOME/frag, else ~/.local/share/frag.
fs::path data_dir();

}  // namespace frag
