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
    int cap;            /* Allocated wchar capacity (including null) */
    int group;          /* 0 = standalone, non-zero = grouped with adjacent ops */
    wchar_t *text;      /* Allocated text buffer */
} UndoEntry;

/* Module state */
static HWND s_hwndEdit = NULL;
static UndoEntry s_undoStack[UNDO_MAX_ENTRIES];
static int s_undoCount = 0;      /* Number of entries in undo stack */
static int s_redoCount = 0;      /* Number of entries available for redo */
static int s_groupDepth = 0;
static int s_groupId = 0;
static int s_nextGroupId = 1;

/* Forward declaration */
static void DeleteRange(int start, int len);
static int EnsureEntryCapacity(UndoEntry *e, int requiredChars);

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
    e->cap = 0;
    e->group = 0;
}

static int EnsureEntryCapacity(UndoEntry *e, int requiredChars)
{
    int newCap;
    wchar_t *newText;
    int i;

    if (!e) return 0;
    if (requiredChars < 1) requiredChars = 1;
    if (requiredChars > UNDO_MAX_TEXT + 1) requiredChars = UNDO_MAX_TEXT + 1;

    if (e->text && e->cap >= requiredChars) return 1;

    newCap = e->cap;
    if (newCap < 16) newCap = 16;
    while (newCap < requiredChars) {
        if (newCap > (UNDO_MAX_TEXT + 1) / 2) {
            newCap = requiredChars;
            break;
        }
        newCap *= 2;
    }

    if (newCap > UNDO_MAX_TEXT + 1) newCap = UNDO_MAX_TEXT + 1;
    if (newCap < requiredChars) return 0;

    newText = (wchar_t *)LocalAlloc(LMEM_FIXED, newCap * sizeof(wchar_t));
    if (!newText) {
        newCap = requiredChars;
        newText = (wchar_t *)LocalAlloc(LMEM_FIXED, newCap * sizeof(wchar_t));
        if (!newText) return 0;
    }

    if (e->text && e->len > 0) {
        int copyLen = e->len;
        if (copyLen > newCap - 1) copyLen = newCap - 1;
        for (i = 0; i < copyLen; i++) newText[i] = e->text[i];
        newText[copyLen] = 0;
    } else {
        newText[0] = 0;
    }

    if (e->text) LocalFree(e->text);
    e->text = newText;
    e->cap = newCap;
    return 1;
}

static void ApplyUndoEntry(UndoEntry *e)
{
    if (e->type == UNDO_INSERT) {
        /* Was an insert - delete the text to undo */
        DeleteRange(e->pos, e->len);
    } else {
        /* Was a delete - insert the text back to undo */
        SendMessageW(s_hwndEdit, EM_SETSEL, e->pos, e->pos);
        SendMessageW(s_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)e->text);
    }
}

static void ApplyRedoEntry(UndoEntry *e)
{
    if (e->type == UNDO_INSERT) {
        /* Was an insert - re-insert the text */
        SendMessageW(s_hwndEdit, EM_SETSEL, e->pos, e->pos);
        SendMessageW(s_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)e->text);
    } else {
        /* Was a delete - re-delete the text */
        DeleteRange(e->pos, e->len);
    }
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
        s_undoStack[i].cap = 0;
        s_undoStack[i].group = 0;
    }
    s_groupDepth = 0;
    s_groupId = 0;
    s_nextGroupId = 1;
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
    s_groupDepth = 0;
    s_groupId = 0;
    s_nextGroupId = 1;
}

void Undo_BeginGroup(void)
{
    if (s_groupDepth == 0) {
        s_groupId = s_nextGroupId++;
        if (s_nextGroupId <= 0) s_nextGroupId = 1;
    }
    s_groupDepth++;
}

void Undo_EndGroup(void)
{
    if (s_groupDepth <= 0) return;
    s_groupDepth--;
    if (s_groupDepth == 0) s_groupId = 0;
}

/*
 * Undo_Record - Record an operation
 * type: 0 = insert, 1 = delete
 */
void Undo_Record(int type, int pos, const wchar_t *text, int len)
{
    UndoEntry *e;
    int i;
    int currentGroup;

    if (!text) return;
    if (len < 0) len = lstrlenW(text);
    if (len == 0) return;
    if (len > UNDO_MAX_TEXT) len = UNDO_MAX_TEXT;
    currentGroup = (s_groupDepth > 0) ? s_groupId : 0;

    /* New edit clears redo stack */
    for (i = s_undoCount; i < s_undoCount + s_redoCount && i < UNDO_MAX_ENTRIES; i++) {
        FreeEntry(&s_undoStack[i]);
    }
    s_redoCount = 0;

    /* Try to coalesce with previous entry for single-char operations */
    if (len == 1 && s_undoCount > 0) {
        e = &s_undoStack[s_undoCount - 1];
        
        /* Coalesce consecutive inserts (typing) */
        if (e->group == currentGroup &&
            type == UNDO_INSERT && e->type == UNDO_INSERT &&
            pos == e->pos + e->len && e->len < UNDO_MAX_TEXT) {
            if (EnsureEntryCapacity(e, e->len + 2)) {
                e->text[e->len] = text[0];
                e->len++;
                e->text[e->len] = 0;
                return;
            }
        }
        
        /* Coalesce consecutive backspaces (deleting backward) */
        if (e->group == currentGroup &&
            type == UNDO_DELETE && e->type == UNDO_DELETE &&
            pos == e->pos - 1 && e->len < UNDO_MAX_TEXT) {
            if (EnsureEntryCapacity(e, e->len + 2)) {
                for (i = e->len; i >= 0; i--) e->text[i + 1] = e->text[i];
                e->text[0] = text[0];
                e->pos = pos;
                e->len++;
                return;
            }
        }
        
        /* Coalesce consecutive deletes (Delete key at same position) */
        if (e->group == currentGroup &&
            type == UNDO_DELETE && e->type == UNDO_DELETE &&
            pos == e->pos && e->len < UNDO_MAX_TEXT) {
            if (EnsureEntryCapacity(e, e->len + 2)) {
                e->text[e->len] = text[0];
                e->len++;
                e->text[e->len] = 0;
                return;
            }
        }
    }

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
    if (!EnsureEntryCapacity(e, len + 1)) return;
    e->type = type;
    e->pos = pos;
    e->group = currentGroup;
    e->len = len;
    {
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
    int group;

    if (s_undoCount == 0 || !s_hwndEdit) return 0;

    s_undoCount--;
    e = &s_undoStack[s_undoCount];
    group = e->group;
    ApplyUndoEntry(e);
    s_redoCount++;

    /* Grouped operations undo together in one user step. */
    if (group > 0) {
        while (s_undoCount > 0 && s_undoStack[s_undoCount - 1].group == group) {
            s_undoCount--;
            e = &s_undoStack[s_undoCount];
            ApplyUndoEntry(e);
            s_redoCount++;
        }
    }

    return 1;
}

/*
 * Undo_Redo - Redo last undone operation
 * Returns 1 if redo was performed
 */
int Undo_Redo(void)
{
    UndoEntry *e;
    int group;

    if (s_redoCount == 0 || !s_hwndEdit) return 0;

    e = &s_undoStack[s_undoCount];
    group = e->group;
    ApplyRedoEntry(e);
    s_undoCount++;
    s_redoCount--;

    /* Grouped operations redo together in one user step. */
    if (group > 0) {
        while (s_redoCount > 0 && s_undoStack[s_undoCount].group == group) {
            e = &s_undoStack[s_undoCount];
            ApplyRedoEntry(e);
            s_undoCount++;
            s_redoCount--;
        }
    }

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
