#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: spawn_test.sh <frag binary> <frag.conf> <folder of SPAWN.EXE>
# frag waits for programs the game leaves running, and then returns promptly
# even with its output in a pipe (Wine's services used to hold it ~30 s).
set -u
frag=$1 conf=$2 dir=$3
FRAG_HOME=$(dirname "$conf")/frag-home
export FRAG_HOME
rm -f "$dir/done.txt"
run() { if [ -n "${DISPLAY:-}" ]; then "$@"; else xvfb-run -a "$@"; fi; }

run "$frag" run "$conf" >/dev/null 2>&1  # creates the prefix (~20 s) outside the timing
rm -f "$dir/done.txt"
start=$(date +%s)
run "$frag" run "$conf" 2>&1 | cat
end=$(date +%s)
[ -f "$dir/done.txt" ] || { echo "FAIL: frag returned before the child program finished"; exit 1; }
echo "PASS: frag waited for the child program"
[ $((end - start)) -le 20 ] || { echo "FAIL: frag took $((end - start)) s to return through a pipe"; exit 1; }
echo "PASS: returned through a pipe in $((end - start)) s"
