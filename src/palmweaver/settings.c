/*
 * Palmweaver - Settings persistence (registry)
 */

#include <windows.h>
#include "resource.h"

#define REG_KEY L"Software\\IntermountainSystems\\Palmweaver"

/* Settings - declared extern in palmweaver.c */
extern int g_bWordWrap;
extern int g_bShowLineNums;
extern int g_bUseTabs;
extern int g_nTabSize;
extern wchar_t g_recentFiles[MAX_RECENT_FILES][MAX_PATH];
extern int g_recentCount;

/*
 * LoadSettings - Load settings from registry
 */
void LoadSettings(void)
{
    HKEY hKey;
    DWORD val, size, type;
    wchar_t name[16];
    int i;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
        return;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"WordWrap", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bWordWrap = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"ShowLineNums", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bShowLineNums = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"UseTabs", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bUseTabs = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"TabSize", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_nTabSize = (int)val;

    /* Load recent files */
    g_recentCount = 0;
    for (i = 0; i < MAX_RECENT_FILES; i++) {
        wsprintfW(name, L"Recent%d", i);
        size = MAX_PATH * sizeof(wchar_t);
        if (RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)g_recentFiles[i], &size) == ERROR_SUCCESS && type == REG_SZ && g_recentFiles[i][0])
            g_recentCount++;
        else
            break;
    }

    RegCloseKey(hKey);
}

/*
 * SaveSettings - Save settings to registry
 */
void SaveSettings(void)
{
    HKEY hKey;
    DWORD disp, val;
    wchar_t name[16];
    int i;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, 0, NULL, &hKey, &disp) != ERROR_SUCCESS)
        return;

    val = (DWORD)g_bWordWrap;
    RegSetValueExW(hKey, L"WordWrap", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bShowLineNums;
    RegSetValueExW(hKey, L"ShowLineNums", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bUseTabs;
    RegSetValueExW(hKey, L"UseTabs", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_nTabSize;
    RegSetValueExW(hKey, L"TabSize", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    /* Save recent files */
    for (i = 0; i < MAX_RECENT_FILES; i++) {
        wsprintfW(name, L"Recent%d", i);
        if (i < g_recentCount && g_recentFiles[i][0])
            RegSetValueExW(hKey, name, 0, REG_SZ, (LPBYTE)g_recentFiles[i], (lstrlenW(g_recentFiles[i]) + 1) * sizeof(wchar_t));
        else
            RegDeleteValueW(hKey, name);
    }

    RegCloseKey(hKey);
}

/*
 * ClearSettings - Delete all registry settings
 */
void ClearSettings(void)
{
    HKEY hKey;
    wchar_t name[256];
    DWORD nameLen;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, 0, &hKey) == ERROR_SUCCESS) {
        while (1) {
            nameLen = 256;
            if (RegEnumValueW(hKey, 0, name, &nameLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;
            RegDeleteValueW(hKey, name);
        }
        RegCloseKey(hKey);
    }
    RegDeleteKeyW(HKEY_CURRENT_USER, REG_KEY);
}
