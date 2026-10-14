/* SPDX-License-Identifier: GPL-3.0-or-later
 * Stand-in for a 1990s video codec DLL (built as IR50_32.DLL): a driver entry
 * point plus the self-registration export that installers call. */
#include <windows.h>

__declspec(dllexport) HRESULT WINAPI DllRegisterServer(void) { return S_OK; }

__declspec(dllexport) LRESULT WINAPI DriverProc(DWORD_PTR id, HDRVR drv, UINT msg, LPARAM p1, LPARAM p2)
{
    (void)id; (void)drv; (void)msg; (void)p1; (void)p2;
    return 0;
}
