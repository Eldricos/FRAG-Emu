/* SPDX-License-Identifier: GPL-3.0-or-later
 * A DLL that deadlocks on the loader lock when loaded: DllMain starts a thread and
 * waits for it, but the thread can't run its DLL_THREAD_ATTACH until DllMain
 * returns. The same frozen state as NFS III's (see deadlock.c).
 */
#include <windows.h>

static DWORD WINAPI worker(void *arg) { return 0; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        WaitForSingleObject(t, INFINITE);
    }
    return TRUE;
}
