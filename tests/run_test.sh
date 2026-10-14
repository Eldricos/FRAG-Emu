#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: run_test.sh <frag binary> <frag.conf> [win16 log file]
# Runs a test program through frag under a virtual X display when none is available.
# With a log file argument, the test passes only if the log has PASS lines and no FAIL.
set -u
frag=$1 conf=$2 log=${3:-}

FRAG_HOME=$(dirname "$conf")/frag-home
export FRAG_HOME
[ -n "$log" ] && rm -f "$log"

if [ -n "${DISPLAY:-}" ]; then
    "$frag" run "$conf" --verbose
else
    xvfb-run -a "$frag" run "$conf" --verbose
fi
rc=$?
WINEPREFIX=$("$frag" prefix "$conf") wineserver -k 2>/dev/null

if [ -n "$log" ]; then
    [ -f "$log" ] || { echo "no log written: $log"; exit 1; }
    cat "$log"
    grep -q '^FAIL' "$log" && exit 1
    grep -q '^PASS' "$log" || exit 1
fi
exit $rc
