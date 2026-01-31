/*
 * Palmweaver - Text Editor for Windows CE
 * palmweaver.c - Main application
 *
 * Build 0.1.0.4 - Add File Open/Save/Save As
 */

#include <windows.h>
#include <commctrl.h>
#include "resource.h"

/*
 * CE SDK header gaps - these constants exist in desktop Windows headers but
 * are missing from CE 2.0 SDK headers. We define them ourselves since the
 * underlying OS still supports the functionality.
 */
#ifndef ICON_SMALL
#define ICON_SMALL 0  /* WM_SETICON parameter for small (title bar) icon */
#endif

#ifndef IDI_PALMWEAVER
#define IDI_PALMWEAVER 1  /* Resource ID fallback if resource.h not included */
#endif

/* Global instance handle (CE has no GetModuleHandle) */
HINSTANCE g_hInst;
HWND g_hwndMain;
HWND g_hwndCB;
HWND g_hwndEdit;
HWND g_hwndStatus;
HWND g_hwndLineNum;
HFONT g_hFont;
HBRUSH g_hBrushBg;
HMENU g_hViewMenu;
HMENU g_hThemeMenu;
HMENU g_hRecentMenu;

/* Current file state */
static wchar_t g_szFilePath[MAX_PATH];
static int g_bDirty = 0;
int g_bWordWrap = 1;  /* Word wrap on by default */
int g_bShowLineNums = 1;  /* Line numbers on by default */
int g_bShowStatusBar = 1; /* Status bar on by default */
int g_bFullScreen = 0;    /* Full screen off by default */
int g_bInverseColors = 0; /* Inverse fg/bg of current theme */
int g_nTheme = 0;         /* Color theme: 0=default, 1=green, 2=amber, 3=blue */
int g_bThemedSelection = 0; /* Theme selection highlight colors (opt-in) */
static int g_lineNumWidth = 20;

/* Theme colors: {foreground, background} */
static COLORREF g_themes[][2] = {
    {RGB(0, 0, 0),       RGB(255, 255, 255)},  /* 0: Default (black on white) */
    {RGB(0, 255, 0),     RGB(0, 0, 0)},        /* 1: Green on black */
    {RGB(255, 191, 0),   RGB(0, 0, 0)},        /* 2: Amber on black */
    {RGB(255, 255, 255), RGB(0, 0, 128)}       /* 3: White on blue */
};
#define THEME_COUNT 4

/* Original system colors (saved at startup, restored on exit/deactivate) */
static COLORREF g_origHighlight;
static COLORREF g_origHighlightText;

/* Tab settings */
int g_bUseTabs = 1;    /* Use tabs (1) or spaces (0) */
int g_nTabSize = 4;    /* Number of spaces per tab */

/* Font settings */
static int g_fontSizes[] = {10, 12, 14, 16};
static int g_fontSizeIdx = 2;  /* Default 14 */
static int g_bFixedFont = 1;   /* Default fixed (Courier New) */

/* Recent files */
wchar_t g_recentFiles[MAX_RECENT_FILES][MAX_PATH] = {0};
int g_recentCount = 0;

/* Edit control subclass */
static WNDPROC g_pfnEditProc = NULL;
static WNDPROC g_pfnLineNumProc = NULL;

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
static void UpdateStatus(void);
static void UpdateTheme(void);
static void ApplySelectionColors(void);
static void RestoreSelectionColors(void);
static void DoFileNew(void);
static int DoFileOpen(void);
static int DoFileSave(void);
static int DoFileSaveAs(void);
static int PromptSave(void);
static void DoGotoLine(void);
static void DoFind(void);
static void DoFindNext(void);
static void DoReplace(void);
static void UpdateLineNumbers(void);
static void AddRecentFile(const wchar_t *path);
static void UpdateRecentMenu(void);
static void OpenRecentFile(int index);
static void DoOptions(void);
static void UpdateFont(void);

/* External: file picker */
int FilePicker(HWND hwndOwner, wchar_t *filePath, int maxPath,
               const wchar_t *title, const wchar_t *filter,
               const wchar_t *defExt, const wchar_t *initialDir,
               int saveMode);

/* External: settings */
void LoadSettings(void);
void SaveSettings(void);

/*
 * HandleGlobalKeys - Centralized keyboard handler for app-wide shortcuts.
 * Returns 1 if key was handled, 0 otherwise.
 */
static int HandleGlobalKeys(UINT msg, WPARAM wParam)
{
    int ctrl = GetKeyState(VK_CONTROL) < 0;
    int alt = GetKeyState(VK_MENU) < 0;

    if (msg == WM_SYSKEYDOWN) {
        /* Alt+X = Exit */
        if ((wParam == 'X' || wParam == 'x') && alt) {
            SendMessage(g_hwndMain, WM_CLOSE, 0, 0);
            return 1;
        }
        /* Alt+Enter = Full Screen */
        if (wParam == VK_RETURN && alt) {
            SendMessage(g_hwndMain, WM_COMMAND, IDM_VIEW_FULLSCREEN, 0);
            return 1;
        }
        /* Alt+I = Inverse Colors */
        if ((wParam == 'I' || wParam == 'i') && alt) {
            SendMessage(g_hwndMain, WM_COMMAND, IDM_VIEW_INVERSE, 0);
            return 1;
        }
    }
    if (msg == WM_KEYDOWN) {
        /* Escape exits full screen */
        if (wParam == VK_ESCAPE && g_bFullScreen) {
            SendMessage(g_hwndMain, WM_COMMAND, IDM_VIEW_FULLSCREEN, 0);
            return 1;
        }
    }
    if (msg == WM_KEYDOWN && ctrl) {
        if (wParam == 'N') { SendMessage(g_hwndMain, WM_COMMAND, IDM_FILE_NEW, 0); return 1; }
        if (wParam == 'O') { SendMessage(g_hwndMain, WM_COMMAND, IDM_FILE_OPEN, 0); return 1; }
        if (wParam == 'S') { SendMessage(g_hwndMain, WM_COMMAND, IDM_FILE_SAVE, 0); return 1; }
        if (wParam == 'W') { SendMessage(g_hwndMain, WM_CLOSE, 0, 0); return 1; }
        if (wParam == 'G') { DoGotoLine(); return 1; }
        if (wParam == 'F') { DoFind(); return 1; }
        if (wParam == 'H') { DoReplace(); return 1; }
        if (wParam == 'L') { SendMessage(g_hwndMain, WM_COMMAND, IDM_VIEW_LINENUMS, 0); return 1; }
        if (wParam == 'A') { SendMessageW(g_hwndEdit, EM_SETSEL, 0, -1); return 1; }
        if (wParam == '3') { DoFindNext(); return 1; }
        /* Zoom: Ctrl+Plus/Minus or Ctrl+=/- */
        if (wParam == VK_ADD || wParam == 0xBB) {  /* Numpad+ or =/+ key */
            if (g_fontSizeIdx < 3) { g_fontSizeIdx++; UpdateFont(); }
            return 1;
        }
        if (wParam == VK_SUBTRACT || wParam == 0xBD) {  /* Numpad- or -/_ key */
            if (g_fontSizeIdx > 0) { g_fontSizeIdx--; UpdateFont(); }
            return 1;
        }
    }
    if (msg == WM_KEYDOWN && wParam == VK_F3) {
        DoFindNext();
        return 1;
    }
    return 0;
}

