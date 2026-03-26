/*
 * Palmweaver - Text Editor for Windows CE
 * palmweaver.c - Main application
 *
 * Build 0.1.0.4 - Add File Open/Save/Save As
 */

#include <windows.h>
#include <commctrl.h>
#include "resource.h"
#include "undo.h"

/*
 * CE SDK header gaps - these constants exist in desktop Windows headers but
 * are missing from CE 2.0 SDK headers. We define them ourselves since the
 * underlying OS still supports the functionality.
 */
#ifndef ICON_SMALL
#define ICON_SMALL 0  /* WM_SETICON parameter for small (title bar) icon */
#endif

#ifndef IDC_ARROW
#define IDC_ARROW MAKEINTRESOURCE(32512)  /* Standard arrow cursor resource */
#endif

#ifndef IDC_WAIT
#define IDC_WAIT MAKEINTRESOURCE(32514)  /* Standard wait/hourglass cursor resource */
#endif

#ifndef WM_SETCURSOR
#define WM_SETCURSOR 0x0020  /* Cursor-setting message missing in some CE headers */
#endif

#ifndef SM_MOUSEPRESENT
#define SM_MOUSEPRESENT 19  /* GetSystemMetrics index for mouse presence */
#endif

#ifndef SM_CXVSCROLL
#define SM_CXVSCROLL 2  /* Vertical scrollbar width metric */
#endif

#ifndef SM_CYHSCROLL
#define SM_CYHSCROLL 3  /* Horizontal scrollbar height metric */
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
HWND g_hwndPagedVScroll;
HWND g_hwndPagedCorner;
HWND g_hwndPagedSeam;
HFONT g_hFont;
HBRUSH g_hBrushBg;
static HBRUSH g_hBrushColInd = NULL;  /* Cached column indicator brush */
HMENU g_hViewMenu;
HMENU g_hThemeMenu;
HMENU g_hRecentMenu;

/* Current file state */
static wchar_t g_szFilePath[MAX_PATH];
static int g_bDirty = 0;
static int g_fileNewlineStyle = 1; /* 1=CRLF, 2=LF, 3=CR, 4=mixed, 5=none/unknown */
int g_nNewlineMode = 0;            /* 0=Auto(preserve), 1=CRLF, 2=LF */
int g_bWordWrap = 1;  /* Word wrap on by default */
int g_bShowLineNums = 1;  /* Line numbers on by default */
int g_bShowStatusBar = 1; /* Status bar on by default */
int g_bFullScreen = 0;    /* Full screen off by default */
static int g_bFSStatusBar = 0; /* Temporary status bar visibility in fullscreen */
int g_bInverseColors = 0; /* Inverse fg/bg of current theme */
int g_nTheme = 0;         /* Color theme: 0=default, 1=green, 2=amber, 3=blue */
int g_bThemedSelection = 0; /* Theme selection highlight colors (opt-in) */
int g_bShowScrollbars = 1;  /* Scrollbars on by default */
int g_bHideTaskbar = 0;     /* Hide taskbar in fullscreen (off by default) */
static int g_lineNumWidth = 20;
static UINT g_lineNumDirtyFlags = 0;
static int g_lineNumTimerActive = 0;
static UINT g_lineNumTextSeq = 0;
static int g_bForceImmediateLineNum = 0;
static int g_lastEditLineCount = -1;
static wchar_t *g_lineNumRenderBuf = NULL;
static wchar_t *g_lineNumCachedOutput = NULL;
static int g_lineNumBufCap = 0;
static UINT g_statusTotalsSeq = 1;
static int g_busyDepth = 0;
static int g_bMousePresent = 1;

#define LINENUM_TIMER_ID         0x4C4E  /* 'LN' */
#define LINENUM_TIMER_SCROLL_MS  60
#define LINENUM_TIMER_TEXT_MS    500
#define LINENUM_DIRTY_TEXT       0x01
#define LINENUM_DIRTY_SCROLL     0x02
#define LINENUM_DIRTY_LAYOUT     0x04
#define PWM_EDIT_POSTJUMP_REPAINT (WM_APP + 0x45)
#define PWM_IS_QUICKNOTE_WINDOW  (WM_APP + 0x46)
#define PWM_QUICKNOTE_ACTIVATE   (WM_APP + 0x47)
#define EDIT_TEXT_LIMIT          0x7FFFFFFE
#define STATUS_TOTALS_INTERVAL_MS 350
#define BUSY_TEXT_THRESHOLD      65535
#define FILE_LOAD_MAX_BYTES      (2UL * 1024UL * 1024UL)
#define FILE_IO_READ_CHUNK       4096
#define NEWLINE_STYLE_CRLF       1
#define NEWLINE_STYLE_LF         2
#define NEWLINE_STYLE_CR         3
#define NEWLINE_STYLE_MIXED      4
#define NEWLINE_STYLE_NONE       5
#define NEWLINE_MODE_AUTO        0
#define NEWLINE_MODE_FORCE_CRLF  1
#define NEWLINE_MODE_FORCE_LF    2
#define PAGED_MODE_THRESHOLD_CHARS 60000
#define PAGED_WINDOW_CHARS_NOWRAP 49152
#define PAGED_WINDOW_CHARS_WRAP   16384
#define PAGED_EDGE_CHARS_NOWRAP   4096
#define PAGED_EDGE_CHARS_WRAP     2048
#define PAGED_VSCROLL_EXTRA_W      0
#define PAGED_VSCROLL_SEAM_OVERLAP 2

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
int g_nColumnLimit = 80;  /* Column limit for reflow */
int g_bAutoWrapTyping = 0;  /* Hard-wrap while typing at column limit */
int g_bShowColumnIndicator = 0;  /* Show visual column indicator (off by default) */

/* Quick Note settings */
int g_bQuickNoteStorage = 0;     /* Prefer storage card for quick notes */
int g_bQuickNoteAutoInit = 0;    /* Auto-create Notes folder on card without prompt */
static int s_bSkipStorageCard = 0;  /* Session flag: user chose device memory */
static int g_bQuickNoteWindow = 0;  /* This process hosts dedicated Quick Note window */

/* Font settings */
static int g_fontSizes[] = {10, 12, 14, 16};
static int g_fontSizeIdx = 2;  /* Default 14 */
static int g_bFixedFont = 1;   /* Default fixed (Courier New) */

/* Display capabilities */
static int g_bColorDisplay = 1;  /* 0 = grayscale, 1 = color */

/* Recent files */
wchar_t g_recentFiles[MAX_RECENT_FILES][MAX_PATH] = {0};
int g_recentCount = 0;

/* Edit control subclass */
static WNDPROC g_pfnEditProc = NULL;
static WNDPROC g_pfnLineNumProc = NULL;
static int g_bReplaceTypingGroupOpen = 0;
static int g_bVScrollThumbTrackActive = 0;
static int g_bPostJumpRepaintPending = 0;
static int g_bPagedMode = 0;
static int g_bPagedLoading = 0;
static int g_bPagedPageDirty = 0;
static int g_bPreserveUndoOnPagedLoad = 0;
static int g_bKeepPagedReplaceUndo = 0;
static wchar_t *g_pagedText = NULL;
static int g_pagedTextLen = 0;
static int g_pagedPageStart = 0;
static int g_pagedPageLen = 0;
static wchar_t *g_pagedSwapBuf = NULL;
static int g_pagedSwapCap = 0;
static int *g_pagedLineStarts = NULL;
static int g_pagedLineCount = 1;
static int g_bMainDestroying = 0;
static char *g_fileIoByteBuf = NULL;
static DWORD g_fileIoByteCap = 0;
static wchar_t *g_fileIoWideBuf = NULL;
static int g_fileIoWideCap = 0;

/* Window class name */
static const WCHAR g_szClassName[] = L"PalmweaverMain";
static const WCHAR g_szAppTitle[] = L"Palmweaver";
static const WCHAR g_szUntitled[] = L"Untitled";

/* File filter for picker */
static const WCHAR g_szFilter[] = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";

/* Status bar message */
static wchar_t g_szStatusMsg[128] = L"";
static int g_bForceStatusRefresh = 0;

/* Forward declarations */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static BOOL InitApplication(HINSTANCE hInstance);
static BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
static void CreateMenuBar(HWND hwndCB);
static void OnSize(HWND hwnd, int cx, int cy);
static void ShowAboutDialog(HWND hwndParent);
static void UpdateTitle(void);
static void BeginBusyCursor(const wchar_t *tag);
static void EndBusyCursor(const wchar_t *tag);
static void ForceIdleCursor(void);
static int GetLoadFileSizeGuarded(HANDLE hFile, DWORD *outSize, const wchar_t *sourceLabel);
static int EnsureFileIoByteBuffer(DWORD requiredBytes);
static int IsLikelyUtf8NoBom(HANDLE hFile, DWORD fileSize);
static int DetectNewlineStyle(const wchar_t *text, int len, int *outNeedsNormalize);
static int NormalizeScratchNewlinesToCrLf(int *inOutLen);
static int ReadFileToUnicodeScratch(HANDLE hFile, DWORD fileSize, wchar_t **outText, int *outLen, int *outNewlineStyle);
static int WriteWideTextWithStyle(HANDLE hFile, const wchar_t *text, DWORD len, int newlineStyle);
static int GetDefaultNewlineStyleForNewFile(void);
static int GetSaveNewlineStyle(void);
static wchar_t *AllocOwnedUnicodeCopy(const wchar_t *text, int len);
static int EnsureLineNumBuffers(int requiredChars);
static void MarkStatusTotalsDirty(void);
static void UpdateStatus(void);
static void SetStatusMessage(const wchar_t *msg);
static void ClearStatusMessage(void);
static int CaptureEditRangeTextFull(HWND hwnd, DWORD start, DWORD end, wchar_t **outText, int *outLen);
static int CaptureEditRangeText(HWND hwnd, DWORD start, DWORD end, wchar_t **outText, int *outLen);
static void RecordUndoDeleteRange(HWND hwnd, DWORD start, DWORD end);
static int IsLikelyTypingKey(UINT vk, int ctrl, int alt);
static void CloseReplaceTypingGroup(void);
static int CmdLineHasQuickNoteSwitch(const wchar_t *cmdLine);
static HWND FindQuickNoteWindow(HWND hwndExclude);
static void ActivateQuickNoteWindow(HWND hwnd, int appendNewline);
static int LaunchQuickNoteWindowProcess(void);
static void DoQuickNoteHotkey(void);
static int EditEndsWithNewline(HWND hwndEdit);
static void QuickNoteAppendNewline(void);
static void UpdateTheme(void);
static void ApplySelectionColors(void);
static void RestoreSelectionColors(void);
static void InvalidateColumnIndicator(void);
static void SetEditVerticalScrollbarVisible(int visible);
static void QueueEditRepaintAfterJump(void);
static void PagedReset(void);
static int PagedRebuildLineStarts(void);
static int PagedCommitPage(void);
static int PagedLoadWindowAt(int globalPos);
static int PagedEnableWithText(wchar_t *text, int len);
static int PagedGetWindowChars(void);
static int PagedGetEdgeChars(void);
static int PagedEnsureSwapBuffer(int requiredChars);
static int PagedGetGlobalSelStart(void);
static int PagedPrepareUndoTarget(int localPos);
static void PagedIndexToLineCol(int index, int *outLine, int *outCol);
static int PagedGetGlobalLineFromLocalChar(int localChar);
static int PagedGetVisibleRows(void);
static void PagedSyncVScroll(void);
static int PagedHandleThumbScroll(UINT scrollCode);
static int PagedProcessScrollCode(UINT scrollCode);
static void PagedMaybeShiftWindowByCaret(void);
static void PagedHandleVScrollEdge(UINT scrollCode);
static int PerformTrackedUndo(void);
static int PerformTrackedRedo(void);
static void DoFileNew(void);
static int DoFileOpen(void);
static int DoFileSave(void);
static int DoFileSaveAs(void);
static int PromptSave(void);
static void DoGotoLine(void);
static void DoFind(void);
static void DoFindNext(void);
static void RefreshEditAfterLargeJump(void);
static void DoReplace(void);
static void DoInsertDateTime(int mode);
static void DoInsertRule(void);
static void DoQuickNote(void);
static void DoReflow(void);
static void UpdateLineNumbers(void);
static void RequestLineNumberRefresh(UINT flags, int immediate);
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

static void ForceIdleCursor(void)
{
    if (!g_bMousePresent) {
        SetCursor(NULL);
        return;
    }

    {
        HCURSOR hArrow = LoadCursor(NULL, IDC_ARROW);
        if (hArrow) SetCursor(hArrow);
        else SetCursor(NULL);
    }
}

static void BeginBusyCursor(const wchar_t *tag)
{
    (void)tag;
    g_busyDepth++;
    if (g_hwndStatus) UpdateStatus();
}

static void MarkStatusTotalsDirty(void)
{
    g_statusTotalsSeq++;
    if (g_statusTotalsSeq == 0) g_statusTotalsSeq = 1;
}

static void EndBusyCursor(const wchar_t *tag)
{
    if (g_busyDepth <= 0) return;

    (void)tag;
    g_busyDepth--;
    if (g_hwndStatus) UpdateStatus();
}

static void RefreshStatusAfterDocumentLoad(void)
{
    g_bForceStatusRefresh = 1;
    MarkStatusTotalsDirty();
    UpdateStatus();
}

