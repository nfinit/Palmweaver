/*
 * Palmweaver - Text Editor for Windows CE
 * palmweaver.c - Main application
 *
 * Build 0.1.0.1 - Initial window with CommandBar and Edit control
 */

#include <windows.h>
#include <commctrl.h>
#include "resource.h"

/* Global instance handle (CE has no GetModuleHandle) */
HINSTANCE g_hInst;
HWND g_hwndMain;
HWND g_hwndCB;
HWND g_hwndEdit;

/* Window class name */
static const WCHAR g_szClassName[] = L"PalmweaverMain";
static const WCHAR g_szAppTitle[] = L"Palmweaver";

/* Forward declarations */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static BOOL InitApplication(HINSTANCE hInstance);
static BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
static void CreateMenuBar(HWND hwndCB);
static void OnSize(HWND hwnd, int cx, int cy);
static void ShowAboutDialog(HWND hwndParent);

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
            SetWindowTextW(g_hwndEdit, L"");
            return 0;

        case IDM_FILE_EXIT:
            DestroyWindow(hwnd);
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
        }
        break;

    case WM_DESTROY:
        CommandBar_Destroy(g_hwndCB);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