/*
 * EditSubclassProc - Catch cursor movement for status updates
 */
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    /* Global shortcuts first */
    if (HandleGlobalKeys(msg, wParam))
        return 0;

    /* Fill background with correct color */
    if (msg == WM_ERASEBKGND) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_hBrushBg);
        return 1;
    }

    /* Apply theme selection colors when edit gains focus */
    if (msg == WM_SETFOCUS && g_bThemedSelection) {
        ApplySelectionColors();
    }

    /* Restore system selection colors when edit loses focus */
    if (msg == WM_KILLFOCUS && g_bThemedSelection) {
        RestoreSelectionColors();
    }

    /* Handle Tab key - insert spaces if configured */
    if (msg == WM_CHAR && wParam == '\t' && !g_bUseTabs) {
        wchar_t spaces[9];
        int i;
        for (i = 0; i < g_nTabSize && i < 8; i++) spaces[i] = ' ';
        spaces[i] = 0;
        SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)spaces);
        return 0;
    }

    /* Block WM_CHAR for Ctrl+key combos we handle (prevents beep) */
    if (msg == WM_CHAR && GetKeyState(VK_CONTROL) < 0) {
        if (wParam == 1 || wParam == 6 || wParam == 7 || wParam == 8 || wParam == 12 || /* Ctrl+A, F, G, H, L */
            wParam == 14 || wParam == 15 || wParam == 19 || wParam == 23) /* Ctrl+N, O, S, W */
            return 0;
    }

    /* Block WM_SYSCHAR for Alt+Enter and Alt+I (prevents beep) */
    if (msg == WM_SYSCHAR && (wParam == VK_RETURN || wParam == 'i' || wParam == 'I'))
        return 0;

    /* Sync line numbers on scroll */
    if (msg == WM_VSCROLL) {
        LRESULT r = CallWindowProc(g_pfnEditProc, hwnd, msg, wParam, lParam);
        UpdateLineNumbers();
        return r;
    }

    if (msg == WM_KEYUP || msg == WM_LBUTTONUP) {
        UpdateStatus();
        UpdateLineNumbers();
    }

    return CallWindowProc(g_pfnEditProc, hwnd, msg, wParam, lParam);
}

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
    WNDCLASSW wc = {0};

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PALMWEAVER));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
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

    /* Set small icon for taskbar */
    SendMessage(g_hwndMain, WM_SETICON, ICON_SMALL,
        (LPARAM)LoadImage(hInstance, MAKEINTRESOURCE(IDI_PALMWEAVER),
            IMAGE_ICON, 16, 16, 0));

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
    HMENU hMenuView;
    HMENU hMenuHelp;
    HMENU hMenuTheme;

    hMenu = CreateMenu();
    hMenuFile = CreatePopupMenu();
    hMenuEdit = CreatePopupMenu();
    hMenuView = CreatePopupMenu();
    hMenuHelp = CreatePopupMenu();
    g_hRecentMenu = CreatePopupMenu();

    /* File menu */
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_NEW, L"&New\tCtrl+N");
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(hMenuFile, MF_POPUP, (UINT)g_hRecentMenu, L"&Recent Files");
    AppendMenuW(hMenuFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_SAVEAS, L"Save &As...");
    AppendMenuW(hMenuFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_OPTIONS, L"&Options...");
    AppendMenuW(hMenuFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuFile, MF_STRING, IDM_FILE_EXIT, L"E&xit\tCtrl+W");

    /* Edit menu */
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_CUT, L"Cu&t\tCtrl+X");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_SELECTALL, L"Select &All\tCtrl+A");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_FIND, L"&Find...\tCtrl+F");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_FINDNEXT, L"Find &Next\tCtrl+3");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_REPLACE, L"&Replace...\tCtrl+H");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_GOTOLINE, L"&Go to Line...\tCtrl+G");

    /* View menu */
    AppendMenuW(hMenuView, MF_STRING | MF_CHECKED, IDM_VIEW_WORDWRAP, L"&Word Wrap");
    AppendMenuW(hMenuView, MF_STRING | MF_CHECKED, IDM_VIEW_LINENUMS, L"&Line Numbers");
    AppendMenuW(hMenuView, MF_STRING | MF_CHECKED, IDM_VIEW_STATUSBAR, L"&Status Bar");
    AppendMenuW(hMenuView, MF_SEPARATOR, 0, NULL);

    /* Theme submenu */
    hMenuTheme = CreatePopupMenu();
    AppendMenuW(hMenuTheme, MF_STRING | MF_CHECKED, IDM_VIEW_THEME_DEFAULT, L"&Default");
    AppendMenuW(hMenuTheme, MF_STRING, IDM_VIEW_THEME_GREEN, L"&Green");
    AppendMenuW(hMenuTheme, MF_STRING, IDM_VIEW_THEME_AMBER, L"&Amber");
    AppendMenuW(hMenuTheme, MF_STRING, IDM_VIEW_THEME_BLUE, L"&Blue");
    AppendMenuW(hMenuTheme, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuTheme, MF_STRING, IDM_VIEW_INVERSE, L"&Inverse Colors\tAlt+I");
    AppendMenuW(hMenuView, MF_POPUP, (UINT)hMenuTheme, L"&Theme");
    g_hThemeMenu = hMenuTheme;

    AppendMenuW(hMenuView, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenuView, MF_STRING, IDM_VIEW_FULLSCREEN, L"&Full Screen\tAlt+Enter");
    g_hViewMenu = hMenuView;

    /* Help menu */
    AppendMenuW(hMenuHelp, MF_STRING, IDM_HELP_ABOUT, L"&About...");

    /* Attach to menu bar */
    AppendMenuW(hMenu, MF_POPUP, (UINT)hMenuFile, L"&File");
    AppendMenuW(hMenu, MF_POPUP, (UINT)hMenuEdit, L"&Edit");
    AppendMenuW(hMenu, MF_POPUP, (UINT)hMenuView, L"&View");
    AppendMenuW(hMenu, MF_POPUP, (UINT)hMenuHelp, L"&Help");

    CommandBar_InsertMenubarEx(hwndCB, NULL, (LPTSTR)hMenu, 0);
}

/*
 * OnSize - Handle window resize
 */
