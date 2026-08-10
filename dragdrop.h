/* dragdrop.h
 *
 * C89-compatible header. Declares a plain C API around the C++/COM
 * drag-and-drop implementation in dragdrop.cpp. Include this from
 * your C89 main loop; do NOT include ole2.h/objidl.h or any COM headers
 * directly in your .c files.
 */
#ifndef DRAGDROP_H
#define DRAGDROP_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once, after OleInitialize() and after your window is created. 
 * Returns 0 on success, nonzero on failure. */
int DD_EnableDropTarget(HWND hwnd);

/* Call once, before you destroy the window. */
void DD_DisableDropTarget(HWND hwnd);

/* Starts dragging out a list of files. paths is an array of wide-char
* C strings, count is the number of entries. Blocks until the drag ends. */
int DD_BeginFileDragOne(const wchar_t* path);

/* Starts dragging out a list of files. paths is an array of wide-char
* C strings, count is the number of entries. Blocks until the drag ends. */
int DD_BeginFileDrag(const wchar_t** paths, int count);

/* Call once at program startup, before any other DD_ function.
 * Wraps OleInitialize(). Returns 0 on success. */
int DD_Initialize(void);

/* Call once at program shutdown. Wraps OleUninitialize(). */
void DD_Uninitialize(void);

/* Optional: register a callback invoked when files are dropped onto the
 * window, so your C89 code finds out about it instead of the C++ layer
 * popping a MessageBox itself. paths/count are only valid for the
 * duration of the callback. */
typedef void (*DD_OnFilesDroppedFn)(const wchar_t** paths, int count, void* userdata);
void DD_SetDropCallback(DD_OnFilesDroppedFn callback, void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* DRAGDROP_GLUE_H */
