// SPDX-License-Identifier: GPL-3.0-or-later
// Puts the host's screen mode back after a game that changed it.
#pragma once

#include <map>
#include <string>

namespace frag {

// Notes each X output's current mode (via xrandr) when created and restores any
// that differ when destroyed. Fullscreen games switch the real screen to their
// own resolution; one that crashes or is killed leaves it there.
class ScreenModeGuard {
public:
    explicit ScreenModeGuard(bool verbose);
    ~ScreenModeGuard();
    ScreenModeGuard(const ScreenModeGuard&) = delete;
    ScreenModeGuard& operator=(const ScreenModeGuard&) = delete;

    // Output name -> current mode, parsed from `xrandr --query`.
    static std::map<std::string, std::string> parse_modes(const std::string& xrandr_output);

private:
    std::map<std::string, std::string> saved_;
    bool verbose_;
};

}  // namespace frag
