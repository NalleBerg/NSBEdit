#include "spinner_dialog.h"
#include <algorithm>
#include <map>

// Static map to associate HWND with SpinnerDialog instance
static std::map<HWND, SpinnerDialog*> g_spinnerInstances;

// Control id of the optional Stop button.
#define SPINNER_STOP_BTN_ID 1001

static HFONT Spinner_CreatePointFont(HWND hwnd, int ptSize, const wchar_t* face)
{
    int dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
    return CreateFontW(-MulDiv(ptSize, dpi > 0 ? dpi : 96, 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static int Spinner_MeasureTextWidth(HWND hwnd, const std::wstring& text)
{
    HDC hdc = GetDC(NULL);
    if (!hdc) return 0;
    HFONT hf = Spinner_CreatePointFont(hwnd, 12, L"Segoe UI");
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    if (old) SelectObject(hdc, old);
    if (hf) DeleteObject(hf);
    ReleaseDC(NULL, hdc);
    return sz.cx;
}

static int Spinner_MeasureTextHeight(HWND hwnd, const std::wstring& text, int maxWidth)
{
    HDC hdc = GetDC(NULL);
    if (!hdc) return 0;
    HFONT hf = Spinner_CreatePointFont(hwnd, 12, L"Segoe UI");
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
    RECT rc = { 0, 0, maxWidth, 0 };
    DrawTextW(hdc, text.c_str(), -1, &rc, DT_WORDBREAK | DT_CENTER | DT_CALCRECT | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
    if (hf) DeleteObject(hf);
    ReleaseDC(NULL, hdc);
    int height = rc.bottom - rc.top;
    return height > 0 ? height : 0;
}

static int Spinner_MeasureFontHeight(HWND hwnd, int ptSize, const wchar_t* face)
{
    HDC hdc = GetDC(NULL);
    if (!hdc) return 0;
    HFONT hf = Spinner_CreatePointFont(hwnd, ptSize, face);
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    if (old) SelectObject(hdc, old);
    if (hf) DeleteObject(hf);
    ReleaseDC(NULL, hdc);
    return tm.tmHeight > 0 ? tm.tmHeight : 0;
}

SpinnerDialog::SpinnerDialog(HWND hParent)
    : m_hParent(hParent)
    , m_hDialog(NULL)
    , m_hSpinnerCtrl(NULL)
    , m_hTextCtrl(NULL)
    , m_spinnerFrame(0)
    , m_visible(false)
{
}

SpinnerDialog::~SpinnerDialog() {
    Hide();
}

void SpinnerDialog::SetTitle(const std::wstring& title) {
    m_title = title;
    if (m_hDialog && IsWindow(m_hDialog)) {
        SetWindowTextW(m_hDialog, m_title.c_str());
    }
}

void SpinnerDialog::CreateDialogWindow() {
    if (m_hDialog && IsWindow(m_hDialog)) {
        return; // Already created
    }
    
    HINSTANCE hInstance = GetModuleHandle(NULL);
    
    // Register window class
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"SpinnerDialogClass";
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        classRegistered = true;
    }
    
    // Create dialog window.  Owned by the parent (not WS_EX_TOPMOST) so it floats
    // above the owner window only and follows it to the background — it must not
    // sit on top of every other app.
    m_hDialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SpinnerDialogClass",
        m_title.c_str(),
        WS_POPUP | WS_CAPTION,
        0, 0, 360, 220,
        m_hParent, NULL, hInstance, NULL
    );
    
    if (!m_hDialog) return;
    
    // Store instance pointer for window procedure
    g_spinnerInstances[m_hDialog] = this;
    
    // Center dialog
    RECT rc;
    GetWindowRect(m_hDialog, &rc);
    int dialogWidth = rc.right - rc.left;
    int dialogHeight = rc.bottom - rc.top;
    
    int x, y;
    if (m_hParent && IsWindow(m_hParent)) {
        // Center on parent
        RECT parentRc;
        GetWindowRect(m_hParent, &parentRc);
        x = parentRc.left + (parentRc.right - parentRc.left - dialogWidth) / 2;
        y = parentRc.top + (parentRc.bottom - parentRc.top - dialogHeight) / 2;
    } else {
        // Center on screen
        x = (GetSystemMetrics(SM_CXSCREEN) - dialogWidth) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - dialogHeight) / 2;
    }
    SetWindowPos(m_hDialog, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    
    // Add icon
    HICON hIcon = LoadIcon(NULL, IDI_INFORMATION);
    if (hIcon) {
        HWND hIconCtrl = CreateWindowExW(0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            0, 0, 60, 60, m_hDialog, NULL, hInstance, NULL);
        SendMessageW(hIconCtrl, STM_SETICON, (WPARAM)hIcon, 0);
    }
    
    // Add text label
    m_hTextCtrl = CreateWindowExW(0, L"STATIC", L"", 
        WS_CHILD | WS_VISIBLE | SS_CENTER, 
        0, 0, 324, 28, m_hDialog, NULL, hInstance, NULL);
    
    HFONT hTextFont = Spinner_CreatePointFont(m_hDialog, 12, L"Segoe UI");
    SendMessageW(m_hTextCtrl, WM_SETFONT, (WPARAM)hTextFont, TRUE);
    
    // Add spinner
    m_hSpinnerCtrl = CreateWindowExW(0, L"STATIC", L"◐", 
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 60, 52, m_hDialog, NULL, hInstance, NULL);
    
    HFONT hSpinnerFont = Spinner_CreatePointFont(m_hDialog, 26, L"Segoe UI Symbol");
    SendMessageW(m_hSpinnerCtrl, WM_SETFONT, (WPARAM)hSpinnerFont, TRUE);

    // Optional Stop button (only when a handler was set).
    EnsureStopButton();

    // Start timer
    SetTimer(m_hDialog, 1, 60, NULL);
}

void SpinnerDialog::LayoutForText(const std::wstring& text) {
    if (!m_hDialog || !IsWindow(m_hDialog)) return;

    const int padX = 30;
    const int padTop = 18;
    const int padBottom = 22;
    const int gap = 10;
    const int iconSz = 48;
    const int minClientW = 340;
    const int maxClientW = 660;
    const int spinnerFontPt = 26;

    int textWidth = Spinner_MeasureTextWidth(m_hDialog, text);
    int clientW = textWidth + padX * 2;
    if (clientW < minClientW) clientW = minClientW;
    if (clientW > maxClientW) clientW = maxClientW;

    int textAreaW = clientW - padX * 2;
    if (textAreaW < 120) textAreaW = 120;
    int textH = Spinner_MeasureTextHeight(m_hDialog, text, textAreaW);
    if (textH < 20) textH = 20;
    // Robustly reserve one line-height per line in the message (DrawText can
    // under-measure wrapped "\r\n" text, which clipped the digits before).
    {
        int lineH = Spinner_MeasureFontHeight(m_hDialog, 12, L"Segoe UI");
        if (lineH < 16) lineH = 16;
        int lines = 1;
        for (wchar_t c : text) if (c == L'\n') ++lines;
        int wantH = lineH * lines + 8;
        if (textH < wantH) textH = wantH;
    }
    int spinnerTextH = Spinner_MeasureFontHeight(m_hDialog, spinnerFontPt, L"Segoe UI Symbol");
    int spinnerSz = std::max(spinnerTextH + 18, 64);

    // Optional Stop button metrics.
    int btnH = 0, btnW = 0;
    if (m_hStopBtn && IsWindow(m_hStopBtn)) {
        int btnFontH = Spinner_MeasureFontHeight(m_hDialog, 12, L"Segoe UI");
        btnH = std::max(btnFontH + 16, 34);
        btnW = Spinner_MeasureTextWidth(m_hDialog, m_stopLabel) + 48;
        if (btnW < 110) btnW = 110;
        if (clientW < btnW + padX * 2) {
            clientW = btnW + padX * 2;
            if (clientW > maxClientW) clientW = maxClientW;
            textAreaW = clientW - padX * 2;
            if (textAreaW < 120) textAreaW = 120;
        }
    }

    // Extra breathing room requested between the animation and the Stop button.
    const int btnExtra = 60;
    int clientH = padTop + iconSz + gap + textH + gap + spinnerSz;
    if (btnH) clientH += gap * 2 + btnExtra + btnH;
    clientH += padBottom;   // breathing room below whatever is last (button or spinner)

    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    int x = 0;
    int y = 0;
    if (m_hParent && IsWindow(m_hParent)) {
        RECT parentRc;
        GetWindowRect(m_hParent, &parentRc);
        x = parentRc.left + (parentRc.right - parentRc.left - winW) / 2;
        y = parentRc.top + (parentRc.bottom - parentRc.top - winH) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;
    }

    // Keep current z-order (SWP_NOZORDER): the dialog is owned by its parent, so it
    // already floats above the owner.  Re-raising it here would keep pulling the
    // owner window in front of whatever the user switched to (focus theft).
    SetWindowPos(m_hDialog, NULL, x, y, winW, winH, SWP_NOACTIVATE | SWP_NOZORDER);

    int iconX = (clientW - iconSz) / 2;
    int textX = padX;
    int textY = padTop + iconSz + gap;
    int spinX = (clientW - spinnerSz) / 2;
    int spinY = textY + textH + gap;

    if (HWND hIconCtrl = GetWindow(m_hDialog, GW_CHILD)) {
        SetWindowPos(hIconCtrl, NULL, iconX, padTop, iconSz, iconSz, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hTextCtrl) {
        SetWindowPos(m_hTextCtrl, NULL, textX, textY, textAreaW, textH + 8, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hSpinnerCtrl) {
        SetWindowPos(m_hSpinnerCtrl, NULL, spinX, spinY, spinnerSz, spinnerSz, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hStopBtn && IsWindow(m_hStopBtn)) {
        int btnX = (clientW - btnW) / 2;
        int btnY = spinY + spinnerSz + gap * 2 + btnExtra;
        SetWindowPos(m_hStopBtn, NULL, btnX, btnY, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void SpinnerDialog::Show(const std::wstring& text) {
    CreateDialogWindow();
    if (!m_hDialog) return;
    
    SetText(text);
    LayoutForText(text);
    ShowWindow(m_hDialog, SW_SHOWNA);   // show without stealing focus (keep editing)
    UpdateWindow(m_hDialog);
    m_visible = true;
    
    // Process a few messages to let dialog initialize
    MSG msg;
    for (int i = 0; i < 5; i++) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16);
    }
}

void SpinnerDialog::Hide() {
    if (m_hDialog && IsWindow(m_hDialog)) {
        KillTimer(m_hDialog, 1);
        g_spinnerInstances.erase(m_hDialog);
        DestroyWindow(m_hDialog);        // destroys child controls too
        m_hDialog = NULL;
        m_hSpinnerCtrl = NULL;
        m_hTextCtrl = NULL;
        m_hStopBtn = NULL;
    }
    if (m_hStopFont) { DeleteObject(m_hStopFont); m_hStopFont = NULL; }
    m_visible = false;
}

void SpinnerDialog::SetText(const std::wstring& text) {
    if (m_hTextCtrl && IsWindow(m_hTextCtrl)) {
        SetWindowTextW(m_hTextCtrl, text.c_str());
        UpdateWindow(m_hTextCtrl);
    }
    LayoutForText(text);
}

bool SpinnerDialog::IsVisible() const {
    return m_visible && m_hDialog && IsWindow(m_hDialog);
}

// Create the owner-drawn Stop button once a handler is set and the dialog exists.
void SpinnerDialog::EnsureStopButton() {
    if (!m_onStop) return;                       // no handler → plain spinner
    if (!m_hDialog || !IsWindow(m_hDialog)) return;
    if (m_hStopBtn && IsWindow(m_hStopBtn)) return;   // already created

    HINSTANCE hInstance = GetModuleHandle(NULL);
    m_hStopBtn = CreateWindowExW(0, L"BUTTON", m_stopLabel.c_str(),
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 110, 34, m_hDialog, (HMENU)(UINT_PTR)SPINNER_STOP_BTN_ID, hInstance, NULL);
    if (!m_hStopFont)
        m_hStopFont = Spinner_CreatePointFont(m_hDialog, 12, L"Segoe UI");
    if (m_hStopBtn && m_hStopFont)
        SendMessageW(m_hStopBtn, WM_SETFONT, (WPARAM)m_hStopFont, TRUE);
}

void SpinnerDialog::SetStopButton(const std::wstring& label, std::function<void()> onStop) {
    m_stopLabel = label;
    m_onStop = std::move(onStop);
    if (m_hDialog && IsWindow(m_hDialog)) {
        EnsureStopButton();
        // Re-layout so the new button gets room and a position.
        wchar_t buf[512] = {};
        if (m_hTextCtrl && IsWindow(m_hTextCtrl)) GetWindowTextW(m_hTextCtrl, buf, 512);
        LayoutForText(buf);
    }
}

LRESULT CALLBACK SpinnerDialog::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hWhiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    
    // Get instance from map
    auto it = g_spinnerInstances.find(hwnd);
    SpinnerDialog* pThis = (it != g_spinnerInstances.end()) ? it->second : nullptr;
    
    switch (uMsg) {
        case WM_TIMER:
            if (wParam == 1 && pThis) {
                const wchar_t* frames[] = { L"◐", L"◓", L"◑", L"◒" };
                pThis->m_spinnerFrame = (pThis->m_spinnerFrame + 1) % 4;
                if (pThis->m_hSpinnerCtrl && IsWindow(pThis->m_hSpinnerCtrl)) {
                    SetWindowTextW(pThis->m_hSpinnerCtrl, frames[pThis->m_spinnerFrame]);
                }
            }
            return 0;
            
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hStatic = (HWND)lParam;
            
            SetBkMode(hdcStatic, OPAQUE);
            SetBkColor(hdcStatic, RGB(255, 255, 255));
            
            // Make spinner blue
            if (pThis && hStatic == pThis->m_hSpinnerCtrl) {
                SetTextColor(hdcStatic, RGB(0, 120, 215));
            } else {
                SetTextColor(hdcStatic, RGB(0, 0, 0));
            }
            
            return (LRESULT)hWhiteBrush;
        }

        case WM_DRAWITEM: {
            if (wParam == SPINNER_STOP_BTN_ID && pThis) {
                DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
                RECT rc = dis->rcItem;
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF fill = pressed ? RGB(150, 30, 30) : RGB(190, 55, 55);
                HBRUSH hb = CreateSolidBrush(fill);
                HPEN   hp = CreatePen(PS_SOLID, 1, RGB(120, 25, 25));
                HGDIOBJ ob = SelectObject(dis->hDC, hb);
                HGDIOBJ op = SelectObject(dis->hDC, hp);
                RoundRect(dis->hDC, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
                SelectObject(dis->hDC, op);
                SelectObject(dis->hDC, ob);
                DeleteObject(hb);
                DeleteObject(hp);

                wchar_t label[256] = {};
                GetWindowTextW(dis->hwndItem, label, 256);
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, RGB(255, 255, 255));
                HFONT hf = pThis->m_hStopFont;
                HGDIOBJ of = hf ? SelectObject(dis->hDC, hf) : NULL;
                DrawTextW(dis->hDC, label, -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                if (of) SelectObject(dis->hDC, of);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == SPINNER_STOP_BTN_ID && pThis && pThis->m_onStop) {
                // Copy the handler before invoking: it may Hide()/destroy us.
                std::function<void()> cb = pThis->m_onStop;
                cb();
                return 0;
            }
            break;

        case WM_CLOSE:
            // Don't allow user to close
            return 0;
    }
    
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
