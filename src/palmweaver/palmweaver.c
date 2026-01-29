/*
 * Palmweaver - Text Editor for Windows CE
 * palmweaver.c - Main application
 *
 * Build 0.1.0.4 - Add File Open/Save/Save As
 */

#include <windows.h>
#include <commctrl.h>
#include "resource.h"

/* Global instance handle (CE has no GetModuleHandle) */
HINSTANCE g_hInst;
HWND g_hwndMain;
HWND g_hwndCB;
HWND g_hwndEdit;

/* Current file state */
static wchar_t g_szFilePath[MAX_PATH];
static int g_bDirty = 0;

/* Window class name */
static const WCHAR g_szClassName[] = L"PalmweaverMain";
static const WCHAR g_szAppTitle[] = L"Palmweaver";
static const WCHAR g_szUntitled[] = L"Untitled";

/* File filter for picker */
static const WCHAR g_szFilter[] = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";

/* Forward declarations */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static BOOL InitApplication(HINSTANCE hInstance);
static BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
static void CreateMenuBar(HWND hwndCB);
static void OnSize(HWND hwnd, int cx, int cy);
static void ShowAboutDialog(HWND hwndParent);
static void UpdateTitle(void);
static void DoFileNew(void);
static int DoFileOpen(void);
static int DoFileSave(void);
static int DoFileSaveAs(void);
static int PromptSave(void);

/* External: file picker */
int FilePicker(HWND hwndOwner, wchar_t *filePath, int maxPath,
               const wchar_t *title, const wchar_t *filter,
               const wchar_t *defExt, const wchar_t *initialDir,
               int saveMode);

/*
 * WinMain - Application entry point
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPWSTR lpCmdLine, int nCmdShow)
{
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;

    if (!InitApplication(hInstance)) {
        return 1;
    }

    if (!InitInstance(hInstance, nCmdShow)) {
        return 1;
    }

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}

/*
 * InitApplication - Register window class
 */
static BOOL InitApplication(HINSTANCE hInstance)
{
    WNDCLASSW wc;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = g_szClassName;

    return RegisterClassW(&wc) != 0;
}

/*
 * InitInstance - Create main window
 */
static BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    g_hInst = hInstance;

    g_hwndMain = CreateWindowW(
        g_szClassName,
        g_szAppTitle,
        WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL);

    if (!g_hwndMain) {
        return FALSE;
    }

    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);

    return TRUE;
}

/*
 * CreateMenuBar - Build menu structure on CommandBar
 */
static void CreateMenuBar(HWND hwndCB)
{
    HMENU hMenu;
    HMENU hMenuFile;
    HMENU hMenuEdit;
    HMENU hMenuHelp;

    hMenu = CreateMenu();
    hMenuFile = CreatePopupMenu();
    hMenuEdit = CreatePopupMenu();
    hMenuHelp = CreatePopupMenu();

    /* File menu */
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_NEW, L"&New");
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_OPEN, L"&Open...");
    AppendMenuW(hMenuFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_SAVE, L"&Save");
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_SAVEAS, L"Save &As...");
    AppendMenuW(hMenuFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_EXIT, L"E&xit");

    /* Edit menu */
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_UNDO, L"&Undo");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_CUT, L"Cu&t");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_COPY, L"&Copy");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_PASTE, L"&Paste");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_SELECTALL, L"Select &All");

    /* Help menu */
    AppendMenuW(hMenuHelp, MF_STRING, IDM_HELP_ABOUT, L"&About...");

    /* Attach to menu bar */
    AppendMenuW(hMenu, MF_POPUP, (UINT)hMenuFile, L"&File");
    AppendMenuW(hMenu, MF_POPUP, (UINT)hMenuEdit, L"&Edit");
    AppendMenuW(hMenu, MF_POPUP, (UINT)hMenuHelp, L"&Help");

    CommandBar_InsertMenubarEx(hwndCB, NULL, (LPTSTR)hMenu, 0);
}

/*
 * OnSize - Handle window resize
 */
static void OnSize(HWND hwnd, int cx, int cy)
{
    int cbHeight;

    (void)hwnd;

    if (!g_hwndCB || !g_hwndEdit) {
        return;
    }

    cbHeight = CommandBar_Height(g_hwndCB);
    MoveWindow(g_hwndEdit, 0, cbHeight, cx, cy - cbHeight, TRUE);
}

