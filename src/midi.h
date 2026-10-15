// SPDX-License-Identifier: GPL-3.0-or-later
// Starts a software MIDI synth (FluidSynth) for the length of a game run.
#pragma once

#include "config.h"

#include <sys/types.h>

namespace frag {

// Windows games play MIDI music through a synth that Wine finds on the ALSA
// sequencer. If none is running and [sound] midi_synth is auto, this starts
// FluidSynth with a General MIDI SoundFont and stops it again when destroyed.
// Problems only produce a warning: the game still runs, without music.
class MidiSynth {
public:
    MidiSynth(const GameConfig& cfg, const fs::path& log, bool verbose);
    ~MidiSynth();
    MidiSynth(const MidiSynth&) = delete;
    MidiSynth& operator=(const MidiSynth&) = delete;

private:
    pid_t pid_ = -1;
    int stdin_fd_ = -1;  // FluidSynth's shell input; closing it makes FluidSynth quit
    bool verbose_;

    void stop();
};

}  // namespace frag
