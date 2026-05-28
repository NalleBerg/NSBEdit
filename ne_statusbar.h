#pragma once
#include <windows.h>

// Creates a custom owner-drawn status bar as a child of hwndParent.
// Height is fixed at S(22) dpi-scaled pixels.
HWND NeStatusBar_Create(HWND hwndParent, int id, HINSTANCE hInst);

// Updates word count, char count, line count, and modified (save) state all at once.
void NeStatusBar_Update(HWND hBar, int words, int chars, int lines, bool modified);

// Lightweight update of the save state only (no count recompute needed).
void NeStatusBar_SetModified(HWND hBar, bool modified);

// Returns the bar's fixed height in pixels (S(22)).
int NeStatusBar_GetHeight(HWND hBar);

// Set the localized label strings displayed in the bar.
//   wordsLabel   e.g. L"Words"
//   charsLabel   e.g. L"Chars"
//   linesLabel   e.g. L"Lines"
//   savedLabel   e.g. L"Saved"
//   unsavedLabel e.g. L"Unsaved"
void NeStatusBar_SetLabels(HWND hBar,
    const wchar_t* wordsLabel, const wchar_t* charsLabel,
    const wchar_t* linesLabel,
    const wchar_t* savedLabel, const wchar_t* unsavedLabel);

// Set the centre info string (encoding / file type). Pass NULL to clear.
void NeStatusBar_SetInfo(HWND hBar, const wchar_t* info);

// Set the current caret position shown between centre and Saved/Unsaved.
// line and col are 1-based. Pass 0/0 to hide.
void NeStatusBar_SetLineCol(HWND hBar, int line, int col);

// Set the localised labels for line/col (e.g. L"Ln", L"Col").
void NeStatusBar_SetLineColLabels(HWND hBar, const wchar_t* lineLabel, const wchar_t* colLabel);

// Switch the status bar between light and dark mode.
void NeStatusBar_SetDarkMode(HWND hBar, bool dark);
