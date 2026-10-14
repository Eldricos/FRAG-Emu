/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Mimics what InstallShield does with a bundled video codec: copy it into the
 * system directory, load it, and call DllRegisterServer. Under Wine's default
 * load order the builtin ir50_32 is loaded instead, which has no
 * DllRegisterServer, and installers fail with error -119. FRAG prefers the
 * game's codec, so this must pass.
 * Exit code is the number of failed checks.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

int main(void)
{
    char dst[MAX_PATH];
    HMODULE mod;
    HRESULT (WINAPI *reg)(void) = NULL;

    GetSystemDirectoryA(dst, sizeof(dst));
    strcat(dst, "\\ir50_32.dll");

    check(CopyFileA("D:\\IR50_32.DLL", dst, FALSE), "copy codec into the system directory");
    mod = LoadLibraryA(dst);
    check(mod != NULL, "load the installed codec");
    if (mod) reg = (HRESULT (WINAPI *)(void))GetProcAddress(mod, "DllRegisterServer");
    check(reg != NULL, "codec exports DllRegisterServer (the game's copy was loaded)");
    if (reg) check(reg() == S_OK, "DllRegisterServer succeeds");

    printf("%d failure(s)\n", failures);
    return failures;
}
