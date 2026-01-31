/*
 * Palmweaver - Text Editor for Windows CE
 * resource.h - Resource identifiers and version
 */

#ifndef RESOURCE_H
#define RESOURCE_H

/* Version */
#define PALMWEAVER_VERSION      L"0.2.0.56"
#define PALMWEAVER_VERSION_A    "0.2.0.56"

/* Menu */
#define IDR_MENU                101

/* Menu Items - File */
#define IDM_FILE_NEW            201
#define IDM_FILE_OPEN           202
#define IDM_FILE_SAVE           203
#define IDM_FILE_SAVEAS         204
#define IDM_FILE_OPTIONS        205
#define IDM_FILE_EXIT           206

/* Menu Items - Edit */
#define IDM_EDIT_UNDO           301
#define IDM_EDIT_CUT            302
#define IDM_EDIT_COPY           303
#define IDM_EDIT_PASTE          304
#define IDM_EDIT_SELECTALL      305
#define IDM_EDIT_GOTOLINE       306
#define IDM_EDIT_FIND           307
#define IDM_EDIT_FINDNEXT       308
#define IDM_EDIT_REPLACE        309

/* Menu Items - View */
#define IDM_VIEW_WORDWRAP       351
#define IDM_VIEW_LINENUMS       352
#define IDM_VIEW_STATUSBAR      353
#define IDM_VIEW_FULLSCREEN     354
#define IDM_VIEW_FONT           355
#define IDM_VIEW_INVERSE        356

/* Menu Items - Help */
#define IDM_HELP_ABOUT          401

/* Recent Files */
#define IDM_RECENT_BASE         500
#define MAX_RECENT_FILES        5

/* Controls */
#define ID_EDIT                 1001
#define ID_STATUSBAR            1002

/* Dialogs */
#define IDD_ABOUT               601

#endif /* RESOURCE_H */