static void OnSize(HWND hwnd, int cx, int cy)
{
    int cbHeight, sbHeight, editLeft, editHeight;
    RECT rcClient, rcStatus;

    if (!g_hwndCB || !g_hwndEdit || !g_hwndStatus) {
        return;
    }

    /* Get actual client size if called with 0,0 */
    if (cx == 0 && cy == 0) {
        GetClientRect(hwnd, &rcClient);
        cx = rcClient.right;
        cy = rcClient.bottom;
    }

    cbHeight = g_bFullScreen ? 0 : CommandBar_Height(g_hwndCB);

    /* Let status bar auto-size, then get its height */
    SendMessageW(g_hwndStatus, WM_SIZE, 0, 0);
    GetWindowRect(g_hwndStatus, &rcStatus);
    sbHeight = (g_bShowStatusBar && !g_bFullScreen) ? (rcStatus.bottom - rcStatus.top) : 0;

    editHeight = cy - cbHeight - sbHeight;
    editLeft = g_bShowLineNums ? g_lineNumWidth : 0;

    if (g_hwndLineNum)
        MoveWindow(g_hwndLineNum, 0, cbHeight, g_lineNumWidth, editHeight, TRUE);

    MoveWindow(g_hwndEdit, editLeft, cbHeight, cx - editLeft, editHeight, TRUE);
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
 * Go to Line dialog
 */
static HWND g_hwndGotoDlg = NULL;
static HWND g_hwndGotoEdit = NULL;
static WNDPROC g_pfnGotoEditProc = NULL;

static void GotoLineNumber(int lineNum)
{
    int charIdx, textLen, curLine, i;
    wchar_t *text;

    if (lineNum < 1) lineNum = 1;

    textLen = GetWindowTextLengthW(g_hwndEdit);
    if (textLen == 0) {
        SendMessage(g_hwndEdit, EM_SETSEL, 0, 0);
        return;
    }

    text = (wchar_t *)LocalAlloc(LMEM_FIXED, (textLen + 1) * sizeof(wchar_t));
    if (!text) return;
    GetWindowTextW(g_hwndEdit, text, textLen + 1);

    /* Find character index of target line */
    charIdx = 0;
    curLine = 1;
    for (i = 0; i < textLen && curLine < lineNum; i++) {
        if (text[i] == '\n') {
            curLine++;
            charIdx = i + 1;
        }
    }
    if (curLine < lineNum) charIdx = textLen;

    LocalFree(text);

    SendMessage(g_hwndEdit, EM_SETSEL, charIdx, charIdx);
    SendMessage(g_hwndEdit, EM_SCROLLCARET, 0, 0);
    SetFocus(g_hwndEdit);
    UpdateStatus();
}

static LRESULT CALLBACK GotoEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            SendMessage(g_hwndGotoDlg, WM_COMMAND, IDOK, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            SendMessage(g_hwndGotoDlg, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    return CallWindowProc(g_pfnGotoEditProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK GotoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"Line:",
            WS_CHILD | WS_VISIBLE,
            5, 8, 30, 16, hwnd, NULL, g_hInst, NULL);
        g_hwndGotoEdit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER,
            38, 5, 60, 20, hwnd, (HMENU)101, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"Go",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            105, 4, 40, 22, hwnd, (HMENU)IDOK, g_hInst, NULL);
        g_pfnGotoEditProc = (WNDPROC)SetWindowLong(g_hwndGotoEdit, GWL_WNDPROC, (LONG)GotoEditProc);
        SetFocus(g_hwndGotoEdit);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[16];
            int lineNum = 0, i;
            GetWindowTextW(g_hwndGotoEdit, buf, 16);
            for (i = 0; buf[i]; i++) lineNum = lineNum * 10 + (buf[i] - '0');
            DestroyWindow(hwnd);
            g_hwndGotoDlg = NULL;
            GotoLineNumber(lineNum);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        g_hwndGotoDlg = NULL;
        SetFocus(g_hwndEdit);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void DoGotoLine(void)
{
    WNDCLASSW wc = {0};
    RECT rc;

    if (g_hwndGotoDlg) {
        SetFocus(g_hwndGotoEdit);
        return;
    }

    wc.lpfnWndProc = GotoWndProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PalmweaverGoto";
    RegisterClassW(&wc);

    GetWindowRect(g_hwndMain, &rc);
#ifndef WS_EX_TOOLWINDOW
#define WS_EX_TOOLWINDOW 0x00000080L  /* Extended style for floating tool windows - missing from CE 2.0 headers */
#endif
    g_hwndGotoDlg = CreateWindowExW(WS_EX_TOOLWINDOW, L"PalmweaverGoto", L"Go to Line",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        rc.left + 20, rc.top + 50, 155, 52,
        g_hwndMain, NULL, g_hInst, NULL);
    ShowWindow(g_hwndGotoDlg, SW_SHOW);
}

/*
 * Find dialog
 */
static HWND g_hwndFindDlg = NULL;
static HWND g_hwndFindEdit = NULL;
static WNDPROC g_pfnFindEditProc = NULL;
static wchar_t g_findText[128] = L"";
static int g_bMatchCase = 0;

static int CharsMatch(wchar_t c1, wchar_t c2)
{
    if (g_bMatchCase) return c1 == c2;
    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
    return c1 == c2;
}

static void DoFindNext(void)
{
    int len, findLen, start, i, j;
    wchar_t *buf;
    DWORD sel;

    if (!g_findText[0]) return;

    findLen = lstrlenW(g_findText);
    len = GetWindowTextLengthW(g_hwndEdit);
    if (len == 0) return;

    buf = (wchar_t *)LocalAlloc(LMEM_FIXED, (len + 1) * sizeof(wchar_t));
    if (!buf) return;
    GetWindowTextW(g_hwndEdit, buf, len + 1);

    SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&sel, 0);
    start = sel + 1;
    if (start > len) start = 0;

    /* Search forward with wrap */
    for (i = start; i <= len - findLen; i++) {
        for (j = 0; j < findLen; j++) {
            if (!CharsMatch(buf[i + j], g_findText[j])) break;
        }
        if (j == findLen) {
            SendMessage(g_hwndEdit, EM_SETSEL, i, i + findLen);
            SendMessage(g_hwndEdit, EM_SCROLLCARET, 0, 0);
            LocalFree(buf);
            return;
        }
    }
    for (i = 0; i < start && i <= len - findLen; i++) {
        for (j = 0; j < findLen; j++) {
            if (!CharsMatch(buf[i + j], g_findText[j])) break;
        }
        if (j == findLen) {
            SendMessage(g_hwndEdit, EM_SETSEL, i, i + findLen);
            SendMessage(g_hwndEdit, EM_SCROLLCARET, 0, 0);
            LocalFree(buf);
            return;
        }
    }
    LocalFree(buf);
    MessageBoxW(g_hwndMain, L"Text not found.", L"Find", MB_OK);
}

static LRESULT CALLBACK FindEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            SendMessage(g_hwndFindDlg, WM_COMMAND, IDOK, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            SendMessage(g_hwndFindDlg, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    return CallWindowProc(g_pfnFindEditProc, hwnd, msg, wParam, lParam);
}

static HWND g_hwndFindCase = NULL;

static LRESULT CALLBACK FindWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hwndFindEdit = CreateWindowW(L"EDIT", g_findText,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            5, 5, 133, 20, hwnd, (HMENU)101, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"Find",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            145, 4, 45, 22, hwnd, (HMENU)IDOK, g_hInst, NULL);
        g_hwndFindCase = CreateWindowW(L"BUTTON", L"Match case",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            5, 28, 85, 18, hwnd, (HMENU)102, g_hInst, NULL);
        SendMessage(g_hwndFindCase, BM_SETCHECK, g_bMatchCase, 0);
        g_pfnFindEditProc = (WNDPROC)SetWindowLong(g_hwndFindEdit, GWL_WNDPROC, (LONG)FindEditProc);
        SetFocus(g_hwndFindEdit);
        SendMessage(g_hwndFindEdit, EM_SETSEL, 0, -1);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetWindowTextW(g_hwndFindEdit, g_findText, 128);
            g_bMatchCase = (int)SendMessage(g_hwndFindCase, BM_GETCHECK, 0, 0);
            DestroyWindow(hwnd);
            g_hwndFindDlg = NULL;
            SetFocus(g_hwndEdit);
            if (g_findText[0]) DoFindNext();
        }
        return 0;

    case WM_CLOSE:
        g_bMatchCase = (int)SendMessage(g_hwndFindCase, BM_GETCHECK, 0, 0);
        DestroyWindow(hwnd);
        g_hwndFindDlg = NULL;
        SetFocus(g_hwndEdit);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void DoFind(void)
{
    WNDCLASSW wc = {0};
    RECT rc;

    if (g_hwndFindDlg) {
        SetFocus(g_hwndFindEdit);
        return;
    }

    wc.lpfnWndProc = FindWndProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PalmweaverFind";
    RegisterClassW(&wc);

    GetWindowRect(g_hwndMain, &rc);
    g_hwndFindDlg = CreateWindowExW(WS_EX_TOOLWINDOW, L"PalmweaverFind", L"Find",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        rc.left + 20, rc.top + 50, 200, 72,
        g_hwndMain, NULL, g_hInst, NULL);
    ShowWindow(g_hwndFindDlg, SW_SHOW);
}

/*
 * Replace dialog
 */
static HWND g_hwndReplaceDlg = NULL;
static HWND g_hwndReplFind = NULL;
static HWND g_hwndReplWith = NULL;
static WNDPROC g_pfnReplFindProc = NULL;
static WNDPROC g_pfnReplWithProc = NULL;
static wchar_t g_replaceText[128] = L"";

#define ID_REPL_FIND    201
#define ID_REPL_REPLACE 202
#define ID_REPL_ALL     203

static void DoReplaceOne(void)
{
    DWORD selStart, selEnd;
    int selLen, findLen, i, match = 1;
    wchar_t *buf;
    int len;

    if (!g_findText[0]) return;

    SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (selEnd <= selStart) { DoFindNext(); return; }

    selLen = selEnd - selStart;
    findLen = lstrlenW(g_findText);
    if (selLen != findLen) { DoFindNext(); return; }

    len = GetWindowTextLengthW(g_hwndEdit);
    buf = (wchar_t *)LocalAlloc(LMEM_FIXED, (len + 1) * sizeof(wchar_t));
    if (!buf) return;
    GetWindowTextW(g_hwndEdit, buf, len + 1);

    for (i = 0; i < selLen && match; i++) {
        if (!CharsMatch(buf[selStart + i], g_findText[i])) match = 0;
    }
    LocalFree(buf);

    if (match) {
        SendMessage(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)g_replaceText);
        g_bDirty = 1;
        UpdateTitle();
    }
    DoFindNext();
}

