/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Win16 smoke test, built with Open Watcom. Run through frag, it checks that a
 * 16-bit program loads, creates a window and runs its message loop to completion.
 * There is no console for Win16 programs, so results are written to HELLO16.LOG
 * next to the exe. Exit code is the number of failed checks.
 */
#include <windows.h>
#include <string.h>

static int failures;
static int got_create, got_timer;
static HFILE log_file = HFILE_ERROR;

static void check(int ok, const char *what)
{
    if (log_file != HFILE_ERROR) {
        _lwrite(log_file, ok ? "PASS: " : "FAIL: ", 6);
        _lwrite(log_file, what, strlen(what));
        _lwrite(log_file, "\r\n", 2);
    }
    if (!ok) failures++;
}

LRESULT CALLBACK __export wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        got_create = 1;
        SetTimer(hwnd, 1, 10, NULL);
        return 0;
    case WM_TIMER:
        got_timer = 1;
        KillTimer(hwnd, 1);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASS wc;
    HWND hwnd;
    MSG msg;
    DWORD ver = GetVersion();

    (void)cmdline;
    (void)show;
    log_file = _lcreat("HELLO16.LOG", 0);

    /* Win16 GetVersion: low byte major, next byte minor. Windows 95/98 report 3.95. */
    check(LOBYTE(LOWORD(ver)) == 3 && HIBYTE(LOWORD(ver)) >= 10, "reports Windows 3.1 or later");

    if (!prev) {
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = wndproc;
        wc.hInstance = inst;
        wc.lpszClassName = "FragHello16";
        check(RegisterClass(&wc) != 0, "RegisterClass");
    }

    hwnd = CreateWindow("FragHello16", "FRAG hello16", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                        320, 200, NULL, NULL, inst, NULL);
    check(hwnd != NULL, "CreateWindow");
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    check(got_create, "WM_CREATE delivered");
    check(got_timer, "WM_TIMER delivered");

    if (log_file != HFILE_ERROR) _lclose(log_file);
    return failures;
}
