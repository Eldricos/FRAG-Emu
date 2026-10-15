/* SPDX-License-Identifier: GPL-3.0-or-later
 * Checks the video memory DirectDraw reports, and that a 640x480 primary surface
 * can be created, the way a 1990s game starts. Wine takes the amount from the
 * OpenGL driver unless FRAG's [wine] video_memory overrides it; VMware's driver
 * says 1 MB, which made LEGO Racers fail with DDERR_OUTOFVIDEOMEMORY.
 * Usage: vidmem.exe <expected MB>. Exit code 0 means pass.
 * DirectDraw reports the total less the memory the screen itself uses.
 */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    unsigned long want = argc > 1 ? strtoul(argv[1], NULL, 10) : 0;
    IDirectDraw7 *dd = NULL;
    IDirectDrawSurface7 *primary = NULL;
    DDSCAPS2 caps;
    DDSURFACEDESC2 desc;
    DWORD total = 0, free_mem = 0;
    HRESULT hr;
    HWND hwnd;

    hr = DirectDrawCreateEx(NULL, (void **)&dd, &IID_IDirectDraw7, NULL);
    if (FAILED(hr)) {
        printf("FAIL DirectDrawCreateEx: %08lx\n", (unsigned long)hr);
        return 1;
    }
    memset(&caps, 0, sizeof(caps));
    caps.dwCaps = DDSCAPS_VIDEOMEMORY;
    hr = IDirectDraw7_GetAvailableVidMem(dd, &caps, &total, &free_mem);
    printf("video memory: %lu MB total, %lu MB free (hr %08lx)\n", total >> 20, free_mem >> 20, (unsigned long)hr);
    /* Wine counts the screen's own image as used: up to 32 MB for 4K. */
    if (FAILED(hr) || (total >> 20) > want || (total >> 20) + 32 < want) {
        printf("FAIL expected %lu MB, less the screen\n", want);
        return 1;
    }

    hwnd = CreateWindowA("STATIC", "vidmem", WS_POPUP, 0, 0, 640, 480, NULL, NULL, NULL, NULL);
    IDirectDraw7_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = IDirectDraw7_CreateSurface(dd, &desc, &primary, NULL);
    if (FAILED(hr)) {
        printf("FAIL creating the primary surface: %08lx\n", (unsigned long)hr);
        return 1;
    }
    IDirectDrawSurface7_Release(primary);
    IDirectDraw7_Release(dd);
    printf("PASS %lu MB of video memory (%lu MB after the screen), primary surface created\n", want,
           total >> 20);
    return 0;
}
