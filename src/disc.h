// SPDX-License-Identifier: GPL-3.0-or-later
// CD images (.iso, .cue/.bin, raw .bin/.img): reading the ISO9660 data track and
// unpacking it into a folder that Wine can use as a CD-ROM drive.
#pragma once

#include "config.h"

#include <cstdint>
#include <string>

namespace frag {

struct DiscInfo {
    std::string label;   // volume label as Windows shows it (Joliet if present)
    uint32_t serial_9x;  // volume serial number as Windows 9x computes it
    uint32_t serial_nt;  // ... and as Windows NT computes it (bytes reversed)
};

// Reads the label and serial number of an image without unpacking it.
DiscInfo read_disc_info(const fs::path& image);

// Unpacks every file of the image's data track into `dest`, which must not exist.
// Uses the Joliet (long name) tree when the disc has one, like Windows does.
void extract_disc(const fs::path& image, const fs::path& dest);

// For each [drive] with an image: unpacks it once into FRAG's disc cache and
// points the drive at that folder, filling in the label and serial from the
// image unless frag.conf sets them.
void prepare_disc_drives(GameConfig& cfg, bool verbose);

}  // namespace frag
