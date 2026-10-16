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

# Exited processes (state Z) don't count; give dying ones a few seconds.
alive() { ps -eo stat=,args= | awk '$1 !~ /^Z/ && $2 ~ /^[A-Z]:.DEADLOCK\.EXE$/ {found=1} END {exit !found}'; }

# $1 = plain: frag's output to a file. $1 = tee: through `| tee`, and Ctrl+C
# reaches tee as well (as in a terminal), so frag's messages hit a dead pipe.
check() {
    : > "$out"
    if [ "$1" = tee ]; then
        "$frag" run "$conf" 2>&1 | tee "$out" > /dev/null &
        sleep 1
        pid=$(pgrep -n -f "^$frag run $conf")
        teepid=$(pgrep -n -f "^tee $out")
    else
        "$frag" run "$conf" > "$out" 2>&1 &
        pid=$! teepid=
    fi
    for i in $(seq 1 90); do grep -q deadlocking "$out" && break; sleep 1; done
    grep -q deadlocking "$out" || { echo "FAIL ($1): game never started"; cat "$out"; kill -9 $pid; exit 1; }
    sleep 3
    kill -0 $pid 2>/dev/null || { echo "FAIL ($1): game didn't freeze"; cat "$out"; exit 1; }
    # tee goes first, as it would in a terminal: it has nothing else to do.
    [ -n "$teepid" ] && { kill -INT $teepid; sleep 1; }
    kill -INT $pid
    for i in $(seq 1 30); do kill -0 $pid 2>/dev/null || break; sleep 1; done
    if kill -0 $pid 2>/dev/null; then
        echo "FAIL ($1): frag still running 30 s after Ctrl+C"; cat "$out"; kill -9 $pid; exit 1
    fi
    for i in $(seq 1 10); do alive || break; sleep 1; done
    if alive; then echo "FAIL ($1): game still running"; ps -eo pid,stat,args | awk '$3 ~ /DEADLOCK\.EXE$/'; exit 1; fi
    echo "ok ($1)"
}
check plain
check tee

# A game that outlives `wineserver -k` (a thread stuck on a lock may never notice
# the server is gone): a wine wrapper whose wineserver ignores -k. frag must then
# kill the prefix's processes itself after 5 s.
wrap=$(dirname "$conf")/stubborn-wine
rm -rf "$wrap" && mkdir -p "$wrap"
real_wine=$(command -v wine) real_server=$(command -v wineserver)
printf '#!/bin/sh\nexec %s "$@"\n' "$real_wine" > "$wrap/wine"
printf '#!/bin/sh\n[ "$1" = -k ] && exit 0\nexec %s "$@"\n' "$real_server" > "$wrap/wineserver"
chmod +x "$wrap/wine" "$wrap/wineserver"
stubborn=$(dirname "$conf")/stubborn.conf
sed "s|^\[game\]|[wine]\nbinary = $wrap/wine\n\n[game]|" "$conf" > "$stubborn"
conf=$stubborn
check stubborn
grep -q "killing its processes" "$out" || { echo "FAIL: the fallback didn't run"; cat "$out"; exit 1; }
WINEPREFIX=$("$frag" prefix "$conf") "$real_server" -k 2>/dev/null
echo "PASS: Ctrl+C stopped a frozen game"
