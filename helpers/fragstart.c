/* SPDX-License-Identifier: GPL-3.0-or-later
 * FRAG helper that starts the game: runs the rest of its command line as a
 * process, with fragfix.dll loaded into it before the game's own code runs, and
 * exits with the game's exit code.
 *
 * The game is created suspended and its first thread gets an APC that loads
 * fragfix.dll. Wine's process start-up ends in NtContinue(alertable), so the APC
 * runs once the game's DLLs are initialised and before its entry point. The APC
 * function is LoadLibraryA at this process's address: Wine maps its builtin DLLs
 * at the same address in every process. 16-bit programs are started as they are.
 * The game must see the memory it would without FRAG: Oregon Trail II crashed
 * when the DLL name sat in a page just below its exe. So the name goes at the top
 * of the address space and is freed once fragfix.dll reports it is loaded, and
 * fragfix.dll is built without a C runtime (nothing allocated at load time).
 * Usage: fragstart.exe <program> [arguments...]
 */
#include <windows.h>
#include <stdio.h>

/* The command line after this program's own name, quoting kept as is. */
static WCHAR *rest_of_command_line(void)
{
    WCHAR *p = GetCommandLineW();
    BOOL quoted = FALSE;
    for (; *p && (quoted || (*p != ' ' && *p != '\t')); p++)
        if (*p == '"') quoted = !quoted;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* TRUE for a 32-bit Windows program (PE, i386); only those get fragfix.dll. */
static BOOL is_win32_exe(const char *path)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    DWORD got;
    BOOL ok = FALSE;
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    if (ReadFile(f, &dos, sizeof(dos), &got, NULL) && got == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(f, &nt, sizeof(nt), &got, NULL) && got == sizeof(nt))
        ok = nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386;
    CloseHandle(f);
    return ok;
}

int main(int argc, char **argv)
{
    static const char dll[] = "fragfix.dll";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    BOOL inject;
    void *remote;

    if (argc < 2) {
        fprintf(stderr, "usage: fragstart.exe <program> [arguments...]\n");
        return 2;
    }
    inject = is_win32_exe(argv[1]) && !GetEnvironmentVariableA("FRAGFIX_OFF", NULL, 0);
    if (!CreateProcessW(NULL, rest_of_command_line(), NULL, NULL, TRUE, inject ? CREATE_SUSPENDED : 0, NULL,
                        NULL, &si, &pi)) {
        fprintf(stderr, "fragstart: cannot start %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    if (inject) {
        char event_name[64];
        HANDLE loaded;
        wsprintfA(event_name, "fragfix-loaded-%lu", pi.dwProcessId);
        loaded = CreateEventA(NULL, TRUE, FALSE, event_name);
        remote = VirtualAllocEx(pi.hProcess, NULL, sizeof(dll), MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN,
                                PAGE_READWRITE);
        if (!remote || !WriteProcessMemory(pi.hProcess, remote, dll, sizeof(dll), NULL) ||
            !QueueUserAPC((PAPCFUNC)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"),
                          pi.hThread, (ULONG_PTR)remote))
            fprintf(stderr, "fragstart: could not load %s into the game (error %lu)\n", dll, GetLastError());
        ResumeThread(pi.hThread);
        if (remote && loaded && WaitForSingleObject(loaded, 10000) == WAIT_OBJECT_0)
            VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
        if (loaded) CloseHandle(loaded);
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    return code;
}
