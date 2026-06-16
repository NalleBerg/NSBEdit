#include "spinner_dialog.h"
#include <algorithm>
#include <map>

// Static map to associate HWND with SpinnerDialog instance
static std::map<HWND, SpinnerDialog*> g_spinnerInstances;

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
    
    // Create dialog window
    m_hDialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"SpinnerDialogClass",
        L"Please Wait",
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
    SetWindowPos(m_hDialog, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE);
    
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
    
    // Start timer
    SetTimer(m_hDialog, 1, 60, NULL);
}

void SpinnerDialog::LayoutForText(const std::wstring& text) {
    if (!m_hDialog || !IsWindow(m_hDialog)) return;

    const int padX = 20;
    const int padTop = 18;
    const int padBottom = 22;
    const int gap = 10;
    const int iconSz = 48;
    const int minClientW = 260;
    const int maxClientW = 520;
    const int spinnerFontPt = 26;

    int textWidth = Spinner_MeasureTextWidth(m_hDialog, text);
    int clientW = textWidth + padX * 2;
    if (clientW < minClientW) clientW = minClientW;
    if (clientW > maxClientW) clientW = maxClientW;

    int textAreaW = clientW - padX * 2;
    if (textAreaW < 120) textAreaW = 120;
    int textH = Spinner_MeasureTextHeight(m_hDialog, text, textAreaW);
    if (textH < 20) textH = 20;
    int spinnerTextH = Spinner_MeasureFontHeight(m_hDialog, spinnerFontPt, L"Segoe UI Symbol");
    int spinnerSz = std::max(spinnerTextH + 18, 64);

    int clientH = padTop + iconSz + gap + textH + gap + spinnerSz + padBottom;

    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION, FALSE, WS_EX_DLGMODALFRAME | WS_EX_TOPMOST);
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

    SetWindowPos(m_hDialog, HWND_TOPMOST, x, y, winW, winH, SWP_NOACTIVATE);

    int iconX = (clientW - iconSz) / 2;
    int textX = padX;
    int textY = padTop + iconSz + gap;
    int spinX = (clientW - spinnerSz) / 2;
    int spinY = textY + textH + gap;

    if (HWND hIconCtrl = GetWindow(m_hDialog, GW_CHILD)) {
        SetWindowPos(hIconCtrl, NULL, iconX, padTop, iconSz, iconSz, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hTextCtrl) {
        SetWindowPos(m_hTextCtrl, NULL, textX, textY, textAreaW, textH + 4, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hSpinnerCtrl) {
        SetWindowPos(m_hSpinnerCtrl, NULL, spinX, spinY, spinnerSz, spinnerSz, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void SpinnerDialog::Show(const std::wstring& text) {
    CreateDialogWindow();
    if (!m_hDialog) return;
    
    SetText(text);
    LayoutForText(text);
    ShowWindow(m_hDialog, SW_SHOW);
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
        DestroyWindow(m_hDialog);
        m_hDialog = NULL;
        m_hSpinnerCtrl = NULL;
        m_hTextCtrl = NULL;
    }
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
        
        case WM_CLOSE:
            // Don't allow user to close
            return 0;
    }
    
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
