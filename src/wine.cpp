// SPDX-License-Identifier: GPL-3.0-or-later
#include "wine.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include <sys/wait.h>
#include <unistd.h>

namespace frag {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Prefix directory names: keep letters, digits, '-', '_' and '.', replace the rest.
std::string sanitize(const std::string& name) {
    std::string out;
    for (unsigned char c : name)
        out += (std::isalnum(c) || c == '-' || c == '_' || c == '.') ? static_cast<char>(c) : '_';
    return out;
}

bool is_windows_path(const std::string& p) {
    return p.size() >= 3 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':' &&
           (p[2] == '\\' || p[2] == '/');
}

// Runs argv with extra environment variables (and in `cwd`, if given), waits, and
// returns the exit code (128 + signal number if it was killed by a signal).
int run_process(const std::vector<std::string>& argv,
                const std::vector<std::pair<std::string, std::string>>& env, bool verbose,
                const fs::path& cwd = {}) {
    if (verbose) {
        std::cerr << "frag:";
        for (const auto& [k, v] : env) std::cerr << ' ' << k << '=' << v;
        for (const auto& a : argv) std::cerr << ' ' << a;
        std::cerr << '\n';
    }
    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
    if (pid == 0) {
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
        for (const auto& [k, v] : env) setenv(k.c_str(), v.c_str(), 1);
        std::vector<char*> cargv;
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        std::cerr << "frag: cannot run " << argv[0] << ": " << std::strerror(errno) << '\n';
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error(std::string("waitpid failed: ") + std::strerror(errno));
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

// Wine's mount manager learns about the host's CD/DVD and USB drives over the D-Bus
// system bus and assigns them drive letters, starting at D:. A drive with no disc
// then has its letter's folder link deleted, which removed FRAG's own D: mapping on
// every run after the first. Games only get the drives frag.conf defines, so Wine
// is pointed at a bus address that doesn't exist.
// Disabled for every Wine process FRAG starts:
// - mscoree, mshtml: the Mono/Gecko install prompts; 9x-era games never need them.
// - winemenubuilder: copies installers' shortcuts into the host desktop's
//   application menu. Games stay self-contained and are started through FRAG.
const char* const kBaseOverrides = "mscoree,mshtml=;winemenubuilder.exe=d";

const std::pair<std::string, std::string> kNoHostDrives{"DBUS_SYSTEM_BUS_ADDRESS",
                                                        "unix:path=/nonexistent/frag-no-host-drives"};

// Writes only when the content differs, so unchanged files keep their timestamps.
// Returns whether the file was written.
bool write_if_changed(const fs::path& file, const std::string& content) {
    if (std::ifstream in(file, std::ios::binary); in) {
        std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old == content) return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!(out << content)) throw std::runtime_error("cannot write " + file.string());
    return true;
}

// Reads a whole file; empty if it can't be read.
std::string read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Where FRAG's own Windows DLLs are: build/dlls next to a built frag, or
// share/frag/dlls for an installed one. Empty if neither exists.
fs::path dll_dir() {
    std::error_code ec;
    const fs::path bin = fs::read_symlink("/proc/self/exe", ec).parent_path();
    if (ec) return {};
    for (const fs::path& dir : {bin / "dlls", bin.parent_path() / "share" / "frag" / "dlls"})
        if (fs::is_directory(dir)) return dir;
    return {};
}

// DLLs FRAG puts into every prefix in place of Wine's. Each is loaded through a
// native,builtin override in run().
// - msadp32.acm: Wine's MS ADPCM codec under its Windows name, "Microsoft ADPCM".
//   Classic Board Games looks it up by that name and has no sound without it.
const char* const kFragDlls[] = {"msadp32.acm"};

bool is_win9x(const std::string& version) {
    return version == "win31" || version == "win95" || version == "win98" || version == "winme";
}

// Windows 9x describes its hardware in two places that Wine leaves empty:
// HKLM\Enum (one key per device, pointing at its driver under
// HKLM\System\CurrentControlSet\Services\Class) and HKEY_DYN_DATA\Config Manager\Enum
// (the devices running now, each naming its HKLM\Enum key in HardWareKey).
// Need for Speed III's 3dsetup reads them to find the display card and fails with
// "Could not get 'HardWareKey' value" (Wine bug 7115). FRAG describes one PCI
// Direct3D card of the era, an NVIDIA RIVA TNT.
const char* const kDisplayEnumKey = "PCI\\VEN_10DE&DEV_0020&SUBSYS_00000000&REV_04\\BUS_01&DEV_00&FUNC_00";

std::string win9x_hardware_reg() {
    std::ostringstream o;
    o << "[HKEY_LOCAL_MACHINE\\Enum\\" << kDisplayEnumKey << "]\n"
      << "\"Class\"=\"Display\"\n"
      << "\"Driver\"=\"Display\\\\0000\"\n"
      << "\"DeviceDesc\"=\"NVIDIA RIVA TNT\"\n"
      << "\"Mfg\"=\"NVIDIA\"\n"
      << "\"HardwareID\"=\"PCI\\\\VEN_10DE&DEV_0020&SUBSYS_00000000&REV_04,PCI\\\\VEN_10DE&DEV_0020\"\n"
      << "\"CompatibleIDs\"=\"PCI\\\\CC_030000,PCI\\\\CC_0300\"\n"
      << "\"ConfigFlags\"=hex:00,00,00,00\n\n"
      << "[HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Class\\Display]\n"
      << "@=\"Display adapters\"\n\n"
      << "[HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Class\\Display\\0000]\n"
      << "\"DriverDesc\"=\"NVIDIA RIVA TNT\"\n"
      << "\"MfgName\"=\"NVIDIA\"\n"
      << "\"Ver\"=\"4.0\"\n"
      << "\"DevLoader\"=\"*vdd,*vflatd\"\n\n"
      << "[HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Services\\Class\\Display\\0000\\DEFAULT]\n"
      << "\"drv\"=\"nvdisp.drv\"\n"
      << "\"vdd\"=\"*vdd\"\n"
      << "\"Mode\"=\"16,640,480\"\n"
      << "\"RefreshRate\"=\"0\"\n\n";
    return o.str();
}

// A string as a .reg value: backslashes and quotes escaped.
std::string reg_escape(const std::string& v) {
    std::string out;
    for (char c : v) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

// HKEY_DYN_DATA only lives as long as the prefix's wineserver, like on Windows 9x,
// where it is rebuilt at every boot. So it is loaded before every game run.
std::string win9x_dyn_data_reg() {
    std::ostringstream o;
    o << "REGEDIT4\n\n"
      << "[HKEY_DYN_DATA\\Config Manager\\Enum\\C1A00000]\n"
      << "\"HardWareKey\"=\"" << reg_escape(kDisplayEnumKey) << "\"\n"
      << "\"Problem\"=hex:00,00,00,00\n"
      << "\"Status\"=hex:0b,60,00,00\n\n";
    return o.str();
}

}  // namespace

fs::path data_dir() {
    // Wine rejects relative WINEPREFIX paths, so always return an absolute path.
    if (const char* h = std::getenv("FRAG_HOME"); h && *h) return fs::absolute(h);
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return fs::absolute(fs::path(x) / "frag");
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("HOME is not set; set FRAG_HOME instead");
    return fs::absolute(fs::path(home) / ".local/share/frag");
}

WinePrefix::WinePrefix(const GameConfig& cfg, bool verbose)
    : cfg_(cfg), verbose_(verbose), path_(data_dir() / "prefixes" / sanitize(cfg.name)) {
    if (!cfg.wine_binary.empty()) {
        wine_ = cfg.wine_binary;
        wine_source_ = "[wine] binary in " + cfg.file.string();
    } else if (const char* w = std::getenv("FRAG_WINE"); w && *w) {
        wine_ = w;
        wine_source_ = "the FRAG_WINE environment variable";
    } else {
        wine_ = "wine";
        wine_source_ = "the default (wine on your PATH)";
    }
}

// Fails early with a clear message instead of a bare exec error from the child.
void WinePrefix::check_wine() const {
    auto executable = [](const fs::path& p) { return access(p.c_str(), X_OK) == 0 && !fs::is_directory(p); };
    if (wine_.find('/') != std::string::npos) {
        if (executable(wine_)) return;
    } else if (const char* path = std::getenv("PATH")) {
        std::string dirs = path;
        for (size_t b = 0, e; b <= dirs.size(); b = e + 1) {
            e = dirs.find(':', b);
            if (e == std::string::npos) e = dirs.size();
            if (e > b && executable(fs::path(dirs.substr(b, e - b)) / wine_)) return;
        }
    }
    throw std::runtime_error("Wine not found: '" + wine_ + "' (from " + wine_source_ +
                             "). Install Wine or correct that setting.");
}

int WinePrefix::wine_cmd(const std::vector<std::string>& args, const std::string& debug) const {
    std::vector<std::string> argv{wine_};
    argv.insert(argv.end(), args.begin(), args.end());
    return run_process(argv,
                       {{"WINEPREFIX", path_.string()},
                        {"WINEDEBUG", debug},
                        {"WINEDLLOVERRIDES", kBaseOverrides},
                        kNoHostDrives},
                       verbose_,
                       // With Z: hidden, Wine can't see the caller's directory and warns.
                       // drive_c doesn't exist until wineboot has created the prefix.
                       fs::is_directory(path_ / "drive_c") ? path_ / "drive_c" : path_);
}

void WinePrefix::prepare() {
    check_wine();
    if (!fs::exists(path_ / "system.reg")) {
        if (verbose_) std::cerr << "frag: creating prefix " << path_ << '\n';
        fs::create_directories(path_);
        if (int rc = wine_cmd({"wineboot", "--init"}); rc != 0)
            throw std::runtime_error("wineboot failed with exit code " + std::to_string(rc));
    }
    sync_drives();
    install_dlls();
    write_registry();
}

// Copies FRAG's DLLs into the folder Wine loads 32-bit system DLLs from: syswow64 in
// a 64-bit prefix, system32 in a 32-bit one. Wine's placeholders there are replaced;
// a Wine upgrade may restore them, so this is checked on every run.
void WinePrefix::install_dlls() {
    const fs::path src = dll_dir();
    const fs::path windows = path_ / "drive_c" / "windows";
    const fs::path system = fs::is_directory(windows / "syswow64") ? windows / "syswow64" : windows / "system32";
    std::vector<std::pair<std::string, fs::path>> files;
    for (const char* name : kFragDlls) files.emplace_back(name, system);
    files.emplace_back("fragdesk.exe", windows);  // helper, see start_desktop()
    for (const auto& [name, dest] : files) {
        const std::string data = src.empty() ? std::string() : read_file(src / name);
        if (data.empty()) {
            std::cerr << "frag: warning: " << name << " not found next to frag; build it with the "
                      << "MinGW cross-compiler (i686-w64-mingw32-gcc) installed\n";
            continue;
        }
        if (write_if_changed(dest / name, data) && verbose_)
            std::cerr << "frag: installed " << name << " into " << dest << '\n';
    }
}

// Points dosdevices/<x>: at each configured drive. Letters FRAG created on an earlier
// run but no longer configured are removed. C: is left alone; Z: is shown or hidden
// per [wine] host_drive.
void WinePrefix::sync_drives() {
    const fs::path dosdevices = path_ / "dosdevices";
    const fs::path managed_file = path_ / ".frag-drives";

    std::set<char> previous;
    if (std::ifstream in(managed_file); in)
        for (char c; in >> c;) previous.insert(c);

    std::set<char> current;
    for (const auto& d : cfg_.drives) {
        char l = static_cast<char>(std::tolower(d.letter));
        current.insert(l);
        fs::path link = dosdevices / (std::string(1, l) + ":");
        std::error_code ec;
        fs::remove(link, ec);
        fs::create_directory_symlink(d.path, link);
        // A "d::" device link means Wine gave this letter to a host drive earlier.
        fs::remove(dosdevices / (std::string(1, l) + "::"), ec);
        // Wine reads a folder drive's volume label from .windows-label in its root.
        // Games check it to find their CD.
        if (!d.label.empty()) write_if_changed(d.path / ".windows-label", d.label);
        // ... and its serial number from .windows-serial (hex). Some games check it.
        if (!d.serial.empty()) write_if_changed(d.path / ".windows-serial", d.serial);
    }
    for (char l : previous)
        if (!current.count(l)) {
            std::error_code ec;
            fs::remove(dosdevices / (std::string(1, l) + ":"), ec);
        }

    std::ofstream out(managed_file, std::ios::trunc);
    for (char l : current) out << l << '\n';

    // Wine maps the whole host filesystem to Z:. Games that scan every drive letter
    // for their CD trip over it; Classic Board Games overflowed a path buffer and
    // crashed. Like DOSBox, games only see C: and their configured drives.
    std::error_code ec;
    fs::remove(dosdevices / "z:", ec);
    fs::remove(dosdevices / "z::", ec);
    if (cfg_.host_drive) fs::create_directory_symlink("/", dosdevices / "z:");
}

// Applies the Windows version and drive types with a single registry import.
void WinePrefix::write_registry() {
    std::ostringstream out;
    out << "REGEDIT4\n"
        << "; frag settings v9 (64-bit registry view)\n\n"
        << "[HKEY_CURRENT_USER\\Software\\Wine]\n"
        << "\"Version\"=\"" << cfg_.windows_version << "\"\n\n"
        << "[HKEY_LOCAL_MACHINE\\Software\\Wine\\Drives]\n";
    for (const auto& d : cfg_.drives)
        out << '"' << static_cast<char>(std::tolower(d.letter)) << ":\"=\"" << d.type << "\"\n";
    // winemenubuilder is disabled (see kBaseOverrides); drop the startup entry that
    // would otherwise log an error every time Wine starts.
    out << "\n[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\RunServices]\n"
        << "\"winemenubuilder\"=-\n";
    // Forget host drives Wine assigned to our letters in older prefixes.
    out << "\n[HKEY_LOCAL_MACHINE\\System\\MountedDevices]\n";
    for (const auto& d : cfg_.drives) out << "\"\\\\DosDevices\\\\" << d.letter << ":\"=-\n";
    out << '\n';
    // Crash reports go to the terminal (register dump, backtrace, threads)
    // instead of a dialog that may vanish before it can be read.
    out << "[HKEY_CURRENT_USER\\Software\\Wine\\WineDbg]\n"
        << "\"ShowCrashDialog\"=dword:00000000\n\n";
    // Direct3D/DirectDraw video memory: Wine asks the OpenGL driver, and some
    // report nonsense. VMware's SVGA3D driver says 1 MB, so not even a 640x480
    // screen fits: LEGO Racers fails with DDERR_OUTOFVIDEOMEMORY and NFS III
    // takes it for missing DirectX 5. Modern cards' many GB confuse old games too.
    out << "[HKEY_CURRENT_USER\\Software\\Wine\\Direct3D]\n"
        << "\"VideoMemorySize\"=\"" << cfg_.video_memory << "\"\n\n";
    if (is_win9x(cfg_.windows_version)) out << win9x_hardware_reg();
    // [wine] desktop: Wine's virtual desktop, a window that holds the game and
    // takes its mode changes instead of the real screen.
    // start.exe (which `wine` uses to launch explorer) and fragdesk.exe start on
    // a desktop of their own, so explorer creates "Default"; see start_desktop().
    const char* const helper_keys[] = {"start.exe", "fragdesk.exe"};
    out << "[HKEY_CURRENT_USER\\Software\\Wine\\Explorer]\n";
    if (cfg_.desktop.empty()) {
        out << "\"Desktop\"=-\n\n";
        for (const char* exe : helper_keys)
            out << "[-HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\" << exe << "\\Explorer]\n\n";
    } else {
        out << "\"Desktop\"=\"Default\"\n\n"
            << "[HKEY_CURRENT_USER\\Software\\Wine\\Explorer\\Desktops]\n"
            << "\"Default\"=\"" << cfg_.desktop << "\"\n\n";
        for (const char* exe : helper_keys)
            out << "[HKEY_CURRENT_USER\\Software\\Wine\\AppDefaults\\" << exe << "\\Explorer]\n"
                << "\"Desktop\"=\"FragProbe\"\n\n";
    }

    const fs::path reg = path_ / "frag-settings.reg";
    if (!write_if_changed(reg, out.str())) return;

    // Z: may be hidden, so Wine can't open host paths; import a copy from C:.
    const fs::path reg_copy = path_ / "drive_c" / "windows" / "frag-settings.reg";
    fs::copy_file(reg, reg_copy, fs::copy_options::overwrite_existing);

    // /reg:64 matters: `wine regedit` is a 32-bit program, so its HKLM\Software
    // writes land in Wow6432Node, where the mount manager never reads drive types.
    int rc = wine_cmd({"reg", "import", "C:\\windows\\frag-settings.reg", "/reg:64"});
    std::error_code ec;
    fs::remove(reg_copy, ec);
    if (rc != 0) {
        fs::remove(reg);  // retry the import next run
        throw std::runtime_error("registry import failed with exit code " + std::to_string(rc));
    }
    // The mount manager only reads drive types when it starts, and wineboot or an
    // earlier run may already have started it. Stop this prefix's wineserver so the
    // game starts with a fresh one.
    restart_wineserver();
}

void WinePrefix::restart_wineserver() const {
    fs::path server = "wineserver";
    if (wine_.find('/') != std::string::npos) server = fs::path(wine_).parent_path() / "wineserver";
    run_process({server.string(), "-k"}, {{"WINEPREFIX", path_.string()}}, verbose_);
    run_process({server.string(), "-w"}, {{"WINEPREFIX", path_.string()}}, verbose_);
}

fs::path WinePrefix::to_host_path(const std::string& windows_path) const {
    if (!is_windows_path(windows_path))
        throw std::runtime_error("not a Windows path: " + windows_path);

    char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(windows_path[0])));
    fs::path root;
    if (letter == 'C') {
        root = path_ / "drive_c";
    } else {
        auto it = std::find_if(cfg_.drives.begin(), cfg_.drives.end(),
                               [&](const Drive& d) { return d.letter == letter; });
        if (it == cfg_.drives.end())
            throw std::runtime_error(std::string("drive ") + letter + ": is not configured");
        root = it->path;
    }

    // Walk each component, matching names case-insensitively.
    fs::path cur = root;
    std::string rest = windows_path.substr(3);
    std::replace(rest.begin(), rest.end(), '\\', '/');
    for (const auto& part : fs::path(rest)) {
        std::string want = part.string();
        if (want.empty() || want == ".") continue;
        fs::path exact = cur / want;
        if (fs::exists(exact)) { cur = exact; continue; }
        bool found = false;
        if (fs::is_directory(cur))
            for (const auto& entry : fs::directory_iterator(cur))
                if (lower(entry.path().filename().string()) == lower(want)) {
                    cur = entry.path();
                    found = true;
                    break;
                }
        if (!found) throw std::runtime_error("path not found: " + windows_path);
    }
    return cur;
}

