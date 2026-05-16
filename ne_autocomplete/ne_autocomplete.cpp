#include "ne_autocomplete.h"
#include "../dpi.h"
#include "../scintilla/Scintilla.h"
#include <windowsx.h>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Palette
// Background: COLOR_INFOBK — matches the system tooltip yellow used by our
//             custom tooltip component, giving a consistent look.
// Selection:  deep forest green — complementary to warm yellow, high contrast
//             with white text, avoids the jarring blue-on-yellow combination.
// Border:     dark amber — ties the popup visually to the yellow background.
// ─────────────────────────────────────────────────────────────────────────────
static const COLORREF kAcSelBg   = RGB(80, 160, 110);  // muted sage green
static const COLORREF kAcSelFg   = RGB(255, 255, 255); // white text on selection
static const COLORREF kAcFg      = RGB(20,  20,  20);  // near-black body text
static const COLORREF kAcBorder  = RGB(120, 100, 20);  // dark amber border

static const int kAcMaxVisible   = 9;    // max items shown without scrolling
static const int kAcPadH         = 8;    // horizontal text padding (px @ 96 dpi)
static const int kAcPadV         = 3;    // vertical text padding  (px @ 96 dpi)

// Deferred acceptance/dismissal — lets us avoid calling DestroyWindow from
// within the WndProc (which is technically legal but confuses some paths).
static const UINT WM_AC_ACCEPT  = WM_USER + 501;
static const UINT WM_AC_DISMISS = WM_USER + 502;

// ─────────────────────────────────────────────────────────────────────────────
// State
// ─────────────────────────────────────────────────────────────────────────────
struct NeAcState {
    HWND     hWnd       = NULL;   // popup HWND (NULL when not visible)
    HWND     hSci       = NULL;   // Scintilla window being subclassed
    std::vector<std::string> items;
    int      selIdx     = 0;      // currently highlighted item
    int      scrollOff  = 0;      // index of first visible item
    int      prefixLen  = 0;      // bytes before caret to replace on accept
    HFONT    hFont      = NULL;
    int      itemH      = 0;      // height of one row in pixels
    NeAcAcceptFn onAccept;
    WNDPROC  sciOldProc = NULL;   // original Scintilla WndProc
    bool     pendingAccept = false; // WM_KEYDOWN consumed; swallow next WM_CHAR then accept
};

static NeAcState g_ac;

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

// Scroll the view so that idx is visible.
static void Ac_EnsureVisible(int idx)
{
    if (idx < g_ac.scrollOff)
        g_ac.scrollOff = idx;
    if (idx >= g_ac.scrollOff + kAcMaxVisible)
        g_ac.scrollOff = idx - kAcMaxVisible + 1;
}

// Dismiss: restore Scintilla subclass, release resources, hide & schedule
// destruction of the popup window.  Safe to call from any context including
// from within AcWndProc (window is hidden immediately; DestroyWindow is deferred
// via PostMessage so it runs from the message loop, not from a nested WndProc).
static void Ac_Dismiss()
{
    g_ac.pendingAccept = false;
    if (g_ac.hSci && g_ac.sciOldProc) {
        SetWindowLongPtrW(g_ac.hSci, GWLP_WNDPROC, (LONG_PTR)g_ac.sciOldProc);
        g_ac.sciOldProc = NULL;
    }
    if (g_ac.hFont) { DeleteObject(g_ac.hFont); g_ac.hFont = NULL; }
    g_ac.hSci     = NULL;
    g_ac.items.clear();
    g_ac.onAccept = nullptr;

    if (g_ac.hWnd) {
        ShowWindow(g_ac.hWnd, SW_HIDE);        // immediate
        PostMessageW(g_ac.hWnd, WM_CLOSE, 0, 0); // deferred destroy
        g_ac.hWnd = NULL;
    }
}

