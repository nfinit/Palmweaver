/*
 * undo.c - Portable undo/redo system for Edit controls
 *
 * Designed for Windows CE 2.0+ (C89, Unicode, no CRT)
 * Can be reused across projects (SQLite/CE, Palmweaver, etc.)
 */

#include "undo.h"

/* Configuration */
#define UNDO_MAX_ENTRIES 64      /* Max undo stack depth */
#define UNDO_MAX_TEXT    32768   /* Max text per entry (32KB) */

/* Operation types */
#define UNDO_INSERT 0
#define UNDO_DELETE 1

/* Undo entry */
typedef struct {
    int type;           /* UNDO_INSERT or UNDO_DELETE */
    int pos;            /* Character position */
    int len;            /* Text length */
    wchar_t *text;      /* Allocated text buffer */
} UndoEntry;

/* Module state */
static HWND s_hwndEdit = NULL;
static UndoEntry s_undoStack[UNDO_MAX_ENTRIES];
static int s_undoCount = 0;      /* Number of entries in undo stack */
static int s_redoCount = 0;      /* Number of entries available for redo */

/*
 * Free a single entry's text buffer
 */
static void FreeEntry(UndoEntry *e)
{
    if (e->text) {
        LocalFree(e->text);
        e->text = NULL;
    }
    e->len = 0;
}

/*
 * Undo_Init - Initialize with target edit control
 */
void Undo_Init(HWND hwndEdit)
{
    int i;
    s_hwndEdit = hwndEdit;
    s_undoCount = 0;
    s_redoCount = 0;
    for (i = 0; i < UNDO_MAX_ENTRIES; i++) {
        s_undoStack[i].text = NULL;
        s_undoStack[i].len = 0;
    }
}

/*
 * Undo_Cleanup - Free all resources
 */
void Undo_Cleanup(void)
{
    int i;
    for (i = 0; i < UNDO_MAX_ENTRIES; i++) {
        FreeEntry(&s_undoStack[i]);
    }
    s_undoCount = 0;
    s_redoCount = 0;
    s_hwndEdit = NULL;
}

/*
 * Undo_Clear - Clear history (call on File New/Open)
 */
void Undo_Clear(void)
{
    int i;
    for (i = 0; i < UNDO_MAX_ENTRIES; i++) {
        FreeEntry(&s_undoStack[i]);
    }
    s_undoCount = 0;
    s_redoCount = 0;
}

/*
 * Undo_Record - Record an operation
 * type: 0 = insert, 1 = delete
 */
void Undo_Record(int type, int pos, const wchar_t *text, int len)
{
    UndoEntry *e;
    int i;

    if (!text) return;
    if (len < 0) len = lstrlenW(text);
    if (len == 0) return;
    if (len > UNDO_MAX_TEXT) len = UNDO_MAX_TEXT;

    /* New edit clears redo stack */
    for (i = s_undoCount; i < s_undoCount + s_redoCount && i < UNDO_MAX_ENTRIES; i++) {
        FreeEntry(&s_undoStack[i]);
    }
    s_redoCount = 0;

    /* If stack is full, shift everything down (discard oldest) */
    if (s_undoCount >= UNDO_MAX_ENTRIES) {
        FreeEntry(&s_undoStack[0]);
        for (i = 0; i < UNDO_MAX_ENTRIES - 1; i++) {
            s_undoStack[i] = s_undoStack[i + 1];
        }
        s_undoCount = UNDO_MAX_ENTRIES - 1;
        s_undoStack[s_undoCount].text = NULL;
    }

    /* Add new entry */
    e = &s_undoStack[s_undoCount];
    FreeEntry(e);  /* Free any existing data at this slot */
    e->type = type;
    e->pos = pos;
    e->len = len;
    e->text = (wchar_t *)LocalAlloc(LMEM_FIXED, (len + 1) * sizeof(wchar_t));
    if (e->text) {
        int j;
        for (j = 0; j < len; j++) e->text[j] = text[j];
        e->text[len] = 0;
        s_undoCount++;
    }
}

/*
 * DeleteRange - Helper to delete text range (CE workaround)
 */
static void DeleteRange(int start, int len)
{
    int totalLen = GetWindowTextLengthW(s_hwndEdit);
    wchar_t *buf, *p;
    int i;

    if (len <= 0 || start < 0 || start + len > totalLen) return;

    buf = (wchar_t *)LocalAlloc(LMEM_FIXED, (totalLen + 1) * sizeof(wchar_t));
    if (!buf) return;

    GetWindowTextW(s_hwndEdit, buf, totalLen + 1);

    /* Shift text after deletion point */
    p = buf + start;
    for (i = start + len; i <= totalLen; i++) {
        *p++ = buf[i];
    }

    SetWindowTextW(s_hwndEdit, buf);
    SendMessageW(s_hwndEdit, EM_SETSEL, start, start);
    LocalFree(buf);
}

/*
 * Undo_Perform - Undo last operation
 * Returns 1 if undo was performed
 */
int Undo_Perform(void)
{
    UndoEntry *e;

    if (s_undoCount == 0 || !s_hwndEdit) return 0;

    s_undoCount--;
    e = &s_undoStack[s_undoCount];

    if (e->type == UNDO_INSERT) {
        /* Was an insert - delete the text to undo */
        DeleteRange(e->pos, e->len);
    } else {
        /* Was a delete - insert the text back to undo */
        SendMessageW(s_hwndEdit, EM_SETSEL, e->pos, e->pos);
        SendMessageW(s_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)e->text);
    }

    /* Move to redo stack (entry stays in place, just adjust counts) */
    s_redoCount++;

    return 1;
}

/*
 * Undo_Redo - Redo last undone operation
 * Returns 1 if redo was performed
 */
int Undo_Redo(void)
{
    UndoEntry *e;

    if (s_redoCount == 0 || !s_hwndEdit) return 0;

    e = &s_undoStack[s_undoCount];

    if (e->type == UNDO_INSERT) {
        /* Was an insert - re-insert the text */
        SendMessageW(s_hwndEdit, EM_SETSEL, e->pos, e->pos);
        SendMessageW(s_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)e->text);
    } else {
        /* Was a delete - re-delete the text */
        DeleteRange(e->pos, e->len);
    }

    s_undoCount++;
    s_redoCount--;

    return 1;
}

/*
 * Undo_CanUndo / Undo_CanRedo - Query state
 */
int Undo_CanUndo(void)
{
    return s_undoCount > 0;
}

int Undo_CanRedo(void)
{
    return s_redoCount > 0;
}
