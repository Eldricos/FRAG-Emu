/* SPDX-License-Identifier: GPL-3.0-or-later
 * FRAG's in-process fixes, loaded into a game by fragstart.exe before the game's
 * own code runs.
 *
 * TerminateThread: on Windows, killing a thread waits until it is back in user
 * mode, so it never dies holding a kernel lock. Wine kills it on the spot, even
 * inside win32u with the user lock held; that lock then stays taken forever and
 * the next thread to need it hangs. When that thread is one exiting (imm32's
 * THREAD_DETACH needs the user lock) it also holds the loader lock, and the
 * whole game freezes ("RtlpWaitForCriticalSection ... loader_section", NFS III).
 * The replacement here suspends the thread and points it at a stub that ends it;
 * Wine applies a new context to a thread inside a system call only when the call
 * returns, i.e. after it has released Wine's locks. A thread that doesn't get
 * there within 100 ms (blocked in a long wait, which holds no such lock) is
 * killed the old way.
 */
#include <windows.h>

ULONG WINAPI RtlNtStatusToDosError(LONG status);

typedef LONG (WINAPI *nt_terminate_thread_fn)(HANDLE, LONG);
nt_terminate_thread_fn frag_nt_terminate_thread;

static BOOL log_calls;

static void log_line(const char *text)
{
    DWORD written;
    if (log_calls) WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, lstrlenA(text), &written, NULL);
}

/* Where a doomed thread resumes: ends itself with the exit code in EBX (not EAX,
 * which a system call that was in progress overwrites with its result). A thread
 * ending itself holds none of Wine's locks, and no DLL notifications are sent,
 * just as with Windows' TerminateThread. */
void frag_exit_stub(void);
__asm__(".text\n"
        ".globl _frag_exit_stub\n"
        "_frag_exit_stub:\n\t"
        "andl $-16, %esp\n\t"
        "subl $8, %esp\n\t"
        "pushl %ebx\n\t"
        "pushl $-2\n\t" /* GetCurrentThread() */
        "call *_frag_nt_terminate_thread\n\t"
        "ud2\n");

static BOOL WINAPI safe_terminate_thread(HANDLE handle, DWORD code)
{
    DWORD tid = GetThreadId(handle);
    HANDLE thread;
    CONTEXT ctx;
    char line[128];

    if (!tid || tid == GetCurrentThreadId()) {
        LONG status = frag_nt_terminate_thread(handle, code);
        if (status) SetLastError(RtlNtStatusToDosError(status));
        return !status;
    }
    thread = OpenThread(THREAD_ALL_ACCESS, FALSE, tid);
    if (thread && SuspendThread(thread) != (DWORD)-1) {
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(thread, &ctx)) {
            ctx.Eip = (DWORD)frag_exit_stub;
            ctx.Ebx = code;
            SetThreadContext(thread, &ctx);
        }
        /* Also undo the game's own suspensions, so the thread can reach the stub. */
        while (ResumeThread(thread) > 1) {}
        if (WaitForSingleObject(thread, 100) == WAIT_OBJECT_0) {
            wsprintfA(line, "fragfix: TerminateThread(%04lx): ended safely\n", tid);
            log_line(line);
            CloseHandle(thread);
            return TRUE;
        }
    }
    wsprintfA(line, "fragfix: TerminateThread(%04lx): thread busy in Wine, killing it now\n", tid);
    log_line(line);
    if (thread) CloseHandle(thread);
    {
        LONG status = frag_nt_terminate_thread(handle, code);
        if (status) SetLastError(RtlNtStatusToDosError(status));
        return !status;
    }
}

/* Overwrites the start of fn with a jump to replacement. */
static BOOL redirect(void *fn, void *replacement)
{
    unsigned char *p = fn;
    DWORD old;
    if (!p || !VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    p[0] = 0xe9;
    *(DWORD *)(p + 1) = (DWORD)replacement - (DWORD)(p + 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE kernelbase = GetModuleHandleA("kernelbase.dll");
        void *k32 = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "TerminateThread");
        void *kb = kernelbase ? (void *)GetProcAddress(kernelbase, "TerminateThread") : NULL;
        char flag[4];

        DisableThreadLibraryCalls(inst);
        log_calls = GetEnvironmentVariableA("FRAGFIX_LOG", flag, sizeof(flag)) > 0;
        frag_nt_terminate_thread =
            (nt_terminate_thread_fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtTerminateThread");
        if (!frag_nt_terminate_thread) return TRUE;
        /* kernel32's export may be a forwarder to kernelbase, or a thunk; cover both. */
        if (kb) redirect(kb, safe_terminate_thread);
        if (k32 && k32 != kb) redirect(k32, safe_terminate_thread);
        log_line("fragfix: loaded\n");
    }
    return TRUE;
}
