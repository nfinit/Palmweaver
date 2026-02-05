/*
 * Palmweaver - Settings persistence (registry)
 */

#include <windows.h>
#include "resource.h"

#define REG_KEY L"Software\\IntermountainSystems\\Palmweaver"

/* Flag to skip saving after clear */
static int g_bSettingsCleared = 0;

/* Settings - declared extern in palmweaver.c */
extern int g_bWordWrap;
extern int g_bShowLineNums;
extern int g_bShowStatusBar;
extern int g_bInverseColors;
extern int g_nTheme;
extern int g_bThemedSelection;
extern int g_bShowScrollbars;
extern int g_bHideTaskbar;
extern int g_bUseTabs;
extern int g_nTabSize;
extern int g_nColumnLimit;
extern wchar_t g_recentFiles[MAX_RECENT_FILES][MAX_PATH];
extern int g_recentCount;

/* File picker last directory - defined in filepicker.c */
void FilePickerGetLastDir(wchar_t *buf, int maxLen);
void FilePickerSetLastDir(const wchar_t *dir);

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
    if (RegQueryValueExW(hKey, L"ShowStatusBar", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bShowStatusBar = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"InverseColors", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bInverseColors = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"Theme", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_nTheme = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"ThemedSelection", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bThemedSelection = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"ShowScrollbars", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bShowScrollbars = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"HideTaskbar", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bHideTaskbar = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"UseTabs", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bUseTabs = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"TabSize", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_nTabSize = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"ColumnLimit", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_nColumnLimit = (int)val;

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

    /* Load last directory */
    {
        wchar_t lastDir[MAX_PATH];
        size = MAX_PATH * sizeof(wchar_t);
        if (RegQueryValueExW(hKey, L"LastDir", NULL, &type, (LPBYTE)lastDir, &size) == ERROR_SUCCESS && type == REG_SZ && lastDir[0])
            FilePickerSetLastDir(lastDir);
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

    /* Don't save if settings were cleared this session */
    if (g_bSettingsCleared)
        return;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, 0, NULL, &hKey, &disp) != ERROR_SUCCESS)
        return;

    val = (DWORD)g_bWordWrap;
    RegSetValueExW(hKey, L"WordWrap", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bShowLineNums;
    RegSetValueExW(hKey, L"ShowLineNums", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bShowStatusBar;
    RegSetValueExW(hKey, L"ShowStatusBar", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bInverseColors;
    RegSetValueExW(hKey, L"InverseColors", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_nTheme;
    RegSetValueExW(hKey, L"Theme", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bThemedSelection;
    RegSetValueExW(hKey, L"ThemedSelection", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bShowScrollbars;
    RegSetValueExW(hKey, L"ShowScrollbars", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bHideTaskbar;
    RegSetValueExW(hKey, L"HideTaskbar", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bUseTabs;
    RegSetValueExW(hKey, L"UseTabs", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_nTabSize;
    RegSetValueExW(hKey, L"TabSize", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_nColumnLimit;
    RegSetValueExW(hKey, L"ColumnLimit", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    /* Save recent files */
    for (i = 0; i < MAX_RECENT_FILES; i++) {
        wsprintfW(name, L"Recent%d", i);
        if (i < g_recentCount && g_recentFiles[i][0])
            RegSetValueExW(hKey, name, 0, REG_SZ, (LPBYTE)g_recentFiles[i], (lstrlenW(g_recentFiles[i]) + 1) * sizeof(wchar_t));
        else
            RegDeleteValueW(hKey, name);
    }

    /* Save last directory */
    {
        wchar_t lastDir[MAX_PATH];
        FilePickerGetLastDir(lastDir, MAX_PATH);
        if (lastDir[0])
            RegSetValueExW(hKey, L"LastDir", 0, REG_SZ, (LPBYTE)lastDir, (lstrlenW(lastDir) + 1) * sizeof(wchar_t));
    }

    RegCloseKey(hKey);
}

/*
 * ClearSettings - Delete all registry settings and reset globals to defaults
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

    /* Reset globals to defaults */
    g_bWordWrap = 1;
    g_bShowLineNums = 1;
    g_bShowStatusBar = 1;
    g_bInverseColors = 0;
    g_nTheme = 0;
    g_bThemedSelection = 0;
    g_bShowScrollbars = 1;
    g_bHideTaskbar = 0;
    g_bUseTabs = 1;
    g_nTabSize = 4;
    g_nColumnLimit = 80;
    g_recentCount = 0;

    /* Prevent SaveSettings from writing on exit */
    g_bSettingsCleared = 1;
}