static int GetLoadFileSizeGuarded(HANDLE hFile, DWORD *outSize, const wchar_t *sourceLabel)
{
    DWORD fileSize;
    DWORD sizeErr;
    wchar_t msg[192];

    if (!outSize || hFile == INVALID_HANDLE_VALUE) return 0;

    fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE) {
        sizeErr = GetLastError();
        if (sizeErr != NO_ERROR) {
            MessageBoxW(g_hwndMain, L"Cannot determine file size.", g_szAppTitle, MB_OK | MB_ICONERROR);
            return 0;
        }
    }

    if (fileSize > FILE_LOAD_MAX_BYTES) {
        if (sourceLabel && sourceLabel[0]) {
            wsprintfW(msg, L"%s file is too large to open safely on this device.", sourceLabel);
        } else {
            lstrcpyW(msg, L"File is too large to open safely on this device.");
        }
        MessageBoxW(g_hwndMain, msg, g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    *outSize = fileSize;
    return 1;
}

static int EnsureFileIoByteBuffer(DWORD requiredBytes)
{
    DWORD newCap;
    char *newBuf;

    if (requiredBytes < 1) requiredBytes = 1;
    if (g_fileIoByteBuf && g_fileIoByteCap >= requiredBytes) return 1;

    newCap = g_fileIoByteCap;
    if (newCap < 4096) newCap = 4096;
    while (newCap < requiredBytes) {
        if (newCap > 0x40000000UL) {
            newCap = requiredBytes;
            break;
        }
        newCap *= 2;
    }

    newBuf = (char *)LocalAlloc(LMEM_FIXED, newCap);
    if (!newBuf) {
        newCap = requiredBytes;
        newBuf = (char *)LocalAlloc(LMEM_FIXED, newCap);
        if (!newBuf) return 0;
    }

    if (g_fileIoByteBuf) LocalFree(g_fileIoByteBuf);
    g_fileIoByteBuf = newBuf;
    g_fileIoByteCap = newCap;
    return 1;
}

static int EnsureFileIoWideBufferAppend(int requiredChars, int keepChars)
{
    int newCap;
    wchar_t *newBuf;
    int i;

    if (requiredChars < 1) requiredChars = 1;
    if (keepChars < 0) keepChars = 0;
    if (g_fileIoWideBuf && g_fileIoWideCap >= requiredChars) return 1;

    newCap = g_fileIoWideCap;
    if (newCap < 4096) newCap = 4096;
    while (newCap < requiredChars) {
        if (newCap > 0x20000000) {
            newCap = requiredChars;
            break;
        }
        newCap *= 2;
    }

    newBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
    if (!newBuf) {
        newCap = requiredChars;
        newBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
        if (!newBuf) return 0;
    }

    if (g_fileIoWideBuf && keepChars > 0) {
        if (keepChars > newCap) keepChars = newCap;
        for (i = 0; i < keepChars; i++) newBuf[i] = g_fileIoWideBuf[i];
    }
    if (keepChars <= newCap) newBuf[keepChars] = 0;

    if (g_fileIoWideBuf) LocalFree(g_fileIoWideBuf);
    g_fileIoWideBuf = newBuf;
    g_fileIoWideCap = newCap;
    return 1;
}

static int IsLikelyUtf8NoBom(HANDLE hFile, DWORD fileSize)
{
    DWORD dwRead = 0;
    DWORD bytesRemaining = 0;
    DWORD toRead = 0;
    unsigned char *uBuf;
    int i;
    int byteVal;
    int reconsume;
    int utf8Need = 0;
    unsigned long utf8Code = 0;
    unsigned long utf8Min = 0;
    int utf8SeqCount = 0;
    int highByteCount = 0;
    int invalidCount = 0;

    if (hFile == INVALID_HANDLE_VALUE || fileSize == 0) return 0;
    if (!EnsureFileIoByteBuffer(FILE_IO_READ_CHUNK + 4)) return 0;
    uBuf = (unsigned char *)g_fileIoByteBuf;

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    bytesRemaining = fileSize;

    while (bytesRemaining > 0) {
        toRead = bytesRemaining;
        if (toRead > FILE_IO_READ_CHUNK) toRead = FILE_IO_READ_CHUNK;
        if (!ReadFile(hFile, g_fileIoByteBuf, toRead, &dwRead, NULL)) {
            SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
            return 0;
        }
        if (dwRead == 0) break;
        bytesRemaining -= dwRead;

        for (i = 0; i < (int)dwRead; i++) {
            byteVal = (int)uBuf[i];
            if (byteVal >= 0x80) highByteCount++;
            reconsume = 1;

            while (reconsume) {
                reconsume = 0;

                if (utf8Need == 0) {
                    if (byteVal <= 0x7F) {
                        /* ASCII */
                    } else if (byteVal >= 0xC2 && byteVal <= 0xDF) {
                        utf8Need = 1;
                        utf8Code = (unsigned long)(byteVal & 0x1F);
                        utf8Min = 0x80UL;
                    } else if (byteVal >= 0xE0 && byteVal <= 0xEF) {
                        utf8Need = 2;
                        utf8Code = (unsigned long)(byteVal & 0x0F);
                        utf8Min = 0x800UL;
                    } else if (byteVal >= 0xF0 && byteVal <= 0xF4) {
                        utf8Need = 3;
                        utf8Code = (unsigned long)(byteVal & 0x07);
                        utf8Min = 0x10000UL;
                    } else {
                        invalidCount++;
                    }
                } else {
                    if ((byteVal & 0xC0) == 0x80) {
                        utf8Code = (utf8Code << 6) | (unsigned long)(byteVal & 0x3F);
                        utf8Need--;
                        if (utf8Need == 0) {
                            if (utf8Code < utf8Min ||
                                utf8Code > 0x10FFFFUL ||
                                (utf8Code >= 0xD800UL && utf8Code <= 0xDFFFUL)) {
                                invalidCount++;
                            } else {
                                utf8SeqCount++;
                            }
                        }
                    } else {
                        invalidCount++;
                        utf8Need = 0;
                        utf8Code = 0;
                        utf8Min = 0;
                        reconsume = 1;
                    }
                }
            }

            if (invalidCount > 0) break;
        }

        if (invalidCount > 0) break;
    }

    if (utf8Need > 0) invalidCount++;

    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    if (highByteCount == 0) return 0;      /* Pure ASCII: ACP and UTF-8 are equivalent. */
    if (invalidCount != 0) return 0;
    return (utf8SeqCount > 0) ? 1 : 0;
}

static int DetectNewlineStyle(const wchar_t *text, int len, int *outNeedsNormalize)
{
    int crlfCount = 0;
    int loneLfCount = 0;
    int loneCrCount = 0;
    int i;

    if (outNeedsNormalize) *outNeedsNormalize = 0;
    if (!text || len <= 0) return NEWLINE_STYLE_NONE;

    for (i = 0; i < len; i++) {
        if (text[i] == L'\r') {
            if (i + 1 < len && text[i + 1] == L'\n') {
                crlfCount++;
                i++;
            } else {
                loneCrCount++;
            }
        } else if (text[i] == L'\n') {
            loneLfCount++;
        }
    }

    if (outNeedsNormalize) {
        if (loneLfCount > 0 || loneCrCount > 0) *outNeedsNormalize = 1;
    }

    if (crlfCount > 0 && loneLfCount == 0 && loneCrCount == 0) return NEWLINE_STYLE_CRLF;
    if (loneLfCount > 0 && crlfCount == 0 && loneCrCount == 0) return NEWLINE_STYLE_LF;
    if (loneCrCount > 0 && crlfCount == 0 && loneLfCount == 0) return NEWLINE_STYLE_CR;
    if (crlfCount == 0 && loneLfCount == 0 && loneCrCount == 0) return NEWLINE_STYLE_NONE;
    return NEWLINE_STYLE_MIXED;
}

static int NormalizeScratchNewlinesToCrLf(int *inOutLen)
{
    int srcLen;
    int dstLen;
    int srcPos;
    int dstPos;

    if (!inOutLen || !g_fileIoWideBuf) return 0;
    srcLen = *inOutLen;
    if (srcLen <= 0) return 1;

    dstLen = srcLen;
    for (srcPos = 0; srcPos < srcLen; srcPos++) {
        if (g_fileIoWideBuf[srcPos] == L'\r') {
            if (srcPos + 1 < srcLen && g_fileIoWideBuf[srcPos + 1] == L'\n') {
                srcPos++;
            } else {
                dstLen++;
            }
        } else if (g_fileIoWideBuf[srcPos] == L'\n') {
            dstLen++;
        }
    }

    if (dstLen == srcLen) return 1;
    if (!EnsureFileIoWideBufferAppend(dstLen + 1, srcLen)) return 0;

    srcPos = srcLen - 1;
    dstPos = dstLen - 1;
    while (srcPos >= 0) {
        if (g_fileIoWideBuf[srcPos] == L'\n') {
            if (srcPos > 0 && g_fileIoWideBuf[srcPos - 1] == L'\r') {
                g_fileIoWideBuf[dstPos--] = L'\n';
                g_fileIoWideBuf[dstPos--] = L'\r';
                srcPos -= 2;
            } else {
                g_fileIoWideBuf[dstPos--] = L'\n';
                g_fileIoWideBuf[dstPos--] = L'\r';
                srcPos--;
            }
        } else if (g_fileIoWideBuf[srcPos] == L'\r') {
            g_fileIoWideBuf[dstPos--] = L'\n';
            g_fileIoWideBuf[dstPos--] = L'\r';
            srcPos--;
        } else {
            g_fileIoWideBuf[dstPos--] = g_fileIoWideBuf[srcPos--];
        }
    }

    *inOutLen = dstLen;
    return 1;
}

static int ReadFileToUnicodeScratch(HANDLE hFile, DWORD fileSize, wchar_t **outText, int *outLen, int *outNewlineStyle)
{
    DWORD dwRead = 0;
    DWORD bytesRemaining = 0;
    DWORD toRead = 0;
    unsigned char bom[3];
    unsigned char *uBuf;
    int textLen = 0;
    int i;
    int decodeMode = 0; /* 0 = ACP fallback, 1 = UTF-16 LE BOM, 2 = UTF-8 BOM, 3 = likely UTF-8 no BOM */
    int startOffset = 0;
    int pendingUtf16Lo = -1;
    int carryByte = -1;
    int utf8Need = 0;
    unsigned long utf8Code = 0;
    unsigned long utf8Min = 0;
    int utf8SeqLen = 0;
    unsigned char utf8Seq[4];
    int chunkBytes;
    int readOffset;
    int needChars;
    int converted;
    int byteVal;
    int reconsume;
    int valid;
    unsigned long cp;
    int j;
    int newlineStyle;
    int needsNormalize;

    if (!outText || !outLen || hFile == INVALID_HANDLE_VALUE) return 0;
    *outText = NULL;
    *outLen = 0;
    if (outNewlineStyle) *outNewlineStyle = NEWLINE_STYLE_CRLF;

    if (!EnsureFileIoByteBuffer(FILE_IO_READ_CHUNK + 4)) return 0;
    if (!EnsureFileIoWideBufferAppend(4096, 0)) return 0;
    uBuf = (unsigned char *)g_fileIoByteBuf;

    if (fileSize >= 2) {
        toRead = (fileSize >= 3) ? 3 : 2;
        if (!ReadFile(hFile, bom, toRead, &dwRead, NULL)) return 0;
        if (dwRead >= 2 && bom[0] == 0xFF && bom[1] == 0xFE) {
            decodeMode = 1;
            startOffset = 2;
        } else if (dwRead >= 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
            decodeMode = 2;
            startOffset = 3;
        }
    }
    if (decodeMode == 0 && fileSize > 0) {
        if (IsLikelyUtf8NoBom(hFile, fileSize)) decodeMode = 3;
    }
    SetFilePointer(hFile, startOffset, NULL, FILE_BEGIN);
    bytesRemaining = fileSize - (DWORD)startOffset;

    if (decodeMode == 1) {
        int expectedChars = (int)(bytesRemaining / 2) + 2;
        if (!EnsureFileIoWideBufferAppend(expectedChars, 0)) return 0;

        while (bytesRemaining > 0) {
            toRead = bytesRemaining;
            if (toRead > FILE_IO_READ_CHUNK) toRead = FILE_IO_READ_CHUNK;
            if (!ReadFile(hFile, g_fileIoByteBuf, toRead, &dwRead, NULL)) return 0;
            if (dwRead == 0) break;
            bytesRemaining -= dwRead;

            for (i = 0; i < (int)dwRead; i++) {
                if (pendingUtf16Lo < 0) {
                    pendingUtf16Lo = (int)uBuf[i];
                } else {
                    if (!EnsureFileIoWideBufferAppend(textLen + 2, textLen)) return 0;
                    g_fileIoWideBuf[textLen++] = (wchar_t)(pendingUtf16Lo | ((int)uBuf[i] << 8));
                    pendingUtf16Lo = -1;
                }
            }
        }

        if (pendingUtf16Lo >= 0) {
            if (!EnsureFileIoWideBufferAppend(textLen + 2, textLen)) return 0;
            g_fileIoWideBuf[textLen++] = (wchar_t)pendingUtf16Lo;
        }
    } else if (decodeMode == 2 || decodeMode == 3) {
        if (!EnsureFileIoWideBufferAppend((int)fileSize + 2, 0)) return 0;
        utf8Need = 0;
        utf8Code = 0;
        utf8Min = 0;
        utf8SeqLen = 0;

        while (bytesRemaining > 0) {
            toRead = bytesRemaining;
            if (toRead > FILE_IO_READ_CHUNK) toRead = FILE_IO_READ_CHUNK;

            if (!ReadFile(hFile, g_fileIoByteBuf, toRead, &dwRead, NULL)) return 0;
            if (dwRead == 0) break;
            bytesRemaining -= dwRead;

            chunkBytes = (int)dwRead;
            for (i = 0; i < chunkBytes; i++) {
                byteVal = (int)uBuf[i];
                reconsume = 1;

                while (reconsume) {
                    reconsume = 0;

                    if (utf8Need == 0) {
                        if (byteVal <= 0x7F) {
                            if (!EnsureFileIoWideBufferAppend(textLen + 2, textLen)) return 0;
                            g_fileIoWideBuf[textLen++] = (wchar_t)byteVal;
                        } else if (byteVal >= 0xC2 && byteVal <= 0xDF) {
                            utf8Need = 1;
                            utf8Code = (unsigned long)(byteVal & 0x1F);
                            utf8Min = 0x80UL;
                            utf8Seq[0] = (unsigned char)byteVal;
                            utf8SeqLen = 1;
                        } else if (byteVal >= 0xE0 && byteVal <= 0xEF) {
                            utf8Need = 2;
                            utf8Code = (unsigned long)(byteVal & 0x0F);
                            utf8Min = 0x800UL;
                            utf8Seq[0] = (unsigned char)byteVal;
                            utf8SeqLen = 1;
                        } else if (byteVal >= 0xF0 && byteVal <= 0xF4) {
                            utf8Need = 3;
                            utf8Code = (unsigned long)(byteVal & 0x07);
                            utf8Min = 0x10000UL;
                            utf8Seq[0] = (unsigned char)byteVal;
                            utf8SeqLen = 1;
                        } else {
                            if (!EnsureFileIoWideBufferAppend(textLen + 2, textLen)) return 0;
                            g_fileIoWideBuf[textLen++] = (wchar_t)byteVal;
                        }
                    } else {
                        if ((byteVal & 0xC0) == 0x80) {
                            utf8Code = (utf8Code << 6) | (unsigned long)(byteVal & 0x3F);
                            if (utf8SeqLen < 4) utf8Seq[utf8SeqLen++] = (unsigned char)byteVal;
                            utf8Need--;

                            if (utf8Need == 0) {
                                valid = 1;
                                if (utf8Code < utf8Min) valid = 0;
                                if (utf8Code > 0x10FFFFUL) valid = 0;
                                if (utf8Code >= 0xD800UL && utf8Code <= 0xDFFFUL) valid = 0;

                                if (valid) {
                                    if (utf8Code <= 0xFFFFUL) {
                                        if (!EnsureFileIoWideBufferAppend(textLen + 2, textLen)) return 0;
                                        g_fileIoWideBuf[textLen++] = (wchar_t)utf8Code;
                                    } else {
                                        cp = utf8Code - 0x10000UL;
                                        if (!EnsureFileIoWideBufferAppend(textLen + 3, textLen)) return 0;
                                        g_fileIoWideBuf[textLen++] = (wchar_t)(0xD800 + (cp >> 10));
                                        g_fileIoWideBuf[textLen++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
                                    }
                                } else {
                                    if (!EnsureFileIoWideBufferAppend(textLen + utf8SeqLen + 1, textLen)) return 0;
                                    for (j = 0; j < utf8SeqLen; j++) {
                                        g_fileIoWideBuf[textLen++] = (wchar_t)utf8Seq[j];
                                    }
                                }

                                utf8Need = 0;
                                utf8Code = 0;
                                utf8Min = 0;
                                utf8SeqLen = 0;
                            }
                        } else {
                            if (!EnsureFileIoWideBufferAppend(textLen + utf8SeqLen + 1, textLen)) return 0;
                            for (j = 0; j < utf8SeqLen; j++) {
                                g_fileIoWideBuf[textLen++] = (wchar_t)utf8Seq[j];
                            }

                            utf8Need = 0;
                            utf8Code = 0;
                            utf8Min = 0;
                            utf8SeqLen = 0;
                            reconsume = 1;
                        }
                    }
                }
            }
        }

        if (utf8Need > 0 && utf8SeqLen > 0) {
            if (!EnsureFileIoWideBufferAppend(textLen + utf8SeqLen + 1, textLen)) return 0;
            for (i = 0; i < utf8SeqLen; i++) {
                g_fileIoWideBuf[textLen++] = (wchar_t)utf8Seq[i];
            }
        }
    } else {
        if (!EnsureFileIoWideBufferAppend((int)fileSize + 2, 0)) return 0;

        while (bytesRemaining > 0) {
            readOffset = (carryByte >= 0) ? 1 : 0;
            if (readOffset) uBuf[0] = (unsigned char)carryByte;

            toRead = bytesRemaining;
            if (toRead > (DWORD)(FILE_IO_READ_CHUNK - readOffset)) {
                toRead = (DWORD)(FILE_IO_READ_CHUNK - readOffset);
            }

            if (!ReadFile(hFile, g_fileIoByteBuf + readOffset, toRead, &dwRead, NULL)) return 0;
            if (dwRead == 0) break;
            bytesRemaining -= dwRead;

            chunkBytes = (int)dwRead + readOffset;
            carryByte = -1;

            /* Prefer ACP conversion in-place; retry with one-byte carry on boundary failures. */
            needChars = MultiByteToWideChar(CP_ACP, 0, g_fileIoByteBuf, chunkBytes, NULL, 0);
            if (needChars <= 0 && chunkBytes > 1 && bytesRemaining > 0) {
                needChars = MultiByteToWideChar(CP_ACP, 0, g_fileIoByteBuf, chunkBytes - 1, NULL, 0);
                if (needChars > 0) {
                    if (!EnsureFileIoWideBufferAppend(textLen + needChars + 1, textLen)) return 0;
                    converted = MultiByteToWideChar(
                        CP_ACP, 0, g_fileIoByteBuf, chunkBytes - 1, g_fileIoWideBuf + textLen, needChars);
                    if (converted > 0) {
                        textLen += converted;
                        carryByte = (int)uBuf[chunkBytes - 1];
                        continue;
                    }
                    needChars = 0;
                }
            }

            if (needChars > 0) {
                if (!EnsureFileIoWideBufferAppend(textLen + needChars + 1, textLen)) return 0;
                converted = MultiByteToWideChar(
                    CP_ACP, 0, g_fileIoByteBuf, chunkBytes, g_fileIoWideBuf + textLen, needChars);
                if (converted > 0) {
                    textLen += converted;
                    continue;
                }
            }

            if (!EnsureFileIoWideBufferAppend(textLen + chunkBytes + 1, textLen)) return 0;
            for (i = 0; i < chunkBytes; i++) {
                g_fileIoWideBuf[textLen++] = (wchar_t)uBuf[i];
            }
        }

        if (carryByte >= 0) {
            if (!EnsureFileIoWideBufferAppend(textLen + 2, textLen)) return 0;
            g_fileIoWideBuf[textLen++] = (wchar_t)carryByte;
        }
    }

    newlineStyle = DetectNewlineStyle(g_fileIoWideBuf, textLen, &needsNormalize);
    if (needsNormalize) {
        if (!NormalizeScratchNewlinesToCrLf(&textLen)) return 0;
    }

    if (!EnsureFileIoWideBufferAppend(textLen + 1, textLen)) return 0;
    g_fileIoWideBuf[textLen] = 0;
    *outText = g_fileIoWideBuf;
    *outLen = textLen;
    if (outNewlineStyle) *outNewlineStyle = newlineStyle;
    return 1;
}

static wchar_t *AllocOwnedUnicodeCopy(const wchar_t *text, int len)
{
    wchar_t *owned;
    int i;

    if (!text || len < 0) return NULL;

    owned = (wchar_t *)LocalAlloc(LMEM_FIXED, (len + 1) * sizeof(wchar_t));
    if (!owned) return NULL;

    for (i = 0; i < len; i++) owned[i] = text[i];
    owned[len] = 0;
    return owned;
}

static int EnsureLineNumBuffers(int requiredChars)
{
    int newCap;
    int oldCap;
    int copyCount;
    wchar_t *newRender;
    wchar_t *newCached;
    int i;

    if (requiredChars < 1) requiredChars = 1;
    if (g_lineNumRenderBuf && g_lineNumCachedOutput && g_lineNumBufCap >= requiredChars) return 1;

    oldCap = g_lineNumBufCap;
    newCap = oldCap;
    if (newCap < 4096) newCap = 4096;
    while (newCap < requiredChars) {
        if (newCap > 0x20000000) {
            newCap = requiredChars;
            break;
        }
        newCap *= 2;
    }

    newRender = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
    if (!newRender) {
        newCap = requiredChars;
        newRender = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
        if (!newRender) return 0;
    }

    newCached = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
    if (!newCached) {
        LocalFree(newRender);
        return 0;
    }

    newCached[0] = 0;
    if (g_lineNumCachedOutput && oldCap > 0) {
        copyCount = oldCap;
        if (copyCount > newCap) copyCount = newCap;
        for (i = 0; i < copyCount; i++) {
            newCached[i] = g_lineNumCachedOutput[i];
            if (!g_lineNumCachedOutput[i]) break;
        }
        if (i >= copyCount) newCached[copyCount] = 0;
    }
    newRender[0] = 0;

    if (g_lineNumRenderBuf) LocalFree(g_lineNumRenderBuf);
    if (g_lineNumCachedOutput) LocalFree(g_lineNumCachedOutput);
    g_lineNumRenderBuf = newRender;
    g_lineNumCachedOutput = newCached;
    g_lineNumBufCap = newCap;
    return 1;
}

static void PagedReset(void)
{
    int wasPaged = g_bPagedMode;

    if (g_pagedText) {
        LocalFree(g_pagedText);
        g_pagedText = NULL;
    }
    if (g_pagedLineStarts) {
        LocalFree(g_pagedLineStarts);
        g_pagedLineStarts = NULL;
    }
    if (g_pagedSwapBuf) {
        LocalFree(g_pagedSwapBuf);
        g_pagedSwapBuf = NULL;
    }
    g_bPagedMode = 0;
    g_bPagedLoading = 0;
    g_bPagedPageDirty = 0;
    g_pagedTextLen = 0;
    g_pagedPageStart = 0;
    g_pagedPageLen = 0;
    g_pagedSwapCap = 0;
    g_pagedLineCount = 1;

    if (wasPaged && g_hwndMain && g_hwndEdit && g_hwndStatus && !g_bMainDestroying) {
        SendMessageW(g_hwndMain, WM_SIZE, 0, 0);
    }
}

static int PagedRebuildLineStarts(void)
{
    int i, count = 1, idx = 1;
    int *starts;

    if (!g_pagedText || g_pagedTextLen < 0) return 0;

    for (i = 0; i < g_pagedTextLen; i++) {
        if (g_pagedText[i] == L'\n') count++;
    }

    starts = (int *)LocalAlloc(LMEM_FIXED, count * sizeof(int));
    if (!starts) return 0;
    starts[0] = 0;

    for (i = 0; i < g_pagedTextLen && idx < count; i++) {
        if (g_pagedText[i] == L'\n') starts[idx++] = i + 1;
    }
    count = idx;

    if (g_pagedLineStarts) LocalFree(g_pagedLineStarts);
    g_pagedLineStarts = starts;
    g_pagedLineCount = count > 0 ? count : 1;
    return 1;
}

static int PagedGetWindowChars(void)
{
    int windowChars = g_bWordWrap ? PAGED_WINDOW_CHARS_WRAP : PAGED_WINDOW_CHARS_NOWRAP;
    if (windowChars < 4096) windowChars = 4096;
    return windowChars;
}

static int PagedGetEdgeChars(void)
{
    int windowChars = PagedGetWindowChars();
    int edgeChars = g_bWordWrap ? PAGED_EDGE_CHARS_WRAP : PAGED_EDGE_CHARS_NOWRAP;
    int maxEdge = windowChars / 3;

    if (maxEdge < 512) maxEdge = 512;
    if (edgeChars > maxEdge) edgeChars = maxEdge;
    if (edgeChars < 512) edgeChars = 512;
    return edgeChars;
}

static int PagedEnsureSwapBuffer(int requiredChars)
{
    int newCap;
    wchar_t *newBuf;

    if (requiredChars < 1) requiredChars = 1;

    if (g_pagedSwapBuf && g_pagedSwapCap >= requiredChars) return 1;

    newCap = g_pagedSwapCap;
    if (newCap < 4096) newCap = 4096;
    while (newCap < requiredChars) {
        if (newCap > 131072) {
            newCap = requiredChars;
            break;
        }
        newCap *= 2;
    }

    newBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
    if (!newBuf) {
        if (newCap != requiredChars) {
            newCap = requiredChars;
            newBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
        }
        if (!newBuf) return 0;
    }

    if (g_pagedSwapBuf) LocalFree(g_pagedSwapBuf);
    g_pagedSwapBuf = newBuf;
    g_pagedSwapCap = newCap;
    return 1;
}

static int PagedCommitPage(void)
{
    int curLen;
    wchar_t *curText;
    int oldLen, newLen, suffixStart, suffixLen, i;
    wchar_t *newDoc;

    if (!g_bPagedMode || !g_bPagedPageDirty) return 1;
    if (!g_hwndEdit) return 0;

    curLen = GetWindowTextLengthW(g_hwndEdit);
    curText = (wchar_t *)LocalAlloc(LMEM_FIXED, (curLen + 1) * sizeof(wchar_t));
    if (!curText) return 0;
    GetWindowTextW(g_hwndEdit, curText, curLen + 1);

    oldLen = g_pagedPageLen;
    newLen = g_pagedTextLen - oldLen + curLen;
    if (newLen < 0) {
        LocalFree(curText);
        return 0;
    }

    newDoc = (wchar_t *)LocalAlloc(LMEM_FIXED, (newLen + 1) * sizeof(wchar_t));
    if (!newDoc) {
        LocalFree(curText);
        return 0;
    }

    for (i = 0; i < g_pagedPageStart; i++) newDoc[i] = g_pagedText[i];
    for (i = 0; i < curLen; i++) newDoc[g_pagedPageStart + i] = curText[i];

    suffixStart = g_pagedPageStart + oldLen;
    suffixLen = g_pagedTextLen - suffixStart;
    if (suffixLen < 0) suffixLen = 0;
    for (i = 0; i < suffixLen; i++) {
        newDoc[g_pagedPageStart + curLen + i] = g_pagedText[suffixStart + i];
    }
    newDoc[newLen] = 0;

    LocalFree(g_pagedText);
    LocalFree(curText);
    g_pagedText = newDoc;
    g_pagedTextLen = newLen;
    g_pagedPageLen = curLen;
    g_bPagedPageDirty = 0;

    PagedRebuildLineStarts();
    MarkStatusTotalsDirty();
    return 1;
}

static int PagedLoadWindowAt(int globalPos)
{
    int start, end, maxStart, pageLen, localPos, i;
    int windowChars;

    if (!g_bPagedMode || !g_pagedText || !g_hwndEdit) return 0;

    if (!PagedCommitPage()) return 0;

    if (globalPos < 0) globalPos = 0;
    if (globalPos > g_pagedTextLen) globalPos = g_pagedTextLen;

    windowChars = PagedGetWindowChars();
    maxStart = g_pagedTextLen - windowChars;
    if (maxStart < 0) maxStart = 0;

    start = globalPos - (windowChars / 3);
    if (start < 0) start = 0;
    if (start > maxStart) start = maxStart;

    end = start + windowChars;
    if (end > g_pagedTextLen) end = g_pagedTextLen;
    pageLen = end - start;
    if (pageLen < 0) pageLen = 0;

    if (!PagedEnsureSwapBuffer(pageLen + 1)) return 0;
    for (i = 0; i < pageLen; i++) g_pagedSwapBuf[i] = g_pagedText[start + i];
    g_pagedSwapBuf[pageLen] = 0;

    g_bPagedLoading = 1;
    SetWindowTextW(g_hwndEdit, g_pagedSwapBuf);
    g_bPagedLoading = 0;

    g_pagedPageStart = start;
    g_pagedPageLen = pageLen;
    g_bPagedPageDirty = 0;

    localPos = globalPos - start;
    if (localPos < 0) localPos = 0;
    if (localPos > pageLen) localPos = pageLen;
    SendMessageW(g_hwndEdit, EM_SETSEL, localPos, localPos);
    SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
    /* Keep scrollbar position stable before heavier refresh work. */
    PagedSyncVScroll();
    if (!g_bPreserveUndoOnPagedLoad) {
        Undo_Clear();
        g_bKeepPagedReplaceUndo = 0;
    }
    RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
    UpdateStatus();
    return 1;
}

static int PagedEnableWithText(wchar_t *text, int len)
{
    if (!text || len < 0) return 0;

    PagedReset();
    g_bPagedMode = 1;
    g_pagedText = text;
    g_pagedTextLen = len;
    g_pagedPageStart = 0;
    g_pagedPageLen = 0;
    g_bPagedPageDirty = 0;
    MarkStatusTotalsDirty();

    if (!PagedRebuildLineStarts()) {
        PagedReset();
        return 0;
    }

    if (!PagedLoadWindowAt(0)) {
        PagedReset();
        return 0;
    }

    if (g_hwndMain && g_hwndEdit && g_hwndStatus) {
        SendMessageW(g_hwndMain, WM_SIZE, 0, 0);
    }

    SetStatusMessage(L"Large file mode active");
    return 1;
}

static int PagedGetGlobalSelStart(void)
{
    DWORD selStart, selEnd;
    (void)selEnd;

    if (!g_bPagedMode || !g_hwndEdit) return 0;
    SendMessageW(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    return g_pagedPageStart + (int)selStart;
}

static int PagedPrepareUndoTarget(int localPos)
{
    int targetGlobal;
    int oldPageStart;
    int preserveUndo;
    int undoDelta;

    if (!g_bPagedMode || !g_pagedText || !g_hwndEdit) return 1;
    if (localPos >= 0 && localPos <= g_pagedPageLen) return 1;

    targetGlobal = g_pagedPageStart + localPos;
    if (targetGlobal < 0) targetGlobal = 0;
    if (targetGlobal > g_pagedTextLen) targetGlobal = g_pagedTextLen;

    oldPageStart = g_pagedPageStart;
    preserveUndo = g_bPreserveUndoOnPagedLoad;
    g_bPreserveUndoOnPagedLoad = 1;
    if (!PagedLoadWindowAt(targetGlobal)) {
        g_bPreserveUndoOnPagedLoad = preserveUndo;
        return 0;
    }
    g_bPreserveUndoOnPagedLoad = preserveUndo;

    undoDelta = oldPageStart - g_pagedPageStart;
    if (undoDelta) Undo_ShiftPositions(undoDelta);
    RefreshEditAfterLargeJump();
    return 1;
}

static void PagedIndexToLineCol(int index, int *outLine, int *outCol)
{
    int lo, hi;
    int line = 1;
    int col = 1;

    if (!outLine || !outCol) return;

    if (!g_bPagedMode || !g_pagedLineStarts || g_pagedLineCount <= 0) {
        *outLine = 1;
        *outCol = 1;
        return;
    }

    if (index < 0) index = 0;
    if (index > g_pagedTextLen) index = g_pagedTextLen;

    lo = 0;
    hi = g_pagedLineCount;
    while (lo < hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (g_pagedLineStarts[mid] <= index) lo = mid + 1;
        else hi = mid;
    }

    line = lo;
    if (line < 1) line = 1;
    if (line > g_pagedLineCount) line = g_pagedLineCount;
    col = index - g_pagedLineStarts[line - 1] + 1;
    if (col < 1) col = 1;

    *outLine = line;
    *outCol = col;
}

static int PagedGetGlobalLineFromLocalChar(int localChar)
{
    int globalIdx;
    int lo, hi;

    if (!g_bPagedMode || !g_pagedLineStarts || g_pagedLineCount <= 0) return 0;

    if (localChar < 0) localChar = 0;
    if (localChar > g_pagedPageLen) localChar = g_pagedPageLen;
    globalIdx = g_pagedPageStart + localChar;
    if (globalIdx < 0) globalIdx = 0;
    if (globalIdx > g_pagedTextLen) globalIdx = g_pagedTextLen;

    lo = 0;
    hi = g_pagedLineCount;
    while (lo < hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (g_pagedLineStarts[mid] <= globalIdx) lo = mid + 1;
        else hi = mid;
    }
    if (lo <= 0) return 0;
    if (lo > g_pagedLineCount) return g_pagedLineCount - 1;
    return lo - 1;
}

static int PagedGetVisibleRows(void)
{
    HDC hdc;
    HFONT hOld;
    TEXTMETRICW tm;
    RECT rc;
    int lineHeight = 0;
    int rows;

    if (!g_hwndEdit || !g_hFont) return 1;

    GetClientRect(g_hwndEdit, &rc);
    hdc = GetDC(g_hwndEdit);
    if (hdc) {
        hOld = (HFONT)SelectObject(hdc, g_hFont);
        if (GetTextMetricsW(hdc, &tm)) lineHeight = tm.tmHeight;
        SelectObject(hdc, hOld);
        ReleaseDC(g_hwndEdit, hdc);
    }

    if (lineHeight <= 0) rows = 1;
    else rows = (rc.bottom - rc.top) / lineHeight;
    if (rows < 1) rows = 1;
    return rows;
}

static void PagedSyncVScroll(void)
{
    SCROLLINFO si;
    HWND hwndScroll;
    int firstVisible, localChar;
    int globalTopLine;
    int visibleRows;

    if (!g_bPagedMode || !g_hwndEdit || !g_pagedLineStarts || g_pagedLineCount <= 0) return;

    firstVisible = (int)SendMessageW(g_hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    localChar = (int)SendMessageW(g_hwndEdit, EM_LINEINDEX, firstVisible, 0);
    if (localChar < 0) localChar = 0;
    globalTopLine = PagedGetGlobalLineFromLocalChar(localChar);
    visibleRows = PagedGetVisibleRows();

    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (g_pagedLineCount > 0) ? (g_pagedLineCount - 1) : 0;
    si.nPage = (UINT)visibleRows;
    si.nPos = globalTopLine;
    hwndScroll = g_hwndPagedVScroll ? g_hwndPagedVScroll : g_hwndEdit;
    if (hwndScroll == g_hwndEdit) SetScrollInfo(hwndScroll, SB_VERT, &si, TRUE);
    else SetScrollInfo(hwndScroll, SB_CTL, &si, TRUE);
}

static int PagedHandleThumbScroll(UINT scrollCode)
{
    SCROLLINFO si;
    HWND hwndScroll;
    int targetLine;
    int targetIdx;

    if (!g_bPagedMode || !g_pagedLineStarts || g_pagedLineCount <= 0) return 0;
    if (!(scrollCode == SB_THUMBTRACK || scrollCode == SB_THUMBPOSITION ||
          scrollCode == SB_TOP || scrollCode == SB_BOTTOM)) return 0;

    /* Suppress page-local thumb tracking; jump once on final thumb position. */
    if (scrollCode == SB_THUMBTRACK) return 1;

    if (scrollCode == SB_TOP) targetLine = 0;
    else if (scrollCode == SB_BOTTOM) targetLine = g_pagedLineCount - 1;
    else {
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS | SIF_POS;
        hwndScroll = g_hwndPagedVScroll ? g_hwndPagedVScroll : g_hwndEdit;
        if (hwndScroll == g_hwndEdit) {
            if (!GetScrollInfo(hwndScroll, SB_VERT, &si)) return 1;
        } else {
            if (!GetScrollInfo(hwndScroll, SB_CTL, &si)) return 1;
        }
        targetLine = si.nTrackPos;
    }

    if (targetLine < 0) targetLine = 0;
    if (targetLine >= g_pagedLineCount) targetLine = g_pagedLineCount - 1;

    targetIdx = g_pagedLineStarts[targetLine];
    if (targetIdx < 0) targetIdx = 0;
    if (targetIdx > g_pagedTextLen) targetIdx = g_pagedTextLen;

    if (PagedLoadWindowAt(targetIdx)) {
        int localIdx = targetIdx - g_pagedPageStart;
        if (localIdx < 0) localIdx = 0;
        if (localIdx > g_pagedPageLen) localIdx = g_pagedPageLen;
        SendMessageW(g_hwndEdit, EM_SETSEL, localIdx, localIdx);
        SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
        RefreshEditAfterLargeJump();
        PagedSyncVScroll();
        RequestLineNumberRefresh(LINENUM_DIRTY_SCROLL, 1);
    }

    return 1;
}

static int PagedProcessScrollCode(UINT scrollCode)
{
    int handled = 1;

    if (!g_bPagedMode) return 0;

    switch (scrollCode) {
    case SB_LINEUP:
        SendMessageW(g_hwndEdit, EM_LINESCROLL, 0, -1);
        break;
    case SB_LINEDOWN:
        SendMessageW(g_hwndEdit, EM_LINESCROLL, 0, 1);
        break;
    case SB_PAGEUP:
        SendMessageW(g_hwndEdit, EM_LINESCROLL, 0, -(PagedGetVisibleRows() - 1));
        break;
    case SB_PAGEDOWN:
        SendMessageW(g_hwndEdit, EM_LINESCROLL, 0, PagedGetVisibleRows() - 1);
        break;
    case SB_TOP:
    case SB_BOTTOM:
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        PagedHandleThumbScroll(scrollCode);
        break;
    case SB_ENDSCROLL:
        break;
    default:
        handled = 0;
        break;
    }

    if (!handled) return 0;

    if (scrollCode != SB_THUMBTRACK) {
        RequestLineNumberRefresh(LINENUM_DIRTY_SCROLL, 1);
        if (scrollCode == SB_LINEUP || scrollCode == SB_LINEDOWN ||
            scrollCode == SB_PAGEUP || scrollCode == SB_PAGEDOWN ||
            scrollCode == SB_ENDSCROLL) {
            PagedHandleVScrollEdge(scrollCode);
        }
        PagedSyncVScroll();
    }

    if (g_bShowColumnIndicator) InvalidateColumnIndicator();
    return 1;
}

static void PagedMaybeShiftWindowByCaret(void)
{
    DWORD selStart, selEnd;
    int globalSel;
    int windowChars;
    int edgeChars;

    if (!g_bPagedMode || g_bPagedLoading || !g_hwndEdit) return;
    windowChars = PagedGetWindowChars();
    edgeChars = PagedGetEdgeChars();
    if (g_pagedTextLen <= windowChars) return;

    SendMessageW(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    (void)selEnd;
    globalSel = g_pagedPageStart + (int)selStart;

    if ((int)selStart >= g_pagedPageLen - edgeChars && g_pagedPageStart + g_pagedPageLen < g_pagedTextLen) {
        int target = globalSel + edgeChars;
        if (target > g_pagedTextLen) target = g_pagedTextLen;
        PagedLoadWindowAt(target);
    } else if ((int)selStart <= edgeChars && g_pagedPageStart > 0) {
        int target = globalSel - edgeChars;
        if (target < 0) target = 0;
        PagedLoadWindowAt(target);
    }
}

static void PagedHandleVScrollEdge(UINT scrollCode)
{
    int firstVisible, lineCount;
    int windowChars;
    int edgeChars;

    if (!g_bPagedMode || g_bPagedLoading || !g_hwndEdit) return;
    windowChars = PagedGetWindowChars();
    edgeChars = PagedGetEdgeChars();

    firstVisible = (int)SendMessageW(g_hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    lineCount = (int)SendMessageW(g_hwndEdit, EM_GETLINECOUNT, 0, 0);

    if ((scrollCode == SB_LINEDOWN || scrollCode == SB_PAGEDOWN || scrollCode == SB_BOTTOM ||
         scrollCode == SB_THUMBPOSITION || scrollCode == SB_ENDSCROLL) &&
        firstVisible >= lineCount - 2 && g_pagedPageStart + g_pagedPageLen < g_pagedTextLen) {
        int target = g_pagedPageStart + g_pagedPageLen - (edgeChars / 2);
        if (target < 0) target = 0;
        PagedLoadWindowAt(target);
    } else if ((scrollCode == SB_LINEUP || scrollCode == SB_PAGEUP || scrollCode == SB_TOP ||
                scrollCode == SB_THUMBPOSITION || scrollCode == SB_ENDSCROLL) &&
               firstVisible <= 1 && g_pagedPageStart > 0) {
        int target = g_pagedPageStart - (windowChars / 2);
        if (target < 0) target = 0;
        PagedLoadWindowAt(target);
    }
}

static int CaptureEditRangeTextFull(HWND hwnd, DWORD start, DWORD end, wchar_t **outText, int *outLen)
{
    int totalLen, span, i;
    wchar_t *fullText;
    wchar_t *slice;

    if (!outText || !outLen || end <= start) return 0;
    *outText = NULL;
    *outLen = 0;

    totalLen = GetWindowTextLengthW(hwnd);
    if ((int)start < 0 || (int)end > totalLen || end <= start) return 0;

    span = (int)(end - start);
    fullText = (wchar_t *)LocalAlloc(LMEM_FIXED, (totalLen + 1) * sizeof(wchar_t));
    if (!fullText) return 0;
    GetWindowTextW(hwnd, fullText, totalLen + 1);

    slice = (wchar_t *)LocalAlloc(LMEM_FIXED, (span + 1) * sizeof(wchar_t));
    if (!slice) {
        LocalFree(fullText);
        return 0;
    }

    for (i = 0; i < span; i++) slice[i] = fullText[(int)start + i];
    slice[span] = 0;
    LocalFree(fullText);

    *outText = slice;
    *outLen = span;
    return 1;
}

/*
 * CaptureEditRangeText - Extract text range [start, end) from Edit control
 * without snapshotting the full document buffer.
 * Returns 1 on success with allocated output buffer (caller frees via LocalFree).
 */
static int CaptureEditRangeText(HWND hwnd, DWORD start, DWORD end, wchar_t **outText, int *outLen)
{
    int outCap, outPos = 0;
    int startLine, endLine, totalLines, line;
    wchar_t *out = NULL;
    wchar_t *lineBuf = NULL;
    int lineBufCap = 0;

    if (!outText || !outLen || end <= start) return 0;
    *outText = NULL;
    *outLen = 0;

    outCap = (int)(end - start);
    if (outCap <= 0) return 0;

    out = (wchar_t *)LocalAlloc(LMEM_FIXED, (outCap + 1) * sizeof(wchar_t));
    if (!out) return 0;

    startLine = (int)SendMessageW(hwnd, EM_LINEFROMCHAR, (WPARAM)start, 0);
    endLine = (int)SendMessageW(hwnd, EM_LINEFROMCHAR, (WPARAM)(end - 1), 0);
    totalLines = (int)SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0);
    if (startLine < 0) startLine = 0;
    if (endLine < startLine) endLine = startLine;
    if (totalLines <= 0) totalLines = endLine + 1;

    for (line = startLine; line <= endLine && outPos < outCap; line++) {
        int lineStart = (int)SendMessageW(hwnd, EM_LINEINDEX, line, 0);
        int lineLen, lineEnd, nextLineStart;
        int chunkStart, chunkEnd;

        if (lineStart < 0) continue;

        lineLen = (int)SendMessageW(hwnd, EM_LINELENGTH, lineStart, 0);
        if (lineLen < 0) lineLen = 0;
        lineEnd = lineStart + lineLen;
        nextLineStart = lineEnd;
        if (line + 1 < totalLines) {
            int nextIdx = (int)SendMessageW(hwnd, EM_LINEINDEX, line + 1, 0);
            if (nextIdx > nextLineStart) nextLineStart = nextIdx;
        }

        chunkStart = ((int)start > lineStart) ? (int)start : lineStart;
        chunkEnd = ((int)end < lineEnd) ? (int)end : lineEnd;
        if (chunkEnd > chunkStart && lineLen > 0) {
            int needCap = lineLen + 1;
            int copied;
            int startOff = chunkStart - lineStart;
            int want = chunkEnd - chunkStart;
            int j;

            if (needCap > 65530) {
                LocalFree(out);
                if (lineBuf) LocalFree(lineBuf);
                return CaptureEditRangeTextFull(hwnd, start, end, outText, outLen);
            }

            if (needCap > lineBufCap) {
                wchar_t *newBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (needCap + 1) * sizeof(wchar_t));
                if (!newBuf) {
                    LocalFree(out);
                    if (lineBuf) LocalFree(lineBuf);
                    return 0;
                }
                if (lineBuf) LocalFree(lineBuf);
                lineBuf = newBuf;
                lineBufCap = needCap + 1;
            }

            *((WORD *)lineBuf) = (WORD)(lineBufCap - 1);
            copied = (int)SendMessageW(hwnd, EM_GETLINE, line, (LPARAM)lineBuf);
            if (copied < 0) copied = 0;
            if (copied > lineBufCap - 1) copied = lineBufCap - 1;
            lineBuf[copied] = 0;

            if (copied < lineLen && chunkEnd > lineStart + copied) {
                LocalFree(out);
                if (lineBuf) LocalFree(lineBuf);
                return CaptureEditRangeTextFull(hwnd, start, end, outText, outLen);
            }

            if (startOff < copied) {
                int avail = copied - startOff;
                if (want > avail) want = avail;
                for (j = 0; j < want && outPos < outCap; j++) {
                    out[outPos++] = lineBuf[startOff + j];
                }
            }
        }

        if (nextLineStart > lineEnd) {
            int breakLen = nextLineStart - lineEnd;
            int bStart = ((int)start > lineEnd) ? (int)start : lineEnd;
            int bEnd = ((int)end < nextLineStart) ? (int)end : nextLineStart;
            static const wchar_t kBreakChars[2] = {L'\r', L'\n'};

            if (breakLen > 2) {
                LocalFree(out);
                if (lineBuf) LocalFree(lineBuf);
                return CaptureEditRangeTextFull(hwnd, start, end, outText, outLen);
            }
            if (bEnd > bStart && breakLen > 0) {
                int offset = bStart - lineEnd;
                int count = bEnd - bStart;
                int j;

                if (offset < 0) offset = 0;
                if (offset > breakLen) offset = breakLen;
                if (count > breakLen - offset) count = breakLen - offset;
                for (j = 0; j < count && outPos < outCap; j++) {
                    out[outPos++] = kBreakChars[offset + j];
                }
            }
        }
    }

    if (lineBuf) LocalFree(lineBuf);
    out[outPos] = 0;
    if (outPos <= 0) {
        LocalFree(out);
        return 0;
    }

    *outText = out;
    *outLen = outPos;
    return 1;
}

static void RecordUndoDeleteRange(HWND hwnd, DWORD start, DWORD end)
{
    wchar_t *deletedText = NULL;
    int deletedLen = 0;

    if (end <= start) return;
    if (CaptureEditRangeText(hwnd, start, end, &deletedText, &deletedLen)) {
        Undo_RecordDelete((int)start, deletedText, deletedLen);
        LocalFree(deletedText);
    }
}

static int IsLikelyTypingKey(UINT vk, int ctrl, int alt)
{
    if (ctrl || alt) return 0;

    switch (vk) {
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_INSERT:
    case VK_DELETE:
    case VK_BACK:
    case VK_ESCAPE:
        return 0;
    default:
        break;
    }

    if (vk >= VK_F1 && vk <= VK_F24) return 0;
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) return 0;
    if (vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL) return 0;

    return 1;
}

static int IsModifierOrToggleKey(UINT vk)
{
    return (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
            vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL);
}

static void CloseReplaceTypingGroup(void)
{
    if (g_bReplaceTypingGroupOpen) {
        Undo_EndGroup();
        g_bReplaceTypingGroupOpen = 0;
    }
}

static void MemZero(void *ptr, DWORD bytes)
{
    BYTE *p = (BYTE *)ptr;
    DWORD i;
    if (!p) return;
    for (i = 0; i < bytes; i++) p[i] = 0;
}

static int CmdLineHasQuickNoteSwitch(const wchar_t *cmdLine)
{
    const wchar_t *p;
    wchar_t token[32];
    int i;

    if (!cmdLine) return 0;

    p = cmdLine;
    while (*p) {
        while (*p == L' ' || *p == L'\t') p++;
        if (!*p) break;

        if (*p == L'"') {
            p++;
            while (*p && *p != L'"') p++;
            if (*p == L'"') p++;
            continue;
        }

        i = 0;
        while (*p && *p != L' ' && *p != L'\t') {
            if (i < (int)(sizeof(token) / sizeof(token[0])) - 1) {
                token[i++] = *p;
            }
            p++;
        }
        token[i] = 0;

        if (lstrcmpiW(token, L"/quicknote") == 0 ||
            lstrcmpiW(token, L"-quicknote") == 0) {
            return 1;
        }
    }

    return 0;
}

static HWND FindQuickNoteWindow(HWND hwndExclude)
{
    HWND hwnd;
    wchar_t className[32];

    hwnd = FindWindowW(g_szClassName, NULL);
    while (hwnd) {
        if (hwnd != hwndExclude) {
            if (SendMessageW(hwnd, PWM_IS_QUICKNOTE_WINDOW, 0, 0)) {
                return hwnd;
            }
        }

        do {
            hwnd = GetWindow(hwnd, GW_HWNDNEXT);
            if (!hwnd) break;
        } while (GetClassNameW(hwnd, className, 32) <= 0 || lstrcmpW(className, g_szClassName) != 0);
    }

    return NULL;
}

static void ActivateQuickNoteWindow(HWND hwnd, int appendNewline)
{
    if (!hwnd) return;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SendMessageW(hwnd, PWM_QUICKNOTE_ACTIVATE, (WPARAM)(appendNewline ? 1 : 0), 0);
}

static int LaunchQuickNoteWindowProcess(void)
{
    wchar_t exePath[MAX_PATH];
    wchar_t cmdLine[(MAX_PATH * 2) + 32];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    int started = 0;

    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) return 0;
    wsprintfW(cmdLine, L"\"%s\" /quicknote", exePath);

    MemZero(&si, sizeof(si));
    MemZero(&pi, sizeof(pi));
    si.cb = sizeof(si);

    if (CreateProcessW(exePath, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        started = 1;
        if (pi.hThread) CloseHandle(pi.hThread);
        if (pi.hProcess) CloseHandle(pi.hProcess);
    }

    return started;
}

static void DoQuickNoteHotkey(void)
{
    HWND hwndQuick;

    if (g_bQuickNoteWindow) {
        DoQuickNote();
        return;
    }

    hwndQuick = FindQuickNoteWindow(g_hwndMain);
    if (hwndQuick) {
        ActivateQuickNoteWindow(hwndQuick, 1);
        return;
    }

    if (LaunchQuickNoteWindowProcess()) return;

    /* Fallback if process launch fails: keep existing in-window behavior. */
    DoQuickNote();
}

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
        /* Alt+L = Toggle Line Numbers */
        if ((wParam == 'L' || wParam == 'l') && alt) {
            SendMessage(g_hwndMain, WM_COMMAND, IDM_VIEW_LINENUMS, 0);
            return 1;
        }
        /* Alt+S = Toggle Scrollbars */
        if ((wParam == 'S' || wParam == 's') && alt) {
            SendMessage(g_hwndMain, WM_COMMAND, IDM_VIEW_SCROLLBARS, 0);
            return 1;
        }
        /* Alt+B = Toggle Status Bar */
        if ((wParam == 'B' || wParam == 'b') && alt) {
            SendMessage(g_hwndMain, WM_COMMAND, IDM_VIEW_STATUSBAR, 0);
            return 1;
        }
        /* Alt+W = Toggle Word Wrap */
        if ((wParam == 'W' || wParam == 'w') && alt) {
            SendMessage(g_hwndMain, WM_COMMAND, IDM_VIEW_WORDWRAP, 0);
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
        int shift = GetKeyState(VK_SHIFT) < 0;
        if (wParam == 'N') { SendMessage(g_hwndMain, WM_COMMAND, IDM_FILE_NEW, 0); return 1; }
        if (wParam == 'O') { SendMessage(g_hwndMain, WM_COMMAND, IDM_FILE_OPEN, 0); return 1; }
        if (wParam == 'S') { SendMessage(g_hwndMain, WM_COMMAND, IDM_FILE_SAVE, 0); return 1; }
        if (wParam == 'W') { SendMessage(g_hwndMain, WM_CLOSE, 0, 0); return 1; }
        if (wParam == 'G') { DoGotoLine(); return 1; }
        if (wParam == 'F') { DoFind(); return 1; }
        if (wParam == 'H') { DoReplace(); return 1; }
        if (wParam == 'R') { DoInsertRule(); return 1; }
        if (wParam == 'Q') { DoQuickNoteHotkey(); return 1; }
        if (wParam == 'J') { DoReflow(); return 1; }
        if (wParam == 'X' && shift) { SendMessage(g_hwndMain, WM_COMMAND, IDM_EDIT_CUTLINE, 0); return 1; }
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
        /* Ctrl+; = Insert Date, Ctrl+Shift+; = Insert Date+Time */
        if (wParam == 0xBA) {  /* VK_OEM_1 = ;/: key */
            DoInsertDateTime(shift ? 2 : 0);
            return 1;
        }
        /* Ctrl+' = Insert Time */
        if (wParam == 0xDE) {  /* VK_OEM_7 = '/\" key */
            DoInsertDateTime(1);
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
 * InvalidateColumnIndicator - Invalidate just the column indicator strip
 */
static void InvalidateColumnIndicator(void)
{
    if (g_bShowColumnIndicator && g_hFont && g_hwndEdit) {
        HDC hdc = GetDC(g_hwndEdit);
        HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFont);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        if (tm.tmAveCharWidth > 0) {
            SCROLLINFO si;
            int scrollX = 0;
            int x;
            RECT rc;
            si.cbSize = sizeof(si);
            si.fMask = SIF_POS;
            if (GetScrollInfo(g_hwndEdit, SB_HORZ, &si)) {
                scrollX = si.nPos;
            }
            x = (tm.tmAveCharWidth * g_nColumnLimit) - scrollX;
            GetClientRect(g_hwndEdit, &rc);
            if (x >= 0 && x < rc.right) {
                rc.left = x;
                rc.right = x + 1;
                InvalidateRect(g_hwndEdit, &rc, FALSE);
            }
        }
        SelectObject(hdc, hOldFont);
        ReleaseDC(g_hwndEdit, hdc);
    }
}

static void SetEditVerticalScrollbarVisible(int visible)
{
    LONG style, newStyle;

    if (!g_hwndEdit) return;

    style = GetWindowLong(g_hwndEdit, GWL_STYLE);
    if (visible) newStyle = style | WS_VSCROLL;
    else newStyle = style & ~WS_VSCROLL;

    if (newStyle != style) {
        SetWindowLong(g_hwndEdit, GWL_STYLE, newStyle);
        SetWindowPos(g_hwndEdit, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

/*
 * EditSubclassProc - Catch cursor movement for status updates
 */
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == PWM_EDIT_POSTJUMP_REPAINT) {
        g_bPostJumpRepaintPending = 0;
        RefreshEditAfterLargeJump();
        return 0;
    }

    /* Keep replacement-typing groups open only while user continues text input. */
    if (g_bReplaceTypingGroupOpen) {
        int ctrl = GetKeyState(VK_CONTROL) < 0;
        int alt = GetKeyState(VK_MENU) < 0;
        int keepOpen = 1;

        if (msg == WM_CHAR) {
            keepOpen = (wParam >= 32 || wParam == '\r' || wParam == '\t');
        } else if (msg == WM_KEYDOWN) {
            keepOpen = IsLikelyTypingKey((UINT)wParam, ctrl, alt);
        } else if (msg == WM_KEYUP) {
            keepOpen = 1;
        } else if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) {
            /* Pointer interaction should terminate the replacement typing run. */
            keepOpen = 0;
        }

        if (!keepOpen) CloseReplaceTypingGroup();
    }

    /* Intercept Ctrl+Z before Edit control's default handler */
    if (msg == WM_KEYDOWN && wParam == 'Z' && GetKeyState(VK_CONTROL) < 0) {
        if (!PerformTrackedUndo()) {
            SendMessageW(g_hwndEdit, EM_UNDO, 0, 0);
        }
        RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
        return 0;
    }
    
    /* Intercept Ctrl+Y for redo */
    if (msg == WM_KEYDOWN && wParam == 'Y' && GetKeyState(VK_CONTROL) < 0) {
        PerformTrackedRedo();
        RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
        return 0;
    }
    
    /* Intercept Ctrl+X for cut with undo tracking */
    if (msg == WM_KEYDOWN && wParam == 'X' && GetKeyState(VK_CONTROL) < 0) {
        DWORD selStart, selEnd;
        SendMessage(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        RecordUndoDeleteRange(hwnd, selStart, selEnd);
        SendMessageW(hwnd, WM_CUT, 0, 0);
        return 0;
    }
    
    /* Intercept Ctrl+V for paste with undo tracking */
    if (msg == WM_KEYDOWN && wParam == 'V' && GetKeyState(VK_CONTROL) < 0) {
        DWORD selStart, selEnd;
        SendMessage(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (OpenClipboard(hwnd)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                wchar_t *pText = (wchar_t *)hData;
                if (pText && pText[0]) {
                    if (selEnd > selStart) {
                        Undo_BeginGroup();
                        RecordUndoDeleteRange(hwnd, selStart, selEnd);
                        Undo_RecordInsert(selStart, pText, -1);
                        Undo_EndGroup();
                    } else {
                        Undo_RecordInsert(selStart, pText, -1);
                    }
                }
            }
            CloseClipboard();
        }
        SendMessageW(hwnd, WM_PASTE, 0, 0);
        return 0;
    }

    /* Track Delete key - record char at cursor before delete */
    if (msg == WM_KEYDOWN && wParam == VK_DELETE) {
        DWORD selStart, selEnd;
        SendMessage(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (selStart == selEnd) {
            /* No selection - deleting single char at cursor */
            int len = GetWindowTextLengthW(hwnd);
            if ((int)selStart < len) {
                RecordUndoDeleteRange(hwnd, selStart, selStart + 1);
            }
        } else {
            /* Selection - record selected text */
            RecordUndoDeleteRange(hwnd, selStart, selEnd);
        }
    }
    
    /* Track Backspace - record char before cursor */
    if (msg == WM_KEYDOWN && wParam == VK_BACK) {
        DWORD selStart, selEnd;
        SendMessage(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (selStart == selEnd && selStart > 0) {
            /* No selection - deleting char before cursor */
            RecordUndoDeleteRange(hwnd, selStart - 1, selStart);
        } else if (selEnd > selStart) {
            /* Selection - record selected text */
            RecordUndoDeleteRange(hwnd, selStart, selEnd);
        }
    }

    /* Clear temporary status only on real key actions, not key-release noise. */
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
        !IsModifierOrToggleKey((UINT)wParam)) {
        ClearStatusMessage();
    }

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
    
    /* Draw column indicator after edit control paints */
    if (msg == WM_PAINT && g_bShowColumnIndicator) {
        LRESULT r = CallWindowProc(g_pfnEditProc, hwnd, msg, wParam, lParam);
        if (g_hFont) {
            HDC hdc = GetDC(hwnd);
            HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFont);
            TEXTMETRICW tm;
            GetTextMetricsW(hdc, &tm);
            if (tm.tmAveCharWidth > 0) {
                SCROLLINFO si;
                int scrollX = 0;
                int x, lineCount, textHeight, firstVis, firstChar, topY, visLines, maxVis;
                RECT rc;
                si.cbSize = sizeof(si);
                si.fMask = SIF_POS;
                if (GetScrollInfo(hwnd, SB_HORZ, &si)) {
                    scrollX = si.nPos;
                }
                x = (tm.tmAveCharWidth * g_nColumnLimit) - scrollX;
                GetClientRect(hwnd, &rc);
                if (x >= 0 && x < rc.right) {
                    if (!g_hBrushColInd) g_hBrushColInd = CreateSolidBrush(RGB(128, 128, 128));
                    firstVis = (int)SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
                    firstChar = (int)SendMessageW(hwnd, EM_LINEINDEX, firstVis, 0);
                    topY = HIWORD(SendMessageW(hwnd, EM_POSFROMCHAR, firstChar, 0));
                    lineCount = (int)SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0);
                    visLines = lineCount - firstVis;
                    maxVis = (rc.bottom - topY) / tm.tmHeight;
                    if (visLines > maxVis) visLines = maxVis;
                    textHeight = topY + (visLines * tm.tmHeight);
                    rc.left = x;
                    rc.right = x + 1;
                    rc.top = topY;
                    if (textHeight < rc.bottom) rc.bottom = textHeight;
                    FillRect(hdc, &rc, g_hBrushColInd);
                }
            }
            SelectObject(hdc, hOldFont);
            ReleaseDC(hwnd, hdc);
        }
        return r;
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
        DWORD selStart;
        wchar_t spaces[9];
        int i;
        SendMessage(hwnd, EM_GETSEL, (WPARAM)&selStart, 0);
        for (i = 0; i < g_nTabSize && i < 8; i++) spaces[i] = ' ';
        spaces[i] = 0;
        Undo_RecordInsert(selStart, spaces, i);
        SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)spaces);
        return 0;
    }

    /* Optional hard wrap while typing at column limit. */
    if (msg == WM_CHAR && wParam >= 32 && GetKeyState(VK_CONTROL) >= 0 && g_bAutoWrapTyping) {
        DWORD selStart, selEnd;
        int line;
        int lineStart;
        int lineLen;
        int col;
        wchar_t insertBuf[4];
        int insertLen;

        SendMessage(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (selStart == selEnd) {
            line = (int)SendMessageW(hwnd, EM_LINEFROMCHAR, selStart, 0);
            lineStart = (int)SendMessageW(hwnd, EM_LINEINDEX, line, 0);
            lineLen = (int)SendMessageW(hwnd, EM_LINELENGTH, selStart, 0);
            if (lineStart >= 0 && lineLen >= 0 && selStart == (DWORD)(lineStart + lineLen)) {
                col = (int)selStart - lineStart;
                if (col >= g_nColumnLimit) {
                    g_bForceImmediateLineNum = 1;
                    insertBuf[0] = L'\r';
                    insertBuf[1] = L'\n';
                    if ((wchar_t)wParam == L' ') {
                        insertBuf[2] = 0;
                        insertLen = 2;
                    } else {
                        insertBuf[2] = (wchar_t)wParam;
                        insertBuf[3] = 0;
                        insertLen = 3;
                    }
                    Undo_RecordInsert(selStart, insertBuf, insertLen);
                    SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)insertBuf);
                    return 0;
                }
            }
        }
    }
    
    /* Track typed characters for undo */
    if (msg == WM_CHAR && wParam >= 32 && GetKeyState(VK_CONTROL) >= 0) {
        DWORD selStart, selEnd;
        wchar_t ch[2];
        SendMessage(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        /* If there's a selection, it will be replaced - record deletion first */
        if (selEnd > selStart) {
            if (!g_bReplaceTypingGroupOpen) {
                Undo_BeginGroup();
                g_bReplaceTypingGroupOpen = 1;
            }
            RecordUndoDeleteRange(hwnd, selStart, selEnd);
            ch[0] = (wchar_t)wParam;
            ch[1] = 0;
            Undo_RecordInsert(selStart, ch, 1);
        } else {
            ch[0] = (wchar_t)wParam;
            ch[1] = 0;
            Undo_RecordInsert(selStart, ch, 1);
        }
    }
    
    /* Track Enter key */
    if (msg == WM_CHAR && wParam == '\r') {
        DWORD selStart, selEnd;
        wchar_t ch[2];
        g_bForceImmediateLineNum = 1;
        SendMessage(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (selEnd > selStart) {
            if (!g_bReplaceTypingGroupOpen) {
                Undo_BeginGroup();
                g_bReplaceTypingGroupOpen = 1;
            }
            RecordUndoDeleteRange(hwnd, selStart, selEnd);
            ch[0] = '\r';
            ch[1] = 0;
            Undo_RecordInsert(selStart, ch, 1);
        } else {
            ch[0] = '\r';
            ch[1] = 0;
            Undo_RecordInsert(selStart, ch, 1);
        }
    }

    /* Block WM_CHAR for Ctrl+key combos we handle (prevents beep) */
    if (msg == WM_CHAR && GetKeyState(VK_CONTROL) < 0) {
        if (wParam == 1 || wParam == 6 || wParam == 7 || wParam == 8 || wParam == 10 || /* Ctrl+A, F, G, H, J */
            wParam == 14 || wParam == 15 || wParam == 17 || wParam == 18 || wParam == 19 || wParam == 22 || wParam == 23 || wParam == 24 || wParam == 25 || /* Ctrl+N, O, Q, R, S, V, W, X, Y */
            wParam == 26) /* Ctrl+Z */
            return 0;
    }

    /* Block WM_SYSCHAR for Alt+Enter, Alt+I, Alt+L, Alt+S, Alt+B, Alt+W (prevents beep) */
    if (msg == WM_SYSCHAR && (wParam == VK_RETURN || wParam == 'i' || wParam == 'I' || 
        wParam == 'l' || wParam == 'L' || wParam == 's' || wParam == 'S' ||
        wParam == 'b' || wParam == 'B' || wParam == 'w' || wParam == 'W'))
        return 0;

    /* Sync line numbers on scroll */
    if (msg == WM_VSCROLL) {
        UINT scrollCode = (UINT)LOWORD(wParam);
        int refreshNow = 0;
        int skipLineNumRefresh = 0;
        LRESULT r;

        if (g_bPagedMode && PagedProcessScrollCode(scrollCode)) return 0;

        r = CallWindowProc(g_pfnEditProc, hwnd, msg, wParam, lParam);

        if (scrollCode == SB_THUMBTRACK) {
            g_bVScrollThumbTrackActive = 1;
            skipLineNumRefresh = 1;
        } else if (scrollCode == SB_THUMBPOSITION || scrollCode == SB_TOP || scrollCode == SB_BOTTOM) {
            refreshNow = 1;
            g_bVScrollThumbTrackActive = 0;
        } else if (scrollCode == SB_ENDSCROLL) {
            if (g_bVScrollThumbTrackActive) refreshNow = 1;
            g_bVScrollThumbTrackActive = 0;
        }

        if (refreshNow) QueueEditRepaintAfterJump();

        if (g_bPagedMode) PagedSyncVScroll();

        if (skipLineNumRefresh) {
            /* Avoid extra gutter churn during thumb tracking; refresh once at settle. */
        } else if (refreshNow) {
            RequestLineNumberRefresh(LINENUM_DIRTY_SCROLL, 1);
        } else {
            RequestLineNumberRefresh(LINENUM_DIRTY_SCROLL, 0);
        }

        PagedHandleVScrollEdge(scrollCode);
        if (g_bShowColumnIndicator) InvalidateColumnIndicator();
        return r;
    }
    
    /* Redraw on horizontal scroll */
    if (msg == WM_HSCROLL && g_bShowColumnIndicator) {
        LRESULT r = CallWindowProc(g_pfnEditProc, hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        return r;
    }

    if (msg == WM_CHAR) {
        ClearStatusMessage();
    }

    if (msg == WM_KEYUP || msg == WM_LBUTTONUP) {
        if (g_bPagedMode) PagedMaybeShiftWindowByCaret();
        if (msg == WM_LBUTTONUP) ClearStatusMessage();
        UpdateStatus();
        RequestLineNumberRefresh(LINENUM_DIRTY_SCROLL, 0);
        if (g_bPagedMode) PagedSyncVScroll();
    }

    if (msg == WM_KILLFOCUS) {
        g_bVScrollThumbTrackActive = 0;
        CloseReplaceTypingGroup();
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
    HDC hdc;
    HWND hwndExistingQuick;

    (void)hPrevInstance;

    g_bQuickNoteWindow = CmdLineHasQuickNoteSwitch(lpCmdLine);
    if (g_bQuickNoteWindow) {
        hwndExistingQuick = FindQuickNoteWindow(NULL);
        if (hwndExistingQuick) {
            ActivateQuickNoteWindow(hwndExistingQuick, 1);
            return 0;
        }
    }

    /* Detect color vs grayscale display */
    hdc = GetDC(NULL);
    g_bColorDisplay = (GetDeviceCaps(hdc, BITSPIXEL) > 4) ? 1 : 0;
    ReleaseDC(NULL, hdc);
    g_bMousePresent = GetSystemMetrics(SM_MOUSEPRESENT) ? 1 : 0;

    if (!InitApplication(hInstance)) {
        return 1;
    }

    if (!InitInstance(hInstance, nCmdShow)) {
        return 1;
    }

    if (g_bQuickNoteWindow) DoQuickNote();

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
    wc.hCursor = NULL;  /* We manage busy cursor explicitly when needed. */
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
    {
        HMENU hMenuCut = CreatePopupMenu();
        AppendMenuW(hMenuCut, MF_STRING, IDM_EDIT_CUT, L"&Selection\tCtrl+X");
        AppendMenuW(hMenuCut, MF_STRING, IDM_EDIT_CUTLINE, L"&Line\tCtrl+Shift+X");
        AppendMenuW(hMenuEdit, MF_POPUP, (UINT)hMenuCut, L"Cu&t...");
    }
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_SELECTALL, L"Select &All\tCtrl+A");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, NULL);
    {
        HMENU hMenuFind = CreatePopupMenu();
        AppendMenuW(hMenuFind, MF_STRING, IDM_EDIT_FIND, L"&Find...\tCtrl+F");
        AppendMenuW(hMenuFind, MF_STRING, IDM_EDIT_FINDNEXT, L"Find &Next\tCtrl+3");
        AppendMenuW(hMenuFind, MF_STRING, IDM_EDIT_REPLACE, L"&Replace...\tCtrl+H");
        AppendMenuW(hMenuEdit, MF_POPUP, (UINT)hMenuFind, L"&Find...");
    }
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_GOTOLINE, L"&Go to Line...\tCtrl+G");
    AppendMenuW(hMenuEdit, MF_SEPARATOR, 0, NULL);
    {
        HMENU hMenuInsert = CreatePopupMenu();
        AppendMenuW(hMenuInsert, MF_STRING, IDM_EDIT_INSDATE, L"&Date\tCtrl+;");
        AppendMenuW(hMenuInsert, MF_STRING, IDM_EDIT_INSTIME, L"&Time\tCtrl+'");
        AppendMenuW(hMenuInsert, MF_STRING, IDM_EDIT_INSDATETIME, L"Date && Ti&me\tCtrl+:");
        AppendMenuW(hMenuInsert, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenuInsert, MF_STRING, IDM_EDIT_INSRULE, L"&Horizontal Rule\tCtrl+R");
        AppendMenuW(hMenuEdit, MF_POPUP, (UINT)hMenuInsert, L"&Insert");
    }
    AppendMenuW(hMenuEdit, MF_STRING, IDM_EDIT_REFLOW, L"Re&flow Paragraph\tCtrl+J");

    /* View menu */
    AppendMenuW(hMenuView, MF_STRING | MF_CHECKED, IDM_VIEW_WORDWRAP, L"&Word Wrap\tAlt+W");
    AppendMenuW(hMenuView, MF_STRING | MF_CHECKED, IDM_VIEW_LINENUMS, L"&Line Numbers\tAlt+L");
    AppendMenuW(hMenuView, MF_STRING | MF_CHECKED, IDM_VIEW_STATUSBAR, L"&Status Bar\tAlt+B");
    AppendMenuW(hMenuView, MF_STRING | MF_CHECKED, IDM_VIEW_SCROLLBARS, L"Scro&llbars\tAlt+S");
    AppendMenuW(hMenuView, MF_SEPARATOR, 0, NULL);

    /* Theme submenu - only show on color displays */
    if (g_bColorDisplay) {
        hMenuTheme = CreatePopupMenu();
        AppendMenuW(hMenuTheme, MF_STRING | MF_CHECKED, IDM_VIEW_THEME_DEFAULT, L"&Default");
        AppendMenuW(hMenuTheme, MF_STRING, IDM_VIEW_THEME_GREEN, L"&Green");
        AppendMenuW(hMenuTheme, MF_STRING, IDM_VIEW_THEME_AMBER, L"&Amber");
        AppendMenuW(hMenuTheme, MF_STRING, IDM_VIEW_THEME_BLUE, L"&Blue");
        AppendMenuW(hMenuView, MF_POPUP, (UINT)hMenuTheme, L"&Theme");
        g_hThemeMenu = hMenuTheme;
    } else {
        g_hThemeMenu = NULL;
    }

    AppendMenuW(hMenuView, MF_STRING, IDM_VIEW_INVERSE, L"&Inverse Colors\tAlt+I");
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
    int cbHeight, sbHeight, editLeft, editHeight, editWidth;
    int reservePagedScroll = 0;
    int showPagedScroll = 0;
    int showPagedCorner = 0;
    int vscrollWidth = 0;
    int vscrollHeight = 0;
    int hscrollHeight = 0;
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
    if (g_bFullScreen)
        sbHeight = g_bFSStatusBar ? (rcStatus.bottom - rcStatus.top) : 0;
    else
        sbHeight = g_bShowStatusBar ? (rcStatus.bottom - rcStatus.top) : 0;

    /* Set status bar parts: left 3/4, right 1/4 */
    {
        int parts[2];
        parts[0] = (cx * 3) / 4;
        parts[1] = -1;
        SendMessageW(g_hwndStatus, SB_SETPARTS, 2, (LPARAM)parts);
    }

    editHeight = cy - cbHeight - sbHeight;
    editLeft = g_bShowLineNums ? g_lineNumWidth : 0;
    editWidth = cx - editLeft;

    if (g_hwndLineNum)
        MoveWindow(g_hwndLineNum, 0, cbHeight, g_lineNumWidth, editHeight, TRUE);
    if (g_bPagedMode && g_hwndPagedVScroll) {
        showPagedScroll = g_bShowScrollbars ? 1 : 0;
        reservePagedScroll = showPagedScroll;
        vscrollWidth = GetSystemMetrics(SM_CXVSCROLL);
        if (vscrollWidth <= 0) vscrollWidth = 16;
        vscrollWidth += PAGED_VSCROLL_EXTRA_W;
        if (showPagedScroll && !g_bWordWrap) {
            hscrollHeight = GetSystemMetrics(SM_CYHSCROLL);
            if (hscrollHeight <= 0) hscrollHeight = 16;
            showPagedCorner = 1;
        }
        if (reservePagedScroll && editWidth > vscrollWidth) editWidth -= vscrollWidth;
    }
    vscrollHeight = editHeight - hscrollHeight;
    if (vscrollHeight < 0) vscrollHeight = 0;

    MoveWindow(g_hwndEdit, editLeft, cbHeight, editWidth, editHeight, TRUE);

    if (g_bPagedMode) SetEditVerticalScrollbarVisible(0);
    else SetEditVerticalScrollbarVisible(g_bShowScrollbars ? 1 : 0);

    if (g_hwndPagedVScroll) {
        if (reservePagedScroll) {
            MoveWindow(g_hwndPagedVScroll,
                editLeft + editWidth - PAGED_VSCROLL_SEAM_OVERLAP,
                cbHeight,
                vscrollWidth + PAGED_VSCROLL_SEAM_OVERLAP,
                vscrollHeight, TRUE);
            ShowWindow(g_hwndPagedVScroll, showPagedScroll ? SW_SHOW : SW_HIDE);
        } else {
            ShowWindow(g_hwndPagedVScroll, SW_HIDE);
        }
    }

    if (g_hwndPagedCorner) {
        if (reservePagedScroll && showPagedCorner) {
            MoveWindow(g_hwndPagedCorner,
                editLeft + editWidth - PAGED_VSCROLL_SEAM_OVERLAP,
                cbHeight + editHeight - hscrollHeight,
                vscrollWidth + PAGED_VSCROLL_SEAM_OVERLAP,
                hscrollHeight, TRUE);
            ShowWindow(g_hwndPagedCorner, SW_SHOW);
        } else {
            ShowWindow(g_hwndPagedCorner, SW_HIDE);
        }
    }

    if (g_hwndPagedSeam) {
        if (reservePagedScroll) {
            int arrowSpan = vscrollWidth;
            int seamTop = cbHeight + arrowSpan + 1;
            int seamX = editLeft + editWidth - 2;
            int seamWidth = 1;  /* Divider only */
            int seamHeight = vscrollHeight - (arrowSpan * 2) - 2;
            if (seamHeight < 0) seamHeight = 0;
            SetWindowPos(g_hwndPagedSeam, HWND_TOP,
                seamX,
                seamTop,
                seamWidth,
                seamHeight,
                SWP_SHOWWINDOW);
        } else {
            ShowWindow(g_hwndPagedSeam, SW_HIDE);
        }
    }

    if (g_bPagedMode) PagedSyncVScroll();
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

    if (g_bPagedMode && g_pagedText && g_pagedLineStarts) {
        if (lineNum > g_pagedLineCount) lineNum = g_pagedLineCount;
        charIdx = g_pagedLineStarts[lineNum - 1];
        if (PagedLoadWindowAt(charIdx)) {
            int localIdx = charIdx - g_pagedPageStart;
            if (localIdx < 0) localIdx = 0;
            if (localIdx > g_pagedPageLen) localIdx = g_pagedPageLen;
            SendMessageW(g_hwndEdit, EM_SETSEL, localIdx, localIdx);
            SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
            SetFocus(g_hwndEdit);
            UpdateStatus();
        }
        return;
    }

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
#ifndef WS_EX_CAPTIONOKBTN
#define WS_EX_CAPTIONOKBTN 0x80000000L  /* OK button in title bar - CE specific */
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

static int BufferMatchAt(const wchar_t *buf, int len, int pos, const wchar_t *needle, int needleLen)
{
    int i;

    if (!buf || !needle || needleLen <= 0) return 0;
    if (pos < 0 || pos + needleLen > len) return 0;

    for (i = 0; i < needleLen; i++) {
        if (!CharsMatch(buf[pos + i], needle[i])) return 0;
    }
    return 1;
}

static int FindNextMatchInBuffer(const wchar_t *buf, int len,
    const wchar_t *needle, int needleLen, int start,
    int *outFoundAt, int *outWrapped)
{
    int i, j;

    if (outFoundAt) *outFoundAt = -1;
    if (outWrapped) *outWrapped = 0;

    if (!buf || !needle || needleLen <= 0 || len < needleLen) return 0;

    if (start < 0) start = 0;
    if (start > len) start = 0;

    for (i = start; i <= len - needleLen; i++) {
        for (j = 0; j < needleLen; j++) {
            if (!CharsMatch(buf[i + j], needle[j])) break;
        }
        if (j == needleLen) {
            if (outFoundAt) *outFoundAt = i;
            return 1;
        }
    }

    for (i = 0; i < start && i <= len - needleLen; i++) {
        for (j = 0; j < needleLen; j++) {
            if (!CharsMatch(buf[i + j], needle[j])) break;
        }
        if (j == needleLen) {
            if (outFoundAt) *outFoundAt = i;
            if (outWrapped) *outWrapped = 1;
            return 1;
        }
    }

    return 0;
}

static void RefreshEditAfterLargeJump(void)
{
    /* Force a full repaint after large jumps to avoid stale scroll artifacts on CE edit controls. */
    InvalidateRect(g_hwndEdit, NULL, TRUE);
    UpdateWindow(g_hwndEdit);
}

static int PerformTrackedUndo(void)
{
    int undoPos;

    if (g_bPagedMode && Undo_CanUndo() && Undo_PeekUndoPos(&undoPos)) {
        if (!PagedPrepareUndoTarget(undoPos)) {
            SetStatusMessage(L"Cannot restore undo target");
            return 1;
        }
    }

    if (Undo_Perform()) {
        g_bKeepPagedReplaceUndo = 0;
        return 1;
    }

    return 0;
}

static int PerformTrackedRedo(void)
{
    int redoPos;

    if (g_bPagedMode && Undo_CanRedo() && Undo_PeekRedoPos(&redoPos)) {
        if (!PagedPrepareUndoTarget(redoPos)) {
            SetStatusMessage(L"Cannot restore redo target");
            return 1;
        }
    }

    if (Undo_Redo()) return 1;
    return 0;
}

static void QueueEditRepaintAfterJump(void)
{
    if (!g_hwndEdit || g_bPostJumpRepaintPending) return;
    g_bPostJumpRepaintPending = 1;
    PostMessage(g_hwndEdit, PWM_EDIT_POSTJUMP_REPAINT, 0, 0);
}

static void DoFindNext(void)
{
    int len, findLen, start, k;
    int canSelectRange, usePaged;
    int preserveUndo = 0;
    int undoPageStart = 0;
    int undoDelta = 0;
    int busy = 0;
    wchar_t *buf = NULL;
    wchar_t msg[96];
    DWORD selStart, selEnd;
    int line, col;
    int localStart, localEnd;
    int foundAt = -1;
    int wrapped = 0;

    if (!g_findText[0]) return;

    findLen = lstrlenW(g_findText);
    usePaged = (g_bPagedMode && g_pagedText != NULL);
    if (usePaged) {
        if (!PagedCommitPage()) return;
        len = g_pagedTextLen;
        canSelectRange = 1;
        buf = g_pagedText;
    } else {
        len = GetWindowTextLengthW(g_hwndEdit);
        canSelectRange = (len <= 65535);
    }
    if (len == 0) return;

    if (len > BUSY_TEXT_THRESHOLD) {
        SetStatusMessage(L"Searching...");
        BeginBusyCursor(L"find");
        busy = 1;
    }

    if (!usePaged) {
        buf = (wchar_t *)LocalAlloc(LMEM_FIXED, (len + 1) * sizeof(wchar_t));
        if (!buf) {
            if (busy) {
                EndBusyCursor(L"find");
                ClearStatusMessage();
            }
            return;
        }
        GetWindowTextW(g_hwndEdit, buf, len + 1);
    }

    SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    (void)selEnd;
    if (usePaged) start = PagedGetGlobalSelStart() + 1;
    else start = (int)selStart + 1;
    if (start > len) start = 0;

    FindNextMatchInBuffer(buf, len, g_findText, findLen, start, &foundAt, &wrapped);

    if (foundAt >= 0) {
        if (usePaged) {
            if (g_bKeepPagedReplaceUndo && Undo_CanUndo()) {
                g_bPreserveUndoOnPagedLoad = 1;
                preserveUndo = 1;
                undoPageStart = g_pagedPageStart;
            }
            if (PagedLoadWindowAt(foundAt)) {
                if (preserveUndo) {
                    undoDelta = undoPageStart - g_pagedPageStart;
                    if (undoDelta) Undo_ShiftPositions(undoDelta);
                }
                localStart = foundAt - g_pagedPageStart;
                localEnd = localStart + findLen;
                if (localStart < 0) localStart = 0;
                if (localEnd > g_pagedPageLen) localEnd = g_pagedPageLen;
                SendMessageW(g_hwndEdit, EM_SETSEL, localStart, localEnd);
                SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
                RefreshEditAfterLargeJump();
            }
            if (preserveUndo) {
                g_bPreserveUndoOnPagedLoad = 0;
            }
            PagedIndexToLineCol(foundAt, &line, &col);
            wsprintfW(msg, wrapped ? L"Found at Ln %d, Col %d (wrapped)" : L"Found at Ln %d, Col %d", line, col);
        } else if (canSelectRange) {
            SendMessage(g_hwndEdit, EM_SETSEL, foundAt, foundAt + findLen);
            SendMessage(g_hwndEdit, EM_SCROLLCARET, 0, 0);
            RefreshEditAfterLargeJump();
            line = (int)SendMessage(g_hwndEdit, EM_LINEFROMCHAR, foundAt, 0) + 1;
            col = foundAt - (int)SendMessage(g_hwndEdit, EM_LINEINDEX, line - 1, 0) + 1;
            wsprintfW(msg, wrapped ? L"Found at Ln %d, Col %d (wrapped)" : L"Found at Ln %d, Col %d", line, col);
        } else {
            line = 1;
            col = 1;
            for (k = 0; k < foundAt; k++) {
                if (buf[k] == L'\n') {
                    line++;
                    col = 1;
                } else {
                    col++;
                }
            }
            wsprintfW(msg, wrapped ? L"Found at Ln %d, Col %d (wrapped, no jump: large file)"
                                   : L"Found at Ln %d, Col %d (no jump: large file)", line, col);
        }
        SetStatusMessage(msg);
    } else {
        SetStatusMessage(L"Text not found");
    }

    if (!usePaged && buf) LocalFree(buf);
    if (busy) EndBusyCursor(L"find");
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
    int replLen, len;
    wchar_t *selText = NULL;
    int selTextLen = 0;
    wchar_t msg[80];
    int replLine, replCol, nextLine, nextCol;

    if (!g_findText[0]) return;

    findLen = lstrlenW(g_findText);
    replLen = lstrlenW(g_replaceText);

    if (g_bPagedMode) {
        int globalStart;
        int foundAt = -1;
        int wrapped = 0;
        int nextStart;
        int nextFound = -1;
        int nextWrapped = 0;
        int newLen;
        int tailStart, tailLen;
        int localStart, localEnd;
        int undoPageStart;
        int undoLocalStart;
        int undoDelta;
        int haveUndoRecord = 0;
        wchar_t *undoDeleted = NULL;
        wchar_t *newDoc;

        if (!g_pagedText) return;
        if (!PagedCommitPage()) return;
        g_bKeepPagedReplaceUndo = 0;

        len = g_pagedTextLen;
        if (len < findLen || findLen <= 0) {
            SetStatusMessage(L"Text not found");
            return;
        }

        SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        globalStart = g_pagedPageStart + (int)selStart;
        if (globalStart < 0) globalStart = 0;
        if (globalStart > len) globalStart = len;

        if (BufferMatchAt(g_pagedText, len, globalStart, g_findText, findLen)) {
            foundAt = globalStart;
        } else {
            nextStart = globalStart + 1;
            if (nextStart > len) nextStart = 0;
            if (!FindNextMatchInBuffer(g_pagedText, len, g_findText, findLen, nextStart, &foundAt, &wrapped)) {
                SetStatusMessage(L"Text not found");
                return;
            }
        }

        PagedIndexToLineCol(foundAt, &replLine, &replCol);
        undoPageStart = g_pagedPageStart;
        if (foundAt < g_pagedPageStart || foundAt + findLen > g_pagedPageStart + g_pagedPageLen) {
            if (!PagedLoadWindowAt(foundAt)) undoPageStart = -1;
            else undoPageStart = g_pagedPageStart;
        }
        if (undoPageStart >= 0) {
            undoLocalStart = foundAt - undoPageStart;
            if (undoLocalStart < 0) undoLocalStart = 0;
            if (undoLocalStart > g_pagedPageLen) undoLocalStart = g_pagedPageLen;

            undoDeleted = (wchar_t *)LocalAlloc(LMEM_FIXED, (findLen + 1) * sizeof(wchar_t));
            if (undoDeleted) {
                for (i = 0; i < findLen; i++) undoDeleted[i] = g_pagedText[foundAt + i];
                undoDeleted[findLen] = 0;
                Undo_BeginGroup();
                Undo_RecordDelete(undoLocalStart, undoDeleted, findLen);
                Undo_RecordInsert(undoLocalStart, g_replaceText, -1);
                Undo_EndGroup();
                LocalFree(undoDeleted);
                haveUndoRecord = 1;
            }
        }

        newLen = len + (replLen - findLen);
        if (newLen < 0) return;
        newDoc = (wchar_t *)LocalAlloc(LMEM_FIXED, (newLen + 1) * sizeof(wchar_t));
        if (!newDoc) {
            MessageBoxW(g_hwndMain, L"Out of memory.", g_szAppTitle, MB_OK | MB_ICONERROR);
            return;
        }

        for (i = 0; i < foundAt; i++) newDoc[i] = g_pagedText[i];
        for (i = 0; i < replLen; i++) newDoc[foundAt + i] = g_replaceText[i];
        tailStart = foundAt + findLen;
        tailLen = len - tailStart;
        if (tailLen < 0) tailLen = 0;
        for (i = 0; i < tailLen; i++) newDoc[foundAt + replLen + i] = g_pagedText[tailStart + i];
        newDoc[newLen] = 0;

        LocalFree(g_pagedText);
        g_pagedText = newDoc;
        g_pagedTextLen = newLen;
        g_bPagedPageDirty = 0;
        PagedRebuildLineStarts();
        MarkStatusTotalsDirty();
        g_bDirty = 1;
        UpdateTitle();
        if (haveUndoRecord) g_bKeepPagedReplaceUndo = 1;

        nextStart = foundAt + replLen;
        if (nextStart > g_pagedTextLen) nextStart = 0;
        if (FindNextMatchInBuffer(g_pagedText, g_pagedTextLen, g_findText, findLen, nextStart, &nextFound, &nextWrapped)) {
            if (haveUndoRecord) {
                g_bPreserveUndoOnPagedLoad = 1;
            }
            if (PagedLoadWindowAt(nextFound)) {
                if (haveUndoRecord) {
                    undoDelta = undoPageStart - g_pagedPageStart;
                    if (undoDelta) Undo_ShiftPositions(undoDelta);
                }
                localStart = nextFound - g_pagedPageStart;
                localEnd = localStart + findLen;
                if (localStart < 0) localStart = 0;
                if (localEnd > g_pagedPageLen) localEnd = g_pagedPageLen;
                SendMessageW(g_hwndEdit, EM_SETSEL, localStart, localEnd);
                SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
                RefreshEditAfterLargeJump();
            }
            if (haveUndoRecord) {
                g_bPreserveUndoOnPagedLoad = 0;
            }
            PagedIndexToLineCol(nextFound, &nextLine, &nextCol);
            if (nextWrapped || wrapped) {
                wsprintfW(msg, L"Replaced Ln %d Col %d, next Ln %d Col %d (wrapped)",
                    replLine, replCol, nextLine, nextCol);
            } else {
                wsprintfW(msg, L"Replaced Ln %d Col %d, next Ln %d Col %d",
                    replLine, replCol, nextLine, nextCol);
            }
        } else {
            if (haveUndoRecord) {
                g_bPreserveUndoOnPagedLoad = 1;
            }
            if (PagedLoadWindowAt(foundAt + replLen)) {
                if (haveUndoRecord) {
                    undoDelta = undoPageStart - g_pagedPageStart;
                    if (undoDelta) Undo_ShiftPositions(undoDelta);
                }
                localStart = (foundAt + replLen) - g_pagedPageStart;
                if (localStart < 0) localStart = 0;
                if (localStart > g_pagedPageLen) localStart = g_pagedPageLen;
                SendMessageW(g_hwndEdit, EM_SETSEL, localStart, localStart);
                SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
            }
            if (haveUndoRecord) {
                g_bPreserveUndoOnPagedLoad = 0;
            }
            wsprintfW(msg, L"Replaced Ln %d Col %d, no more matches", replLine, replCol);
        }
        SetStatusMessage(msg);
        return;
    }

    SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    selLen = selEnd - selStart;
    
    /* If no selection or wrong length, find first */
    if (selLen != findLen) {
        DoFindNext();
        SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        selLen = selEnd - selStart;
        if (selLen != findLen) return;  /* Nothing found */
    }

    if (!CaptureEditRangeText(g_hwndEdit, selStart, selEnd, &selText, &selTextLen)) return;
    if (selTextLen != findLen) {
        LocalFree(selText);
        DoFindNext();
        return;
    }

    for (i = 0; i < selLen && match; i++) {
        if (!CharsMatch(selText[i], g_findText[i])) match = 0;
    }

    if (match) {
        replLine = (int)SendMessage(g_hwndEdit, EM_LINEFROMCHAR, selStart, 0) + 1;
        replCol = selStart - (int)SendMessage(g_hwndEdit, EM_LINEINDEX, replLine - 1, 0) + 1;
        
        /* Record undo: delete found text, insert replacement */
        Undo_BeginGroup();
        Undo_RecordDelete(selStart, selText, selLen);
        Undo_RecordInsert(selStart, g_replaceText, -1);
        Undo_EndGroup();
        SendMessage(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)g_replaceText);
        g_bDirty = 1;
        UpdateTitle();
        LocalFree(selText);
        
        /* Find next and show combined message */
        DoFindNext();
        SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        if (selEnd > selStart) {
            nextLine = (int)SendMessage(g_hwndEdit, EM_LINEFROMCHAR, selStart, 0) + 1;
            nextCol = selStart - (int)SendMessage(g_hwndEdit, EM_LINEINDEX, nextLine - 1, 0) + 1;
            wsprintfW(msg, L"Replaced Ln %d Col %d, next Ln %d Col %d", replLine, replCol, nextLine, nextCol);
        } else {
            wsprintfW(msg, L"Replaced Ln %d Col %d, no more matches", replLine, replCol);
        }
        SetStatusMessage(msg);
        return;
    }
    LocalFree(selText);
    DoFindNext();
}

static int EnsureReplaceAllBuffer(wchar_t **buf, int *cap, int required, int keepChars)
{
    int newCap;
    int i;
    wchar_t *newBuf;

    if (!buf || !cap) return 0;
    if (required <= *cap) return 1;

    newCap = *cap;
    if (newCap < 256) newCap = 256;
    while (newCap < required) {
        if (newCap > 0x20000000) {
            newCap = required;
            break;
        }
        newCap *= 2;
    }

    newBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
    if (!newBuf) {
        newCap = required;
        newBuf = (wchar_t *)LocalAlloc(LMEM_FIXED, (newCap + 1) * sizeof(wchar_t));
        if (!newBuf) return 0;
    }

    if (*buf && keepChars > 0) {
        if (keepChars > newCap) keepChars = newCap;
        for (i = 0; i < keepChars; i++) newBuf[i] = (*buf)[i];
    }
    newBuf[keepChars] = 0;

    if (*buf) LocalFree(*buf);
    *buf = newBuf;
    *cap = newCap;
    return 1;
}

static int DoReplaceAll(void)
{
    int len, findLen, replLen, count = 0, i, j;
    int outPos = 0;
    int outCap = 0;
    int matched;
    int busy = 0;
    wchar_t *buf, *newBuf;
    DWORD selStart;
    int keepGlobalPos;

    if (!g_findText[0]) return 0;

    findLen = lstrlenW(g_findText);
    replLen = lstrlenW(g_replaceText);

    if (g_bPagedMode) {
        if (!g_pagedText) return 0;
        if (!PagedCommitPage()) return 0;
        len = g_pagedTextLen;
    } else {
        len = GetWindowTextLengthW(g_hwndEdit);
    }

    if (len == 0) return 0;
    if (len > BUSY_TEXT_THRESHOLD) {
        SetStatusMessage(L"Replacing...");
        BeginBusyCursor(L"replall");
        busy = 1;
    }

    if (g_bPagedMode) {
        buf = g_pagedText;
        SendMessageW(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, 0);
        keepGlobalPos = g_pagedPageStart + (int)selStart;
        if (keepGlobalPos < 0) keepGlobalPos = 0;
        if (keepGlobalPos > len) keepGlobalPos = len;
    } else {
        buf = (wchar_t *)LocalAlloc(LMEM_FIXED, (len + 1) * sizeof(wchar_t));
        if (!buf) {
            if (busy) {
                EndBusyCursor(L"replall");
                ClearStatusMessage();
            }
            return 0;
        }
        GetWindowTextW(g_hwndEdit, buf, len + 1);
        keepGlobalPos = 0;
    }

    newBuf = NULL;
    if (!EnsureReplaceAllBuffer(&newBuf, &outCap, len + 1, 0)) {
        if (!g_bPagedMode) LocalFree(buf);
        if (busy) {
            EndBusyCursor(L"replall");
            ClearStatusMessage();
        }
        return 0;
    }

    i = 0;
    while (i < len) {
        matched = 0;
        if (i <= len - findLen) {
            matched = 1;
            for (j = 0; j < findLen; j++) {
                if (!CharsMatch(buf[i + j], g_findText[j])) {
                    matched = 0;
                    break;
                }
            }
        }

        if (matched) {
            if (!EnsureReplaceAllBuffer(&newBuf, &outCap, outPos + replLen + 1, outPos)) {
                if (!g_bPagedMode) LocalFree(buf);
                if (busy) {
                    EndBusyCursor(L"replall");
                    ClearStatusMessage();
                }
                LocalFree(newBuf);
                return 0;
            }
            for (j = 0; j < replLen; j++) newBuf[outPos++] = g_replaceText[j];
            i += findLen;
            count++;
        } else {
            if (!EnsureReplaceAllBuffer(&newBuf, &outCap, outPos + 2, outPos)) {
                if (!g_bPagedMode) LocalFree(buf);
                if (busy) {
                    EndBusyCursor(L"replall");
                    ClearStatusMessage();
                }
                LocalFree(newBuf);
                return 0;
            }
            newBuf[outPos++] = buf[i++];
        }
    }

    if (!EnsureReplaceAllBuffer(&newBuf, &outCap, outPos + 1, outPos)) {
        if (!g_bPagedMode) LocalFree(buf);
        if (busy) {
            EndBusyCursor(L"replall");
            ClearStatusMessage();
        }
        LocalFree(newBuf);
        return 0;
    }
    newBuf[outPos] = 0;

    if (count == 0) {
        LocalFree(newBuf);
        if (!g_bPagedMode) LocalFree(buf);
        if (busy) {
            EndBusyCursor(L"replall");
            ClearStatusMessage();
        }
        return 0;
    }

    /* Record undo: delete all, insert new */
    if (g_bPagedMode) {
        int localPos;
        LocalFree(g_pagedText);
        g_pagedText = newBuf;
        g_pagedTextLen = lstrlenW(newBuf);
        g_bPagedPageDirty = 0;
        PagedRebuildLineStarts();
        MarkStatusTotalsDirty();
        g_bDirty = 1;
        UpdateTitle();

        if (keepGlobalPos > g_pagedTextLen) keepGlobalPos = g_pagedTextLen;
        if (PagedLoadWindowAt(keepGlobalPos)) {
            localPos = keepGlobalPos - g_pagedPageStart;
            if (localPos < 0) localPos = 0;
            if (localPos > g_pagedPageLen) localPos = g_pagedPageLen;
            SendMessageW(g_hwndEdit, EM_SETSEL, localPos, localPos);
            SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
        }
    } else {
        Undo_BeginGroup();
        Undo_RecordDelete(0, buf, len);
        Undo_RecordInsert(0, newBuf, -1);
        Undo_EndGroup();
        SetWindowTextW(g_hwndEdit, newBuf);
        g_bDirty = 1;
        UpdateTitle();
        LocalFree(newBuf);
        LocalFree(buf);
    }

    if (busy) {
        EndBusyCursor(L"replall");
        ClearStatusMessage();
    }
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
        SetForegroundWindow(hwnd);
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
 * DoInsertDateTime - Insert date and/or time at cursor
 * mode: 0=date, 1=time, 2=both
 */
static void DoInsertDateTime(int mode)
{
    SYSTEMTIME st;
    wchar_t buf[64];
    int pos;

    GetLocalTime(&st);

    if (mode == 0) {
        /* Date only: YYYY-MM-DD */
        wsprintfW(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    } else if (mode == 1) {
        /* Time only: HH:MM */
        wsprintfW(buf, L"%02d:%02d", st.wHour, st.wMinute);
    } else {
        /* Both: YYYY-MM-DD HH:MM */
        wsprintfW(buf, L"%04d-%02d-%02d %02d:%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    }

    SetFocus(g_hwndEdit);
    SendMessageW(g_hwndEdit, EM_GETSEL, (WPARAM)&pos, (LPARAM)NULL);
    Undo_RecordInsert(pos, buf, -1);
    SendMessageW(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)buf);
    g_bDirty = 1;
}

/*
 * DoInsertRule - Insert horizontal rule on its own line
 * Width: 40 chars on narrow displays (<=480px), 72 chars otherwise
 * Skips leading newline if cursor is already at start of empty line
 */
static void DoInsertRule(void)
{
    wchar_t buf[80];
    wchar_t *p = buf;
    int i, width, selStart;
    int atLineStart = 0;

    width = (GetSystemMetrics(SM_CXSCREEN) <= 480) ? 40 : 72;

    /* Check if cursor is at start of line */
    SendMessageW(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, 0);
    if (selStart == 0) {
        atLineStart = 1;
    } else {
        int lineIdx = (int)SendMessageW(g_hwndEdit, EM_LINEFROMCHAR, selStart, 0);
        int lineStart = (int)SendMessageW(g_hwndEdit, EM_LINEINDEX, lineIdx, 0);
        if (selStart == lineStart) atLineStart = 1;
    }

    if (!atLineStart) {
        *p++ = L'\r';
        *p++ = L'\n';
    }
    for (i = 0; i < width; i++) *p++ = L'-';
    *p++ = L'\r';
    *p++ = L'\n';
    *p = 0;

    SetFocus(g_hwndEdit);
    Undo_RecordInsert(selStart, buf, -1);
    SendMessageW(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)buf);
    g_bDirty = 1;
}

/*
 * DoReflow - Reflow selected text or current paragraph to column limit
 */
static void DoReflow(void)
{
    int selStart, selEnd, paraStart, paraEnd;
    int selLen, len, col, i, j, wordStart, blankCount;
    int busy = 0;
    wchar_t *text = NULL;
    wchar_t *out = NULL;
    wchar_t *p;

    SendMessageW(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);

    /* If no selection, find current paragraph */
    if (selStart == selEnd) {
        int lineIdx = (int)SendMessageW(g_hwndEdit, EM_LINEFROMCHAR, selStart, 0);
        int totalLen = GetWindowTextLengthW(g_hwndEdit);

        /* Find paragraph start (search backward for blank line) */
        paraStart = (int)SendMessageW(g_hwndEdit, EM_LINEINDEX, lineIdx, 0);
        while (lineIdx > 0) {
            int prevLine = (int)SendMessageW(g_hwndEdit, EM_LINEINDEX, lineIdx - 1, 0);
            int prevLen = (int)SendMessageW(g_hwndEdit, EM_LINELENGTH, prevLine, 0);
            if (prevLen == 0) break;
            lineIdx--;
            paraStart = prevLine;
        }

        /* Find paragraph end (search forward for blank line) */
        lineIdx = (int)SendMessageW(g_hwndEdit, EM_LINEFROMCHAR, selStart, 0);
        paraEnd = (int)SendMessageW(g_hwndEdit, EM_LINEINDEX, lineIdx, 0);
        paraEnd += (int)SendMessageW(g_hwndEdit, EM_LINELENGTH, paraEnd, 0);
        while (paraEnd < totalLen) {
            int nextLine = paraEnd + 2; /* Skip \r\n */
            int nextLen = (int)SendMessageW(g_hwndEdit, EM_LINELENGTH, nextLine, 0);
            if (nextLen == 0 || nextLine >= totalLen) break;
            paraEnd = nextLine + nextLen;
        }

        selStart = paraStart;
        selEnd = paraEnd;
    }

    if (selEnd <= selStart) return;

    selLen = selEnd - selStart;
    if (selLen > BUSY_TEXT_THRESHOLD) {
        SetStatusMessage(L"Reflowing...");
        BeginBusyCursor(L"reflow");
        busy = 1;
    }

    if (!CaptureEditRangeText(g_hwndEdit, (DWORD)selStart, (DWORD)selEnd, &text, &len)) {
        if (busy) {
            EndBusyCursor(L"reflow");
            ClearStatusMessage();
        }
        return;
    }

    out = (wchar_t *)LocalAlloc(LMEM_FIXED, (len * 2 + 1) * sizeof(wchar_t));
    if (!out) {
        LocalFree(text);
        if (busy) {
            EndBusyCursor(L"reflow");
            ClearStatusMessage();
        }
        return;
    }

    /* Select the range for replacement */
    SendMessageW(g_hwndEdit, EM_SETSEL, selStart, selEnd);

    /* Reflow: convert line breaks to spaces, then re-wrap */
    p = out;
    col = 0;
    wordStart = -1;
    for (i = 0; i <= len; i++) {
        wchar_t ch = text[i];

        /* Handle line breaks: preserve paragraph boundaries (blank lines) */
        if (ch == L'\r') continue;
        if (ch == L'\n') {
            /* Peek ahead for blank line (paragraph boundary) */
            j = i + 1;
            blankCount = 0;
            while (j < len) {
                if (text[j] == L'\r' && j + 1 < len && text[j + 1] == L'\n') {
                    blankCount++;
                    j += 2;
                } else if (text[j] == L'\n') {
                    blankCount++;
                    j++;
                } else {
                    break;
                }
            }
            if (blankCount > 0) {
                /* Paragraph break: trim trailing space, preserve blank lines */
                if (p > out && *(p-1) == L' ') p--;
                *p++ = L'\r'; *p++ = L'\n';
                while (blankCount > 0) {
                    *p++ = L'\r'; *p++ = L'\n';
                    blankCount--;
                }
                col = 0;
                wordStart = -1;
                i = j - 1;
                continue;
            }
            /* Single line break within paragraph: convert to space */
            ch = L' ';
        }

        /* Skip multiple spaces */
        if (ch == L' ' && (p == out || *(p-1) == L' ' || *(p-1) == L'\n')) continue;

        /* Track word boundaries */
        if (ch == L' ' || ch == 0) {
            wordStart = (int)(p - out) + 1;
        } else if (wordStart < 0 || *(p > out ? p-1 : p) == L' ' || *(p > out ? p-1 : p) == L'\n') {
            wordStart = (int)(p - out);
        }

        /* Check if adding this char exceeds limit */
        if (ch != 0 && col >= g_nColumnLimit && ch != L' ') {
            /* Wrap before current word if possible */
            if (wordStart > 0) {
                wchar_t *ws = out + wordStart;
                if (ws > out && *(ws-1) == L' ') {
                    *(ws-1) = L'\r';
                    /* Insert \n after \r */
                    { wchar_t *q; for (q = p; q > ws; q--) *q = *(q-1); }
                    *ws = L'\n';
                    p++;
                    col = (int)(p - ws) - 1;
                    wordStart = (int)(ws - out) + 1;
                }
            }
        }

        if (ch == 0) break;
        *p++ = ch;
        col++;

        /* Reset col after line break */
        if (ch == L'\n') col = 0;
    }
    *p = 0;

    /* Record undo as one atomic reflow operation. */
    Undo_BeginGroup();
    Undo_RecordDelete(selStart, text, len);
    Undo_RecordInsert(selStart, out, -1);
    Undo_EndGroup();

    /* Replace selection with reflowed text */
    SendMessageW(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)out);
    g_bDirty = 1;

    LocalFree(text);
    LocalFree(out);
    if (busy) {
        EndBusyCursor(L"reflow");
        ClearStatusMessage();
    }
    SetFocus(g_hwndEdit);
}

/*
 * Options dialog
 */
static HWND g_hwndOptionsDlg = NULL;
static HWND g_hwndOptUseTabs = NULL;
static HWND g_hwndOptUseSpaces = NULL;
static HWND g_hwndOptTabSize = NULL;
static HWND g_hwndOptColumnLimit = NULL;
static HWND g_hwndOptFixedFont = NULL;
static HWND g_hwndOptShowColInd = NULL;
static HWND g_hwndOptNewlineMode = NULL;
static HWND g_hwndOptAutoWrap = NULL;
/* Options dialog control IDs */
#define IDC_OPT_TAB       100
#define IDC_OPT_USETABS   101
#define IDC_OPT_USESPACES 102
#define IDC_OPT_TABSIZE   103
#define IDC_OPT_CLEARREG  104
#define IDC_OPT_FONTSIZE  105
#define IDC_OPT_FIXEDFONT 106
#define IDC_OPT_THEMEDSEL 107
#define IDC_OPT_HIDETASKBAR 108
#define IDC_OPT_COLUMNLIMIT 109
#define IDC_OPT_QNSTORAGE   110
#define IDC_OPT_QNAUTOINIT  111
#define IDC_OPT_SHOWCOLIND  112
#define IDC_OPT_NEWLINEMODE 113
#define IDC_OPT_AUTOWRAP    114

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
        RequestLineNumberRefresh(LINENUM_DIRTY_LAYOUT, 1);
    }
}

/* Options dialog - tabbed layout */
static HWND g_hwndOptTab = NULL;
#define OPT_EDITOR_CTRL_COUNT 12
#define OPT_DISPLAY_CTRL_COUNT 5
#define OPT_STORAGE_CTRL_COUNT 2
static HWND g_optEditorCtrls[OPT_EDITOR_CTRL_COUNT];   /* Editor tab controls */
static HWND g_optDisplayCtrls[OPT_DISPLAY_CTRL_COUNT]; /* Display tab controls */
static HWND g_optStorageCtrls[OPT_STORAGE_CTRL_COUNT]; /* Storage tab controls */

static void ShowOptionsTab(int tab)
{
    int i;
    for (i = 0; i < OPT_EDITOR_CTRL_COUNT; i++) ShowWindow(g_optEditorCtrls[i], tab == 0 ? SW_SHOW : SW_HIDE);
    for (i = 0; i < OPT_DISPLAY_CTRL_COUNT; i++) ShowWindow(g_optDisplayCtrls[i], tab == 1 ? SW_SHOW : SW_HIDE);
    for (i = 0; i < OPT_STORAGE_CTRL_COUNT; i++) ShowWindow(g_optStorageCtrls[i], tab == 2 ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK OptionsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        {
            TCITEMW tci = {0};
            RECT tabRc;
            wchar_t buf[8];
            int x, y;

            /* Tab control */
            g_hwndOptTab = CreateWindowW(WC_TABCONTROLW, NULL,
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                4, 4, 282, 132, hwnd, (HMENU)IDC_OPT_TAB, g_hInst, NULL);
            tci.mask = TCIF_TEXT;
            tci.pszText = L"Editor";
            SendMessage(g_hwndOptTab, TCM_INSERTITEMW, 0, (LPARAM)&tci);
            tci.pszText = L"Display";
            SendMessage(g_hwndOptTab, TCM_INSERTITEMW, 1, (LPARAM)&tci);
            tci.pszText = L"Storage";
            SendMessage(g_hwndOptTab, TCM_INSERTITEMW, 2, (LPARAM)&tci);

            /* Get tab content area */
            SetRect(&tabRc, 0, 0, 282, 132);
            SendMessage(g_hwndOptTab, TCM_ADJUSTRECT, FALSE, (LPARAM)&tabRc);
            x = tabRc.left + 4;
            y = tabRc.top + 2;

            /* Editor tab: Indentation, Reflow, Clear Settings */
            g_optEditorCtrls[0] = CreateWindowW(L"STATIC", L"Indent:",
                WS_CHILD, x, y + 2, 40, 16, g_hwndOptTab, NULL, g_hInst, NULL);
            g_optEditorCtrls[1] = CreateWindowW(L"BUTTON", L"Tabs",
                WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
                x + 42, y, 45, 20, g_hwndOptTab, (HMENU)IDC_OPT_USETABS, g_hInst, NULL);
            g_hwndOptUseTabs = g_optEditorCtrls[1];
            g_optEditorCtrls[2] = CreateWindowW(L"BUTTON", L"Spaces:",
                WS_CHILD | BS_AUTORADIOBUTTON,
                x + 90, y, 58, 20, g_hwndOptTab, (HMENU)IDC_OPT_USESPACES, g_hInst, NULL);
            g_hwndOptUseSpaces = g_optEditorCtrls[2];
            wsprintfW(buf, L"%d", g_nTabSize);
            g_optEditorCtrls[3] = CreateWindowW(L"EDIT", buf,
                WS_CHILD | WS_BORDER | ES_NUMBER,
                x + 150, y, 22, 20, g_hwndOptTab, (HMENU)IDC_OPT_TABSIZE, g_hInst, NULL);
            g_hwndOptTabSize = g_optEditorCtrls[3];

            g_optEditorCtrls[4] = CreateWindowW(L"STATIC", L"Reflow to",
                WS_CHILD, x, y + 26, 55, 16, g_hwndOptTab, NULL, g_hInst, NULL);
            wsprintfW(buf, L"%d", g_nColumnLimit);
            g_optEditorCtrls[5] = CreateWindowW(L"EDIT", buf,
                WS_CHILD | WS_BORDER | ES_NUMBER,
                x + 55, y + 24, 30, 20, g_hwndOptTab, (HMENU)IDC_OPT_COLUMNLIMIT, g_hInst, NULL);
            g_hwndOptColumnLimit = g_optEditorCtrls[5];
            g_optEditorCtrls[6] = CreateWindowW(L"STATIC", L"columns",
                WS_CHILD, x + 88, y + 26, 50, 16, g_hwndOptTab, NULL, g_hInst, NULL);
            g_optEditorCtrls[7] = CreateWindowW(L"BUTTON", L"Show indicator",
                WS_CHILD | BS_AUTOCHECKBOX,
                x + 140, y + 24, 105, 20, g_hwndOptTab, (HMENU)IDC_OPT_SHOWCOLIND, g_hInst, NULL);
            g_hwndOptShowColInd = g_optEditorCtrls[7];
            if (g_bShowColumnIndicator) SendMessage(g_hwndOptShowColInd, BM_SETCHECK, 1, 0);
            g_optEditorCtrls[8] = CreateWindowW(L"BUTTON", L"Auto-wrap at column limit",
                WS_CHILD | BS_AUTOCHECKBOX,
                x, y + 50, 140, 20, g_hwndOptTab, (HMENU)IDC_OPT_AUTOWRAP, g_hInst, NULL);
            g_hwndOptAutoWrap = g_optEditorCtrls[8];
            SendMessage(g_hwndOptAutoWrap, BM_SETCHECK, g_bAutoWrapTyping, 0);
            g_optEditorCtrls[9] = CreateWindowW(L"BUTTON", L"Clear Settings and Registry...",
                WS_CHILD | BS_PUSHBUTTON,
                x, y + 74, 205, 22, g_hwndOptTab, (HMENU)IDC_OPT_CLEARREG, g_hInst, NULL);
            g_optEditorCtrls[10] = CreateWindowW(L"STATIC", L"Newlines:",
                WS_CHILD, x + 145, y + 53, 50, 16, g_hwndOptTab, NULL, g_hInst, NULL);
            g_optEditorCtrls[11] = CreateWindowW(L"COMBOBOX", NULL,
                WS_CHILD | CBS_DROPDOWNLIST,
                x + 192, y + 50, 55, 90, g_hwndOptTab, (HMENU)IDC_OPT_NEWLINEMODE, g_hInst, NULL);
            g_hwndOptNewlineMode = g_optEditorCtrls[11];
            SendMessageW(g_hwndOptNewlineMode, CB_ADDSTRING, 0, (LPARAM)L"Auto");
            SendMessageW(g_hwndOptNewlineMode, CB_ADDSTRING, 0, (LPARAM)L"CRLF");
            SendMessageW(g_hwndOptNewlineMode, CB_ADDSTRING, 0, (LPARAM)L"LF");
            if (g_nNewlineMode < NEWLINE_MODE_AUTO || g_nNewlineMode > NEWLINE_MODE_FORCE_LF)
                g_nNewlineMode = NEWLINE_MODE_AUTO;
            SendMessageW(g_hwndOptNewlineMode, CB_SETCURSEL, g_nNewlineMode, 0);

            SendMessage(g_bUseTabs ? g_hwndOptUseTabs : g_hwndOptUseSpaces, BM_SETCHECK, 1, 0);

            /* Display tab: Font, Theme highlights, Hide taskbar */
            g_optDisplayCtrls[0] = CreateWindowW(L"STATIC", L"Font:",
                WS_CHILD, x, y + 2, 35, 16, g_hwndOptTab, NULL, g_hInst, NULL);
            g_optDisplayCtrls[1] = CreateWindowW(L"COMBOBOX", NULL,
                WS_CHILD | CBS_DROPDOWNLIST,
                x + 35, y, 45, 80, g_hwndOptTab, (HMENU)IDC_OPT_FONTSIZE, g_hInst, NULL);
            SendMessageW(g_optDisplayCtrls[1], CB_ADDSTRING, 0, (LPARAM)L"10");
            SendMessageW(g_optDisplayCtrls[1], CB_ADDSTRING, 0, (LPARAM)L"12");
            SendMessageW(g_optDisplayCtrls[1], CB_ADDSTRING, 0, (LPARAM)L"14");
            SendMessageW(g_optDisplayCtrls[1], CB_ADDSTRING, 0, (LPARAM)L"16");
            SendMessageW(g_optDisplayCtrls[1], CB_SETCURSEL, g_fontSizeIdx, 0);
            g_optDisplayCtrls[2] = CreateWindowW(L"BUTTON", L"Fixed width",
                WS_CHILD | BS_AUTOCHECKBOX,
                x + 85, y, 90, 20, g_hwndOptTab, (HMENU)IDC_OPT_FIXEDFONT, g_hInst, NULL);
            g_hwndOptFixedFont = g_optDisplayCtrls[2];
            SendMessage(g_hwndOptFixedFont, BM_SETCHECK, g_bFixedFont, 0);

            g_optDisplayCtrls[3] = CreateWindowW(L"BUTTON", L"Theme selection highlights",
                WS_CHILD | BS_AUTOCHECKBOX,
                x, y + 24, 180, 20, g_hwndOptTab, (HMENU)IDC_OPT_THEMEDSEL, g_hInst, NULL);
            SendMessage(g_optDisplayCtrls[3], BM_SETCHECK, g_bThemedSelection, 0);
            g_optDisplayCtrls[4] = CreateWindowW(L"BUTTON", L"Hide taskbar in fullscreen",
                WS_CHILD | BS_AUTOCHECKBOX,
                x, y + 48, 180, 20, g_hwndOptTab, (HMENU)IDC_OPT_HIDETASKBAR, g_hInst, NULL);
            SendMessage(g_optDisplayCtrls[4], BM_SETCHECK, g_bHideTaskbar, 0);

            /* Storage tab: Quick note options */
            g_optStorageCtrls[0] = CreateWindowW(L"BUTTON", L"Prefer storage card for quick notes",
                WS_CHILD | BS_AUTOCHECKBOX,
                x, y, 220, 20, g_hwndOptTab, (HMENU)IDC_OPT_QNSTORAGE, g_hInst, NULL);
            SendMessage(g_optStorageCtrls[0], BM_SETCHECK, g_bQuickNoteStorage, 0);
            g_optStorageCtrls[1] = CreateWindowW(L"BUTTON", L"Auto initialize folders on card",
                WS_CHILD | BS_AUTOCHECKBOX,
                x, y + 24, 195, 20, g_hwndOptTab, (HMENU)IDC_OPT_QNAUTOINIT, g_hInst, NULL);
            SendMessage(g_optStorageCtrls[1], BM_SETCHECK, g_bQuickNoteAutoInit, 0);

            ShowOptionsTab(0);
        }
        return 0;

    case WM_NOTIFY:
        {
            NMHDR *nmh = (NMHDR *)lParam;
            if (nmh->idFrom == IDC_OPT_TAB && nmh->code == TCN_SELCHANGE) {
                ShowOptionsTab(TabCtrl_GetCurSel(g_hwndOptTab));
            }
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[8];
            int size, newSizeIdx, newFixed, newThemedSel, newNewlineMode;

            g_bUseTabs = (int)SendMessage(g_hwndOptUseTabs, BM_GETCHECK, 0, 0);
            GetWindowTextW(g_hwndOptTabSize, buf, 8);
            size = 0;
            { int i; for (i = 0; buf[i]; i++) size = size * 10 + (buf[i] - '0'); }
            if (size >= 1 && size <= 8) g_nTabSize = size;

            GetWindowTextW(g_hwndOptColumnLimit, buf, 8);
            size = 0;
            { int i; for (i = 0; buf[i]; i++) size = size * 10 + (buf[i] - '0'); }
            if (size >= 20 && size <= 200) g_nColumnLimit = size;
            
            g_bShowColumnIndicator = (int)SendMessage(g_hwndOptShowColInd, BM_GETCHECK, 0, 0);
            g_bAutoWrapTyping = (int)SendMessage(g_hwndOptAutoWrap, BM_GETCHECK, 0, 0);
            InvalidateColumnIndicator();

            newSizeIdx = (int)SendMessageW(g_optDisplayCtrls[1], CB_GETCURSEL, 0, 0);
            if (newSizeIdx < 0 || newSizeIdx >= (int)(sizeof(g_fontSizes) / sizeof(g_fontSizes[0])))
                newSizeIdx = g_fontSizeIdx;
            newFixed = (int)SendMessage(g_hwndOptFixedFont, BM_GETCHECK, 0, 0);
            if (newSizeIdx != g_fontSizeIdx || newFixed != g_bFixedFont) {
                g_fontSizeIdx = newSizeIdx;
                g_bFixedFont = newFixed;
                UpdateFont();
            }

            newThemedSel = (int)SendMessage(g_optDisplayCtrls[3], BM_GETCHECK, 0, 0);
            if (newThemedSel != g_bThemedSelection) {
                if (!newThemedSel) RestoreSelectionColors();
                g_bThemedSelection = newThemedSel;
            }

            g_bHideTaskbar = (int)SendMessage(g_optDisplayCtrls[4], BM_GETCHECK, 0, 0);
            g_bQuickNoteStorage = (int)SendMessage(g_optStorageCtrls[0], BM_GETCHECK, 0, 0);
            g_bQuickNoteAutoInit = (int)SendMessage(g_optStorageCtrls[1], BM_GETCHECK, 0, 0);
            newNewlineMode = (int)SendMessageW(g_hwndOptNewlineMode, CB_GETCURSEL, 0, 0);
            if (newNewlineMode < NEWLINE_MODE_AUTO || newNewlineMode > NEWLINE_MODE_FORCE_LF)
                newNewlineMode = NEWLINE_MODE_AUTO;
            g_nNewlineMode = newNewlineMode;
            if (!g_szFilePath[0]) {
                g_fileNewlineStyle = GetDefaultNewlineStyleForNewFile();
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

    if (g_hwndOptionsDlg) {
        SetFocus(g_hwndOptionsDlg);
        return;
    }

    wc.lpfnWndProc = OptionsWndProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PalmweaverOptions";
    RegisterClassW(&wc);

    g_hwndOptionsDlg = CreateWindowExW(WS_EX_CAPTIONOKBTN, L"PalmweaverOptions", L"Options",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        30, 20, 300, 168,
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

static void RequestLineNumberRefresh(UINT flags, int immediate)
{
    UINT delayMs;

    g_lineNumDirtyFlags |= flags;
    if (flags & LINENUM_DIRTY_TEXT) g_lineNumTextSeq++;

    if (!g_bShowLineNums) {
        if (g_lineNumTimerActive && g_hwndMain) {
            KillTimer(g_hwndMain, LINENUM_TIMER_ID);
            g_lineNumTimerActive = 0;
        }
        g_lineNumDirtyFlags = 0;
        return;
    }

    if (immediate || !g_hwndMain) {
        if (g_lineNumTimerActive && g_hwndMain) {
            KillTimer(g_hwndMain, LINENUM_TIMER_ID);
            g_lineNumTimerActive = 0;
        }
        g_lineNumDirtyFlags = 0;
        UpdateLineNumbers();
        return;
    }

    delayMs = (flags & LINENUM_DIRTY_TEXT) ? LINENUM_TIMER_TEXT_MS : LINENUM_TIMER_SCROLL_MS;

    /* Debounce text edits by restarting timer, but leave scroll timer running. */
    if (g_lineNumTimerActive && (flags & LINENUM_DIRTY_TEXT)) {
        KillTimer(g_hwndMain, LINENUM_TIMER_ID);
        g_lineNumTimerActive = 0;
    }

    if (!g_lineNumTimerActive) {
        if (SetTimer(g_hwndMain, LINENUM_TIMER_ID, delayMs, NULL)) {
            g_lineNumTimerActive = 1;
        } else {
            g_lineNumDirtyFlags = 0;
            UpdateLineNumbers();
        }
    }
}

static void UpdateLineNumbers(void)
{
    static wchar_t *cachedText = NULL;
    static int *cachedLineStarts = NULL;
    static int cachedLineStartCount = 1;
    static UINT cachedTextSeq = 0;
    static int cachedLen = -1;
    static int cachedFirstVisible = -1;
    static int cachedVisLines = -1;
    static int cachedWrapMode = -1;
    static HWND cachedEditHwnd = NULL;
    wchar_t *cachedOutput;
    wchar_t *buf;
    wchar_t *text = NULL;
    int i, visLines, firstVisible, pos = 0, textLen;
    int charIdx, logicalLine;
    int textChanged;
    int wrapChanged;
    int editRecreated;
    int logicalTotal = 1;
    int visibleRows, displayEnd;
    int lineNumLimit;
    RECT rcEdit;
    int lineHeight = 0;

    if (!g_bShowLineNums || !g_hwndLineNum || !g_hFont) return;

    visLines = (int)SendMessage(g_hwndEdit, EM_GETLINECOUNT, 0, 0);
    textLen = GetWindowTextLengthW(g_hwndEdit);
    firstVisible = (int)SendMessage(g_hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    textChanged = (cachedTextSeq != g_lineNumTextSeq);
    wrapChanged = (cachedWrapMode != g_bWordWrap);
    editRecreated = (cachedEditHwnd != g_hwndEdit);

    /* Quick exit if scroll position unchanged and line count same */
    if (!textChanged &&
        !wrapChanged &&
        !editRecreated &&
        textLen == cachedLen &&
        firstVisible == cachedFirstVisible &&
        visLines == cachedVisLines)
        return;

    if (!EnsureLineNumBuffers(4096)) return;
    cachedWrapMode = g_bWordWrap;
    cachedEditHwnd = g_hwndEdit;
    buf = g_lineNumRenderBuf;
    cachedOutput = g_lineNumCachedOutput;
    lineNumLimit = g_lineNumBufCap - 16;
    if (lineNumLimit < 1) lineNumLimit = 1;

    if (g_bPagedMode && g_pagedLineStarts && g_pagedLineCount > 0) {
        int globalIdx;

        if (g_bPagedPageDirty) {
            /* Keep paged line-start metadata aligned before rendering global gutter numbers. */
            if (PagedCommitPage()) {
                visLines = (int)SendMessage(g_hwndEdit, EM_GETLINECOUNT, 0, 0);
                textLen = GetWindowTextLengthW(g_hwndEdit);
            }
        }

        if (cachedText) { LocalFree(cachedText); cachedText = NULL; }
        if (cachedLineStarts) { LocalFree(cachedLineStarts); cachedLineStarts = NULL; }
        cachedLineStartCount = 1;
        cachedLen = textLen;
        cachedFirstVisible = firstVisible;
        cachedVisLines = visLines;
        cachedTextSeq = g_lineNumTextSeq;
        logicalTotal = g_pagedLineCount;

        /* Auto-size gutter width based on global line count in paged mode. */
        {
            HDC hdc = GetDC(g_hwndLineNum);
            HFONT hOld = (HFONT)SelectObject(hdc, g_hFont);
            SIZE sz;
            TEXTMETRICW tm;
            wchar_t numBuf[16];
            int newWidth;

            wsprintfW(numBuf, L"%d", logicalTotal);
            GetTextExtentPoint32W(hdc, numBuf, lstrlenW(numBuf), &sz);
            if (GetTextMetricsW(hdc, &tm)) lineHeight = tm.tmHeight;

            newWidth = sz.cx + 10;
            if (newWidth < 20) newWidth = 20;

            SelectObject(hdc, hOld);
            ReleaseDC(g_hwndLineNum, hdc);

            if (newWidth != g_lineNumWidth) {
                g_lineNumWidth = newWidth;
                SendMessage(g_hwndMain, WM_SIZE, 0, 0);
                InvalidateRect(g_hwndLineNum, NULL, FALSE);
                InvalidateRect(g_hwndEdit, NULL, FALSE);
                InvalidateColumnIndicator();
            }
        }

        GetClientRect(g_hwndEdit, &rcEdit);
        if (lineHeight > 0) visibleRows = (rcEdit.bottom - rcEdit.top) / lineHeight + 2;
        else visibleRows = 40;
        if (visibleRows < 1) visibleRows = 1;

        displayEnd = firstVisible + visibleRows;
        if (displayEnd > visLines) displayEnd = visLines;

        charIdx = (int)SendMessage(g_hwndEdit, EM_LINEINDEX, firstVisible, 0);
        if (charIdx < 0) charIdx = 0;
        globalIdx = g_pagedPageStart + charIdx;
        logicalLine = 1;
        {
            int lo = 0;
            int hi = g_pagedLineCount;
            while (lo < hi) {
                int mid = lo + ((hi - lo) >> 1);
                if (g_pagedLineStarts[mid] <= globalIdx) lo = mid + 1;
                else hi = mid;
            }
            logicalLine = lo;
            if (logicalLine < 1) logicalLine = 1;
        }

        for (i = firstVisible; i < displayEnd && pos < lineNumLimit; i++) {
            int isLogicalStart = 0;

            charIdx = (int)SendMessage(g_hwndEdit, EM_LINEINDEX, i, 0);
            if (charIdx < 0) charIdx = 0;
            globalIdx = g_pagedPageStart + charIdx;

            {
                int lo = 0;
                int hi = g_pagedLineCount - 1;
                while (lo <= hi) {
                    int mid = lo + ((hi - lo) >> 1);
                    if (g_pagedLineStarts[mid] == globalIdx) {
                        isLogicalStart = 1;
                        break;
                    }
                    if (g_pagedLineStarts[mid] < globalIdx) lo = mid + 1;
                    else hi = mid - 1;
                }
            }

            if (isLogicalStart) {
                pos += wsprintfW(buf + pos, L"%d\r\n", logicalLine);
                logicalLine++;
            } else {
                pos += wsprintfW(buf + pos, L"\r\n");
            }
        }
        if (pos >= g_lineNumBufCap) pos = g_lineNumBufCap - 1;
        buf[pos] = 0;

        if (lstrcmpW(buf, cachedOutput) != 0) {
            lstrcpyW(cachedOutput, buf);
            SetWindowTextW(g_hwndLineNum, buf);
        }
        return;
    }

    /* Refresh cached text and logical line starts when content changes. */
    if (textChanged || textLen != cachedLen || (textLen > 0 && !cachedText && !cachedLineStarts)) {
        int newlineCount = 0;
        int idx = 1;

        if (cachedText) { LocalFree(cachedText); cachedText = NULL; }
        if (cachedLineStarts) { LocalFree(cachedLineStarts); cachedLineStarts = NULL; }
        cachedLineStartCount = 1;
        cachedLen = textLen;

        if (textLen > 0) {
            cachedText = (wchar_t *)LocalAlloc(LMEM_FIXED, (textLen + 1) * sizeof(wchar_t));
            if (cachedText) GetWindowTextW(g_hwndEdit, cachedText, textLen + 1);
        }
        text = cachedText;

        cachedTextSeq = g_lineNumTextSeq;

        if (text) {
            for (i = 0; i < textLen; i++) {
                if (text[i] == '\n') newlineCount++;
            }

            cachedLineStartCount = newlineCount + 1;
            cachedLineStarts = (int *)LocalAlloc(LMEM_FIXED, cachedLineStartCount * sizeof(int));
            if (cachedLineStarts) {
                cachedLineStarts[0] = 0;
                for (i = 0; i < textLen && idx < cachedLineStartCount; i++) {
                    if (text[i] == '\n') cachedLineStarts[idx++] = i + 1;
                }
                cachedLineStartCount = idx;
            }

            /* Keep compact line-start cache and drop duplicate full-text copy. */
            if (cachedLineStarts && cachedText) {
                LocalFree(cachedText);
                cachedText = NULL;
                text = NULL;
            }
        }
    } else {
        text = cachedText;
    }

    cachedFirstVisible = firstVisible;
    cachedVisLines = visLines;

    if (cachedLineStartCount > 0) logicalTotal = cachedLineStartCount;

    /* Auto-size gutter width based on line count */
    {
        HDC hdc = GetDC(g_hwndLineNum);
        HFONT hOld = (HFONT)SelectObject(hdc, g_hFont);
        SIZE sz;
        TEXTMETRICW tm;
        wchar_t numBuf[16];
        int newWidth;

        wsprintfW(numBuf, L"%d", logicalTotal);
        GetTextExtentPoint32W(hdc, numBuf, lstrlenW(numBuf), &sz);
        if (GetTextMetricsW(hdc, &tm)) lineHeight = tm.tmHeight;

        newWidth = sz.cx + 10;
        if (newWidth < 20) newWidth = 20;

        SelectObject(hdc, hOld);
        ReleaseDC(g_hwndLineNum, hdc);

        if (newWidth != g_lineNumWidth) {
            g_lineNumWidth = newWidth;
            SendMessage(g_hwndMain, WM_SIZE, 0, 0);
            /* Let paint happen naturally; avoid forced synchronous redraw here. */
            InvalidateRect(g_hwndLineNum, NULL, FALSE);
            InvalidateRect(g_hwndEdit, NULL, FALSE);
            InvalidateColumnIndicator();
        }
    }

    GetClientRect(g_hwndEdit, &rcEdit);
    if (lineHeight > 0) {
        visibleRows = (rcEdit.bottom - rcEdit.top) / lineHeight + 2;
    } else {
        visibleRows = 40;
    }
    if (visibleRows < 1) visibleRows = 1;

    displayEnd = firstVisible + visibleRows;
    if (displayEnd > visLines) displayEnd = visLines;

    /* Find logical line number at first visible line */
    charIdx = (int)SendMessage(g_hwndEdit, EM_LINEINDEX, firstVisible, 0);
    if (charIdx < 0) charIdx = 0;
    logicalLine = 1;
    if (cachedLineStarts && cachedLineStartCount > 0) {
        int lo = 0;
        int hi = cachedLineStartCount;
        while (lo < hi) {
            int mid = lo + ((hi - lo) >> 1);
            if (cachedLineStarts[mid] <= charIdx) lo = mid + 1;
            else hi = mid;
        }
        logicalLine = lo;
        if (logicalLine < 1) logicalLine = 1;
    } else if (text) {
        for (i = 0; i < charIdx && i < textLen; i++) {
            if (text[i] == '\n') logicalLine++;
        }
    }

    /* Build line number text - blank for wrapped continuations */
    for (i = firstVisible; i < displayEnd && pos < lineNumLimit; i++) {
        int isLogicalStart = 1;

        charIdx = (int)SendMessage(g_hwndEdit, EM_LINEINDEX, i, 0);
        if (charIdx < 0) charIdx = 0;

        if (i != 0) {
            if (cachedLineStarts && cachedLineStartCount > 0) {
                int lo = 0;
                int hi = cachedLineStartCount - 1;
                isLogicalStart = 0;

                while (lo <= hi) {
                    int mid = lo + ((hi - lo) >> 1);
                    if (cachedLineStarts[mid] == charIdx) {
                        isLogicalStart = 1;
                        break;
                    }
                    if (cachedLineStarts[mid] < charIdx) lo = mid + 1;
                    else hi = mid - 1;
                }
            } else if (text) {
                isLogicalStart = (charIdx > 0 && charIdx <= textLen && text[charIdx - 1] == '\n');
            }
        }

        if (isLogicalStart) {
            pos += wsprintfW(buf + pos, L"%d\r\n", logicalLine);
            logicalLine++;
        } else {
            pos += wsprintfW(buf + pos, L"\r\n");
        }
    }
    if (pos >= g_lineNumBufCap) pos = g_lineNumBufCap - 1;
    buf[pos] = 0;

    /* Only update control if output changed */
    if (lstrcmpW(buf, cachedOutput) != 0) {
        lstrcpyW(cachedOutput, buf);
        SetWindowTextW(g_hwndLineNum, buf);
    }
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
    DWORD dwSize;
    wchar_t *pWBuf = NULL;
    wchar_t *pagedOwned = NULL;
    int textLen = 0;
    int busy = 0;

    if (index < 0 || index >= g_recentCount) return;
    if (!PromptSave()) return;

    lstrcpyW(path, g_recentFiles[index]);

    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hwndMain, L"Cannot open file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return;
    }

    if (!GetLoadFileSizeGuarded(hFile, &dwSize, NULL)) {
        CloseHandle(hFile);
        return;
    }
    if (dwSize > BUSY_TEXT_THRESHOLD) {
        SetStatusMessage(L"Loading file...");
        BeginBusyCursor(L"openrecent");
        busy = 1;
    }
    if (!ReadFileToUnicodeScratch(hFile, dwSize, &pWBuf, &textLen, &g_fileNewlineStyle)) {
        CloseHandle(hFile);
        if (busy) {
            EndBusyCursor(L"openrecent");
            ClearStatusMessage();
        }
        MessageBoxW(g_hwndMain, L"Cannot load file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(hFile);

    if (textLen > PAGED_MODE_THRESHOLD_CHARS) {
        pagedOwned = AllocOwnedUnicodeCopy(pWBuf, textLen);
        if (!pagedOwned) {
            if (busy) {
                EndBusyCursor(L"openrecent");
                ClearStatusMessage();
            }
            MessageBoxW(g_hwndMain, L"Out of memory.", g_szAppTitle, MB_OK | MB_ICONERROR);
            return;
        }
        if (!PagedEnableWithText(pagedOwned, textLen)) {
            LocalFree(pagedOwned);
            if (busy) {
                EndBusyCursor(L"openrecent");
                ClearStatusMessage();
            }
            MessageBoxW(g_hwndMain, L"Cannot enable large file mode.", g_szAppTitle, MB_OK | MB_ICONERROR);
            return;
        }
    } else {
        PagedReset();
        SetWindowTextW(g_hwndEdit, pWBuf);
    }

    lstrcpyW(g_szFilePath, path);
    g_bDirty = 0;
    Undo_Clear();
    UpdateTitle();
    RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
    AddRecentFile(path);
    if (busy) {
        EndBusyCursor(L"openrecent");
        ClearStatusMessage();
    }
    RefreshStatusAfterDocumentLoad();
}

/*
 * UpdateStatus - Update status bar with cursor position
 */
static void UpdateStatus(void)
{
    static DWORD s_lastSelStart = (DWORD)-1;
    static int s_lastLines = -1;
    static int s_lastChars = -1;
    static UINT s_lastTotalsSeq = 0;
    static DWORD s_nextTotalsTick = 0;
    DWORD selStart, selEnd;
    DWORD selGlobal;
    int line, col;
    int lineStart;
    int totalLines;
    int totalChars;
    DWORD nowTick;
    wchar_t buf[64];

    if (!g_hwndStatus) return;

    if (g_bForceStatusRefresh) {
        s_lastSelStart = (DWORD)-1;
        s_lastTotalsSeq = 0;
        s_nextTotalsTick = 0;
        g_bForceStatusRefresh = 0;
    }

    /* If there's a status message, show it once then let it persist */
    if (g_szStatusMsg[0]) {
        SendMessageW(g_hwndStatus, SB_SETTEXTW, 0, (LPARAM)g_szStatusMsg);
        return;
    }

    SendMessageW(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    if (g_bPagedMode) selGlobal = (DWORD)(g_pagedPageStart + (int)selStart);
    else selGlobal = selStart;

    /* Update left part if cursor moved */
    if (selGlobal != s_lastSelStart) {
        s_lastSelStart = selGlobal;
        if (g_bPagedMode) {
            PagedIndexToLineCol((int)selGlobal, &line, &col);
            wsprintfW(buf, L"Ln %d, Col %d", line, col);
        } else {
            line = (int)SendMessageW(g_hwndEdit, EM_LINEFROMCHAR, selStart, 0);
            lineStart = (int)SendMessageW(g_hwndEdit, EM_LINEINDEX, line, 0);
            if (lineStart < 0) lineStart = 0;
            col = (int)selStart - lineStart;
            if (col < 0) col = 0;
            wsprintfW(buf, L"Ln %d, Col %d", line + 1, col + 1);
        }
        SendMessageW(g_hwndStatus, SB_SETTEXTW, 0, (LPARAM)buf);
    }

    /* Recompute totals only after text changes, still throttled for responsiveness. */
    if (s_lastTotalsSeq != g_statusTotalsSeq) {
        nowTick = GetTickCount();
        if (s_nextTotalsTick != 0 && (LONG)(nowTick - s_nextTotalsTick) < 0) {
            return;
        }

        if (g_bPagedMode) {
            totalLines = g_pagedLineCount;
            totalChars = g_pagedTextLen;
            if (g_bPagedPageDirty) {
                int curLen = GetWindowTextLengthW(g_hwndEdit);
                totalChars = g_pagedTextLen - g_pagedPageLen + curLen;
                if (totalChars < 0) totalChars = 0;
            }
        } else {
            totalLines = (int)SendMessageW(g_hwndEdit, EM_GETLINECOUNT, 0, 0);
            totalChars = GetWindowTextLengthW(g_hwndEdit);
        }

        s_lastTotalsSeq = g_statusTotalsSeq;
        s_nextTotalsTick = nowTick + STATUS_TOTALS_INTERVAL_MS;

        if (totalLines != s_lastLines || totalChars != s_lastChars) {
            s_lastLines = totalLines;
            s_lastChars = totalChars;
            wsprintfW(buf, L"%d line%s, %d char%s",
                totalLines, totalLines == 1 ? L"" : L"s",
                totalChars, totalChars == 1 ? L"" : L"s");
            SendMessageW(g_hwndStatus, SB_SETTEXTW, 1, (LPARAM)buf);
        }
    }
}

/*
 * SetStatusMessage - Display a temporary message in the status bar
 */
static void SetStatusMessage(const wchar_t *msg)
{
    if (msg && g_hwndStatus) {
        int i;
        for (i = 0; i < 127 && msg[i]; i++) g_szStatusMsg[i] = msg[i];
        g_szStatusMsg[i] = 0;
        SendMessageW(g_hwndStatus, SB_SETTEXTW, 0, (LPARAM)g_szStatusMsg);
        UpdateWindow(g_hwndStatus);
    }
}

/*
 * ClearStatusMessage - Clear temporary message, restore normal status
 */
static void ClearStatusMessage(void)
{
    if (g_szStatusMsg[0]) {
        g_szStatusMsg[0] = 0;
        g_bForceStatusRefresh = 1;
        UpdateStatus();
        if (g_hwndStatus) UpdateWindow(g_hwndStatus);
    }
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

    PagedReset();
    SetWindowTextW(g_hwndEdit, L"");
    g_szFilePath[0] = 0;
    g_fileNewlineStyle = GetDefaultNewlineStyleForNewFile();
    g_bDirty = 0;
    Undo_Clear();
    UpdateTitle();
    RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
    RefreshStatusAfterDocumentLoad();
}

/*
 * DoFileOpen - Open a text file
 */
static int DoFileOpen(void)
{
    wchar_t szFile[MAX_PATH] = L"";
    HANDLE hFile;
    DWORD dwSize;
    wchar_t *pWBuf = NULL;
    wchar_t *pagedOwned = NULL;
    int textLen = 0;
    int busy = 0;

    if (!PromptSave()) return 0;

    if (!FilePicker(g_hwndMain, szFile, MAX_PATH,
            L"Open File", g_szFilter, NULL, NULL, 0)) {
        return 0;
    }

    hFile = CreateFileW(szFile, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hwndMain, L"Cannot open file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    if (!GetLoadFileSizeGuarded(hFile, &dwSize, NULL)) {
        CloseHandle(hFile);
        return 0;
    }
    if (dwSize > BUSY_TEXT_THRESHOLD) {
        SetStatusMessage(L"Loading file...");
        BeginBusyCursor(L"open");
        busy = 1;
    }
    if (!ReadFileToUnicodeScratch(hFile, dwSize, &pWBuf, &textLen, &g_fileNewlineStyle)) {
        CloseHandle(hFile);
        if (busy) {
            EndBusyCursor(L"open");
            ClearStatusMessage();
        }
        MessageBoxW(g_hwndMain, L"Cannot load file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }
    CloseHandle(hFile);

    if (textLen > PAGED_MODE_THRESHOLD_CHARS) {
        pagedOwned = AllocOwnedUnicodeCopy(pWBuf, textLen);
        if (!pagedOwned) {
            if (busy) {
                EndBusyCursor(L"open");
                ClearStatusMessage();
            }
            MessageBoxW(g_hwndMain, L"Out of memory.", g_szAppTitle, MB_OK | MB_ICONERROR);
            return 0;
        }
        if (!PagedEnableWithText(pagedOwned, textLen)) {
            LocalFree(pagedOwned);
            if (busy) {
                EndBusyCursor(L"open");
                ClearStatusMessage();
            }
            MessageBoxW(g_hwndMain, L"Cannot enable large file mode.", g_szAppTitle, MB_OK | MB_ICONERROR);
            return 0;
        }
    } else {
        PagedReset();
        SetWindowTextW(g_hwndEdit, pWBuf);
    }

    lstrcpyW(g_szFilePath, szFile);
    g_bDirty = 0;
    Undo_Clear();
    UpdateTitle();
    RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
    AddRecentFile(szFile);
    if (busy) {
        EndBusyCursor(L"open");
        ClearStatusMessage();
    }
    RefreshStatusAfterDocumentLoad();
    return 1;
}

static int WriteWideTextWithStyle(HANDLE hFile, const wchar_t *text, DWORD len, int newlineStyle)
{
    DWORD i;
    DWORD dwWritten;
    DWORD bytesToWrite;
    int outCount;
    wchar_t ch;
    wchar_t *outBuf;

    if (!hFile || !text) return 0;
    if (len == 0) return 1;

    if (newlineStyle != NEWLINE_STYLE_LF && newlineStyle != NEWLINE_STYLE_CR) {
        bytesToWrite = len * sizeof(wchar_t);
        if (!WriteFile(hFile, text, bytesToWrite, &dwWritten, NULL)) return 0;
        return dwWritten == bytesToWrite;
    }

    if (!EnsureFileIoWideBufferAppend(FILE_IO_READ_CHUNK + 4, 0)) return 0;
    outBuf = g_fileIoWideBuf;
    outCount = 0;

    for (i = 0; i < len; i++) {
        ch = text[i];
        if (newlineStyle == NEWLINE_STYLE_LF) {
            if (ch == L'\r') {
                if (i + 1 < len && text[i + 1] == L'\n') i++;
                ch = L'\n';
            }
        } else {
            if (ch == L'\r') {
                if (i + 1 < len && text[i + 1] == L'\n') i++;
                ch = L'\r';
            } else if (ch == L'\n') {
                ch = L'\r';
            }
        }

        outBuf[outCount++] = ch;
        if (outCount >= FILE_IO_READ_CHUNK) {
            bytesToWrite = (DWORD)outCount * sizeof(wchar_t);
            if (!WriteFile(hFile, outBuf, bytesToWrite, &dwWritten, NULL)) return 0;
            if (dwWritten != bytesToWrite) return 0;
            outCount = 0;
        }
    }

    if (outCount > 0) {
        bytesToWrite = (DWORD)outCount * sizeof(wchar_t);
        if (!WriteFile(hFile, outBuf, bytesToWrite, &dwWritten, NULL)) return 0;
        if (dwWritten != bytesToWrite) return 0;
    }

    return 1;
}

static int GetDefaultNewlineStyleForNewFile(void)
{
    if (g_nNewlineMode == NEWLINE_MODE_FORCE_LF) return NEWLINE_STYLE_LF;
    return NEWLINE_STYLE_CRLF;
}

static int GetSaveNewlineStyle(void)
{
    if (g_nNewlineMode == NEWLINE_MODE_FORCE_LF) return NEWLINE_STYLE_LF;
    if (g_nNewlineMode == NEWLINE_MODE_FORCE_CRLF) return NEWLINE_STYLE_CRLF;
    return g_fileNewlineStyle;
}

/*
 * DoFileSave - Save current file (or Save As if untitled)
 */
static int DoFileSave(void)
{
    HANDLE hFile;
    DWORD dwLen, dwWritten;
    wchar_t *pText = NULL;
    unsigned char bom[2] = {0xFF, 0xFE};
    int writeOk = 0;

    if (!g_szFilePath[0]) {
        return DoFileSaveAs();
    }

    hFile = CreateFileW(g_szFilePath, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hwndMain, L"Cannot save file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    if (g_bPagedMode) {
        if (!PagedCommitPage()) {
            CloseHandle(hFile);
            MessageBoxW(g_hwndMain, L"Cannot commit large-file page.", g_szAppTitle, MB_OK | MB_ICONERROR);
            return 0;
        }
        dwLen = (DWORD)g_pagedTextLen;
        pText = g_pagedText;
    } else {
        dwLen = GetWindowTextLengthW(g_hwndEdit);
        pText = (wchar_t *)LocalAlloc(LMEM_FIXED, (dwLen + 1) * sizeof(wchar_t));
        if (!pText) {
            CloseHandle(hFile);
            MessageBoxW(g_hwndMain, L"Out of memory.", g_szAppTitle, MB_OK | MB_ICONERROR);
            return 0;
        }
        GetWindowTextW(g_hwndEdit, pText, dwLen + 1);
    }

    if (!WriteFile(hFile, bom, 2, &dwWritten, NULL) || dwWritten != 2) {
        CloseHandle(hFile);
        if (!g_bPagedMode) LocalFree(pText);
        MessageBoxW(g_hwndMain, L"Cannot save file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    writeOk = WriteWideTextWithStyle(hFile, pText, dwLen, GetSaveNewlineStyle());
    if (!writeOk) {
        CloseHandle(hFile);
        if (!g_bPagedMode) LocalFree(pText);
        MessageBoxW(g_hwndMain, L"Cannot save file.", g_szAppTitle, MB_OK | MB_ICONERROR);
        return 0;
    }

    CloseHandle(hFile);
    if (!g_bPagedMode) LocalFree(pText);

    g_bDirty = 0;
    UpdateTitle();

    /* Show save confirmation in status bar */
    {
        wchar_t msg[128];
        wsprintfW(msg, L"Saved: %s", g_szFilePath);
        SetStatusMessage(msg);
    }
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
            L"Save File", g_szFilter, L"txt", NULL, 1)) {
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
 * EditEndsWithNewline - Returns 1 when edit text already ends at a newline
 * boundary (no extra separator needed).
 */
static int EditEndsWithNewline(HWND hwndEdit)
{
    int len;
    int lineCount;
    int lastLine;
    int lastLineStart;

    if (!hwndEdit) return 0;
    len = GetWindowTextLengthW(hwndEdit);
    if (len <= 0) return 0;

    lineCount = (int)SendMessageW(hwndEdit, EM_GETLINECOUNT, 0, 0);
    if (lineCount <= 0) return 0;

    lastLine = lineCount - 1;
    lastLineStart = (int)SendMessageW(hwndEdit, EM_LINEINDEX, lastLine, 0);
    if (lastLineStart < 0) return 0;

    return lastLineStart >= len;
}

/*
 * QuickNoteAppendNewline - Append entry separator in current quick-note window
 * without reloading/changing the active file.
 */
static void QuickNoteAppendNewline(void)
{
    int len;

    if (!g_hwndEdit) return;
    len = GetWindowTextLengthW(g_hwndEdit);
    SendMessageW(g_hwndEdit, EM_SETSEL, len, len);
    if (len > 0 && !EditEndsWithNewline(g_hwndEdit)) {
        g_bForceImmediateLineNum = 1;
        SendMessageW(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)L"\r\n");
    }
    SetFocus(g_hwndEdit);
    UpdateStatus();
}

/*
 * DoQuickNote - Open/create today's dated note file
 */
static void DoQuickNote(void)
{
    SYSTEMTIME st;
    wchar_t notesDir[MAX_PATH];
    wchar_t path[MAX_PATH];
    HANDLE hFile;
    DWORD dwSize, attr;
    wchar_t *pWBuf = NULL;
    int len;
    int useStorage = 0;
    int needsInit = 0;

    if (!PromptSave()) return;
    PagedReset();

    /* Check for storage card if preferred (and not skipped this session) */
    if (g_bQuickNoteStorage && !s_bSkipStorageCard) {
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(L"\\Storage Card*", &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            wchar_t testPath[MAX_PATH];
            wchar_t firstCard[MAX_PATH];
            firstCard[0] = 0;

            /* Scan cards, prefer one with existing Notes folder */
            do {
                if (!firstCard[0]) lstrcpyW(firstCard, fd.cFileName);
                wsprintfW(testPath, L"\\%s\\Notes", fd.cFileName);
                if (GetFileAttributesW(testPath) != 0xFFFFFFFF) {
                    lstrcpyW(notesDir, testPath);
                    useStorage = 1;
                    break;
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);

            /* No Notes folder found - would need to create on first card */
            if (!useStorage && firstCard[0]) {
                wsprintfW(notesDir, L"\\%s\\Notes", firstCard);
                needsInit = 1;
                useStorage = 1;
            }
        }
    }

    /* Prompt before initializing on storage card (unless auto-init enabled) */
    if (needsInit && !g_bQuickNoteAutoInit) {
        int result = MessageBoxW(g_hwndMain,
            L"Create Notes folder on storage card?\n\nYes = Use card\nNo = Use device memory (this session)",
            L"Quick Note", MB_YESNO | MB_ICONQUESTION);
        if (result != IDYES) {
            s_bSkipStorageCard = 1;
            useStorage = 0;
        }
    }

    /* Fall back to My Documents\Notes */
    if (!useStorage) {
        lstrcpyW(notesDir, L"\\My Documents\\Notes");
    }

    /* Ensure Notes directory exists */
    attr = GetFileAttributesW(notesDir);
    if (attr == 0xFFFFFFFF) {
        CreateDirectoryW(notesDir, NULL);
    }

    GetLocalTime(&st);
    wsprintfW(path, L"%s\\%04d-%02d-%02d.txt", notesDir,
        st.wYear, st.wMonth, st.wDay);

    /* Try to open existing file */
    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile != INVALID_HANDLE_VALUE) {
        /* Load existing content */
        if (!GetLoadFileSizeGuarded(hFile, &dwSize, L"Quick Note")) {
            CloseHandle(hFile);
            return;
        }
        if (ReadFileToUnicodeScratch(hFile, dwSize, &pWBuf, &len, &g_fileNewlineStyle)) {
            SetWindowTextW(g_hwndEdit, pWBuf);
        } else {
            MessageBoxW(g_hwndMain, L"Cannot load Quick Note file.", g_szAppTitle, MB_OK | MB_ICONERROR);
            CloseHandle(hFile);
            return;
        }
        CloseHandle(hFile);
    } else {
        /* New file - start empty */
        SetWindowTextW(g_hwndEdit, L"");
        g_fileNewlineStyle = GetDefaultNewlineStyleForNewFile();
    }

    lstrcpyW(g_szFilePath, path);
    g_bDirty = 0;
    UpdateTitle();
    QuickNoteAppendNewline();
    RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
}

/*
 * MainWndProc - Main window message handler
 */
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case PWM_IS_QUICKNOTE_WINDOW:
        return g_bQuickNoteWindow ? 1 : 0;

    case PWM_QUICKNOTE_ACTIVATE:
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        if (wParam) QuickNoteAppendNewline();
        if (g_hwndEdit) SetFocus(g_hwndEdit);
        return 0;

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
            CheckMenuItem(g_hViewMenu, IDM_VIEW_SCROLLBARS,
                g_bShowScrollbars ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(g_hViewMenu, IDM_VIEW_INVERSE,
                g_bInverseColors ? MF_CHECKED : MF_UNCHECKED);
            if (g_hThemeMenu) {
                CheckMenuItem(g_hThemeMenu, IDM_VIEW_THEME_DEFAULT, MF_UNCHECKED);
                CheckMenuItem(g_hThemeMenu, IDM_VIEW_THEME_DEFAULT + g_nTheme, MF_CHECKED);
            }
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

            /* Create Edit control - style depends on word wrap and scrollbar settings */
            editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL;
            if (g_bShowScrollbars) {
                editStyle |= WS_VSCROLL;
                if (!g_bWordWrap) editStyle |= WS_HSCROLL;
            }
            if (!g_bWordWrap)
                editStyle |= ES_AUTOHSCROLL;

            g_hwndEdit = CreateWindowW(
                L"EDIT", NULL, editStyle,
                0, 0, 0, 0,
                hwnd, (HMENU)ID_EDIT, g_hInst, NULL);
            SendMessageW(g_hwndEdit, EM_LIMITTEXT, (WPARAM)EDIT_TEXT_LIMIT, 0);
            SendMessage(g_hwndEdit, EM_SETMARGINS, EC_LEFTMARGIN, MAKELONG(2, 0));

            SendMessage(g_hwndEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            /* Subclass edit control for cursor tracking */
            g_pfnEditProc = (WNDPROC)SetWindowLong(g_hwndEdit, GWL_WNDPROC,
                (LONG)EditSubclassProc);

            /* Dedicated vertical scrollbar for paged mode (hidden by default). */
            g_hwndPagedVScroll = CreateWindowW(
                L"SCROLLBAR", NULL,
                WS_CHILD | SBS_VERT,
                0, 0, 0, 0,
                hwnd, (HMENU)1004, g_hInst, NULL);
            if (g_hwndPagedVScroll) ShowWindow(g_hwndPagedVScroll, SW_HIDE);

            /* Dead-corner filler for paged mode when both scrollbars are visible. */
            g_hwndPagedCorner = CreateWindowW(
                L"STATIC", NULL,
                WS_CHILD | WS_BORDER,
                0, 0, 0, 0,
                hwnd, (HMENU)1005, g_hInst, NULL);
            if (g_hwndPagedCorner) ShowWindow(g_hwndPagedCorner, SW_HIDE);

            /* 1px seam mask to hide the internal edit/divider artifact in paged mode. */
            g_hwndPagedSeam = CreateWindowW(
                L"STATIC", NULL,
                WS_CHILD,
                0, 0, 0, 0,
                hwnd, (HMENU)1006, g_hInst, NULL);
            if (g_hwndPagedSeam) ShowWindow(g_hwndPagedSeam, SW_HIDE);

            /* Initialize undo system */
            Undo_Init(g_hwndEdit);

            SetFocus(g_hwndEdit);
            UpdateTitle();
            UpdateStatus();
            SetStatusMessage(L"Palmweaver " PALMWEAVER_VERSION);
            RequestLineNumberRefresh(LINENUM_DIRTY_LAYOUT, 1);
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

    case WM_VSCROLL:
        if ((HWND)lParam == g_hwndPagedVScroll && g_bPagedMode) {
            if (PagedProcessScrollCode((UINT)LOWORD(wParam))) return 0;
        }
        break;

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
            if (!PerformTrackedUndo()) {
                /* Fall back to built-in undo for user typing */
                SendMessageW(g_hwndEdit, EM_UNDO, 0, 0);
            }
            RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
            return 0;

        case IDM_EDIT_CUT:
            {
                DWORD selStart, selEnd;
                SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
                RecordUndoDeleteRange(g_hwndEdit, selStart, selEnd);
            }
            SendMessageW(g_hwndEdit, WM_CUT, 0, 0);
            SetFocus(g_hwndEdit);
            return 0;

        case IDM_EDIT_COPY:
            SendMessageW(g_hwndEdit, WM_COPY, 0, 0);
            SetFocus(g_hwndEdit);
            return 0;

        case IDM_EDIT_PASTE:
            {
                DWORD selStart, selEnd;
                SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
                /* Get clipboard text to record insert */
                if (OpenClipboard(hwnd)) {
                    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                    if (hData) {
                        wchar_t *pText = (wchar_t *)hData;
                        if (pText && pText[0]) {
                            if (selEnd > selStart) {
                                Undo_BeginGroup();
                                RecordUndoDeleteRange(g_hwndEdit, selStart, selEnd);
                                Undo_RecordInsert(selStart, pText, -1);
                                Undo_EndGroup();
                            } else {
                                Undo_RecordInsert(selStart, pText, -1);
                            }
                        }
                    }
                    CloseClipboard();
                }
            }
            SendMessageW(g_hwndEdit, WM_PASTE, 0, 0);
            SetFocus(g_hwndEdit);
            return 0;

        case IDM_EDIT_SELECTALL:
            SetFocus(g_hwndEdit);
            SendMessageW(g_hwndEdit, EM_SETSEL, 0, -1);
            return 0;

        case IDM_EDIT_CUTLINE:
            {
                int line, lineStart, lineEnd, len;
                DWORD selStart, selEnd;
                wchar_t *cutText = NULL;
                int cutLen = 0;
                SendMessage(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
                line = (int)SendMessage(g_hwndEdit, EM_LINEFROMCHAR, selStart, 0);
                lineStart = (int)SendMessage(g_hwndEdit, EM_LINEINDEX, line, 0);
                lineEnd = (int)SendMessage(g_hwndEdit, EM_LINEINDEX, line + 1, 0);
                len = GetWindowTextLengthW(g_hwndEdit);
                if (lineEnd < 0) lineEnd = len;  /* Last line */
                if (lineEnd > lineStart) {
                    if (CaptureEditRangeText(g_hwndEdit, lineStart, lineEnd, &cutText, &cutLen)) {
                        Undo_RecordDelete(lineStart, cutText, cutLen);
                        /* Copy to clipboard */
                        if (OpenClipboard(hwnd)) {
                            HLOCAL hMem = LocalAlloc(LMEM_MOVEABLE, (cutLen + 1) * sizeof(wchar_t));
                            if (hMem) {
                                wchar_t *p = (wchar_t *)LocalLock(hMem);
                                int i;
                                for (i = 0; i < cutLen; i++) p[i] = cutText[i];
                                p[cutLen] = 0;
                                LocalUnlock(hMem);
                                EmptyClipboard();
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            }
                            CloseClipboard();
                        }
                        LocalFree(cutText);
                    }
                    SendMessage(g_hwndEdit, EM_SETSEL, lineStart, lineEnd);
                    SendMessage(g_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
                    g_bDirty = 1;
                    UpdateTitle();
                }
                SetFocus(g_hwndEdit);
            }
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

        case IDM_EDIT_INSDATE:
            DoInsertDateTime(0);
            return 0;

        case IDM_EDIT_INSTIME:
            DoInsertDateTime(1);
            return 0;

        case IDM_EDIT_INSDATETIME:
            DoInsertDateTime(2);
            return 0;

        case IDM_EDIT_INSRULE:
            DoInsertRule();
            return 0;

        case IDM_EDIT_REFLOW:
            DoReflow();
            return 0;

        case IDM_VIEW_WORDWRAP:
            {
                /* Must recreate edit control to change word wrap on CE */
                int textLen = 0;
                wchar_t *text = NULL;
                DWORD editStyle;
                int selStart, selEnd;
                int wasPaged = g_bPagedMode;
                int pagedCaretGlobal = 0;
                int oldPagedStart = 0;
                int savedFirstVisible;
                int savedTopChar;
                int savedTopGlobal;
                int targetTopLine = -1;
                int curFirst;
                int restoredTop = 0;

                /* Save text and selection */
                SendMessageW(g_hwndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
                savedFirstVisible = (int)SendMessageW(g_hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
                savedTopChar = (int)SendMessageW(g_hwndEdit, EM_LINEINDEX, savedFirstVisible, 0);
                if (savedTopChar < 0) savedTopChar = 0;
                savedTopGlobal = savedTopChar;
                if (wasPaged) {
                    oldPagedStart = g_pagedPageStart;
                    pagedCaretGlobal = g_pagedPageStart + selStart;
                    savedTopGlobal = g_pagedPageStart + savedTopChar;
                    if (!PagedCommitPage()) {
                        MessageBoxW(g_hwndMain, L"Cannot commit large-file page.", g_szAppTitle, MB_OK | MB_ICONERROR);
                        return 0;
                    }
                } else {
                    textLen = GetWindowTextLengthW(g_hwndEdit);
                    if (textLen > 0) {
                        text = (wchar_t *)LocalAlloc(LMEM_FIXED, (textLen + 1) * sizeof(wchar_t));
                        if (!text) {
                            MessageBoxW(g_hwndMain, L"Out of memory.", g_szAppTitle, MB_OK | MB_ICONERROR);
                            return 0;
                        }
                        GetWindowTextW(g_hwndEdit, text, textLen + 1);
                    }
                }

                g_bWordWrap = !g_bWordWrap;
                CheckMenuItem(g_hViewMenu, IDM_VIEW_WORDWRAP,
                    g_bWordWrap ? MF_CHECKED : MF_UNCHECKED);

                /* Destroy old edit */
                DestroyWindow(g_hwndEdit);

                /* Recreate with new style */
                editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL;
                if (g_bShowScrollbars) {
                    editStyle |= WS_VSCROLL;
                    if (!g_bWordWrap) editStyle |= WS_HSCROLL;
                }
                if (!g_bWordWrap) editStyle |= ES_AUTOHSCROLL;

                g_hwndEdit = CreateWindowW(L"EDIT", NULL, editStyle,
                    0, 0, 0, 0, hwnd, (HMENU)ID_EDIT, g_hInst, NULL);
                SendMessageW(g_hwndEdit, EM_LIMITTEXT, (WPARAM)EDIT_TEXT_LIMIT, 0);
                SendMessage(g_hwndEdit, EM_SETMARGINS, EC_LEFTMARGIN, MAKELONG(2, 0));
                SendMessage(g_hwndEdit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                g_pfnEditProc = (WNDPROC)SetWindowLong(g_hwndEdit, GWL_WNDPROC,
                    (LONG)EditSubclassProc);

                /* Re-init undo with new edit control (preserves history) */
                Undo_Init(g_hwndEdit);

                /* Size recreated controls before restoring content/selection. */
                SendMessage(hwnd, WM_SIZE, 0, 0);

                /* Restore text and selection */
                if (wasPaged) {
                    int loadOk;
                    g_bPreserveUndoOnPagedLoad = 1;
                    loadOk = PagedLoadWindowAt(pagedCaretGlobal);
                    g_bPreserveUndoOnPagedLoad = 0;
                    if (loadOk) {
                        int undoDelta = oldPagedStart - g_pagedPageStart;
                        if (undoDelta) Undo_ShiftPositions(undoDelta);
                    }
                    if (!loadOk) {
                        SendMessageW(g_hwndEdit, EM_SETSEL, selStart, selEnd);
                    }
                    {
                        int targetTopLocal = savedTopGlobal - g_pagedPageStart;
                        if (targetTopLocal < 0) targetTopLocal = 0;
                        if (targetTopLocal > g_pagedPageLen) targetTopLocal = g_pagedPageLen;
                        targetTopLine = (int)SendMessageW(g_hwndEdit, EM_LINEFROMCHAR, targetTopLocal, 0);
                    }
                } else {
                    if (text) {
                        SetWindowTextW(g_hwndEdit, text);
                        LocalFree(text);
                    }
                    SendMessageW(g_hwndEdit, EM_SETSEL, selStart, selEnd);
                    targetTopLine = (int)SendMessageW(g_hwndEdit, EM_LINEFROMCHAR, savedTopChar, 0);
                }

                if (!g_bWordWrap) {
                    /* Reset horizontal scroll baseline after recreating no-wrap edit control. */
                    SendMessageW(g_hwndEdit, EM_LINESCROLL, (WPARAM)-32767, 0);
                }
                SetFocus(g_hwndEdit);
                if (targetTopLine >= 0) {
                    curFirst = (int)SendMessageW(g_hwndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
                    if (curFirst != targetTopLine) {
                        SendMessageW(g_hwndEdit, EM_LINESCROLL, 0, targetTopLine - curFirst);
                    }
                    restoredTop = 1;
                }
                if (!restoredTop) SendMessageW(g_hwndEdit, EM_SCROLLCARET, 0, 0);
                RequestLineNumberRefresh(LINENUM_DIRTY_LAYOUT | LINENUM_DIRTY_SCROLL, 1);
            }
            return 0;

        case IDM_VIEW_LINENUMS:
            g_bShowLineNums = !g_bShowLineNums;
            CheckMenuItem(g_hViewMenu, IDM_VIEW_LINENUMS,
                g_bShowLineNums ? MF_CHECKED : MF_UNCHECKED);
            ShowWindow(g_hwndLineNum, g_bShowLineNums ? SW_SHOW : SW_HIDE);
            SendMessage(hwnd, WM_SIZE, 0, 0);
            if (g_bShowLineNums) RequestLineNumberRefresh(LINENUM_DIRTY_LAYOUT, 0);
            SetFocus(g_hwndEdit);
            return 0;

        case IDM_VIEW_STATUSBAR:
            if (g_bFullScreen) {
                /* In fullscreen: toggle temporary visibility without changing preference */
                g_bFSStatusBar = !g_bFSStatusBar;
                ShowWindow(g_hwndStatus, g_bFSStatusBar ? SW_SHOW : SW_HIDE);
            } else {
                /* Normal mode: toggle the saved preference */
                g_bShowStatusBar = !g_bShowStatusBar;
                ShowWindow(g_hwndStatus, g_bShowStatusBar ? SW_SHOW : SW_HIDE);
                CheckMenuItem(g_hViewMenu, IDM_VIEW_STATUSBAR,
                    g_bShowStatusBar ? MF_CHECKED : MF_UNCHECKED);
            }
            SendMessage(hwnd, WM_SIZE, 0, 0);
            return 0;

        case IDM_VIEW_SCROLLBARS:
            {
                LONG style = GetWindowLong(g_hwndEdit, GWL_STYLE);
                LONG newStyle = style;
                g_bShowScrollbars = !g_bShowScrollbars;
                if (g_bShowScrollbars) {
                    if (!g_bPagedMode) newStyle |= WS_VSCROLL;
                    if (!g_bWordWrap) newStyle |= WS_HSCROLL;
                } else {
                    newStyle &= ~(WS_VSCROLL | WS_HSCROLL);
                }
                if (g_bPagedMode) newStyle &= ~WS_VSCROLL;
                if (newStyle != style) {
                    SetWindowLong(g_hwndEdit, GWL_STYLE, newStyle);
                    SetWindowPos(g_hwndEdit, NULL, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                }
                if (g_bPagedMode) SetEditVerticalScrollbarVisible(0);
                CheckMenuItem(g_hViewMenu, IDM_VIEW_SCROLLBARS,
                    g_bShowScrollbars ? MF_CHECKED : MF_UNCHECKED);
                SendMessage(hwnd, WM_SIZE, 0, 0);
            }
            return 0;

        case IDM_VIEW_THEME_DEFAULT:
        case IDM_VIEW_THEME_GREEN:
        case IDM_VIEW_THEME_AMBER:
        case IDM_VIEW_THEME_BLUE:
            if (g_hThemeMenu) {
                CheckMenuItem(g_hThemeMenu, IDM_VIEW_THEME_DEFAULT + g_nTheme, MF_UNCHECKED);
                g_nTheme = LOWORD(wParam) - IDM_VIEW_THEME_DEFAULT;
                CheckMenuItem(g_hThemeMenu, IDM_VIEW_THEME_DEFAULT + g_nTheme, MF_CHECKED);
                UpdateTheme();
            }
            return 0;

        case IDM_VIEW_INVERSE:
            g_bInverseColors = !g_bInverseColors;
            CheckMenuItem(g_hViewMenu, IDM_VIEW_INVERSE,
                g_bInverseColors ? MF_CHECKED : MF_UNCHECKED);
            UpdateTheme();
            return 0;

        case IDM_VIEW_FULLSCREEN:
            g_bFullScreen = !g_bFullScreen;
            if (g_bFullScreen) {
                g_bFSStatusBar = 0;  /* Reset temporary status bar when entering fullscreen */
                ShowWindow(g_hwndStatus, SW_HIDE);
            } else {
                /* Restore status bar based on saved preference when exiting */
                ShowWindow(g_hwndStatus, g_bShowStatusBar ? SW_SHOW : SW_HIDE);
            }
            ShowWindow(g_hwndCB, g_bFullScreen ? SW_HIDE : SW_SHOW);
            CheckMenuItem(g_hViewMenu, IDM_VIEW_FULLSCREEN,
                g_bFullScreen ? MF_CHECKED : MF_UNCHECKED);
            if (g_bHideTaskbar) {
                static RECT s_rcRestore;
                HWND hwndTaskbar = FindWindowW(L"HHTaskBar", NULL);
                if (g_bFullScreen) {
                    int cx = GetSystemMetrics(SM_CXSCREEN);
                    int cy = GetSystemMetrics(SM_CYSCREEN);
                    GetWindowRect(hwnd, &s_rcRestore);
                    if (hwndTaskbar) ShowWindow(hwndTaskbar, SW_HIDE);
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, cx, cy, SWP_SHOWWINDOW);
                } else {
                    if (hwndTaskbar) ShowWindow(hwndTaskbar, SW_SHOW);
                    SetWindowPos(hwnd, HWND_NOTOPMOST,
                        s_rcRestore.left, s_rcRestore.top,
                        s_rcRestore.right - s_rcRestore.left,
                        s_rcRestore.bottom - s_rcRestore.top, SWP_SHOWWINDOW);
                }
            }
            SendMessage(hwnd, WM_SIZE, 0, 0);
            return 0;

        case IDM_HELP_ABOUT:
            ShowAboutDialog(hwnd);
            return 0;

        case ID_EDIT:
            /* Edit control notification */
            if (HIWORD(wParam) == EN_CHANGE) {
                int lineCountNow = (int)SendMessageW(g_hwndEdit, EM_GETLINECOUNT, 0, 0);
                int lineStructureChanged = 0;
                if (lineCountNow > 0) {
                    if (g_lastEditLineCount >= 0 && lineCountNow != g_lastEditLineCount) {
                        lineStructureChanged = 1;
                    }
                    g_lastEditLineCount = lineCountNow;
                }
                if (g_bPagedLoading) {
                    RequestLineNumberRefresh(LINENUM_DIRTY_LAYOUT, 1);
                    return 0;
                }
                if (g_bPagedMode) g_bPagedPageDirty = 1;
                MarkStatusTotalsDirty();
                if (!g_bDirty) {
                    g_bDirty = 1;
                    UpdateTitle();
                }
                UpdateStatus();
                if (g_bForceImmediateLineNum || lineStructureChanged) {
                    g_bForceImmediateLineNum = 0;
                    RequestLineNumberRefresh(LINENUM_DIRTY_TEXT | LINENUM_DIRTY_LAYOUT, 1);
                } else {
                    RequestLineNumberRefresh(LINENUM_DIRTY_TEXT, 0);
                }
                if (g_bShowColumnIndicator) InvalidateColumnIndicator();
            }
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == LINENUM_TIMER_ID) {
            KillTimer(hwnd, LINENUM_TIMER_ID);
            g_lineNumTimerActive = 0;
            if (g_lineNumDirtyFlags) {
                g_lineNumDirtyFlags = 0;
                UpdateLineNumbers();
            }
            return 0;
        }
        break;

    case WM_SETCURSOR:
    {
        if (!g_bMousePresent) SetCursor(NULL);
        else ForceIdleCursor();
        return 1;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        if ((HWND)lParam == g_hwndLineNum || (HWND)lParam == g_hwndEdit) {
            COLORREF fg = g_bInverseColors ? g_themes[g_nTheme][1] : g_themes[g_nTheme][0];
            COLORREF bg = g_bInverseColors ? g_themes[g_nTheme][0] : g_themes[g_nTheme][1];
            SetTextColor((HDC)wParam, fg);
            SetBkColor((HDC)wParam, bg);
            return (LRESULT)g_hBrushBg;
        }
        if ((HWND)lParam == g_hwndPagedSeam) {
            COLORREF bg = g_bInverseColors ? g_themes[g_nTheme][0] : g_themes[g_nTheme][1];
            SetBkColor((HDC)wParam, bg);
            return (LRESULT)g_hBrushBg;
        }
        if ((HWND)lParam == g_hwndPagedCorner) {
            COLORREF bg = GetSysColor(COLOR_BTNFACE);
            SetBkColor((HDC)wParam, bg);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;

    case WM_DESTROY:
        g_bMainDestroying = 1;
        CloseReplaceTypingGroup();
        PagedReset();
        g_bVScrollThumbTrackActive = 0;
        g_bPostJumpRepaintPending = 0;
        while (g_busyDepth > 0) EndBusyCursor(L"destroy");
        if (g_lineNumTimerActive) {
            KillTimer(hwnd, LINENUM_TIMER_ID);
            g_lineNumTimerActive = 0;
        }
        /* Restore taskbar if hidden */
        if (g_bFullScreen && g_bHideTaskbar) {
            HWND hwndTaskbar = FindWindowW(L"HHTaskBar", NULL);
            if (hwndTaskbar) ShowWindow(hwndTaskbar, SW_SHOW);
        }
        RestoreSelectionColors();
        SaveSettings();
        if (g_fileIoByteBuf) {
            LocalFree(g_fileIoByteBuf);
            g_fileIoByteBuf = NULL;
            g_fileIoByteCap = 0;
        }
        if (g_fileIoWideBuf) {
            LocalFree(g_fileIoWideBuf);
            g_fileIoWideBuf = NULL;
            g_fileIoWideCap = 0;
        }
        if (g_hBrushBg) DeleteObject(g_hBrushBg);
        if (g_hBrushColInd) DeleteObject(g_hBrushColInd);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_lineNumRenderBuf) {
            LocalFree(g_lineNumRenderBuf);
            g_lineNumRenderBuf = NULL;
        }
        if (g_lineNumCachedOutput) {
            LocalFree(g_lineNumCachedOutput);
            g_lineNumCachedOutput = NULL;
        }
        g_lineNumBufCap = 0;
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
