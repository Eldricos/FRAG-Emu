#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: stale_drive_test.sh <frag binary> <frag.conf for hello32 on D:>
# Reproduces a prefix where Wine gave D: to a host CD drive: the d:: device link
# points at /dev/sr0 and FRAG's d: folder link is gone. Wine then fails with
# "failed to open D:\...: c0000135". FRAG must repair this on the next run.
set -u
frag=$1 conf=$2
here=$(dirname "$0")

"$here/run_test.sh" "$frag" "$conf" >/dev/null 2>&1 || { echo "setup run failed"; exit 1; }

FRAG_HOME=$(dirname "$conf")/frag-home
export FRAG_HOME
prefix=$("$frag" prefix "$conf")
rm -f "$prefix/dosdevices/d:"
ln -sfn /dev/sr0 "$prefix/dosdevices/d::"

"$here/run_test.sh" "$frag" "$conf" || exit 1
[ -L "$prefix/dosdevices/d::" ] && { echo "FAIL: stale d:: link was not removed"; exit 1; }
echo "PASS: stale host drive link repaired"
