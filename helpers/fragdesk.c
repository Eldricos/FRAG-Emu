/* SPDX-License-Identifier: GPL-3.0-or-later
 * FRAG helper, run inside the game's prefix before the game when [wine] desktop
 * is set: exits 0 once Wine's virtual desktop "Default" exists with the given
 * size, 1 if not yet.
 *
 * Every Wine program creates the "Default" desktop when it starts if it doesn't
 * exist yet, and a desktop created that way is an ordinary one, the size of the
 * screen. So explorer must create it first, and this helper must not: FRAG's
 * registry settings start it (and start.exe) on a desktop of their own
 * ("FragProbe", via AppDefaults), and it only opens "Default" if it exists.
 * Usage: fragdesk.exe <width>x<height>
 */
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    HDESK desk;
    int w = 0, h = 0;
    if (argc < 2 || sscanf(argv[1], "%dx%d", &w, &h) != 2) return 2;
    desk = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
    if (!desk || !SetThreadDesktop(desk)) return 1;
    return GetSystemMetrics(SM_CXSCREEN) == w && GetSystemMetrics(SM_CYSCREEN) == h ? 0 : 1;
}
