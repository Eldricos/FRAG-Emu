// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace frag {

namespace {

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

[[noreturn]] void fail(const fs::path& file, int line, const std::string& msg) {
    throw std::runtime_error(file.string() + ":" + std::to_string(line) + ": " + msg);
}

}  // namespace

GameConfig load_config(const fs::path& path) {
    fs::path file = fs::is_directory(path) ? path / "frag.conf" : path;
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot open config: " + file.string());

    GameConfig cfg;
    cfg.file = fs::absolute(file);
    const fs::path base = cfg.file.parent_path();

    std::string section;
    Drive* drive = nullptr;
    int lineno = 0;
    for (std::string raw; std::getline(in, raw);) {
        ++lineno;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        // Inline comments start with whitespace followed by '#' or ';'.
        for (size_t i = 1; i < line.size(); ++i)
            if ((line[i] == '#' || line[i] == ';') && (line[i - 1] == ' ' || line[i - 1] == '\t')) {
                line = trim(line.substr(0, i));
                break;
            }

        if (line.front() == '[') {
            if (line.back() != ']') fail(file, lineno, "unterminated section header");
            section = lower(trim(line.substr(1, line.size() - 2)));
            drive = nullptr;
            if (section.rfind("drive", 0) == 0) {
                std::string letter = trim(section.substr(5));
                if (letter.size() != 1 || letter[0] < 'a' || letter[0] > 'y' || letter[0] == 'c')
                    fail(file, lineno, "drive section must be [drive X] with X in A-Y, not C");
                cfg.drives.push_back(Drive{static_cast<char>(std::toupper(letter[0])), {}, "hd", {}});
                drive = &cfg.drives.back();
                section = "drive";
            } else if (section != "game" && section != "wine" && section != "dlls") {
                fail(file, lineno, "unknown section [" + section + "]");
            }
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) fail(file, lineno, "expected key = value");
        std::string key = lower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));

        if (section == "game") {
            if (key == "name") cfg.name = value;
            else if (key == "exe") cfg.exe = value;
            else if (key == "args") cfg.args = split_ws(value);
            else fail(file, lineno, "unknown key '" + key + "' in [game]");
        } else if (section == "wine") {
            if (key == "version") cfg.windows_version = lower(value);
            else if (key == "binary") cfg.wine_binary = value;
            else if (key == "host_drive") {
                std::string v = lower(value);
                if (v == "yes" || v == "true" || v == "1") cfg.host_drive = true;
                else if (v == "no" || v == "false" || v == "0") cfg.host_drive = false;
                else fail(file, lineno, "host_drive must be yes or no");
            }
            else fail(file, lineno, "unknown key '" + key + "' in [wine]");
        } else if (section == "dlls") {
            cfg.dll_overrides[lower(key)] = lower(value);
        } else if (section == "drive") {
            if (key == "path") drive->path = fs::weakly_canonical(base / value);
            else if (key == "type") {
                drive->type = lower(value);
                if (drive->type != "hd" && drive->type != "cdrom")
                    fail(file, lineno, "drive type must be hd or cdrom");
            } else if (key == "label") drive->label = value;
            else fail(file, lineno, "unknown key '" + key + "' in [drive]");
        } else {
            fail(file, lineno, "key outside of a section");
        }
    }

    if (cfg.name.empty()) throw std::runtime_error(file.string() + ": [game] name is required");
    if (cfg.exe.empty()) throw std::runtime_error(file.string() + ": [game] exe is required");
    for (const auto& d : cfg.drives) {
        if (d.path.empty())
            throw std::runtime_error(file.string() + ": drive " + d.letter + ": has no path");
        if (!fs::is_directory(d.path))
            throw std::runtime_error(file.string() + ": drive " + d.letter +
                                     ": path is not a directory: " + d.path.string());
    }
    return cfg;
}

}  // namespace frag