std::string WinePrefix::to_windows_path(const fs::path& host_path) const {
    std::vector<std::pair<char, fs::path>> roots{{'C', path_ / "drive_c"}};
    for (const auto& d : cfg_.drives) roots.emplace_back(d.letter, d.path);

    const fs::path target = fs::weakly_canonical(host_path);
    for (const auto& [letter, root] : roots) {
        const fs::path rel = target.lexically_relative(fs::weakly_canonical(root));
        if (rel.empty() || *rel.begin() == "..") continue;
        std::string win = std::string(1, letter) + ":";
        for (const auto& part : rel)
            if (part != ".") win += "\\" + part.string();
        return win;
    }
    return {};
}

// The program a CD's autorun.inf starts ("open=" under [autorun]), as Windows
// does when the disc is inserted. Uses the first CD-ROM drive that has one.
std::string WinePrefix::autorun_exe(std::vector<std::string>& args) const {
    for (const auto& d : cfg_.drives) {
        if (d.type != "cdrom") continue;
        fs::path inf;
        try {
            inf = to_host_path(std::string(1, d.letter) + ":\\AUTORUN.INF");
        } catch (const std::exception&) {
            continue;
        }
        std::ifstream in(inf);
        bool in_autorun = false;
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && line[0] == '[') {
                in_autorun = lower(line).rfind("[autorun]", 0) == 0;
                continue;
            }
            auto eq = line.find('=');
            if (!in_autorun || eq == std::string::npos) continue;
            std::string key = lower(line.substr(0, eq));
            key.erase(key.find_last_not_of(" \t") + 1);
            if (key != "open") continue;
            std::istringstream words(line.substr(eq + 1));
            std::string exe;
            words >> exe;
            for (std::string a; words >> a;) args.push_back(a);
            std::replace(exe.begin(), exe.end(), '/', '\\');
            if (exe.size() >= 1 && exe[0] == '\\') exe.erase(0, 1);
            return std::string(1, d.letter) + ":\\" + exe;
        }
    }
    return {};
}

