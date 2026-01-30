/*
 * Palmweaver - Settings persistence (registry)
 */

#include <windows.h>
#include "resource.h"

#define REG_KEY L"Software\\IntermountainSystems\\Palmweaver"

/* Settings - declared extern in palmweaver.c */
extern int g_bWordWrap;
extern int g_bShowLineNums;

/*
 * LoadSettings - Load settings from registry
 */
void LoadSettings(void)
{
    HKEY hKey;
    DWORD val, size, type;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, 0, &hKey) != ERROR_SUCCESS)
        return;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"WordWrap", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bWordWrap = (int)val;

    size = sizeof(DWORD);
    if (RegQueryValueExW(hKey, L"ShowLineNums", NULL, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD)
        g_bShowLineNums = (int)val;

    RegCloseKey(hKey);
}

/*
 * SaveSettings - Save settings to registry
 */
void SaveSettings(void)
{
    HKEY hKey;
    DWORD disp, val;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, 0, NULL, &hKey, &disp) != ERROR_SUCCESS)
        return;

    val = (DWORD)g_bWordWrap;
    RegSetValueExW(hKey, L"WordWrap", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    val = (DWORD)g_bShowLineNums;
    RegSetValueExW(hKey, L"ShowLineNums", 0, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    RegCloseKey(hKey);
}
