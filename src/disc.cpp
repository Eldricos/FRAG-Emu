// SPDX-License-Identifier: GPL-3.0-or-later
#include "disc.h"

#include "wine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

namespace frag {

namespace {

constexpr size_t kSector = 2048;

// Where the data track's 2048-byte sectors are in the image file.
struct DataTrack {
    fs::path file;
    uint64_t start = 0;       // byte offset of the track's first sector
    size_t sector_size = kSector;
    size_t data_offset = 0;   // 16 for raw MODE1/2352, 24 for MODE2/2352 (XA form 1)
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Finds a file named in a .cue sheet next to it, ignoring case like Windows tools do.
fs::path cue_file(const fs::path& cue, const std::string& name) {
    fs::path dir = cue.parent_path();
    if (fs::exists(dir / name)) return dir / name;
    for (const auto& e : fs::directory_iterator(dir))
        if (lower(e.path().filename().string()) == lower(name)) return e.path();
    throw std::runtime_error(cue.string() + ": file not found: " + name);
}

// The first data track of a cue sheet. Mixed-mode game CDs have it as track 1,
// followed by the audio tracks.
DataTrack parse_cue(const fs::path& cue) {
    std::ifstream in(cue);
    if (!in) throw std::runtime_error("cannot open " + cue.string());
    fs::path file;
    bool in_data = false;
    DataTrack t;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line);
        std::string cmd;
        ls >> cmd;
        cmd = lower(cmd);
        if (cmd == "file") {
            std::string rest;
            std::getline(ls, rest);
            auto q1 = rest.find('"'), q2 = rest.rfind('"');
            std::string name;
            if (q1 != std::string::npos && q2 > q1) name = rest.substr(q1 + 1, q2 - q1 - 1);
            else std::istringstream(rest) >> name;
            file = cue_file(cue, name);
        } else if (cmd == "track") {
            std::string num, mode;
            ls >> num >> mode;
            mode = lower(mode);
            in_data = mode != "audio";
            if (in_data) {
                t.file = file;
                if (mode == "mode1/2048") t.sector_size = 2048, t.data_offset = 0;
                else if (mode == "mode1/2352") t.sector_size = 2352, t.data_offset = 16;
                else if (mode == "mode2/2352") t.sector_size = 2352, t.data_offset = 24;
                else if (mode == "mode2/2336") t.sector_size = 2336, t.data_offset = 8;
                else throw std::runtime_error(cue.string() + ": unsupported track mode " + mode);
            }
        } else if (cmd == "index" && in_data) {
            std::string num, msf;
            ls >> num >> msf;
            if (num != "01" && num != "1") continue;
            int m = 0, s = 0, f = 0;
            if (std::sscanf(msf.c_str(), "%d:%d:%d", &m, &s, &f) != 3)
                throw std::runtime_error(cue.string() + ": bad INDEX " + msf);
            t.start = static_cast<uint64_t>((m * 60 + s) * 75 + f) * t.sector_size;
            return t;
        }
    }
    throw std::runtime_error(cue.string() + ": no data track");
}

// Raw images start with the 12-byte CD sync pattern; mode byte 15 tells
// MODE1 (data at 16) from MODE2 XA (data at 24).
DataTrack detect_track(const fs::path& image) {
    if (lower(image.extension().string()) == ".cue") return parse_cue(image);
    DataTrack t;
    t.file = image;
    std::ifstream in(image, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + image.string());
    unsigned char head[16] = {};
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    static const unsigned char sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    if (in && std::memcmp(head, sync, sizeof(sync)) == 0) {
        t.sector_size = 2352;
        t.data_offset = head[15] == 2 ? 24 : 16;
    }
    return t;
}

class Disc {
public:
    explicit Disc(const fs::path& image) : image_(image), track_(detect_track(image)),
                                            in_(track_.file, std::ios::binary) {
        if (!in_) throw std::runtime_error("cannot open " + track_.file.string());
    }

    std::vector<unsigned char> read(uint32_t lba, size_t sectors = 1) {
        std::vector<unsigned char> out(sectors * kSector);
        if (track_.sector_size == kSector) {
            seek(lba);
            in_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
            check();
        } else {
            for (size_t i = 0; i < sectors; ++i) {
                seek(lba + static_cast<uint32_t>(i));
                in_.read(reinterpret_cast<char*>(out.data() + i * kSector), kSector);
                check();
            }
        }
        return out;
    }

    const fs::path& image() const { return image_; }

private:
    fs::path image_;
    DataTrack track_;
    std::ifstream in_;