// Accept item at idx: dismiss popup then invoke the callback.
static void Ac_Accept(int idx)
{
    if (idx < 0 || idx >= (int)g_ac.items.size()) { Ac_Dismiss(); return; }

    NeAcAcceptFn cb  = g_ac.onAccept;   // copy before Ac_Dismiss clears it
    std::string  item = g_ac.items[idx];
    int          plen = g_ac.prefixLen;

    Ac_Dismiss();
    if (cb) cb(item, plen);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scintilla subclass proc — intercepts navigation keys while popup is visible
// ─────────────────────────────────────────────────────────────────────────────
static LRESULT CALLBACK AcSciSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
    // Tab/Enter: WM_KEYDOWN was already consumed; now swallow the WM_CHAR
    // (TranslateMessage already posted it) then fire the acceptance.
    if (msg == WM_CHAR && g_ac.pendingAccept) {
        g_ac.pendingAccept = false;
        Ac_Accept(g_ac.selIdx);
        return 0; // swallow the '\t' or '\r' — never reaches Scintilla
    }

    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        // Tab / Enter: consume the keydown, set flag to swallow the WM_CHAR next
        if ((wParam == VK_TAB || wParam == VK_RETURN) && g_ac.hWnd) {
            g_ac.pendingAccept = true;
            return 0;
        }

        // Up / Down / Escape
        if (NeAutoComplete_HandleKey(wParam)) return 0;

        // Backspace / Delete: dismiss popup, pass key to Scintilla normally
        if (wParam == VK_BACK || wParam == VK_DELETE) {
            WNDPROC old = g_ac.sciOldProc;  // save before Ac_Dismiss clears it
            Ac_Dismiss();
            return CallWindowProcW(old, hwnd, msg, wParam, lParam);
        }
    }

    // Dismiss when Scintilla loses focus — but NOT when focus went to the
    // popup itself (user is about to click an item).
    if (msg == WM_KILLFOCUS) {
        if ((HWND)wParam == g_ac.hWnd)
            return CallWindowProcW(g_ac.sciOldProc, hwnd, msg, wParam, lParam);
        WNDPROC old = g_ac.sciOldProc;
        Ac_Dismiss();
        return CallWindowProcW(old, hwnd, msg, wParam, lParam);
    }

    return CallWindowProcW(g_ac.sciOldProc, hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// Popup WndProc
// ─────────────────────────────────────────────────────────────────────────────
static LRESULT CALLBACK AcWndProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    // ── Paint ────────────────────────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        // Background (system tooltip yellow)
        HBRUSH hBrBg = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
        FillRect(hdc, &rc, hBrBg);
        DeleteObject(hBrBg);

        // Border (1 px dark amber)
        {
            HPEN   hPen  = CreatePen(PS_SOLID, 1, kAcBorder);
            HPEN   hOldP = (HPEN)  SelectObject(hdc, hPen);
            HBRUSH hOldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, rc.right, rc.bottom);
            SelectObject(hdc, hOldP);
            SelectObject(hdc, hOldB);
            DeleteObject(hPen);
        }

        // Items
        HFONT hOldFont = (HFONT)SelectObject(hdc, g_ac.hFont);
        SetBkMode(hdc, TRANSPARENT);

        int visible = std::min((int)g_ac.items.size(), kAcMaxVisible);
        for (int i = 0; i < visible; ++i) {
            int dataIdx = g_ac.scrollOff + i;
            if (dataIdx >= (int)g_ac.items.size()) break;

            RECT ir = { rc.left + 1,
                        1 + i * g_ac.itemH,
                        rc.right - 1,
                        1 + (i + 1) * g_ac.itemH };

            if (dataIdx == g_ac.selIdx) {
                HBRUSH hSel = CreateSolidBrush(kAcSelBg);
                FillRect(hdc, &ir, hSel);
                DeleteObject(hSel);
                SetTextColor(hdc, kAcSelFg);
            } else {
                SetTextColor(hdc, kAcFg);
            }

            // Convert UTF-8 / ANSI bytes to wchar for GDI rendering
            const std::string& s = g_ac.items[dataIdx];
            int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
            std::wstring ws(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), wlen);

            RECT tr = { ir.left + kAcPadH, ir.top + kAcPadV,
                        ir.right - kAcPadH, ir.bottom };
            DrawTextW(hdc, ws.c_str(), -1, &tr,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                      DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        // Scroll indicators (▲ / ▼) if the list is taller than the popup
        if ((int)g_ac.items.size() > kAcMaxVisible) {
            SetTextColor(hdc, kAcFg);
            if (g_ac.scrollOff > 0) {
                RECT ar = { rc.right - S(18), 1, rc.right - 1, 1 + g_ac.itemH };
                DrawTextW(hdc, L"\u25B2", -1, &ar,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            int lastVis = std::min((int)g_ac.items.size(),
                                   g_ac.scrollOff + kAcMaxVisible) - 1;
            if (g_ac.scrollOff + kAcMaxVisible < (int)g_ac.items.size()) {
                RECT ar = { rc.right - S(18),
                            1 + lastVis * g_ac.itemH,
                            rc.right - 1,
                            1 + (lastVis + 1) * g_ac.itemH };
                DrawTextW(hdc, L"\u25BC", -1, &ar,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        SelectObject(hdc, hOldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Mouse ─────────────────────────────────────────────────────────────────
    // Explicitly prevent focus steal when clicked — belt-and-suspenders with
    // WS_EX_NOACTIVATE, but some Windows versions still move focus without this.
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_LBUTTONDOWN: {
        int y   = GET_Y_LPARAM(lParam);
        int idx = g_ac.scrollOff + (y - 1) / g_ac.itemH;
        if (idx >= 0 && idx < (int)g_ac.items.size()) {
            g_ac.selIdx = idx;
            // Defer via PostMessage so DestroyWindow is not called inside WndProc
            PostMessageW(hwnd, WM_AC_ACCEPT, 0, 0);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int y   = GET_Y_LPARAM(lParam);
        int idx = g_ac.scrollOff + (y - 1) / g_ac.itemH;
        if (idx >= 0 && idx < (int)g_ac.items.size() && idx != g_ac.selIdx) {
            g_ac.selIdx = idx;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta  = GET_WHEEL_DELTA_WPARAM(wParam);
        int newOff = g_ac.scrollOff + (delta < 0 ? 1 : -1);
        newOff = std::max(0, std::min(newOff,
                          (int)g_ac.items.size() - kAcMaxVisible));
        if (newOff != g_ac.scrollOff) {
            g_ac.scrollOff = newOff;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    // ── Deferred actions ──────────────────────────────────────────────────────
    case WM_AC_ACCEPT:
        Ac_Accept(g_ac.selIdx);
        return 0;

    case WM_AC_DISMISS:
        Ac_Dismiss();
        return 0;

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    case WM_CLOSE:
        DestroyWindow(hwnd);  // safe — called from message loop via PostMessage
        return 0;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void NeAutoComplete_Register(HINSTANCE hInst)
{
    WNDCLASSEXW wc  = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DROPSHADOW;
    wc.lpfnWndProc   = AcWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"NsbAutoComplete";
    RegisterClassExW(&wc); // harmless if already registered
}

void NeAutoComplete_Show(HWND hSci,
                         const std::vector<std::string>& items,
                         int prefixLen,
                         NeAcAcceptFn onAccept)
{
    if (items.empty()) { NeAutoComplete_Hide(); return; }

    // Dismiss any existing popup first
    if (g_ac.hWnd) Ac_Dismiss();

    g_ac.hSci      = hSci;
    g_ac.items     = items;
    g_ac.selIdx    = 0;
    g_ac.scrollOff = 0;
    g_ac.prefixLen = prefixLen;
    g_ac.onAccept  = onAccept;

    // Create 12pt Segoe UI font, DPI-aware from the Scintilla window
    UINT dpi = GetDpiForWindow(hSci);
    g_ac.hFont = CreateFontW(
        -MulDiv(12, (int)dpi, 72), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    // Measure row height using real font metrics
    {
        HDC   hdc  = GetDC(hSci);
        HFONT hOld = (HFONT)SelectObject(hdc, g_ac.hFont);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        g_ac.itemH = tm.tmHeight + 2 * kAcPadV;
        SelectObject(hdc, hOld);
        ReleaseDC(hSci, hdc);
    }

    // Measure max item width for popup sizing
    int maxW = S(200);
    {
        HDC   hdc  = GetDC(hSci);
        HFONT hOld = (HFONT)SelectObject(hdc, g_ac.hFont);
        for (const auto& s : items) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
            std::wstring ws(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), wlen);
            SIZE sz;
            GetTextExtentPoint32W(hdc, ws.c_str(), std::max(0, (int)ws.size() - 1), &sz);
            maxW = std::max(maxW, (int)sz.cx + 2 * kAcPadH + S(20));
        }
        SelectObject(hdc, hOld);
        ReleaseDC(hSci, hdc);
    }
    maxW = std::min(maxW, S(640));

    int visible = std::min((int)items.size(), kAcMaxVisible);
    int popupH  = visible * g_ac.itemH + 2; // +2 for top/bottom border px

    // Position below the caret; flip above if it won't fit on screen
    LRESULT sciPos = SendMessageW(hSci, SCI_GETCURRENTPOS, 0, 0);
    int caretX = (int)SendMessageW(hSci, SCI_POINTXFROMPOSITION, 0, (LPARAM)sciPos);
    int caretY = (int)SendMessageW(hSci, SCI_POINTYFROMPOSITION, 0, (LPARAM)sciPos);
    int lineH  = (int)SendMessageW(hSci, SCI_TEXTHEIGHT,
                     (WPARAM)SendMessageW(hSci, SCI_LINEFROMPOSITION, (WPARAM)sciPos, 0), 0);

    POINT ptCaret = { caretX, caretY };
    ClientToScreen(hSci, &ptCaret);

    int screenY = ptCaret.y + lineH;
    POINT check = { ptCaret.x, screenY };
    HMONITOR hmon = MonitorFromPoint(check, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(hmon, &mi);
    if (screenY + popupH > mi.rcWork.bottom)
        screenY = ptCaret.y - popupH; // flip above the caret line

    g_ac.hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"NsbAutoComplete", L"",
        WS_POPUP,
        ptCaret.x, screenY, maxW, popupH,
        hSci, NULL, GetModuleHandleW(NULL), NULL);

    if (!g_ac.hWnd) { Ac_Dismiss(); return; }

    // Subclass Scintilla to intercept keyboard while popup is visible
    g_ac.sciOldProc = (WNDPROC)SetWindowLongPtrW(
        hSci, GWLP_WNDPROC, (LONG_PTR)AcSciSubclassProc);

    ShowWindow(g_ac.hWnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_ac.hWnd);
}

void NeAutoComplete_Hide()
{
    Ac_Dismiss();
}

bool NeAutoComplete_IsVisible()
{
    return g_ac.hWnd != NULL;
}

bool NeAutoComplete_HandleKey(WPARAM vk)
{
    if (!g_ac.hWnd) return false;

    switch (vk) {
    case VK_UP:
        if (g_ac.selIdx > 0) {
            --g_ac.selIdx;
            Ac_EnsureVisible(g_ac.selIdx);
            InvalidateRect(g_ac.hWnd, NULL, FALSE);
        }
        return true;

    case VK_DOWN:
        if (g_ac.selIdx < (int)g_ac.items.size() - 1) {
            ++g_ac.selIdx;
            Ac_EnsureVisible(g_ac.selIdx);
            InvalidateRect(g_ac.hWnd, NULL, FALSE);
        }
        return true;

    case VK_RETURN:
    case VK_TAB:
        Ac_Accept(g_ac.selIdx);
        return true;

    case VK_ESCAPE:
        Ac_Dismiss();
        return true;
    }

    return false;
}
