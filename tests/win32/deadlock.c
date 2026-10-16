/* SPDX-License-Identifier: GPL-3.0-or-later
 * Freezes for good on the loader lock (via deadlock_dll.dll), like NFS III did.
 * Wine then ignores Ctrl+C, so FRAG must stop the game itself; checked by
 * interrupt_test.sh.
 */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    printf("deadlocking\n");
    fflush(stdout);
    LoadLibraryA("DEADLOCK_DLL.DLL");
    printf("FAIL: did not deadlock\n");
    return 1;
}