/*
 * ShowAboutDialog - Display About message box
 */
static void ShowAboutDialog(HWND hwndParent)
{
    /* Simple message box for now; can upgrade to dialog later */
    MessageBoxW(hwndParent,
        L"Palmweaver " PALMWEAVER_VERSION L"\n\n"
        L"A text editor for Windows CE",
        L"About Palmweaver",
        MB_OK | MB_ICONINFORMATION);
}

/*
 * UpdateTitle - Set window title based on current file
 */
static void UpdateTitle(void)
{
    wchar_t title[MAX_PATH + 32];
    const wchar_t *name;
    const wchar_t *p;

    if (g_szFilePath[0]) {
        /* Extract filename from path */
        name = g_szFilePath;
        p = g_szFilePath;
        while (*p) {
            if (*p == L'\\') name = p + 1;
            p++;
        }
    } else {
        name = g_szUntitled;
    }

    if (g_bDirty) {
        wsprintfW(title, L"%s* - %s", name, g_szAppTitle);
    } else {
        wsprintfW(title, L"%s - %s", name, g_szAppTitle);
    }
    SetWindowTextW(g_hwndMain, title);
}

/*
 * PromptSave - Ask user to save if dirty; returns 1 to proceed, 0 to cancel
 */
static int PromptSave(void)
{
    int result;

    if (!g_bDirty) return 1;

    result = MessageBoxW(g_hwndMain,
        L"Save changes to current file?",
        g_szAppTitle,
        MB_YESNOCANCEL | MB_ICONQUESTION);

    if (result == IDCANCEL) return 0;
    if (result == IDYES) {
        if (!DoFileSave()) return 0;
    }
    return 1;
}

/*
 * DoFileNew - Clear editor for new file
 */
static void DoFileNew(void)
{
    if (!PromptSave()) return;

    SetWindowTextW(g_hwndEdit, L"");
    g_szFilePath[0] = 0;
    g_bDirty = 0;
    UpdateTitle();
}

/*
 * DoFileOpen - Open a text file
 */
