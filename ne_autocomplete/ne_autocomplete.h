#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// NsbAutoComplete — custom owner-draw autocomplete popup
//
// Appearance: system tooltip background (warm yellow) + dark-amber border.
// Selected item uses deep forest-green highlight so it harmonises with the
// yellow background without clashing the way standard Windows blue does.
//
// Phrase-completion mode (multi-token prefix containing a space) is handled
// here rather than through Scintilla's built-in SCI_AUTOCSHOW, which cancels
// itself when the entered text includes non-word characters like spaces or '='.
//
// Keyboard while popup is visible (Scintilla is subclassed):
//   ↑ / ↓   Move selection
//   Enter / Tab  Accept selection and replace prefix in the Scintilla document
//   Escape  Dismiss without inserting
//   Backspace / Delete  Dismiss and pass keystroke to Scintilla
//   Any other key  Passed through to Scintilla; SCN_CHARADDED will refresh or
//                  dismiss the popup on the next call to NeAutoComplete_Show.
// ─────────────────────────────────────────────────────────────────────────────

// Called when the user accepts a completion item.
//   item      – accepted text (document byte encoding, typically UTF-8)
//   prefixLen – how many bytes before the current caret to replace
using NeAcAcceptFn = std::function<void(const std::string& item, int prefixLen)>;

// Register the "NsbAutoComplete" window class (once per process).
void NeAutoComplete_Register(HINSTANCE hInst);

// Show (or replace) the autocomplete popup next to the Scintilla caret.
//   hSci      – the Scintilla HWND; used for caret positioning and keyboard
//               subclassing while the popup is visible
//   items     – sorted list of completion candidates (document byte encoding)
//   prefixLen – bytes before the caret that will be replaced on acceptance
//   onAccept  – callback invoked when the user accepts an item
void NeAutoComplete_Show(HWND hSci,
                         const std::vector<std::string>& items,
                         int prefixLen,
                         NeAcAcceptFn onAccept);

// Dismiss the popup. No-op if not currently visible.
void NeAutoComplete_Hide();

// True if the popup is currently shown.
bool NeAutoComplete_IsVisible();

// Forward a WM_KEYDOWN wParam to the popup for navigation.
// Returns true if the key was consumed (caller must NOT pass it to Scintilla).
bool NeAutoComplete_HandleKey(WPARAM vk);
