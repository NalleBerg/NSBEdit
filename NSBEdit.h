#pragma once
// NSBEdit — standalone RTF notepad
// Entry point: wWinMain in NSBEdit.cpp.
// See NSBEdit.cpp for full implementation.

#include <windows.h>
#include <string>
#include <vector>

const wchar_t* Ne_Ls(const wchar_t* key);

// ── AI code-block syntax highlighting ─────────────────────────────────────────
// One colored run of code text (concatenated runs reproduce the input exactly).
struct NsbCodeStyleRun {
    std::wstring text;
    COLORREF     color;
};

// Syntax-highlight `code` for the markdown fence language `fenceLang` (e.g.
// "cpp", "python", "js") using the editor's Scintilla/Lexilla lexers. Returns
// colored runs whose concatenated text equals `code`. Colors use the light
// editor palette so they read well on the light AI code-cell background.
// Falls back to a single default-colored run when no lexer matches.
// Implemented in NSBEdit.cpp.
std::vector<NsbCodeStyleRun> NsbAi_HighlightCode(const std::wstring& code,
                                                 const std::wstring& fenceLang);

// Styled modal input dialog (owner-draw buttons, DPI-aware).  Returns true and
// fills `out` when the user confirms.  `initialValue` pre-fills the edit; set
// `password` to mask the field (e.g. for an API key).  Implemented in NSBEdit.cpp.
bool Ne_ShowInputDialog(HWND parent, const wchar_t* title, const wchar_t* prompt,
                        std::wstring& out, const std::wstring& initialValue, bool password);
