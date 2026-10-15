/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Sound diagnostic. Run through frag to see what a game sees:
 *   - every wave and MIDI output device Wine reports,
 *   - where the MIDI mapper sends music (registry CurrentInstrument),
 *   - whether each device opens (error codes as games see them),
 *   - DirectSound initialisation,
 * then it plays a short tune through the MIDI mapper and a beep through the
 * wave mapper, so the user can hear which path works.
 * Usage: soundprobe.exe [--quiet]   (--quiet skips the audible part)
 * Always exits 0 unless it crashes; read the printed report.
 */
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <mmreg.h>
#include <msacm.h>
#include <stdio.h>
#include <string.h>

static void midi_report(void)
{
    UINT n = midiOutGetNumDevs(), i;
    char value[256];
    DWORD size = sizeof(value);
    HKEY key;

    printf("\n== MIDI output devices: %u\n", n);
    for (i = 0; i < n; i++) {
        MIDIOUTCAPSA caps;
        HMIDIOUT h;
        MMRESULT open;
        memset(&caps, 0, sizeof(caps));
        midiOutGetDevCapsA(i, &caps, sizeof(caps));
        open = midiOutOpen(&h, i, 0, 0, CALLBACK_NULL);
        printf("  %u: '%s' technology=%u -> open %s (%u)\n", i, caps.szPname, caps.wTechnology,
               open == MMSYSERR_NOERROR ? "ok" : "FAILED", open);
        if (open == MMSYSERR_NOERROR) midiOutClose(h);
    }

    value[0] = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Multimedia\\MIDIMap",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExA(key, "CurrentInstrument", NULL, NULL, (BYTE *)value, &size) != ERROR_SUCCESS)
            value[0] = 0;
        RegCloseKey(key);
    }
    printf("  MIDI mapper target (CurrentInstrument): %s\n", value[0] ? value : "(not set: first device)");
    if (value[0]) {
        /* Wine's mapper matches the full device name; on a mismatch it falls back to device 0 */
        BOOL found = FALSE;
        for (i = 0; i < n && !found; i++) {
            MIDIOUTCAPSA caps;
            memset(&caps, 0, sizeof(caps));
            midiOutGetDevCapsA(i, &caps, sizeof(caps));
            found = strcmp(caps.szPname, value) == 0;
        }
        if (!found) printf("  WARNING: no device has that exact name; the mapper falls back to device 0\n");
    }
}

