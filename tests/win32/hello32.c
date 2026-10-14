/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Win32 smoke test. Run through frag, it checks that:
 *   - the prefix reports Windows 98 (GetVersionEx),
 *   - the program sees itself and its working directory on the expected drive,
 *   - a window can be created and a message loop runs to completion.
 * Usage: hello32.exe <expected drive letter>
 * Exit code is the number of failed checks.
 */
#include <windows.h>
#include <stdio.h>
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
    char buf[MAX_PATH];
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
