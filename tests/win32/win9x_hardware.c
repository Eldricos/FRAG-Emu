/* SPDX-License-Identifier: GPL-3.0-or-later
 * Finds the display card the way Windows 9x programs do, e.g. Need for Speed III's
 * 3dsetup: each running device under HKEY_DYN_DATA\Config Manager\Enum names its
 * HKLM\Enum key in HardWareKey; that key has Class "Display" and a Driver key under
 * HKLM\System\CurrentControlSet\Services\Class. Exit code 0 means pass.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int get_string(HKEY root, const char *path, const char *value, char *out, DWORD size)
{
    HKEY k;
    DWORD type;
    LONG r;
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    r = RegQueryValueExA(k, value, NULL, &type, (BYTE *)out, &size);
    RegCloseKey(k);
    return r == ERROR_SUCCESS && type == REG_SZ;
}

int main(void)
{
    HKEY devices;
    char id[64], dev[300], hwkey[256], cls[64], driver[128], desc[128];
    DWORD i, n;
    int found = 0;

    if (RegOpenKeyExA(HKEY_DYN_DATA, "Config Manager\\Enum", 0, KEY_READ, &devices) != ERROR_SUCCESS) {
        printf("FAIL cannot open HKEY_DYN_DATA\\Config Manager\\Enum\n");
        return 1;
    }
    for (i = 0;; i++) {
        n = sizeof(id);
        if (RegEnumKeyExA(devices, i, id, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        snprintf(dev, sizeof(dev), "Config Manager\\Enum\\%s", id);
        if (!get_string(HKEY_DYN_DATA, dev, "HardWareKey", hwkey, sizeof(hwkey))) {
            printf("FAIL device %s has no HardWareKey\n", id);
            return 1;
        }
        snprintf(dev, sizeof(dev), "Enum\\%s", hwkey);
        if (!get_string(HKEY_LOCAL_MACHINE, dev, "Class", cls, sizeof(cls))) {
            printf("FAIL HKLM\\%s has no Class\n", dev);
            return 1;
        }
        printf("device %s: %s (%s)\n", id, hwkey, cls);
        if (strcmp(cls, "Display") != 0) continue;
        if (!get_string(HKEY_LOCAL_MACHINE, dev, "Driver", driver, sizeof(driver))) {
            printf("FAIL display has no Driver\n");
            return 1;
        }
        snprintf(dev, sizeof(dev), "System\\CurrentControlSet\\Services\\Class\\%s", driver);
        if (!get_string(HKEY_LOCAL_MACHINE, dev, "DriverDesc", desc, sizeof(desc))) {
            printf("FAIL HKLM\\%s has no DriverDesc\n", dev);
            return 1;
        }
        printf("display driver: %s\n", desc);
        found = 1;
    }
    RegCloseKey(devices);
    if (!found) {
        printf("FAIL no display adapter among the running devices\n");
        return 1;
    }
    printf("PASS display card found through HKEY_DYN_DATA\n");
    return 0;
}