static int DoFileOpen(void)
{
    wchar_t szFile[MAX_PATH] = L"";
    HANDLE hFile;
    DWORD dwSize, dwRead;
    char *pBuf;
    wchar_t *pWBuf;
    int i;

    if (!PromptSave()) return 0;

    if (!FilePicker(g_hwndMain, szFile, MAX_PATH,
            L"Open File", g_szFilter, NULL, L"\\My Documents", 0)) {
        return 0;
    }

    hFile = CreateFileW(szFile, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hwndMain, L"Cannot open file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    dwSize = GetFileSize(hFile, NULL);

    /* Allocate buffer for file content + null */
    pBuf = (char *)LocalAlloc(LMEM_FIXED, dwSize + 1);
    if (!pBuf) {
        CloseHandle(hFile);
        MessageBoxW(g_hwndMain, L"Out of memory.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    ReadFile(hFile, pBuf, dwSize, &dwRead, NULL);
    CloseHandle(hFile);
    pBuf[dwRead] = 0;

    /* Check for UTF-16 BOM */
    if (dwRead >= 2 && (unsigned char)pBuf[0] == 0xFF && (unsigned char)pBuf[1] == 0xFE) {
        /* UTF-16 LE - skip BOM, null-terminate */
        wchar_t *pWide = (wchar_t *)(pBuf + 2);
        int nChars = (dwRead - 2) / sizeof(wchar_t);
        pWide[nChars] = 0;
        SetWindowTextW(g_hwndEdit, pWide);
    } else {
        /* Assume ANSI - convert to Unicode */
        pWBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (dwRead + 1) * sizeof(wchar_t));
        if (pWBuf) {
            for (i = 0; i < (int)dwRead; i++) {
                pWBuf[i] = (wchar_t)(unsigned char)pBuf[i];
            }
            pWBuf[dwRead] = 0;
            SetWindowTextW(g_hwndEdit, pWBuf);
            LocalFree(pWBuf);
        }
    }

    LocalFree(pBuf);

    lstrcpyW(g_szFilePath, szFile);
    g_bDirty = 0;
    UpdateTitle();
    return 1;
}

/*
 * DoFileSave - Save current file (or Save As if untitled)
 */
static int DoFileSave(void)
{
    HANDLE hFile;
    DWORD dwLen, dwWritten;
    wchar_t *pText;
    unsigned char bom[2] = {0xFF, 0xFE};

    if (!g_szFilePath[0]) {
        return DoFileSaveAs();
    }

    hFile = CreateFileW(g_szFilePath, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hwndMain, L"Cannot save file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    dwLen = GetWindowTextLengthW(g_hwndEdit);
    pText = (wchar_t *)LocalAlloc(LMEM_FIXED, (dwLen + 1) * sizeof(wchar_t));
    if (!pText) {
        CloseHandle(hFile);
        MessageBoxW(g_hwndMain, L"Out of memory.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    GetWindowTextW(g_hwndEdit, pText, dwLen + 1);

    /* Write UTF-16 LE BOM */
    WriteFile(hFile, bom, 2, &dwWritten, NULL);
    /* Write text */
    WriteFile(hFile, pText, dwLen * sizeof(wchar_t), &dwWritten, NULL);

    CloseHandle(hFile);
    LocalFree(pText);

    g_bDirty = 0;
    UpdateTitle();
    return 1;
}

/*
 * DoFileSaveAs - Save with new filename
 */
static int DoFileSaveAs(void)
{
    wchar_t szFile[MAX_PATH];

    if (g_szFilePath[0]) {
        lstrcpyW(szFile, g_szFilePath);
    } else {
        lstrcpyW(szFile, L"");
    }

    if (!FilePicker(g_hwndMain, szFile, MAX_PATH,
            L"Save File", g_szFilter, L"txt", L"\\My Documents", 1)) {
        return 0;
    }

    lstrcpyW(g_szFilePath, szFile);
    return DoFileSave();
}

/*
 * MainWndProc - Main window message handler
 */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        /* Create CommandBar */
        g_hwndCB = CommandBar_Create(g_hInst, hwnd, 1);
        CreateMenuBar(g_hwndCB);
        CommandBar_AddAdornments(g_hwndCB, 0, 0);

        /* Create Edit control - fills client area below CommandBar */
        g_hwndEdit = CreateWindowW(
            L"EDIT", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            hwnd, (HMENU)ID_EDIT, g_hInst, NULL);

        SetFocus(g_hwndEdit);
        UpdateTitle();
        return 0;

    case WM_SIZE:
        OnSize(hwnd, LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_SETFOCUS:
        if (g_hwndEdit) {
            SetFocus(g_hwndEdit);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_FILE_NEW:
            DoFileNew();
            return 0;

        case IDM_FILE_OPEN:
            DoFileOpen();
            return 0;

        case IDM_FILE_SAVE:
            DoFileSave();
            return 0;

        case IDM_FILE_SAVEAS:
            DoFileSaveAs();
            return 0;

        case IDM_FILE_EXIT:
            if (PromptSave()) {
                DestroyWindow(hwnd);
            }
            return 0;

        case IDM_EDIT_UNDO:
            SendMessageW(g_hwndEdit, EM_UNDO, 0, 0);
            return 0;

        case IDM_EDIT_CUT:
            SendMessageW(g_hwndEdit, WM_CUT, 0, 0);
            return 0;

        case IDM_EDIT_COPY:
            SendMessageW(g_hwndEdit, WM_COPY, 0, 0);
            return 0;

        case IDM_EDIT_PASTE:
            SendMessageW(g_hwndEdit, WM_PASTE, 0, 0);
            return 0;

        case IDM_EDIT_SELECTALL:
            SetFocus(g_hwndEdit);
            SendMessageW(g_hwndEdit, EM_SETSEL, 0, -1);
            return 0;

        case IDM_HELP_ABOUT:
            ShowAboutDialog(hwnd);
            return 0;

        case ID_EDIT:
            /* Edit control notification */
            if (HIWORD(wParam) == EN_CHANGE) {
                if (!g_bDirty) {
                    g_bDirty = 1;
                    UpdateTitle();
                }
            }
            return 0;
        }
        break;

    case WM_DESTROY:
        CommandBar_Destroy(g_hwndCB);
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        if (PromptSave()) {
            DestroyWindow(hwnd);
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