static int DoReplaceAll(void)
{
    int len, findLen, replLen, count = 0, i, j;
    wchar_t *buf, *newBuf, *p;

    if (!g_findText[0]) return 0;

    findLen = lstrlenW(g_findText);
    replLen = lstrlenW(g_replaceText);
    len = GetWindowTextLengthW(g_hwndEdit);
    if (len == 0) return 0;

    buf = (wchar_t *)LocalAlloc(LMEM_FIXED, (len + 1) * sizeof(wchar_t));
    if (!buf) return 0;
    GetWindowTextW(g_hwndEdit, buf, len + 1);

    /* Count matches */
    for (i = 0; i <= len - findLen; i++) {
        for (j = 0; j < findLen; j++) {
            if (!CharsMatch(buf[i + j], g_findText[j])) break;
        }
        if (j == findLen) count++;
    }

    if (count == 0) {
        LocalFree(buf);
        return 0;
    }

    newBuf = (wchar_t *)LocalAlloc(LMEM_FIXED,
        (len + count * (replLen - findLen) + 1) * sizeof(wchar_t));
    if (!newBuf) { LocalFree(buf); return 0; }

    p = newBuf;
    for (i = 0; i <= len; i++) {
        if (i <= len - findLen) {
            for (j = 0; j < findLen; j++) {
                if (!CharsMatch(buf[i + j], g_findText[j])) break;
            }
            if (j == findLen) {
                for (j = 0; j < replLen; j++) *p++ = g_replaceText[j];
                i += findLen - 1;
                continue;
            }
        }
        *p++ = buf[i];
    }

    SetWindowTextW(g_hwndEdit, newBuf);
    g_bDirty = 1;
    UpdateTitle();

    LocalFree(newBuf);
    LocalFree(buf);
    return count;
}

static LRESULT CALLBACK ReplEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WNDPROC origProc = (hwnd == g_hwndReplFind) ? g_pfnReplFindProc : g_pfnReplWithProc;
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_TAB) {
            SetFocus((hwnd == g_hwndReplFind) ? g_hwndReplWith : g_hwndReplFind);
            return 0;
        }
        if (wParam == VK_RETURN) {
            SendMessage(GetParent(hwnd), WM_COMMAND, ID_REPL_FIND, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            SendMessage(GetParent(hwnd), WM_CLOSE, 0, 0);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wParam == '\t' || wParam == '\r' || wParam == 27))
        return 0;
    return CallWindowProc(origProc, hwnd, msg, wParam, lParam);
}

static HWND g_hwndReplCase = NULL;

