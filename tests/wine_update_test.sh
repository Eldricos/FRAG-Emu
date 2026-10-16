#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: wine_update_test.sh <frag binary> <frag.conf for hello32>
# A prefix used with a new Wine version must be updated (wineboot -u) once:
# a Wine 9.0 prefix run with Wine 11 aborted on a missing cryptbase.dll.
# A wrapper around the real wine reports a different version.
set -u
frag=$1 conf=$2
here=$(dirname "$0")
work=$(dirname "$conf")/wine-update-test
rm -rf "$work" && mkdir -p "$work"
real=$(command -v wine)
printf '#!/bin/sh\n[ "$1" = --version ] && { echo wine-99.0-frag-test; exit 0; }\nexec %s "$@"\n' "$real" > "$work/wine"
ln -s "$(command -v wineserver)" "$work/wineserver"
chmod +x "$work/wine"
cp "$conf" "$work/base.conf"
sed "s|^\[game\]|[wine]\nbinary = $work/wine\n\n[game]|" "$conf" > "$work/new.conf"
# run_test.sh keeps each prefix next to its config: share one here.
FRAG_HOME=$work/frag-home
export FRAG_HOME

run() { if [ -n "${DISPLAY:-}" ]; then "$frag" run "$1"; else xvfb-run -a "$frag" run "$1"; fi; }

run "$work/base.conf" > "$work/1.txt" 2>&1 || { echo "FAIL: first run"; cat "$work/1.txt"; exit 1; }
grep -q "updating the prefix" "$work/1.txt" && { echo "FAIL: a new prefix was 'updated'"; exit 1; }
run "$work/new.conf" > "$work/2.txt" 2>&1 || { echo "FAIL: run with the new Wine"; cat "$work/2.txt"; exit 1; }
grep "updating the prefix" "$work/2.txt" || { echo "FAIL: no update for the new Wine version"; cat "$work/2.txt"; exit 1; }
run "$work/new.conf" > "$work/3.txt" 2>&1 || { echo "FAIL: second run with the new Wine"; exit 1; }
grep -q "updating the prefix" "$work/3.txt" && { echo "FAIL: updated again"; exit 1; }
echo "PASS: prefix updated once for a new Wine version"
