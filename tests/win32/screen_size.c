/* SPDX-License-Identifier: GPL-3.0-or-later
 * Checks the screen size a game sees, e.g. Wine's virtual desktop from
 * [wine] desktop = 800x600. Usage: screen_size.exe <width>x<height>
 * Exit code 0 means pass.
 */
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN), want_w = 0, want_h = 0;
    if (argc > 1) sscanf(argv[1], "%dx%d", &want_w, &want_h);
    printf("screen: %dx%d\n", w, h);
    if (w != want_w || h != want_h) {
        printf("FAIL screen is not %dx%d\n", want_w, want_h);
        return 1;
    }
    printf("PASS screen is %dx%d\n", w, h);
    return 0;
}
