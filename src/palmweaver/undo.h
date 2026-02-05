/*
 * undo.h - Portable undo/redo system for Edit controls
 *
 * Designed for Windows CE 2.0+ (C89, Unicode, no CRT)
 * Can be reused across projects (SQLite/CE, Palmweaver, etc.)
 *
 * Usage:
 *   1. Call Undo_Init(hwndEdit) at startup
 *   2. Call Undo_Record() before operations you control
 *   3. Wire Undo_Perform() to Ctrl+Z / Edit menu
 *   4. Call Undo_Clear() on File New/Open
 */

#ifndef UNDO_H
#define UNDO_H

#include <windows.h>

/* Initialize undo system with target edit control */
void Undo_Init(HWND hwndEdit);

/* Cleanup and free resources */
void Undo_Cleanup(void);

/* Record an operation (call BEFORE making the change)
 * type: 0 = insert (text will be added at pos)
 *       1 = delete (text at pos will be removed)
 * pos: character position in edit control
 * text: the text being inserted or deleted
 * len: length of text (-1 to auto-calculate via lstrlenW)
 */
void Undo_Record(int type, int pos, const wchar_t *text, int len);

/* Convenience wrappers */
#define Undo_RecordInsert(pos, text, len) Undo_Record(0, (pos), (text), (len))
#define Undo_RecordDelete(pos, text, len) Undo_Record(1, (pos), (text), (len))

/* Perform undo (Ctrl+Z) - returns 1 if undo was performed */
int Undo_Perform(void);

/* Perform redo (Ctrl+Y) - returns 1 if redo was performed */
int Undo_Redo(void);

/* Clear undo/redo history (call on File New/Open) */
void Undo_Clear(void);

/* Query state for menu enable/disable */
int Undo_CanUndo(void);
int Undo_CanRedo(void);

#endif /* UNDO_H */
