/* SPDX-License-Identifier: GPL-3.0-or-later
 * TerminateThread the way Win9x games use it. Wine kills a thread on the spot,
 * even inside win32u holding the user lock, which then stays locked: the next
 * thread that needs it hangs, and the game with it (NFS III froze on the loader
 * lock this way). FRAG's fragfix.dll makes the kill wait for the thread to leave
 * Wine. Checks:
 *  - 100 kills of a thread busy in user32 leave the user lock free;
 *  - a thread blocked in an endless wait, a suspended one, and one ending itself
 *    all end, with the exit code given;
 *  - killing a thread while it starts or exits (running DLL notifications, with
 *    the loader lock held; NFS III does this) leaves the loader lock free.
 * Exit code 0 means pass.
 */
#include <windows.h>
#include <stdio.h>

static HWND hwnd;
static HANDLE never;

static DWORD WINAPI busy_in_user32(void *arg)
{
    for (;;) {
        IsWindow(hwnd);
        GetWindowLongA(hwnd, GWL_STYLE);
    }
    return 0;
}

static DWORD WINAPI is_window(void *arg) { return IsWindow(hwnd); }

static DWORD WINAPI waits_forever(void *arg) { return WaitForSingleObject(never, INFINITE); }

static DWORD WINAPI ends_itself(void *arg)
{
    TerminateThread(GetCurrentThread(), 5);
    return 1;
}

static DWORD WINAPI returns_at_once(void *arg) { return 0; }

/* A new thread can start and finish, i.e. the loader lock is free. */
static int check_loader_free(const char *what)
{
    HANDLE t = CreateThread(NULL, 0, returns_at_once, NULL, 0, NULL);
    DWORD r = WaitForSingleObject(t, 5000);
    CloseHandle(t);
    if (r != WAIT_OBJECT_0) {
        printf("FAIL %s: loader lock left taken\n", what);
        return 1;
    }
    printf("PASS %s\n", what);
    return 0;
}

static int check_ended(const char *what, HANDLE thread, DWORD want)
{
    DWORD code = 0;
    if (WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0) {
        printf("FAIL %s: thread still running\n", what);
        return 1;
    }
    GetExitCodeThread(thread, &code);
    if (code != want) {
        printf("FAIL %s: exit code %lu, expected %lu\n", what, code, want);
        return 1;
    }
    printf("PASS %s\n", what);
    return 0;
}

int main(void)
{
    HANDLE t;
    int i, failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    hwnd = CreateWindowA("STATIC", "thread_kill", 0, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    never = CreateEventA(NULL, TRUE, FALSE, NULL);

    for (i = 0; i < 100; i++) {
        t = CreateThread(NULL, 0, busy_in_user32, NULL, 0, NULL);
        Sleep(2 + i % 7);
        if (!TerminateThread(t, 3)) {
            printf("FAIL TerminateThread returned FALSE (error %lu)\n", GetLastError());
            return 1;
        }
        if (WaitForSingleObject(t, 5000) != WAIT_OBJECT_0) {
            printf("FAIL busy thread %d still running\n", i + 1);
            return 1;
        }
        CloseHandle(t);
        t = CreateThread(NULL, 0, is_window, NULL, 0, NULL);
        if (WaitForSingleObject(t, 3000) == WAIT_TIMEOUT) {
            printf("FAIL user lock left taken after %d kills of a busy thread\n", i + 1);
            return 1;
        }
        CloseHandle(t);
    }
    printf("PASS 100 kills of a thread busy in user32\n");

    t = CreateThread(NULL, 0, waits_forever, NULL, 0, NULL);
    Sleep(50);
    TerminateThread(t, 42);
    failed += check_ended("kill a waiting thread", t, 42);

    t = CreateThread(NULL, 0, busy_in_user32, NULL, 0, NULL);
    Sleep(20);
    SuspendThread(t);
    TerminateThread(t, 7);
    failed += check_ended("kill a suspended thread", t, 7);

    t = CreateThread(NULL, 0, ends_itself, NULL, 0, NULL);
    failed += check_ended("thread kills itself", t, 5);

    /* Slow thread notifications from here on. */
    if (!LoadLibraryA("THREAD_KILL_DLL.DLL")) {
        printf("FAIL cannot load THREAD_KILL_DLL.DLL\n");
        return 1;
    }
    t = CreateThread(NULL, 0, busy_in_user32, NULL, 0, NULL);
    Sleep(100);
    TerminateThread(t, 9);
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    failed += check_loader_free("kill a starting thread");

    t = CreateThread(NULL, 0, returns_at_once, NULL, 0, NULL);
    Sleep(400); /* past the start, into the exit */
    TerminateThread(t, 9);
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    failed += check_loader_free("kill an exiting thread");

    return failed ? 1 : 0;
}
