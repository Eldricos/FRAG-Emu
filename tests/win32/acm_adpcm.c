/* SPDX-License-Identifier: GPL-3.0-or-later
 * Checks the MS ADPCM codec the way 1990s games use it: found among the installed
 * ACM drivers by its Windows name, "Microsoft ADPCM", and able to decode a block.
 * Classic Board Games shows "Could not initialize sound" when the name doesn't match.
 * Exit code 0 means pass.
 */
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>
#include <stdio.h>
#include <string.h>

static HACMDRIVERID found;

static BOOL CALLBACK driver_cb(HACMDRIVERID id, DWORD_PTR inst, DWORD support)
{
    ACMDRIVERDETAILSA d;
    memset(&d, 0, sizeof(d));
    d.cbStruct = sizeof(d);
    if (acmDriverDetailsA(id, &d, 0) == MMSYSERR_NOERROR) {
        printf("driver '%s' / '%s'\n", d.szShortName, d.szLongName);
        if (strcmp(d.szShortName, "Microsoft ADPCM") == 0) found = id;
    }
    return TRUE;
}

static BOOL CALLBACK format_cb(HACMDRIVERID id, LPACMFORMATDETAILSA fd, DWORD_PTR inst, DWORD support)
{
    return FALSE; /* the first ADPCM format is enough; acmFormatEnum filled it in */
}

int main(void)
{
    static BYTE adpcm_buf[4096], pcm_buf[65536];
    BYTE adpcm_fmt[256];
    WAVEFORMATEX pcm;
    ACMFORMATDETAILSA fd;
    ACMSTREAMHEADER hdr;
    HACMDRIVER drv;
    HACMSTREAM stream;
    WAVEFORMATEX *wf = (WAVEFORMATEX *)adpcm_fmt;
    MMRESULT r;

    acmDriverEnum(driver_cb, 0, 0);
    if (!found) {
        printf("FAIL no ACM driver named 'Microsoft ADPCM'\n");
        return 1;
    }
    if (acmDriverOpen(&drv, found, 0) != MMSYSERR_NOERROR) {
        printf("FAIL cannot open the driver\n");
        return 1;
    }

    memset(adpcm_fmt, 0, sizeof(adpcm_fmt));
    wf->wFormatTag = WAVE_FORMAT_ADPCM;
    memset(&fd, 0, sizeof(fd));
    fd.cbStruct = sizeof(fd);
    fd.dwFormatTag = WAVE_FORMAT_ADPCM;
    fd.pwfx = wf;
    fd.cbwfx = sizeof(adpcm_fmt);
    r = acmFormatEnumA(drv, &fd, format_cb, 0, ACM_FORMATENUMF_WFORMATTAG);
    if (r != MMSYSERR_NOERROR || wf->wFormatTag != WAVE_FORMAT_ADPCM || !wf->nBlockAlign) {
        printf("FAIL no ADPCM format (%u)\n", r);
        return 1;
    }
    printf("format %lu Hz, %u channel(s), block %u bytes\n", (unsigned long)wf->nSamplesPerSec,
           wf->nChannels, wf->nBlockAlign);

    memset(&pcm, 0, sizeof(pcm));
    pcm.wFormatTag = WAVE_FORMAT_PCM;
    r = acmFormatSuggest(NULL, wf, &pcm, sizeof(pcm), ACM_FORMATSUGGESTF_WFORMATTAG);
    if (r == MMSYSERR_NOERROR) r = acmStreamOpen(&stream, NULL, wf, &pcm, NULL, 0, 0, 0);
    if (r != MMSYSERR_NOERROR) {
        printf("FAIL cannot open an ADPCM to PCM stream (%u)\n", r);
        return 1;
    }

    /* An all-zero block is valid: predictor 0, silence. */
    memset(&hdr, 0, sizeof(hdr));
    hdr.cbStruct = sizeof(hdr);
    hdr.pbSrc = adpcm_buf;
    hdr.cbSrcLength = wf->nBlockAlign <= sizeof(adpcm_buf) ? wf->nBlockAlign : sizeof(adpcm_buf);
    hdr.pbDst = pcm_buf;
    hdr.cbDstLength = sizeof(pcm_buf);
    r = acmStreamPrepareHeader(stream, &hdr, 0);
    if (r == MMSYSERR_NOERROR) r = acmStreamConvert(stream, &hdr, ACM_STREAMCONVERTF_BLOCKALIGN);
    if (r != MMSYSERR_NOERROR || !hdr.cbDstLengthUsed) {
        printf("FAIL decoding a block (%u, %lu bytes out)\n", r, (unsigned long)hdr.cbDstLengthUsed);
        return 1;
    }
    printf("decoded %lu bytes into %lu bytes of PCM\n", (unsigned long)hdr.cbSrcLengthUsed,
           (unsigned long)hdr.cbDstLengthUsed);
    acmStreamUnprepareHeader(stream, &hdr, 0);
    acmStreamClose(stream, 0);
    acmDriverClose(drv, 0);
    printf("PASS MS ADPCM codec found by its Windows name and decodes\n");
    return 0;
}
