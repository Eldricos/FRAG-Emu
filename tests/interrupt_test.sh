#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: interrupt_test.sh <frag binary> <frag.conf for deadlock.exe>
# A game frozen on the loader lock ignores Ctrl+C. frag must stop it on Ctrl+C,
# clean up and exit, instead of waiting forever (which left the owner's screen
# at the game's resolution).
set -u
[ -n "${DISPLAY:-}" ] || exec xvfb-run -a "$0" "$@"
frag=$1 conf=$2
FRAG_HOME=$(dirname "$conf")/frag-home
export FRAG_HOME
out=$(dirname "$conf")/interrupt.out

"$frag" run "$conf" > "$out" 2>&1 &
pid=$!
for i in $(seq 1 90); do grep -q deadlocking "$out" && break; sleep 1; done
grep -q deadlocking "$out" || { echo "FAIL: game never started"; cat "$out"; kill -9 $pid; exit 1; }
sleep 3
kill -0 $pid 2>/dev/null || { echo "FAIL: game didn't freeze"; cat "$out"; exit 1; }
kill -INT $pid
for i in $(seq 1 30); do kill -0 $pid 2>/dev/null || break; sleep 1; done
if kill -0 $pid 2>/dev/null; then
    echo "FAIL: frag still running 30 s after Ctrl+C"; cat "$out"; kill -9 $pid; exit 1
fi
cat "$out"
# Exited processes (state Z) don't count; give dying ones a few seconds.
alive() { ps -eo stat=,args= | grep -v '^Z' | grep -q '[D]EADLOCK.EXE'; }
for i in $(seq 1 10); do alive || break; sleep 1; done
if alive; then echo "FAIL: game still running"; ps -eo pid,stat,args | grep '[D]EADLOCK'; exit 1; fi
echo "PASS: Ctrl+C stopped a frozen game"
