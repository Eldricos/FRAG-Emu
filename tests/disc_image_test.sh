#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: disc_image_test.sh <frag binary> <hello32.exe> <work dir>
# Builds a CD image holding hello32.exe and a long-named file (Joliet), then runs
# it from [drive D] image = ... as .iso and as .cue/.bin (raw MODE1/2352 sectors
# plus an audio track, like a mixed-mode game CD). hello32 checks the drive is a
# CD-ROM with the image's label and serial number and finds the long-named file.
set -u
frag=$1 exe=$2 work=$3
here=$(dirname "$0")
command -v genisoimage >/dev/null || { echo "SKIP: genisoimage not installed"; exit 77; }
command -v python3 >/dev/null || { echo "SKIP: python3 not installed"; exit 77; }

rm -rf "$work" && mkdir -p "$work/src/LongDirectoryName"
cp "$exe" "$work/src/HELLO32.EXE"
echo "hello from the CD" > "$work/src/LongDirectoryName/a_long_file_name.text"
# Like many game CDs: autorun.inf, and an installer in a subfolder in lower case.
printf '[autorun]\r\nopen=HELLO32.EXE D FRAGDISC\r\n' > "$work/src/AUTORUN.INF"
mkdir -p "$work/src/Install" && cp "$exe" "$work/src/Install/setup.exe"
genisoimage -quiet -J -V FRAGDISC -o "$work/test.iso" "$work/src" || { echo "FAIL: genisoimage"; exit 1; }

# Expected serial, computed independently: byte sums of the Joliet descriptor, Win9x order.
# Also writes test.bin/test.cue: the ISO as raw 2352-byte sectors, then 2 s of audio.
serial=$(python3 - "$work" <<'PY'
import sys
work = sys.argv[1]
iso = open(f"{work}/test.iso", "rb").read()
lba = 16
while iso[lba*2048+1:lba*2048+6] == b"CD001":
    d = iso[lba*2048:(lba+1)*2048]
    if d[0] == 2 and d[88:90] == b"%/":
        break
    lba += 1
sums = [sum(d[j::4]) & 0xff for j in range(4)]
print("%02x%02x%02x%02x" % (sums[3], sums[2], sums[1], sums[0]))
with open(f"{work}/test.bin", "wb") as f:
    for i in range(len(iso) // 2048):
        m, s, fr = (i + 150) // 4500, (i + 150) // 75 % 60, (i + 150) % 75
        bcd = lambda v: (v // 10) << 4 | v % 10
        f.write(b"\0" + b"\xff" * 10 + b"\0" + bytes([bcd(m), bcd(s), bcd(fr), 1]))
        f.write(iso[i*2048:(i+1)*2048] + b"\0" * 288)
    data_sectors = len(iso) // 2048
    f.write(b"\0" * 2352 * 150)
mm, ss, ff = data_sectors // 4500, data_sectors // 75 % 60, data_sectors % 75
open(f"{work}/test.cue", "w").write(
    'FILE "TEST.BIN" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
    f'  TRACK 02 AUDIO\n    INDEX 01 {mm:02d}:{ss:02d}:{ff:02d}\n')
PY
) || { echo "FAIL: building the test images"; exit 1; }
echo "expected serial: $serial"

for image in test.iso test.cue; do
    cat > "$work/frag.conf" <<CONF
[game]
name = disc-image-test
exe = D:\\HELLO32.EXE
args = D FRAGDISC $serial D:\\LongDirectoryName\\a_long_file_name.text

[drive D]
image = $image

[sound]
midi_synth = off
CONF
    echo "== $image"
    "$here/run_test.sh" "$frag" "$work/frag.conf" > "$work/out.txt" 2>&1
    rc=$?
    grep -E 'PASS|FAIL|label|file:|frag:' "$work/out.txt"
    [ $rc -eq 0 ] || { echo "FAIL: $image (exit $rc)"; exit 1; }
    # The second run reuses the unpacked files.
    "$here/run_test.sh" "$frag" "$work/frag.conf" > "$work/out.txt" 2>&1 || { echo "FAIL: $image second run"; exit 1; }
    grep -q unpacking "$work/out.txt" && { echo "FAIL: $image was unpacked again"; exit 1; }
done
# No exe: start what autorun.inf names, like inserting the disc in Windows.
printf '[game]\nname = disc-image-test\n\n[drive D]\nimage = test.iso\n\n[sound]\nmidi_synth = off\n' \
    > "$work/frag.conf"
echo "== autorun.inf"
"$here/run_test.sh" "$frag" "$work/frag.conf" > "$work/out.txt" 2>&1
rc=$?
grep -E 'autorun|FAIL|label' "$work/out.txt"
[ $rc -eq 0 ] && grep -q 'from autorun.inf' "$work/out.txt" || { echo "FAIL: autorun.inf (exit $rc)"; exit 1; }

# A wrong exe: the error lists the installer found in the subfolder.
sed -i 's|^\[game\]|[game]\nexe = D:\\SETUP.EXE|' "$work/frag.conf"
echo "== wrong exe"
"$here/run_test.sh" "$frag" "$work/frag.conf" > "$work/out.txt" 2>&1 && { echo "FAIL: wrong exe ran"; exit 1; }
grep -A2 'path not found' "$work/out.txt"
grep -qF 'exe = D:\Install\setup.exe' "$work/out.txt" || { echo "FAIL: no hint for the installer"; exit 1; }
echo "PASS: games run from .iso and .cue/.bin images"
