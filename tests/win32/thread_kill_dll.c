/* SPDX-License-Identifier: GPL-3.0-or-later
 * For thread_kill.exe: a DLL whose thread notifications take 300 ms, so a thread
 * that is starting or exiting holds the loader lock long enough to be killed
 * there (in Wine, dozens of DLLs make this window long for real games).
 */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_THREAD_ATTACH || reason == DLL_THREAD_DETACH) Sleep(300);
    return TRUE;
}
