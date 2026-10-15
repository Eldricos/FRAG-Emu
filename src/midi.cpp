// SPDX-License-Identifier: GPL-3.0-or-later
#include "midi.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace frag {

namespace {

// General MIDI SoundFonts in the locations distributions install them to.
const char* const kSoundFonts[] = {
    "/usr/share/sounds/sf2/FluidR3_GM.sf2",    // Debian/Ubuntu fluid-soundfont-gm
    "/usr/share/sounds/sf2/default-GM.sf2",
    "/usr/share/sounds/sf3/default-GM.sf3",    // Debian/Ubuntu musescore-general-soundfont
    "/usr/share/soundfonts/FluidR3_GM.sf2",    // Fedora, Arch
    "/usr/share/soundfonts/default.sf2",
};

// The ALSA sequencer's client list. FRAG_SEQ_CLIENTS replaces it in tests.
fs::path seq_clients_file() {
    if (const char* f = std::getenv("FRAG_SEQ_CLIENTS"); f && *f) return f;
    return "/proc/asound/seq/clients";
}

// Whether a software synth is already on the sequencer, e.g. a FluidSynth the
// user started or one from an earlier run.
bool synth_running() {
    std::ifstream in(seq_clients_file());
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("Client", 0) != 0) continue;
        std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return std::tolower(c); });
        for (const char* name : {"fluid", "timidity", "munt"})
            if (line.find(name) != std::string::npos) return true;
    }
    return false;
}

bool on_path(const std::string& program) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string dirs = path;
    for (size_t b = 0, e; b <= dirs.size(); b = e + 1) {
        e = dirs.find(':', b);
        if (e == std::string::npos) e = dirs.size();
        if (e > b && access((dirs.substr(b, e - b) + "/" + program).c_str(), X_OK) == 0) return true;
    }
    return false;
}

}  // namespace

MidiSynth::MidiSynth(const GameConfig& cfg, const fs::path& log, bool verbose) : verbose_(verbose) {
    if (cfg.midi_synth == "off" || synth_running()) return;

    fs::path soundfont = cfg.soundfont;
    if (soundfont.empty())
        for (const char* sf : kSoundFonts)
            if (fs::is_regular_file(sf)) { soundfont = sf; break; }
    const char* install = "install them with: sudo apt install fluidsynth fluid-soundfont-gm";
    if (!on_path("fluidsynth")) {
        std::cerr << "frag: note: FluidSynth not found, so MIDI music will be silent; " << install << '\n';
        return;
    }
    if (soundfont.empty() || !fs::is_regular_file(soundfont)) {
        std::cerr << "frag: note: no General MIDI SoundFont "
                  << (soundfont.empty() ? std::string("found") : "at " + soundfont.string())
                  << ", so MIDI music will be silent; " << install << " (or set [sound] soundfont)\n";
        return;
    }

    // Close-on-exec, so Wine and the game don't inherit the write end and keep
    // FluidSynth alive after FRAG closes it.
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return;
    std::vector<std::string> args{"fluidsynth", "-a", "pulseaudio", "-m", "alsa_seq", soundfont.string()};

    pid_ = fork();
    if (pid_ == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);  // don't outlive FRAG
        // FluidSynth's shell keeps it running while its input stays open; with -i
        // (no shell) and no server it exits at once.
        dup2(fds[0], STDIN_FILENO);
        int out = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (out >= 0) {
            dup2(out, STDOUT_FILENO);
            dup2(out, STDERR_FILENO);
        }
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(fds[0]);
    if (pid_ < 0) {
        close(fds[1]);
        return;
    }
    stdin_fd_ = fds[1];
    if (verbose_) std::cerr << "frag: started FluidSynth (pid " << pid_ << ") with " << soundfont << '\n';

    // Wine lists MIDI devices once, when the game first uses winmm, so the synth
    // must be on the sequencer before the game starts.
    for (int i = 0; i < 50; ++i) {
        if (synth_running()) return;
        if (waitpid(pid_, nullptr, WNOHANG) == pid_) {
            pid_ = -1;
            close(stdin_fd_);
            stdin_fd_ = -1;
            std::cerr << "frag: warning: FluidSynth exited at startup, so MIDI music will be silent; see "
                      << log << '\n';
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cerr << "frag: warning: FluidSynth did not appear on the ALSA sequencer within 5 s; see " << log
              << '\n';
}

MidiSynth::~MidiSynth() { stop(); }

void MidiSynth::stop() {
    if (pid_ <= 0) return;
    close(stdin_fd_);  // end of input: FluidSynth's shell quits
    for (int i = 0; i < 20; ++i) {
        if (waitpid(pid_, nullptr, WNOHANG) == pid_) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    kill(pid_, SIGTERM);
    waitpid(pid_, nullptr, 0);
}

}  // namespace frag
