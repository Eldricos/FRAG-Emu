#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: midi_synth_test.sh <frag binary> <frag.conf for hello32>
# Checks that frag starts FluidSynth for a game when no MIDI synth is running,
# stops it when the game exits, and leaves an already running synth alone.
# A stand-in fluidsynth script plays the synth; FRAG_SEQ_CLIENTS stands in for
# the ALSA sequencer's client list.
set -u
frag=$1 conf=$2
here=$(dirname "$0")
work=$(dirname "$conf")/midi-synth-test
rm -rf "$work" && mkdir -p "$work/bin"

FRAG_SEQ_CLIENTS=$work/clients
export FRAG_SEQ_CLIENTS
: > "$FRAG_SEQ_CLIENTS"
cat > "$work/bin/fluidsynth" <<SCRIPT
#!/bin/sh
echo "\$@" > "$work/args"
echo 'Client 129 : "FLUID Synth (\$\$)" [User Legacy]' >> "$FRAG_SEQ_CLIENTS"
cat > /dev/null    # FluidSynth's shell: runs until its input closes
echo exited > "$work/exited"
SCRIPT
chmod +x "$work/bin/fluidsynth"
: > "$work/test.sf2"
sed "s|^\[game\]|[sound]\nsoundfont = $work/test.sf2\n\n[game]|" "$conf" > "$work/frag.conf"
PATH=$work/bin:$PATH
export PATH

"$here/run_test.sh" "$frag" "$work/frag.conf" || { echo "FAIL: game run failed"; exit 1; }
[ -f "$work/args" ] || { echo "FAIL: FluidSynth was not started"; exit 1; }
echo "FluidSynth args: $(cat "$work/args")"
grep -q -- "-m alsa_seq" "$work/args" || { echo "FAIL: not on the ALSA sequencer"; exit 1; }
grep -q -- "$work/test.sf2" "$work/args" || { echo "FAIL: SoundFont not passed"; exit 1; }
[ -f "$work/exited" ] || { echo "FAIL: FluidSynth still running after the game"; exit 1; }

# A synth is already on the sequencer (left in the clients file): don't start another.
rm -f "$work/args"
"$here/run_test.sh" "$frag" "$work/frag.conf" || { echo "FAIL: second run failed"; exit 1; }
[ -f "$work/args" ] && { echo "FAIL: started a second synth"; exit 1; }

# midi_synth = off
: > "$FRAG_SEQ_CLIENTS"
sed -i "s|^\[sound\]|[sound]\nmidi_synth = off|" "$work/frag.conf"
"$here/run_test.sh" "$frag" "$work/frag.conf" || { echo "FAIL: third run failed"; exit 1; }
[ -f "$work/args" ] && { echo "FAIL: started a synth with midi_synth = off"; exit 1; }
echo "PASS: FluidSynth started, stopped, and skipped when not needed"
