/* SPDX-License-Identifier: GPL-3.0-or-later
 * FRAG's in-process fixes, loaded into a game by fragstart.exe before the game's
 * own code runs.
 *
 * TerminateThread: Windows ends a thread blocked in a wait at once, but one
 * running kernel code only when it gets back to user mode, so it never dies
 * holding a kernel lock. Wine kills it on the spot, even inside win32u with the
 * user lock held; that lock then stays taken and the next thread to need it
 * hangs. The replacement here (NFS III froze at "RtlpWaitForCriticalSection ...
 * loader_section" without it):
 *  - waits while the thread holds the loader lock: games kill threads that are
 *    starting or exiting, i.e. running DLL notifications under that lock, which
 *    takes long in Wine (dozens of DLLs) and would leave the lock taken forever;
 *    an exiting thread usually just finishes. Sent messages are handled while
 *    waiting, in case the thread waits for this one;
 *  - kills a thread blocked in a wait system call right away, like Windows;
 *  - otherwise points the suspended thread at a stub in user mode and kills it
 *    once it is there. Wine applies a new context to a thread inside a system
 *    call only when the call returns, i.e. after it has released Wine's locks.
 *    A thread that doesn't get there within 100 ms is killed where it is.
 */
#include <windows.h>

ULONG WINAPI RtlNtStatusToDosError(LONG status);

typedef LONG (WINAPI *nt_terminate_thread_fn)(HANDLE, LONG);
nt_terminate_thread_fn frag_nt_terminate_thread;

static BOOL log_calls;

/* System calls that wait for something; a thread inside one holds no lock. */
static const struct { const char *dll, *name; } wait_calls[] = {
    {"ntdll.dll", "NtWaitForSingleObject"},
    {"ntdll.dll", "NtWaitForMultipleObjects"},
    {"ntdll.dll", "NtDelayExecution"},
    {"ntdll.dll", "NtSignalAndWaitForSingleObject"},
    {"ntdll.dll", "NtWaitForKeyedEvent"},
    {"ntdll.dll", "NtWaitForAlertByThreadId"},
    {"ntdll.dll", "NtRemoveIoCompletion"},
    {"ntdll.dll", "NtRemoveIoCompletionEx"},
    {"win32u.dll", "NtUserMsgWaitForMultipleObjectsEx"},
    {"win32u.dll", "NtUserGetMessage"},
    {"win32u.dll", "NtUserWaitMessage"},
    {"win32u.dll", "NtUserWaitForInputIdle"},
};
static ULONG_PTR wait_call_addr[ARRAYSIZE(wait_calls)];

static void log_line(const char *text)
{
    DWORD written;
    if (log_calls) WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, lstrlenA(text), &written, NULL);
}

static void log_kill(DWORD tid, const char *how)
{
    char line[160];
    wsprintfA(line, "fragfix: TerminateThread(%04lx): %s\n", tid, how);
    log_line(line);
}

/* Where a doomed thread resumes: it sets the flag EBX points to and spins in
 * user mode, where it holds none of Wine's locks, until it is killed there. (It
 * doesn't end itself: threads that did left Wine 9's thread list so that the next
 * DLL with TLS data loaded by the game looped forever.) */
void frag_park_stub(void);
__asm__(".text\n"
        ".globl _frag_park_stub\n"
        "_frag_park_stub:\n\t"
        "lock incl (%ebx)\n"
        "1:\tpause\n\t"
        "jmp 1b\n");

/* The PEB's LoaderLock (offset 0xa0 in a 32-bit PEB). */
static RTL_CRITICAL_SECTION *loader_lock(void)
{
    char *peb;
    __asm__("movl %%fs:0x30, %0" : "=r"(peb));
    return *(RTL_CRITICAL_SECTION **)(peb + 0xa0);
}

/* TRUE if thread tid owns the loader lock, or may be in the middle of taking it
 * (counted in but no owner recorded yet). */
static BOOL loader_lock_held_by(DWORD tid)
{
    RTL_CRITICAL_SECTION *cs = loader_lock();
    DWORD owner = (DWORD)(ULONG_PTR)cs->OwningThread;
    return owner == tid || (!owner && cs->LockCount != -1);
}