static LRESULT CALLBACK ReplaceWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"Find:",
            WS_CHILD | WS_VISIBLE, 5, 7, 45, 16, hwnd, NULL, g_hInst, NULL);
        g_hwndReplFind = CreateWindowW(L"EDIT", g_findText,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            55, 5, 175, 20, hwnd, (HMENU)101, g_hInst, NULL);
        CreateWindowW(L"STATIC", L"Replace:",
            WS_CHILD | WS_VISIBLE, 5, 32, 50, 16, hwnd, NULL, g_hInst, NULL);
        g_hwndReplWith = CreateWindowW(L"EDIT", g_replaceText,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            55, 30, 175, 20, hwnd, (HMENU)102, g_hInst, NULL);
        g_hwndReplCase = CreateWindowW(L"BUTTON", L"Match case",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            130, 58, 85, 18, hwnd, (HMENU)103, g_hInst, NULL);
        SendMessage(g_hwndReplCase, BM_SETCHECK, g_bMatchCase, 0);
        CreateWindowW(L"BUTTON", L"Find",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            5, 80, 55, 22, hwnd, (HMENU)ID_REPL_FIND, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"Replace",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            65, 80, 60, 22, hwnd, (HMENU)ID_REPL_REPLACE, g_hInst, NULL);
        CreateWindowW(L"BUTTON", L"All",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            175, 80, 55, 22, hwnd, (HMENU)ID_REPL_ALL, g_hInst, NULL);
        g_pfnReplFindProc = (WNDPROC)SetWindowLong(g_hwndReplFind, GWL_WNDPROC, (LONG)ReplEditProc);
        g_pfnReplWithProc = (WNDPROC)SetWindowLong(g_hwndReplWith, GWL_WNDPROC, (LONG)ReplEditProc);
        SetFocus(g_hwndReplFind);
        SendMessage(g_hwndReplFind, EM_SETSEL, 0, -1);
        return 0;

    case WM_COMMAND:
        GetWindowTextW(g_hwndReplFind, g_findText, 128);
        GetWindowTextW(g_hwndReplWith, g_replaceText, 128);
        g_bMatchCase = (int)SendMessage(g_hwndReplCase, BM_GETCHECK, 0, 0);
        if (LOWORD(wParam) == ID_REPL_FIND) {
            if (g_findText[0]) DoFindNext();
        } else if (LOWORD(wParam) == ID_REPL_REPLACE) {
            if (g_findText[0]) DoReplaceOne();
        } else if (LOWORD(wParam) == ID_REPL_ALL) {
            if (g_findText[0]) {
                int n = DoReplaceAll();
                wchar_t buf[64];
                wsprintfW(buf, L"%d replacement%s made.", n, n == 1 ? L"" : L"s");
                MessageBoxW(hwnd, buf, L"Replace All", MB_OK);
            }
        }
        return 0;

    case WM_CLOSE:
        g_bMatchCase = (int)SendMessage(g_hwndReplCase, BM_GETCHECK, 0, 0);
        DestroyWindow(hwnd);
        g_hwndReplaceDlg = NULL;
        SetFocus(g_hwndEdit);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void DoReplace(void)
{
    WNDCLASSW wc = {0};
    RECT rc;

    if (g_hwndReplaceDlg) {
        SetFocus(g_hwndReplFind);
        return;
    }

    if (g_hwndFindDlg) {
        DestroyWindow(g_hwndFindDlg);
        g_hwndFindDlg = NULL;
    }

    wc.lpfnWndProc = ReplaceWndProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PalmweaverReplace";
    RegisterClassW(&wc);

    GetWindowRect(g_hwndMain, &rc);
    g_hwndReplaceDlg = CreateWindowExW(WS_EX_TOOLWINDOW, L"PalmweaverReplace", L"Replace",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        rc.left + 20, rc.top + 50, 240, 130,
        g_hwndMain, NULL, g_hInst, NULL);
    ShowWindow(g_hwndReplaceDlg, SW_SHOW);
}

/*
 * Options dialog
 */
static HWND g_hwndOptionsDlg = NULL;
static HWND g_hwndOptUseTabs = NULL;
static HWND g_hwndOptUseSpaces = NULL;
static HWND g_hwndOptTabSize = NULL;

#define IDC_OPT_USETABS   101
#define IDC_OPT_USESPACES 102
#define IDC_OPT_TABSIZE   103
#define IDC_OPT_CLEARREG  104
#define IDC_OPT_FONTSIZE  105
#define IDC_OPT_FIXEDFONT 106
#define IDC_OPT_THEMEDSEL 107

/* External: settings */
void ClearSettings(void);