// Setup programs on the configured drives, for "exe not found" errors: games put
// them in subfolders and use any capitalisation.
std::string WinePrefix::exe_not_found_hint() const {
    std::vector<std::string> found;
    for (const auto& d : cfg_.drives) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(d.path, ec);
             !ec && it != fs::recursive_directory_iterator() && found.size() < 10; it.increment(ec)) {
            if (it.depth() > 2) {
                it.disable_recursion_pending();
                continue;
            }
            const std::string name = lower(it->path().filename().string());
            if (name != "setup.exe" && name != "install.exe" && name != "autorun.exe") continue;
            std::string win = std::string(1, d.letter) + ":";
            for (const auto& part : it->path().lexically_relative(d.path)) win += "\\" + part.string();
            found.push_back(win);
        }
    }
    if (found.empty()) return {};
    std::string hint = "\nPrograms that look like installers on the game's drives:";
    for (const auto& f : found) hint += "\n  exe = " + f;
    return hint;
}

// Loads the Windows 9x devices into HKEY_DYN_DATA (see win9x_dyn_data_reg). That
// key lives in the wineserver, so a server is started that stays up for 30 s
// after its last program exits; otherwise it could exit between the import and
// the game's start and take the keys with it.
void WinePrefix::load_win9x_hardware() const {
    fs::path server = "wineserver";
    if (wine_.find('/') != std::string::npos) server = fs::path(wine_).parent_path() / "wineserver";
    // Exit code 2: a server is already running, which serves just as well.
    run_process({server.string(), "-p30"}, {{"WINEPREFIX", path_.string()}}, verbose_);
    const fs::path reg = path_ / "drive_c" / "windows" / "frag-dyndata.reg";
    write_if_changed(reg, win9x_dyn_data_reg());
    // regedit, not reg: reg.exe doesn't know HKEY_DYN_DATA.
    if (int rc = wine_cmd({"regedit", "/s", "C:\\windows\\frag-dyndata.reg"}); rc != 0)
        std::cerr << "frag: warning: loading Windows 9x hardware info failed (exit code " << rc << ")\n";
}

