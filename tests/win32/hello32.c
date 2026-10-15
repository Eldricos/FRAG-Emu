/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Win32 smoke test. Run through frag, it checks that:
 *   - the prefix reports Windows 98 (GetVersionEx),
 *   - the program sees itself and its working directory on the expected drive,
 *   - a window can be created and a message loop runs to completion.
 *   - the host filesystem drive Z: is hidden,
 *   - optionally, that its drive is a CD-ROM with the expected volume label,
 *     serial number (hex) and a file at the given path.
 * Usage: hello32.exe <expected drive letter> [<expected CD label> [<serial> [<file>]]]
 * Exit code is the number of failed checks.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int got_create, got_timer;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        got_create = 1;
        SetTimer(hwnd, 1, 10, NULL);
        return 0;
    case WM_TIMER:
        got_timer = 1;
        KillTimer(hwnd, 1);
        PostMessageA(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(int argc, char **argv)
{
    char expected = argc > 1 ? argv[1][0] : 'D';
    const char *cd_label = argc > 2 ? argv[2] : NULL;
    const char *cd_serial = argc > 3 ? argv[3] : NULL;
    const char *cd_file = argc > 4 ? argv[4] : NULL;
    DWORD serial;
    char buf[MAX_PATH], root[] = "?:\\", label[64];
    OSVERSIONINFOA ver;
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;

    memset(&ver, 0, sizeof(ver));
    ver.dwOSVersionInfoSize = sizeof(ver);
    GetVersionExA(&ver);
    printf("version: %lu.%lu platform %lu\n", ver.dwMajorVersion, ver.dwMinorVersion, ver.dwPlatformId);
    check(ver.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS && ver.dwMajorVersion == 4 &&
              ver.dwMinorVersion == 10,
          "reports Windows 98");

    GetModuleFileNameA(NULL, buf, sizeof(buf));
    printf("module: %s\n", buf);
    check((buf[0] & ~0x20) == (expected & ~0x20) && buf[1] == ':', "exe is on the expected drive");

    GetCurrentDirectoryA(sizeof(buf), buf);
    printf("cwd: %s\n", buf);
    check((buf[0] & ~0x20) == (expected & ~0x20) && buf[1] == ':', "working directory is on the expected drive");

    check(!(GetLogicalDrives() & (1u << ('Z' - 'A'))), "host filesystem drive Z: is hidden");

    if (cd_label) {
        root[0] = expected;
        check(GetDriveTypeA(root) == DRIVE_CDROM, "drive is a CD-ROM");
        label[0] = 0;
        serial = 0;
        GetVolumeInformationA(root, label, sizeof(label), &serial, NULL, NULL, NULL, 0);
        printf("label: %s, serial: %08lx\n", label, serial);
        check(strcmp(label, cd_label) == 0, "CD has the expected volume label");
        if (cd_serial) check(serial == strtoul(cd_serial, NULL, 16), "CD has the expected serial number");
        if (cd_file) {
            printf("file: %s\n", cd_file);
            check(GetFileAttributesA(cd_file) != INVALID_FILE_ATTRIBUTES, "file on the CD is found");
        }
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "FragHello32";
    check(RegisterClassA(&wc) != 0, "RegisterClass");

    hwnd = CreateWindowA("FragHello32", "FRAG hello32", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                         320, 200, NULL, NULL, wc.hInstance, NULL);
    check(hwnd != NULL, "CreateWindow");
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        while (GetMessageA(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    check(got_create, "WM_CREATE delivered");
    check(got_timer, "WM_TIMER delivered");

    printf("%d failure(s)\n", failures);
    return failures;
}
