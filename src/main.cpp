// SPDX-License-Identifier: GPL-3.0-or-later
// frag: launches Windows 3.x/9x games from a per-game frag.conf.
#include "config.h"
#include "disc.h"
#include "midi.h"
#include "wine.h"

#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifndef FRAG_VERSION
#define FRAG_VERSION "0.0.0"
#endif

namespace {

void usage(std::ostream& out) {
    out << "usage: frag run <frag.conf | game-dir> [options] [-- extra game args]\n"
           "       frag prefix <frag.conf | game-dir>\n"
           "       frag --version\n"
           "\n"
           "options for run:\n"
           "  -v, --verbose            print the commands FRAG runs\n"
           "  --wine-debug CHANNELS    set WINEDEBUG (default: fixme-all), e.g. +loaddll,+relay\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        usage(args.empty() ? std::cerr : std::cout);
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "--version") {
        std::cout << "frag " << FRAG_VERSION << '\n';
        return 0;
    }

    const std::string& command = args[0];
    if ((command != "run" && command != "prefix") || args.size() < 2) {
        usage(std::cerr);
        return 2;
    }

    frag::RunOptions opts;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--") {
            opts.extra_args.assign(args.begin() + static_cast<long>(i) + 1, args.end());
            break;
        } else if (args[i] == "-v" || args[i] == "--verbose") {
            opts.verbose = true;
        } else if (args[i] == "--wine-debug" && i + 1 < args.size()) {
            opts.wine_debug = args[++i];
        } else {
            std::cerr << "frag: unknown option " << args[i] << '\n';
            usage(std::cerr);
            return 2;
        }
    }

    try {
        frag::GameConfig cfg = frag::load_config(args[1]);
        frag::WinePrefix prefix(cfg, opts.verbose);
        if (command == "prefix") {
            std::cout << prefix.path().string() << '\n';
            return 0;
        }
        frag::prepare_disc_drives(cfg, opts.verbose);
        prefix.prepare();
        frag::MidiSynth synth(cfg, prefix.path() / "fluidsynth.log", opts.verbose);
        return prefix.run(opts);
    } catch (const std::exception& e) {
        std::cerr << "frag: " << e.what() << '\n';
        return 1;
    }
}