static void wave_report(void)
{
    UINT n = waveOutGetNumDevs(), i;
    static const struct { DWORD rate; WORD bits, channels; } formats[] = {
        {11025, 8, 1}, {22050, 8, 1}, {22050, 16, 2}, {44100, 16, 2},
    };

    printf("\n== Wave output devices: %u\n", n);
    for (i = 0; i < n; i++) {
        WAVEOUTCAPSA caps;
        memset(&caps, 0, sizeof(caps));
        waveOutGetDevCapsA(i, &caps, sizeof(caps));
        printf("  %u: '%s'\n", i, caps.szPname);
    }
    for (i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        WAVEFORMATEX wf;
        HWAVEOUT h;
        MMRESULT open;
        memset(&wf, 0, sizeof(wf));
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = formats[i].channels;
        wf.nSamplesPerSec = formats[i].rate;
        wf.wBitsPerSample = formats[i].bits;
        wf.nBlockAlign = wf.nChannels * wf.wBitsPerSample / 8;
        wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
        open = waveOutOpen(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
        printf("  wave mapper %5lu Hz %2u-bit %s -> open %s (%u)\n", formats[i].rate, formats[i].bits,
               formats[i].channels == 2 ? "stereo" : "mono  ", open == MMSYSERR_NOERROR ? "ok" : "FAILED", open);
        if (open == MMSYSERR_NOERROR) waveOutClose(h);
    }
}

static BOOL CALLBACK acm_tag_cb(HACMDRIVERID id, LPACMFORMATTAGDETAILSA d, DWORD_PTR inst, DWORD support)
{
    printf("      format tag 0x%04lx '%s'\n", (unsigned long)d->dwFormatTag, d->szFormatTag);
    return TRUE;
}

static BOOL CALLBACK acm_driver_cb(HACMDRIVERID id, DWORD_PTR inst, DWORD support)
{
    ACMDRIVERDETAILSA d;
    HACMDRIVER h;
    memset(&d, 0, sizeof(d));
    d.cbStruct = sizeof(d);
    if (acmDriverDetailsA(id, &d, 0) != MMSYSERR_NOERROR) {
        printf("  (details failed)\n");
        return TRUE;
    }
    printf("  '%s' / '%s' mid=%u pid=%u formats=%lu\n", d.szShortName, d.szLongName, d.wMid, d.wPid,
           (unsigned long)d.cFormatTags);
    if (acmDriverOpen(&h, id, 0) == MMSYSERR_NOERROR) {
        ACMFORMATTAGDETAILSA t;
        memset(&t, 0, sizeof(t));
        t.cbStruct = sizeof(t);
        acmFormatTagEnumA(h, &t, acm_tag_cb, 0, 0);
        acmDriverClose(h, 0);
    }
    return TRUE;
}

/* Games look up codecs by name (Classic Board Games wants "Microsoft ADPCM") */
static void acm_report(void)
{
    printf("\n== ACM audio codecs\n");
    acmDriverEnum(acm_driver_cb, 0, 0);
}

static void dsound_report(void)
{
    IDirectSound *ds = NULL;
    HRESULT hr = DirectSoundCreate(NULL, &ds, NULL);
    printf("\n== DirectSoundCreate: %s (0x%08lx)\n", SUCCEEDED(hr) ? "ok" : "FAILED", (unsigned long)hr);
    if (ds) IDirectSound_Release(ds);
}

static void play_midi(void)
{
    static const BYTE notes[] = {60, 64, 67, 72};
    HMIDIOUT h;
    MMRESULT r = midiOutOpen(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
    UINT i;

    printf("\n== Playing 4 piano notes through the MIDI mapper... ");
    if (r != MMSYSERR_NOERROR) {
        printf("FAILED to open (%u)\n", r);
        return;
    }
    midiOutShortMsg(h, 0x000000C0);                 /* program change: piano */
    for (i = 0; i < sizeof(notes); i++) {
        midiOutShortMsg(h, 0x007F0090 | (notes[i] << 8));  /* note on */
        Sleep(350);
        midiOutShortMsg(h, 0x00000080 | (notes[i] << 8));  /* note off */
    }
    midiOutClose(h);
    printf("done. Did you hear them?\n");
}

static void play_wave(void)
{
    enum { RATE = 22050, LEN = RATE / 2 };
    static BYTE samples[LEN];
    WAVEFORMATEX wf;
    WAVEHDR hdr;
    HWAVEOUT h;
    MMRESULT r;
    int i;

    for (i = 0; i < LEN; i++) samples[i] = ((i / 25) & 1) ? 160 : 96;   /* ~440 Hz square wave */
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = RATE;
    wf.wBitsPerSample = 8;
    wf.nBlockAlign = 1;
    wf.nAvgBytesPerSec = RATE;

    printf("== Playing a half-second beep through the wave mapper... ");
    r = waveOutOpen(&h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        printf("FAILED to open (%u)\n", r);
        return;
    }
    memset(&hdr, 0, sizeof(hdr));
    hdr.lpData = (LPSTR)samples;
    hdr.dwBufferLength = LEN;
    waveOutPrepareHeader(h, &hdr, sizeof(hdr));
    waveOutWrite(h, &hdr, sizeof(hdr));
    for (i = 0; i < 40 && !(hdr.dwFlags & WHDR_DONE); i++) Sleep(50);
    waveOutReset(h);
    waveOutUnprepareHeader(h, &hdr, sizeof(hdr));
    waveOutClose(h);
    printf("done. Did you hear it?\n");
}

int main(int argc, char **argv)
{
    int quiet = argc > 1 && strcmp(argv[1], "--quiet") == 0;

    printf("FRAG sound probe\n");
    midi_report();
    wave_report();
    dsound_report();
    acm_report();
    if (!quiet) {
        play_midi();
        play_wave();
    }
    return 0;
}
