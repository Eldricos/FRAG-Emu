// SPDX-License-Identifier: GPL-3.0-or-later
#include "display.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace frag {

namespace {

std::string xrandr_query() {
    if (!std::getenv("DISPLAY")) return {};
    FILE* p = popen("xrandr --query 2>/dev/null", "r");
    if (!p) return {};
    std::string out;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof(buf), p)) > 0;) out.append(buf, n);
    pclose(p);
    return out;
}

void set_mode(const std::string& output, const std::string& mode) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("xrandr", "xrandr", "--output", output.c_str(), "--mode", mode.c_str(), nullptr);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, nullptr, 0);
}

}  // namespace

// Output lines look like "Virtual-1 connected primary 1920x1080+0+0 ...", followed
// by indented mode lines; the current mode's rate is marked with '*'.
std::map<std::string, std::string> ScreenModeGuard::parse_modes(const std::string& text) {
    std::map<std::string, std::string> modes;
    std::istringstream in(text);
    std::string output;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        if (line[0] != ' ') {
            std::istringstream words(line);
            std::string name, state;
            words >> name >> state;
            output = state == "connected" ? name : std::string();
            continue;
        }
        if (output.empty() || line.find('*') == std::string::npos) continue;
        std::istringstream words(line);
        std::string mode;
        words >> mode;
        modes.emplace(output, mode);
    }
    return modes;
}

ScreenModeGuard::ScreenModeGuard(bool verbose) : saved_(parse_modes(xrandr_query())), verbose_(verbose) {}

ScreenModeGuard::~ScreenModeGuard() {
    if (saved_.empty()) return;
    const auto now = parse_modes(xrandr_query());
    for (const auto& [output, mode] : saved_) {
        auto it = now.find(output);
        if (it != now.end() && it->second == mode) continue;
        if (verbose_) std::cerr << "frag: restoring " << output << " to " << mode << '\n';
        set_mode(output, mode);
    }
}

}  // namespace frag
