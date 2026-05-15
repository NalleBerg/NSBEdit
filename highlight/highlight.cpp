// NSBEdit — highlight.cpp
// Multi-range active/inactive search highlight for RichEdit controls.
// See highlight.h for the public API.

#include "highlight.h"

// ── internal helpers ─────────────────────────────────────────────────────────────────

static void GetCharFmt(HWND hEdit, int pos, CHARFORMAT2W& cf)
{
    CHARRANGE cr = { (LONG)pos, (LONG)(pos + 1) };
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BACKCOLOR | CFM_EFFECTS2;
    SendMessageW(hEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// Apply fg+bg to a whole range in one pass.
static void ApplyFgBg(HWND hEdit, int start, int end, COLORREF fg, COLORREF bg)
{
    CHARRANGE cr = { (LONG)start, (LONG)end };
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf = {};
    cf.cbSize      = sizeof(cf);
    cf.dwMask      = CFM_COLOR | CFM_BACKCOLOR;
    cf.dwEffects   = 0;  // clear CFE_AUTOCOLOR and CFE_AUTOBACKCOLOR
    cf.crTextColor = fg;
    cf.crBackColor = bg;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// Apply only BG to a whole range (FG untouched).
static void ApplyBgOnly(HWND hEdit, int start, int end, COLORREF bg)
{
    CHARRANGE cr = { (LONG)start, (LONG)end };
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf = {};
    cf.cbSize      = sizeof(cf);
    cf.dwMask      = CFM_BACKCOLOR;
    cf.dwEffects   = 0;  // clear CFE_AUTOBACKCOLOR
    cf.crBackColor = bg;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// Restore a range to inactive state: original FG (per-char) + inactiveBg (whole range).
// Used when demoting the active match back to inactive.
static void ApplyInactive(HWND hEdit,
                          const std::vector<NeCharSaved>& saved,
                          int start, int end,
                          COLORREF inactiveBg)
{
    // Restore original FG per-character (saved vector is sorted by pos).
    for (const auto& s : saved) {
        if (s.pos >= end)   break;
        if (s.pos < start)  continue;
        CHARRANGE cr = { (LONG)s.pos, (LONG)(s.pos + 1) };
        SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
        CHARFORMAT2W cf = {};
        cf.cbSize      = sizeof(cf);
        cf.dwMask      = CFM_COLOR;
        cf.dwEffects   = s.effects & CFE_AUTOCOLOR;
        cf.crTextColor = s.fg;
        SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    // Apply inactive BG to the whole range in one pass.
    ApplyBgOnly(hEdit, start, end, inactiveBg);
}

// Restore a single character from saved data.
static void RestoreChar(HWND hEdit, const NeCharSaved& s)
{
    CHARRANGE cr = { (LONG)s.pos, (LONG)(s.pos + 1) };
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf = {};
    cf.cbSize      = sizeof(cf);
    cf.dwMask      = CFM_COLOR | CFM_BACKCOLOR;
    cf.dwEffects   = s.effects & (CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR);
    cf.crTextColor = s.fg;
    cf.crBackColor = s.bg;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

// ── public API ───────────────────────────────────────────────────────────────────────

void NeHighlight_SetAll(HWND hEdit,
                        const std::vector<NeHighlightRange>& matches,
                        int activeIdx,
                        COLORREF activeFg, COLORREF activeBg,
                        COLORREF inactiveBg,
                        NeHighlightState& state)
{
    if (!hEdit) return;
    NeHighlight_Clear(hEdit, state);
    if (matches.empty()) return;

    // ── Phase 1: save original per-char colours (hides selection-jumping) ──
    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);

    int totalChars = 0;
    for (const auto& r : matches) totalChars += r.end - r.start;
    state.saved.reserve(totalChars);

    for (const auto& r : matches) {
        for (int i = r.start; i < r.end; i++) {
            CHARFORMAT2W cf = {};
            GetCharFmt(hEdit, i, cf);
            NeCharSaved s;
            s.pos     = i;
            s.fg      = cf.crTextColor;
            s.bg      = cf.crBackColor;
            s.effects = cf.dwEffects & (CFE_AUTOCOLOR | CFE_AUTOBACKCOLOR);
            state.saved.push_back(s);
        }
    }

    state.matches   = matches;
    state.activeIdx = activeIdx;

    // Re-enable redraw before applying colours so EM_SETCHARFORMAT takes effect.
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);

    // ── Phase 2: apply highlight colours ───────────────────────────────────
    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);

    for (const auto& r : matches)
        ApplyBgOnly(hEdit, r.start, r.end, inactiveBg);

    if (activeIdx >= 0 && activeIdx < (int)matches.size()) {
        const auto& ar = matches[activeIdx];
        ApplyFgBg(hEdit, ar.start, ar.end, activeFg, activeBg);
        CHARRANGE cr = { (LONG)ar.start, (LONG)ar.end };
        SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    }

    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    // Re-set the selection with redraw on so RichEdit scrolls to it correctly.
    if (activeIdx >= 0 && activeIdx < (int)matches.size()) {
        const auto& ar = matches[activeIdx];
        CHARRANGE cr = { (LONG)ar.start, (LONG)ar.end };
        SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    }
}

void NeHighlight_SetActive(HWND hEdit, int newActiveIdx,
                           COLORREF activeFg, COLORREF activeBg,
                           COLORREF inactiveBg,
                           NeHighlightState& state)
{
    if (!hEdit || state.matches.empty()) return;
    if (newActiveIdx < 0 || newActiveIdx >= (int)state.matches.size()) return;
    if (newActiveIdx == state.activeIdx) return;

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);

    // Demote previous active range → inactive (restore original FG, set inactiveBg).
    if (state.activeIdx >= 0 && state.activeIdx < (int)state.matches.size()) {
        const auto& pr = state.matches[state.activeIdx];
        ApplyInactive(hEdit, state.saved, pr.start, pr.end, inactiveBg);
    }

    // Promote new range → active.
    const auto& nr = state.matches[newActiveIdx];
    ApplyFgBg(hEdit, nr.start, nr.end, activeFg, activeBg);
    CHARRANGE cr = { (LONG)nr.start, (LONG)nr.end };
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

    state.activeIdx = newActiveIdx;

    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    // Re-set the selection with redraw on so RichEdit scrolls to it correctly.
    {
        const auto& nr2 = state.matches[newActiveIdx];
        CHARRANGE cr2 = { (LONG)nr2.start, (LONG)nr2.end };
        SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr2);
        SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    }
}

void NeHighlight_Clear(HWND hEdit, NeHighlightState& state)
{
    if (!hEdit || state.saved.empty()) return;

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);

    for (const NeCharSaved& s : state.saved)
        RestoreChar(hEdit, s);

    // Collapse selection to end of the active range (non-intrusive).
    if (state.activeIdx >= 0 && state.activeIdx < (int)state.matches.size()) {
        LONG p = (LONG)state.matches[state.activeIdx].end;
        CHARRANGE cr = { p, p };
        SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    }

    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hEdit, NULL, FALSE);

    state.saved.clear();
    state.matches.clear();
    state.activeIdx = -1;
}