static void UpdateFont(void)
{
    LOGFONTW lf = {0};
    HFONT hNewFont;

    lf.lfHeight = g_fontSizes[g_fontSizeIdx];
    if (g_bFixedFont) {
        lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        lstrcpyW(lf.lfFaceName, L"Courier New");
    } else {
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
        lstrcpyW(lf.lfFaceName, L"Tahoma");
    }

    hNewFont = CreateFontIndirectW(&lf);
    if (hNewFont) {
        DeleteObject(g_hFont);
        g_hFont = hNewFont;
        SendMessage(g_hwndEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(g_hwndLineNum, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        UpdateLineNumbers();
    }
}

static LRESULT CALLBACK OptionsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND hwndFontSize, hwndFixedFont, hwndThemedSel;

    switch (msg) {
    case WM_CREATE:
        {
            wchar_t buf[8];
            /* Row 1: Indentation */
            CreateWindowW(L"STATIC", L"Indentation:",
                WS_CHILD | WS_VISIBLE, 10, 12, 70, 16, hwnd, NULL, g_hInst, NULL);
            g_hwndOptUseTabs = CreateWindowW(L"BUTTON", L"Tabs",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                85, 10, 50, 20, hwnd, (HMENU)IDC_OPT_USETABS, g_hInst, NULL);
            g_hwndOptUseSpaces = CreateWindowW(L"BUTTON", L"Spaces:",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                140, 10, 60, 20, hwnd, (HMENU)IDC_OPT_USESPACES, g_hInst, NULL);
            wsprintfW(buf, L"%d", g_nTabSize);
            g_hwndOptTabSize = CreateWindowW(L"EDIT", buf,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                202, 10, 25, 20, hwnd, (HMENU)IDC_OPT_TABSIZE, g_hInst, NULL);

            /* Row 2: Font */
            CreateWindowW(L"STATIC", L"Font size:",
                WS_CHILD | WS_VISIBLE, 10, 40, 55, 16, hwnd, NULL, g_hInst, NULL);
            hwndFontSize = CreateWindowW(L"COMBOBOX", NULL,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                70, 37, 50, 80, hwnd, (HMENU)IDC_OPT_FONTSIZE, g_hInst, NULL);
            SendMessageW(hwndFontSize, CB_ADDSTRING, 0, (LPARAM)L"10");
            SendMessageW(hwndFontSize, CB_ADDSTRING, 0, (LPARAM)L"12");
            SendMessageW(hwndFontSize, CB_ADDSTRING, 0, (LPARAM)L"14");
            SendMessageW(hwndFontSize, CB_ADDSTRING, 0, (LPARAM)L"16");
            SendMessageW(hwndFontSize, CB_SETCURSEL, g_fontSizeIdx, 0);
            hwndFixedFont = CreateWindowW(L"BUTTON", L"Fixed width font",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                130, 38, 120, 20, hwnd, (HMENU)IDC_OPT_FIXEDFONT, g_hInst, NULL);
            SendMessage(hwndFixedFont, BM_SETCHECK, g_bFixedFont, 0);

            /* Row 3: Theme option */
            hwndThemedSel = CreateWindowW(L"BUTTON", L"Theme selection highlight",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                10, 66, 160, 20, hwnd, (HMENU)IDC_OPT_THEMEDSEL, g_hInst, NULL);
            SendMessage(hwndThemedSel, BM_SETCHECK, g_bThemedSelection, 0);

            /* Row 4: Buttons */
            CreateWindowW(L"BUTTON", L"Clear Settings",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 94, 100, 24, hwnd, (HMENU)IDC_OPT_CLEARREG, g_hInst, NULL);
            CreateWindowW(L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                180, 94, 50, 24, hwnd, (HMENU)IDOK, g_hInst, NULL);
            SendMessage(g_bUseTabs ? g_hwndOptUseTabs : g_hwndOptUseSpaces, BM_SETCHECK, 1, 0);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[8];
            int size, newSizeIdx, newFixed, newThemedSel;
            g_bUseTabs = (int)SendMessage(g_hwndOptUseTabs, BM_GETCHECK, 0, 0);
            GetWindowTextW(g_hwndOptTabSize, buf, 8);
            size = 0;
            { int i; for (i = 0; buf[i]; i++) size = size * 10 + (buf[i] - '0'); }
            if (size >= 1 && size <= 8) g_nTabSize = size;

            newSizeIdx = (int)SendMessageW(hwndFontSize, CB_GETCURSEL, 0, 0);
            newFixed = (int)SendMessage(hwndFixedFont, BM_GETCHECK, 0, 0);
            if (newSizeIdx != g_fontSizeIdx || newFixed != g_bFixedFont) {
                g_fontSizeIdx = newSizeIdx;
                g_bFixedFont = newFixed;
                UpdateFont();
            }

            newThemedSel = (int)SendMessage(hwndThemedSel, BM_GETCHECK, 0, 0);
            if (newThemedSel != g_bThemedSelection) {
                if (!newThemedSel) RestoreSelectionColors();
                g_bThemedSelection = newThemedSel;
            }

            DestroyWindow(hwnd);
            g_hwndOptionsDlg = NULL;
            SetFocus(g_hwndEdit);
        } else if (LOWORD(wParam) == IDC_OPT_CLEARREG) {
            if (MessageBoxW(hwnd, L"Clear all settings and recent files?",
                    L"Clear Settings", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                ClearSettings();
                UpdateTheme();
                MessageBoxW(hwnd, L"Settings cleared. Changes take effect on restart.",
                    L"Clear Settings", MB_OK);
            }
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        g_hwndOptionsDlg = NULL;
        SetFocus(g_hwndEdit);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void DoOptions(void)
{
    WNDCLASSW wc = {0};
    RECT rc;

    if (g_hwndOptionsDlg) {
        SetFocus(g_hwndOptionsDlg);
        return;
    }

    wc.lpfnWndProc = OptionsWndProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PalmweaverOptions";
    RegisterClassW(&wc);

    GetWindowRect(g_hwndMain, &rc);
    g_hwndOptionsDlg = CreateWindowExW(WS_EX_TOOLWINDOW, L"PalmweaverOptions", L"Options",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        rc.left + 30, rc.top + 60, 260, 145,
        g_hwndMain, NULL, g_hInst, NULL);
    ShowWindow(g_hwndOptionsDlg, SW_SHOW);
}

/*
 * Line number gutter
 */
static LRESULT CALLBACK LineNumProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    /* Block all input - read only display */
    if (msg == WM_CHAR || msg == WM_KEYDOWN || msg == WM_LBUTTONDOWN ||
        msg == WM_RBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
        return 0;

    /* Fill background with correct color */
    if (msg == WM_ERASEBKGND) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_hBrushBg);
        return 1;
    }

    return CallWindowProc(g_pfnLineNumProc, hwnd, msg, wParam, lParam);
}

static void UpdateLineNumbers(void)
{
    static wchar_t *cachedText = NULL;
    static int cachedLen = -1;
    wchar_t buf[4096];
    wchar_t *text = NULL;
    int i, visLines, firstVisible, pos = 0, textLen;
    int charIdx, logicalLine;

    if (!g_bShowLineNums || !g_hwndLineNum || !g_hFont) return;

    visLines = (int)SendMessage(g_hwndEdit, EM_GETLINECOUNT, 0, 0);
    textLen = GetWindowTextLengthW(g_hwndEdit);

    /* Cache text for performance */
    if (textLen == cachedLen && cachedText) {
        text = cachedText;
    } else {
        if (cachedText) { LocalFree(cachedText); cachedText = NULL; }
        cachedLen = textLen;
        if (textLen > 0) {
            cachedText = (wchar_t *)LocalAlloc(LMEM_FIXED, (textLen + 1) * sizeof(wchar_t));
            if (cachedText) GetWindowTextW(g_hwndEdit, cachedText, textLen + 1);
        }
        text = cachedText;
    }

    /* Auto-size gutter width based on line count */
    {
        int logicalTotal = 1;
        if (text) {
            for (i = 0; i < textLen; i++)
                if (text[i] == '\n') logicalTotal++;
        }
        {
            HDC hdc = GetDC(g_hwndLineNum);
            HFONT hOld = (HFONT)SelectObject(hdc, g_hFont);
            SIZE sz;
            wchar_t numBuf[16];
            int newWidth;
            wsprintfW(numBuf, L"%d", logicalTotal);
            GetTextExtentPoint32W(hdc, numBuf, lstrlenW(numBuf), &sz);
            newWidth = sz.cx + 10;
            if (newWidth < 20) newWidth = 20;
            SelectObject(hdc, hOld);
            ReleaseDC(g_hwndLineNum, hdc);
            if (newWidth != g_lineNumWidth) {
                g_lineNumWidth = newWidth;
                SendMessage(g_hwndMain, WM_SIZE, 0, 0);
                UpdateWindow(g_hwndMain);
            }
        }
    }

    firstVisible = (int)SendMessage(g_hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    /* Find logical line number at first visible line */
    charIdx = (int)SendMessage(g_hwndEdit, EM_LINEINDEX, firstVisible, 0);
    logicalLine = 1;
    if (text) {
        for (i = 0; i < charIdx && i < textLen; i++)
            if (text[i] == '\n') logicalLine++;
    }

    /* Build line number text - blank for wrapped continuations */
    for (i = firstVisible; i < visLines && pos < 4000; i++) {
        charIdx = (int)SendMessage(g_hwndEdit, EM_LINEINDEX, i, 0);
        if (i == 0 || (text && charIdx > 0 && text[charIdx - 1] == '\n')) {
            pos += wsprintfW(buf + pos, L"%d\r\n", logicalLine);
            logicalLine++;
        } else {
            pos += wsprintfW(buf + pos, L"\r\n");
        }
    }
    buf[pos] = 0;
    SetWindowTextW(g_hwndLineNum, buf);
}

/*
 * Recent files management
 */
static void AddRecentFile(const wchar_t *path)
{
    int i, j;
    /* Check if already in list, move to top if so */
    for (i = 0; i < g_recentCount; i++) {
        if (lstrcmpiW(g_recentFiles[i], path) == 0) {
            for (j = i; j > 0; j--)
                lstrcpyW(g_recentFiles[j], g_recentFiles[j-1]);
            lstrcpyW(g_recentFiles[0], path);
            UpdateRecentMenu();
            return;
        }
    }
    /* Shift down and add at top */
    for (i = MAX_RECENT_FILES - 1; i > 0; i--)
        lstrcpyW(g_recentFiles[i], g_recentFiles[i-1]);
    lstrcpyW(g_recentFiles[0], path);
    if (g_recentCount < MAX_RECENT_FILES) g_recentCount++;
    UpdateRecentMenu();
}

static void UpdateRecentMenu(void)
{
    int i, j;
    if (!g_hRecentMenu) return;

    while (RemoveMenu(g_hRecentMenu, 0, MF_BYPOSITION));

    if (g_recentCount == 0) {
        AppendMenuW(g_hRecentMenu, MF_STRING | MF_GRAYED, 0, L"(none)");
    } else {
        for (i = 0; i < g_recentCount; i++) {
            wchar_t item[MAX_PATH + 8];
            const wchar_t *name = g_recentFiles[i];
            int len = lstrlenW(name);
            for (j = len - 1; j >= 0; j--)
                if (name[j] == '\\') { name = &g_recentFiles[i][j+1]; break; }
            wsprintfW(item, L"&%d %s", i + 1, name);
            AppendMenuW(g_hRecentMenu, MF_STRING, IDM_RECENT_BASE + i, item);
        }
    }
}

static void OpenRecentFile(int index)
{
    wchar_t path[MAX_PATH];
    HANDLE hFile;
    DWORD dwSize, dwRead;
    char *pBuf;
    wchar_t *pWBuf;
    int i;

    if (index < 0 || index >= g_recentCount) return;
    if (!PromptSave()) return;

    lstrcpyW(path, g_recentFiles[index]);

    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hwndMain, L"Cannot open file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return;
    }

    dwSize = GetFileSize(hFile, NULL);
    pBuf = (char *)LocalAlloc(LMEM_FIXED, dwSize + 1);
    if (!pBuf) {
        CloseHandle(hFile);
        return;
    }

    ReadFile(hFile, pBuf, dwSize, &dwRead, NULL);
    CloseHandle(hFile);
    pBuf[dwRead] = 0;

    if (dwRead >= 2 && (unsigned char)pBuf[0] == 0xFF && (unsigned char)pBuf[1] == 0xFE) {
        wchar_t *pWide = (wchar_t *)(pBuf + 2);
        int nChars = (dwRead - 2) / sizeof(wchar_t);
        pWide[nChars] = 0;
        SetWindowTextW(g_hwndEdit, pWide);
    } else {
        pWBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (dwRead + 1) * sizeof(wchar_t));
        if (pWBuf) {
            for (i = 0; i < (int)dwRead; i++)
                pWBuf[i] = (wchar_t)(unsigned char)pBuf[i];
            pWBuf[dwRead] = 0;
            SetWindowTextW(g_hwndEdit, pWBuf);
            LocalFree(pWBuf);
        }
    }

    LocalFree(pBuf);
    lstrcpyW(g_szFilePath, path);
    g_bDirty = 0;
    UpdateTitle();
    UpdateLineNumbers();
    AddRecentFile(path);
}

/*
 * UpdateStatus - Update status bar with cursor position
 */
static void UpdateStatus(void)
{
    static DWORD s_lastSel = (DWORD)-1;
    DWORD sel;
    int line, col;
    int lineStart;
    wchar_t buf[64];

    sel = (DWORD)SendMessageW(g_hwndEdit, EM_GETSEL, 0, 0);
    if (sel == s_lastSel) return;
    s_lastSel = sel;

    /* Get line number from character index (caret is in LOWORD) */
    line = (int)SendMessageW(g_hwndEdit, EM_LINEFROMCHAR, LOWORD(sel), 0);
    /* Get character index of line start */
    lineStart = (int)SendMessageW(g_hwndEdit, EM_LINEINDEX, line, 0);
    col = LOWORD(sel) - lineStart;

    wsprintfW(buf, L"Ln %d, Col %d", line + 1, col + 1);
    SendMessageW(g_hwndStatus, SB_SETTEXTW, 0, (LPARAM)buf);
}

/*
 * UpdateTheme - Recreate background brush and repaint for current theme
 */
static void UpdateTheme(void)
{
    COLORREF bg;

    bg = g_bInverseColors ? g_themes[g_nTheme][0] : g_themes[g_nTheme][1];
    if (g_hBrushBg) DeleteObject(g_hBrushBg);
    g_hBrushBg = CreateSolidBrush(bg);

    if (g_bThemedSelection && GetFocus() == g_hwndEdit)
        ApplySelectionColors();

    InvalidateRect(g_hwndEdit, NULL, TRUE);
    InvalidateRect(g_hwndLineNum, NULL, TRUE);
}

/*
 * ApplySelectionColors - Set system highlight colors to match theme (inverted)
 */
static void ApplySelectionColors(void)
{
    int elements[2];
    COLORREF colors[2];

    elements[0] = COLOR_HIGHLIGHT;
    elements[1] = COLOR_HIGHLIGHTTEXT;
    /* Selection uses inverted theme colors for contrast */
    colors[0] = g_bInverseColors ? g_themes[g_nTheme][1] : g_themes[g_nTheme][0];
    colors[1] = g_bInverseColors ? g_themes[g_nTheme][0] : g_themes[g_nTheme][1];
    SetSysColors(2, elements, colors);
}

/*
 * RestoreSelectionColors - Restore original system highlight colors
 */
static void RestoreSelectionColors(void)
{
    int elements[2];
    COLORREF colors[2];

    elements[0] = COLOR_HIGHLIGHT;
    elements[1] = COLOR_HIGHLIGHTTEXT;
    colors[0] = g_origHighlight;
    colors[1] = g_origHighlightText;
    SetSysColors(2, elements, colors);
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
    UpdateLineNumbers();
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
    UpdateLineNumbers();
    AddRecentFile(szFile);
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
    if (DoFileSave()) {
        AddRecentFile(szFile);
        return 1;
    }
    return 0;
}

/*
 * MainWndProc - Main window message handler
 */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        {
            LOGFONTW lf = {0};
            DWORD editStyle;

            /* Load settings before creating controls */
            LoadSettings();

            /* Save original system highlight colors */
            g_origHighlight = GetSysColor(COLOR_HIGHLIGHT);
            g_origHighlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);

            /* Create initial background brush for current theme */
            g_hBrushBg = CreateSolidBrush(g_bInverseColors ? g_themes[g_nTheme][0] : g_themes[g_nTheme][1]);

            /* Create CommandBar */
            g_hwndCB = CommandBar_Create(g_hInst, hwnd, 1);
            CreateMenuBar(g_hwndCB);
            CommandBar_AddAdornments(g_hwndCB, 0, 0);

            /* Update menu checkmarks to match loaded settings */
            CheckMenuItem(g_hViewMenu, IDM_VIEW_WORDWRAP,
                g_bWordWrap ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(g_hViewMenu, IDM_VIEW_LINENUMS,
                g_bShowLineNums ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(g_hViewMenu, IDM_VIEW_STATUSBAR,
                g_bShowStatusBar ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(g_hThemeMenu, IDM_VIEW_INVERSE,
                g_bInverseColors ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(g_hThemeMenu, IDM_VIEW_THEME_DEFAULT, MF_UNCHECKED);
            CheckMenuItem(g_hThemeMenu, IDM_VIEW_THEME_DEFAULT + g_nTheme, MF_CHECKED);
            UpdateRecentMenu();

            /* Create Status bar */
            g_hwndStatus = CreateWindowW(STATUSCLASSNAMEW, NULL,
                WS_CHILD | (g_bShowStatusBar ? WS_VISIBLE : 0),
                0, 0, 0, 0, hwnd, (HMENU)ID_STATUSBAR, g_hInst, NULL);

            /* Create monospace font */
            lf.lfHeight = 14;
            lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
            lstrcpyW(lf.lfFaceName, L"Courier New");
            g_hFont = CreateFontIndirectW(&lf);

            /* Create line number gutter */
            g_hwndLineNum = CreateWindowW(
                L"EDIT", L"",
                WS_CHILD | WS_BORDER | (g_bShowLineNums ? WS_VISIBLE : 0) |
                ES_MULTILINE | ES_RIGHT,
                0, 0, g_lineNumWidth, 0,
                hwnd, (HMENU)1003, g_hInst, NULL);
            SendMessage(g_hwndLineNum, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(g_hwndLineNum, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(0, 2));
            g_pfnLineNumProc = (WNDPROC)SetWindowLong(g_hwndLineNum, GWL_WNDPROC,
                (LONG)LineNumProc);

            /* Create Edit control - style depends on word wrap setting */
            editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL;
            if (!g_bWordWrap)
                editStyle |= WS_HSCROLL | ES_AUTOHSCROLL;

            g_hwndEdit = CreateWindowW(
                L"EDIT", NULL, editStyle,
                0, 0, 0, 0,
                hwnd, (HMENU)ID_EDIT, g_hInst, NULL);
            SendMessage(g_hwndEdit, EM_SETMARGINS, EC_LEFTMARGIN, MAKELONG(2, 0));

            SendMessage(g_hwndEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            /* Subclass edit control for cursor tracking */
            g_pfnEditProc = (WNDPROC)SetWindowLong(g_hwndEdit, GWL_WNDPROC,
                (LONG)EditSubclassProc);

            SetFocus(g_hwndEdit);
            UpdateTitle();
            UpdateStatus();
            UpdateLineNumbers();
        }
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

        case IDM_FILE_OPTIONS:
            DoOptions();
            return 0;

        case IDM_FILE_EXIT:
            if (PromptSave()) {
                DestroyWindow(hwnd);
            }
            return 0;

        default:
            /* Handle recent files menu */
            if (LOWORD(wParam) >= IDM_RECENT_BASE && 
                LOWORD(wParam) < IDM_RECENT_BASE + MAX_RECENT_FILES) {
                OpenRecentFile(LOWORD(wParam) - IDM_RECENT_BASE);
                return 0;
            }
            break;

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

        case IDM_EDIT_FIND:
            DoFind();
            return 0;

        case IDM_EDIT_FINDNEXT:
            DoFindNext();
            return 0;

        case IDM_EDIT_REPLACE:
            DoReplace();
            return 0;

        case IDM_EDIT_GOTOLINE:
            DoGotoLine();
            return 0;

        case IDM_VIEW_WORDWRAP:
            {
                LONG style = GetWindowLong(g_hwndEdit, GWL_STYLE);
                g_bWordWrap = !g_bWordWrap;
                if (g_bWordWrap) {
                    style &= ~(WS_HSCROLL | ES_AUTOHSCROLL);
                    CheckMenuItem(g_hViewMenu, IDM_VIEW_WORDWRAP, MF_CHECKED);
                } else {
                    style |= WS_HSCROLL | ES_AUTOHSCROLL;
                    CheckMenuItem(g_hViewMenu, IDM_VIEW_WORDWRAP, MF_UNCHECKED);
                }
                SetWindowLong(g_hwndEdit, GWL_STYLE, style);
                SetWindowPos(g_hwndEdit, NULL, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                UpdateLineNumbers();
                SetFocus(g_hwndEdit);
            }
            return 0;

        case IDM_VIEW_LINENUMS:
            g_bShowLineNums = !g_bShowLineNums;
            CheckMenuItem(g_hViewMenu, IDM_VIEW_LINENUMS,
                g_bShowLineNums ? MF_CHECKED : MF_UNCHECKED);
            ShowWindow(g_hwndLineNum, g_bShowLineNums ? SW_SHOW : SW_HIDE);
            SendMessage(hwnd, WM_SIZE, 0, 0);
            if (g_bShowLineNums) UpdateLineNumbers();
            SetFocus(g_hwndEdit);
            return 0;

        case IDM_VIEW_STATUSBAR:
            g_bShowStatusBar = !g_bShowStatusBar;
            ShowWindow(g_hwndStatus, g_bShowStatusBar ? SW_SHOW : SW_HIDE);
            CheckMenuItem(g_hViewMenu, IDM_VIEW_STATUSBAR,
                g_bShowStatusBar ? MF_CHECKED : MF_UNCHECKED);
            SendMessage(hwnd, WM_SIZE, 0, 0);
            return 0;

        case IDM_VIEW_THEME_DEFAULT:
        case IDM_VIEW_THEME_GREEN:
        case IDM_VIEW_THEME_AMBER:
        case IDM_VIEW_THEME_BLUE:
            CheckMenuItem(g_hThemeMenu, IDM_VIEW_THEME_DEFAULT + g_nTheme, MF_UNCHECKED);
            g_nTheme = LOWORD(wParam) - IDM_VIEW_THEME_DEFAULT;
            CheckMenuItem(g_hThemeMenu, IDM_VIEW_THEME_DEFAULT + g_nTheme, MF_CHECKED);
            UpdateTheme();
            return 0;

        case IDM_VIEW_INVERSE:
            g_bInverseColors = !g_bInverseColors;
            CheckMenuItem(g_hThemeMenu, IDM_VIEW_INVERSE,
                g_bInverseColors ? MF_CHECKED : MF_UNCHECKED);
            UpdateTheme();
            return 0;

        case IDM_VIEW_FULLSCREEN:
            g_bFullScreen = !g_bFullScreen;
            ShowWindow(g_hwndStatus, g_bFullScreen ? SW_HIDE : (g_bShowStatusBar ? SW_SHOW : SW_HIDE));
            ShowWindow(g_hwndCB, g_bFullScreen ? SW_HIDE : SW_SHOW);
            CheckMenuItem(g_hViewMenu, IDM_VIEW_FULLSCREEN,
                g_bFullScreen ? MF_CHECKED : MF_UNCHECKED);
            SendMessage(hwnd, WM_SIZE, 0, 0);
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
                UpdateStatus();
                UpdateLineNumbers();
            }
            return 0;
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == g_hwndLineNum || (HWND)lParam == g_hwndEdit) {
            COLORREF fg = g_bInverseColors ? g_themes[g_nTheme][1] : g_themes[g_nTheme][0];
            COLORREF bg = g_bInverseColors ? g_themes[g_nTheme][0] : g_themes[g_nTheme][1];
            SetTextColor((HDC)wParam, fg);
            SetBkColor((HDC)wParam, bg);
            return (LRESULT)g_hBrushBg;
        }
        break;

    case WM_DESTROY:
        RestoreSelectionColors();
        SaveSettings();
        if (g_hBrushBg) DeleteObject(g_hBrushBg);
        if (g_hFont) DeleteObject(g_hFont);
        CommandBar_Destroy(g_hwndCB);
        PostQuitMessage(0);
        return 0;

    case WM_INITMENUPOPUP:
        if (g_bThemedSelection)
            RestoreSelectionColors();
        break;

    case WM_ENTERMENULOOP:
        if (g_bThemedSelection)
            RestoreSelectionColors();
        break;

    case WM_EXITMENULOOP:
        if (g_bThemedSelection && GetFocus() == g_hwndEdit)
            ApplySelectionColors();
        break;

    case WM_CLOSE:
        if (PromptSave()) {
            DestroyWindow(hwnd);
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