// 32-bit games decode video (Indeo, Cinepak, MPEG...) through 32-bit GStreamer
// plugins, and the decoders are in the libav plugin, which distributions don't
// install for 32-bit by default. Without it LEGO Racers only said "Unable to set
// message drain". Checks the Debian/Ubuntu and Arch locations.
static void warn_missing_video_decoders() {
    for (const char* dir : {"/usr/lib/i386-linux-gnu/gstreamer-1.0", "/usr/lib32/gstreamer-1.0"}) {
        if (!fs::is_directory(dir)) continue;
        if (fs::exists(fs::path(dir) / "libgstlibav.so")) return;
        std::cerr << "frag: note: 32-bit GStreamer has no libav plugin, so games' videos may not play; "
                  << (std::string(dir).find("i386") != std::string::npos
                          ? "install it with: sudo apt install gstreamer1.0-libav:i386"
                          : "install lib32-gst-libav")
                  << '\n';
        return;
    }
}

int WinePrefix::run(const RunOptions& opts) {
    warn_missing_video_decoders();
    // Windows paths go to Wine as-is so the program sees itself on the right drive.
    std::string exe = cfg_.exe;
    std::vector<std::string> autorun_args;
    if (exe.empty()) {
        exe = autorun_exe(autorun_args);
        if (exe.empty()) throw std::runtime_error("no exe set and no autorun.inf on a CD drive; set [game] exe");
        if (opts.verbose) std::cerr << "frag: starting " << exe << " from autorun.inf\n";
    }
    fs::path host_exe;
    if (is_windows_path(exe)) {
        try {
            host_exe = to_host_path(exe);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error(e.what() + exe_not_found_hint());
        }
    } else {
        host_exe = fs::weakly_canonical(cfg_.file.parent_path() / exe);
        exe = to_windows_path(host_exe);
        if (exe.empty()) {
            if (!cfg_.host_drive)
                throw std::runtime_error("exe " + host_exe.string() +
                                         " is not on C: or any [drive]; add a [drive] for its "
                                         "folder or set host_drive = yes under [wine]");
            exe = host_exe.string();  // Wine resolves it through Z:
        }
    }
    if (!fs::is_regular_file(host_exe)) throw std::runtime_error("exe not found: " + host_exe.string());

    // Many games load data relative to the current directory, so start in the exe's folder.
    fs::current_path(host_exe.parent_path());

    // Prefer video codecs a game installs over Wine's built-in ones. Installers
    // self-register them, which fails against Wine's builtins (InstallShield
    // error -119), and the originals are more faithful. Wine falls back to its
    // builtin when the game ships none. Config [dlls] entries take precedence.
    std::map<std::string, std::string> dlls = {
        {"ir32_32", "native,builtin"},   // Indeo 3
        {"ir41_32", "native,builtin"},   // Indeo 4
        {"ir50_32", "native,builtin"},   // Indeo 5
        {"iccvid", "native,builtin"},    // Cinepak
        {"msvidc32", "native,builtin"},  // Microsoft Video 1
    };
    // FRAG's own replacements for Wine DLLs (see install_dlls).
    for (const char* name : kFragDlls) dlls[name] = "native,builtin";
    for (const auto& [dll, mode] : cfg_.dll_overrides) dlls[dll] = mode;
    std::string overrides = kBaseOverrides;
    for (const auto& [dll, mode] : dlls) overrides += ";" + dll + "=" + mode;

    // Leftovers of an earlier run (a game that froze and was closed, say) keep the
    // prefix's wineserver and desktop alive; the game would join that desktop, and
    // [wine] desktop couldn't replace it. Start every run from a clean slate.
    restart_wineserver();

    // The desktop first: any Wine program (regedit included) would otherwise
    // bring up the desktop without the configured size.
    const pid_t desktop = cfg_.desktop.empty() ? -1 : start_desktop();
    if (is_win9x(cfg_.windows_version)) load_win9x_hardware();

    std::vector<std::string> argv{wine_, exe};
    argv.insert(argv.end(), autorun_args.begin(), autorun_args.end());
    argv.insert(argv.end(), cfg_.args.begin(), cfg_.args.end());
    argv.insert(argv.end(), opts.extra_args.begin(), opts.extra_args.end());
    const int rc = run_process(argv,
                               {{"WINEPREFIX", path_.string()},
                                {"WINEDEBUG", opts.wine_debug},
                                {"WINEDLLOVERRIDES", overrides},
                                kNoHostDrives},
                               opts.verbose);
    if (desktop > 0) {
        kill(desktop, SIGTERM);
        waitpid(desktop, nullptr, 0);
    }
    return rc;
}

