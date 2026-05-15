#pragma once
#include <windows.h>
#include <richedit.h>
#include <vector>

// ── NSBEdit Search Highlight API ──────────────────────────────────────────────
//
// Highlights all match ranges inside a RichEdit control at once:
//   - The active match uses activeFg + activeBg.
//   - All other matches use their original text colour + inactiveBg.
//
// Per-character original colour data is saved so NeHighlight_Clear() restores
// the document to exactly its previous state.
//
// Typical usage (Find / Replace dialog):
//
//   static NeHighlightState s_hl;
//
//   // After collecting all match positions:
//   NeHighlight_SetAll(hEdit, ranges, activeIdx,
//                      NE_HL_FG, NE_HL_BG, NE_HL_BG_INACTIVE, s_hl);
//
//   // When navigating to the next match (fast — repaints only 2 ranges):
//   NeHighlight_SetActive(hEdit, newIdx,
//                         NE_HL_FG, NE_HL_BG, NE_HL_BG_INACTIVE, s_hl);
//
//   // On dialog close:
//   NeHighlight_Clear(hEdit, s_hl);
// ─────────────────────────────────────────────────────────────────────────────

// Search-result highlight colours.
#define NE_HL_FG          RGB(  2,  52,  85)  // active: dark navy text
#define NE_HL_BG          RGB(254, 244, 205)  // active: warm cream background
#define NE_HL_BG_INACTIVE RGB(217, 237, 225)  // inactive matches: soft green

// Per-character saved colour state (internal).
struct NeCharSaved {
    int      pos;
    COLORREF fg;       // original crTextColor
    COLORREF bg;       // original crBackColor
    DWORD    effects;  // CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR bits only
};

// One match range [start, end).
struct NeHighlightRange {
    int start;
    int end;
};

// Opaque state owned by the caller.  Zero-initialise before first use.
struct NeHighlightState {
    std::vector<NeCharSaved>      saved;      // original per-char data (all ranges)
    std::vector<NeHighlightRange> matches;   // all match ranges
    int                           activeIdx = -1;
};

// Apply highlights for all match ranges.  activeIdx is shown with activeFg/Bg;
// all other ranges get inactiveBg with their original foreground colour.
// Any previous state is cleared first.
void NeHighlight_SetAll(HWND hEdit,
                        const std::vector<NeHighlightRange>& matches,
                        int activeIdx,
                        COLORREF activeFg, COLORREF activeBg,
                        COLORREF inactiveBg,
                        NeHighlightState& state);

// Fast navigation: repaints only the previously active and the newly active
// range.  Everything else is left untouched.
void NeHighlight_SetActive(HWND hEdit, int newActiveIdx,
                           COLORREF activeFg, COLORREF activeBg,
                           COLORREF inactiveBg,
                           NeHighlightState& state);

// Restore all saved character colours and reset state.
// Safe to call when state is already empty.
void NeHighlight_Clear(HWND hEdit, NeHighlightState& state);