/* The context of a thread suspended inside a system call has EIP just after the
 * call in the function's stub. */
static BOOL in_wait_call(ULONG_PTR eip)
{
    int i;
    for (i = 0; i < ARRAYSIZE(wait_calls); i++)
        if (wait_call_addr[i] && eip - wait_call_addr[i] < 32) return TRUE;
    return FALSE;
}

static BOOL kill_now(HANDLE handle, DWORD code)
{
    LONG status = frag_nt_terminate_thread(handle, code);
    if (status) SetLastError(RtlNtStatusToDosError(status));
    return !status;
}

static BOOL WINAPI safe_terminate_thread(HANDLE handle, DWORD code)
{
    DWORD tid = GetThreadId(handle), waited;
    BOOL caller_holds_lock = loader_lock_held_by(GetCurrentThreadId());
    volatile LONG *parked;
    HANDLE thread;
    CONTEXT ctx;
    MSG msg;

    if (!tid || tid == GetCurrentThreadId()) return kill_now(handle, code);
    thread = OpenThread(THREAD_ALL_ACCESS, FALSE, tid);
    if (!thread) return kill_now(handle, code);
    if (WaitForSingleObject(thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(thread);
        return TRUE;
    }
    /* Watched without suspending: suspending interrupts the thread's own waits,
     * and Wine restarts a Sleep with its full time, so polling would stall it.
     * The check is repeated with the thread suspended before it is moved. */
    for (waited = 0;;) {
        if (!caller_holds_lock && waited < 3000 && loader_lock_held_by(tid)) {
            if (MsgWaitForMultipleObjects(1, &thread, FALSE, 1, QS_SENDMESSAGE) == WAIT_OBJECT_0) {
                log_kill(tid, "ended by itself while waiting for the loader lock");
                CloseHandle(thread);
                return TRUE;
            }
            PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
            waited++;
            continue;
        }
        if (SuspendThread(thread) == (DWORD)-1) {
            CloseHandle(thread);
            return kill_now(handle, code);
        }
        if (caller_holds_lock || waited >= 3000 || !loader_lock_held_by(tid)) break;
        ResumeThread(thread);
    }
    if (waited >= 3000) log_kill(tid, "still holds the loader lock after 3 s");
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (!GetThreadContext(thread, &ctx) || in_wait_call(ctx.Eip)) {
        /* Blocked in a wait: like Windows, end it there. Moving it would make it
         * die after the wait returns, e.g. owning a critical section it was
         * granted, which then stays taken. */
        log_kill(tid, "ended in a wait");
        CloseHandle(thread);
        return kill_now(handle, code);
    }
    parked = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*parked));
    ctx.Eip = (DWORD)frag_park_stub;
    ctx.Ebx = (DWORD)parked;
    SetThreadContext(thread, &ctx);
    /* Also undo the game's own suspensions, so the thread can reach the stub. */
    while (ResumeThread(thread) > 1) {}
    for (waited = 0; waited < 100 && !*parked; waited++) Sleep(1);
    /* If it never got there the flag may still be written later, so it is kept. */
    if (*parked) {
        log_kill(tid, "ended safely");
        HeapFree(GetProcessHeap(), 0, (void *)parked);
    } else
        log_kill(tid, "thread busy in Wine, killing it now");
    CloseHandle(thread);
    return kill_now(handle, code);
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
        {
            int i;
            for (i = 0; i < ARRAYSIZE(wait_calls); i++) {
                HMODULE mod = GetModuleHandleA(wait_calls[i].dll);
                if (mod) wait_call_addr[i] = (ULONG_PTR)GetProcAddress(mod, wait_calls[i].name);
            }
        }
        /* kernel32's export may be a forwarder to kernelbase, or a thunk; cover both. */
        if (kb) redirect(kb, safe_terminate_thread);
        if (k32 && k32 != kb) redirect(k32, safe_terminate_thread);
        log_line("fragfix: loaded\n");
    }
    return TRUE;
}