    void seek(uint32_t lba) {
        in_.seekg(static_cast<std::streamoff>(track_.start + uint64_t(lba) * track_.sector_size +
                                              track_.data_offset));
    }
    void check() {
        if (!in_) throw std::runtime_error(image_.string() + ": image is truncated or not a CD image");
    }
};

uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

// The descriptor Windows reads names and the label from: the Joliet supplementary
// descriptor if there is one, else the primary one. Returns its sector number.
uint32_t find_descriptor(Disc& disc, bool& joliet) {
    uint32_t primary = 0;
    for (uint32_t lba = 16; lba < 16 + 64; ++lba) {
        auto d = disc.read(lba);
        if (std::memcmp(d.data() + 1, "CD001", 5) != 0) break;
        if (d[0] == 1 && !primary) primary = lba;
        // Joliet: a supplementary descriptor with UCS-2 escape %/@, %/C or %/E.
        if (d[0] == 2 && d[88] == '%' && d[89] == '/' && (d[90] == '@' || d[90] == 'C' || d[90] == 'E')) {
            joliet = true;
            return lba;
        }
        if (d[0] == 255) break;
    }
    if (!primary) throw std::runtime_error(disc.image().string() + ": no ISO9660 file system found");
    joliet = false;
    return primary;
}

std::string ucs2be_to_utf8(const unsigned char* p, size_t len) {
    std::string out;
    for (size_t i = 0; i + 1 < len; i += 2) {
        unsigned c = p[i] << 8 | p[i + 1];
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) out += static_cast<char>(0xc0 | c >> 6), out += static_cast<char>(0x80 | (c & 0x3f));
        else out += static_cast<char>(0xe0 | c >> 12), out += static_cast<char>(0x80 | ((c >> 6) & 0x3f)),
             out += static_cast<char>(0x80 | (c & 0x3f));
    }
    return out;
}

std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

// A directory record's name as Windows shows it: Joliet names are UCS-2; ISO9660
// names lose their ";1" version and the "." of names without an extension.
std::string record_name(const unsigned char* name, size_t len, bool joliet) {
    std::string n = joliet ? ucs2be_to_utf8(name, len) : std::string(reinterpret_cast<const char*>(name), len);
    if (auto semi = n.find(';'); semi != std::string::npos) n.erase(semi);
    if (!n.empty() && n.back() == '.') n.pop_back();
    return n;
}

// Sets a file's modification time from a directory record's recording date.
void set_mtime(const fs::path& path, const unsigned char* date) {
    std::tm tm{};
    tm.tm_year = date[0];
    tm.tm_mon = date[1] - 1;
    tm.tm_mday = date[2];
    tm.tm_hour = date[3];
    tm.tm_min = date[4];
    tm.tm_sec = date[5];
    if (date[1] < 1 || date[1] > 12 || date[2] < 1) return;
    time_t t = timegm(&tm) - static_cast<signed char>(date[6]) * 15 * 60;  // offset from GMT, 15-min units
    struct timespec ts[2] = {{t, 0}, {t, 0}};
    utimensat(AT_FDCWD, path.c_str(), ts, 0);
}

void extract_dir(Disc& disc, uint32_t lba, uint32_t size, const fs::path& dest, bool joliet,
                 std::set<uint32_t>& visited, int depth) {
    if (depth > 32 || !visited.insert(lba).second) return;  // corrupt or looping image
    const auto data = disc.read(lba, (size + kSector - 1) / kSector);
    std::string continuing;  // name of a file whose next extent follows (multi-extent)
    for (size_t pos = 0; pos < data.size();) {
        const unsigned char* r = data.data() + pos;
        if (r[0] == 0) {  // records don't cross sectors; the rest of this one is padding
            pos = (pos / kSector + 1) * kSector;
            continue;
        }
        const size_t len = r[0];
        if (len < 34 || pos + len > data.size()) break;
        pos += len;
        const size_t name_len = r[32];
        if (33 + name_len > len) continue;
        if (name_len == 1 && (r[33] == 0 || r[33] == 1)) continue;  // "." and ".."
        const unsigned char flags = r[25];
        if (flags & 0x04) continue;  // associated file (Mac resource fork)
        std::string name = record_name(r + 33, name_len, joliet);
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
            name.find('\0') != std::string::npos)
            continue;
        const uint32_t extent = le32(r + 2), length = le32(r + 10);
        const fs::path out = dest / name;
        if (flags & 0x02) {
            fs::create_directory(out);
            extract_dir(disc, extent, length, out, joliet, visited, depth + 1);
            set_mtime(out, r + 18);
            continue;
        }
        const bool append = continuing == name;
        {
            std::ofstream f(out, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!f) throw std::runtime_error("cannot write " + out.string());
            for (uint32_t done = 0, s = extent; done < length; s += 64) {
                const auto chunk = disc.read(s, std::min<uint32_t>(64, (length - done + kSector - 1) / kSector));
                const uint32_t n = std::min<uint32_t>(length - done, static_cast<uint32_t>(chunk.size()));
                f.write(reinterpret_cast<const char*>(chunk.data()), n);
                done += n;
            }
            if (!f) throw std::runtime_error("cannot write " + out.string() + " (disk full?)");
        }
        continuing = (flags & 0x80) ? name : std::string();
        if (continuing.empty()) {
            // Files on a CD are read-only, and Windows reports them that way.
            chmod(out.c_str(), 0444);
            set_mtime(out, r + 18);
        }
    }
}

