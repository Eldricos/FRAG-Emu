/* SPDX-License-Identifier: GPL-3.0-or-later
 * Like an installer whose small first program starts the real one and exits:
 * "spawn.exe" starts "spawn.exe child" and returns at once; the child waits 3 s,
 * then writes done.txt next to itself. frag must wait for the child, and still
 * return promptly once it has finished (spawn_test.sh checks both).
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char path[MAX_PATH], cmd[MAX_PATH + 16];
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    FILE *f;

    GetModuleFileNameA(NULL, path, sizeof(path));
    if (argc > 1 && !strcmp(argv[1], "child")) {
        Sleep(3000);
        strcpy(strrchr(path, '\\') + 1, "done.txt");
        if (!(f = fopen(path, "w"))) return 1;
        fputs("done\n", f);
        fclose(f);
        return 0;
    }
    snprintf(cmd, sizeof(cmd), "\"%s\" child", path);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 1;
    return 0;
}