// Opens Wine's virtual desktop of the configured size before the game starts.
// Every Wine program creates the "Default" desktop when it starts if it doesn't
// exist yet, as an ordinary screen-sized one; only explorer creates it virtual.
// With just the registry setting, the game won that race in FRAG prefixes and saw
// the full screen (stock prefixes got lucky: winemenubuilder, run at boot, let
// explorer go first; FRAG disables it). So explorer is started first, start.exe
// launching it is kept off "Default" (registry), and fragdesk.exe reports when
// the desktop is ready. Returns explorer's pid.
pid_t WinePrefix::start_desktop() const {
    const pid_t pid = fork();
    if (pid == 0) {
        setenv("WINEPREFIX", path_.c_str(), 1);
        setenv("WINEDEBUG", "-all", 1);
        setenv("WINEDLLOVERRIDES", kBaseOverrides, 1);
        setenv(kNoHostDrives.first.c_str(), kNoHostDrives.second.c_str(), 1);
        const std::string arg = "/desktop=Default," + cfg_.desktop;
        execlp(wine_.c_str(), wine_.c_str(), "explorer", arg.c_str(), nullptr);
        _exit(127);
    }
    if (pid < 0) return -1;
    if (verbose_) std::cerr << "frag: opening a " << cfg_.desktop << " desktop window\n";
    for (int i = 0; i < 60; ++i) {
        if (wine_cmd({"C:\\windows\\fragdesk.exe", cfg_.desktop}) == 0) return pid;
        if (waitpid(pid, nullptr, WNOHANG) == pid) break;
        usleep(250 * 1000);
    }
    std::cerr << "frag: warning: the " << cfg_.desktop << " desktop window did not appear; "
              << "the game may see the full screen\n";
    return pid;
}

}  // namespace frag