// Stable across runs and compilers, unlike std::hash: FNV-1a.
std::string fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool is_9x(const std::string& version) {
    return version == "win31" || version == "win95" || version == "win98" || version == "winme";
}

}  // namespace

DiscInfo read_disc_info(const fs::path& image) {
    Disc disc(image);
    bool joliet = false;
    const auto d = disc.read(find_descriptor(disc, joliet));
    DiscInfo info;
    info.label = rtrim(joliet ? ucs2be_to_utf8(d.data() + 40, 32)
                              : std::string(reinterpret_cast<const char*>(d.data() + 40), 32));
    // Windows' CD serial number: a byte-wise checksum of the volume descriptor.
    // Windows 9x and NT put the bytes in opposite orders (Wine does the same).
    unsigned char sum[4] = {};
    for (size_t i = 0; i < kSector; i += 4)
        for (int j = 0; j < 4; ++j) sum[j] = static_cast<unsigned char>(sum[j] + d[i + j]);
    info.serial_9x = uint32_t(sum[3]) << 24 | sum[2] << 16 | sum[1] << 8 | sum[0];
    info.serial_nt = uint32_t(sum[0]) << 24 | sum[1] << 16 | sum[2] << 8 | sum[3];
    return info;
}

void extract_disc(const fs::path& image, const fs::path& dest) {
    Disc disc(image);
    bool joliet = false;
    const auto d = disc.read(find_descriptor(disc, joliet));
    const unsigned char* root = d.data() + 156;
    fs::create_directories(dest);
    std::set<uint32_t> visited;
    extract_dir(disc, le32(root + 2), le32(root + 10), dest, joliet, visited, 0);
}

void prepare_disc_drives(GameConfig& cfg, bool verbose) {
    for (auto& drive : cfg.drives) {
        if (drive.image.empty()) continue;
        const DiscInfo info = read_disc_info(drive.image);
        if (drive.label.empty()) drive.label = info.label;
        if (drive.serial.empty()) {
            char buf[9];
            std::snprintf(buf, sizeof(buf), "%08x", is_9x(cfg.windows_version) ? info.serial_9x : info.serial_nt);
            drive.serial = buf;
        }

        // One folder per image file: a changed or replaced image gets a new one.
        const auto mtime = fs::last_write_time(drive.image).time_since_epoch().count();
        const std::string key = drive.image.string() + '\n' + std::to_string(fs::file_size(drive.image)) +
                                '\n' + std::to_string(mtime);
        const fs::path dir = data_dir() / "discs" / (drive.image.stem().string() + "-" + fnv1a(key).substr(0, 12));
        if (!fs::is_directory(dir)) {
            std::cerr << "frag: unpacking " << drive.image.filename().string()
                      << " (first run only; this can take a minute)\n";
            // Unpack beside the final folder and rename at the end, so an
            // interrupted run never leaves a half-filled disc behind.
            const fs::path tmp = dir.string() + ".partial";
            std::error_code ec;
            fs::remove_all(tmp, ec);
            try {
                extract_disc(drive.image, tmp);
            } catch (...) {
                fs::remove_all(tmp, ec);
                throw;
            }
            fs::rename(tmp, dir);
        }
        if (verbose)
            std::cerr << "frag: drive " << drive.letter << ": is " << drive.image << " (label '" << drive.label
                      << "', serial " << drive.serial << ") unpacked in " << dir << '\n';
        drive.path = dir;
    }
}

}  // namespace frag
