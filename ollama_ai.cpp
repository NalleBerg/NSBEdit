#include "ollama_ai.h"

#include "dpi.h"
#include "ne_ai_client.h"
#include "ne_ai_bootstrap.h"
#include "ne_profiles.h"

#include <algorithm>
#include <gdiplus.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <string>

#define IDC_AI_HEADER             1951
#define IDC_AI_LOG                1952
#define IDC_AI_INPUT              1953
#define IDC_AI_SEND_BTN           1954
#define IDC_AI_COPY_BTN           1955
#define IDC_AI_CLEAR_BTN          1956
#define IDC_AI_STATUS             1957
#define IDC_AI_CLOSE_BTN          1958

#define IDM_AI_MODEL_DEFAULT      1901
#define IDM_AI_MODEL_FALLBACK     1902
#define IDM_AI_MODE_LOCAL         1910
#define IDM_AI_MODE_CLOUD         1911
#define IDM_AI_SIGN_IN            1920
#define IDM_AI_SIGN_OUT           1921
#define IDM_AI_OPEN_PROVIDER      1922
#define IDM_AI_LOG_CLEAR          1930
#define IDM_AI_LOG_COPY           1931
#define IDM_AI_SEND               1932
#define IDM_AI_ABOUT              1933

namespace {

constexpr WORD kSystemIconInfo = 32516;
constexpr WORD kSystemIconError = 32513;
constexpr WORD kSystemArrowCursor = 32512;

enum class AiBtnTone { Blue, Green, Red };

struct AiButtonSpec {
    int id = 0;
    std::wstring text;
    AiBtnTone tone = AiBtnTone::Blue;
    int width = 0;
    bool useOllamaImage = false;
    bool useCopyGlyph = false;
};

struct AiDialogData {
    int buttonCount = 0;
    AiButtonSpec buttons[4];
};

struct AiWindowState {
    HWND hHeader = NULL;
    HWND hLog = NULL;
    HWND hInput = NULL;
    HWND hStatus = NULL;
    HWND hSendBtn = NULL;
    HWND hCopyBtn = NULL;
    HWND hClearBtn = NULL;
    HWND hCloseBtn = NULL;
    HFONT hFont = NULL;
    HFONT hPaneFont = NULL;
    AiDialogData* dd = NULL;
    std::wstring model;
    std::wstring fallback;
    bool cloudMode = false;
    bool signedIn = false;
};

static HWND s_hwndAiWindow = NULL;

static std::wstring Ai_GetExeDir()
{
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) return {};
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (!slash) return {};
    *(slash + 1) = L'\0';
    return exePath;
}

static Gdiplus::Image* Ai_GetOllamaButtonImage()
{
    static Gdiplus::Image* s_ollamaImage = NULL;
    static bool s_ollamaImageTried = false;
    if (!s_ollamaImageTried) {
        s_ollamaImageTried = true;
        std::wstring imagePath = Ai_GetExeDir();
        if (!imagePath.empty()) {
            imagePath += L"ollama.png";
            s_ollamaImage = Gdiplus::Image::FromFile(imagePath.c_str(), FALSE);
        }
        if (s_ollamaImage && s_ollamaImage->GetLastStatus() != Gdiplus::Ok) {
            delete s_ollamaImage;
            s_ollamaImage = NULL;
        }
    }
    return s_ollamaImage;
}

static std::string Ai_WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 1) return {};
    std::string out((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, NULL, NULL);
    return out;
}

static std::wstring Ai_DefaultModelName()
{
    const NeAiBootstrapConfig& bootstrap = NeAiBootstrap_Get();
    return bootstrap.model.empty() ? L"qwen2.5-coder:7b" : bootstrap.model;
}

static std::wstring Ai_FallbackModelName()
{
    const NeAiBootstrapConfig& bootstrap = NeAiBootstrap_Get();
    return bootstrap.fallback.empty() ? L"qwen2.5-coder:3b" : bootstrap.fallback;
}

static void Ai_NormalizeModelName(std::wstring& model)
{
    if (model == L"qwen2.5-coder:7b-instruct" || model == L"qwen2.5-coder") {
        model = L"qwen2.5-coder:7b";
    } else if (model == L"qwen2.5-coder:3b-instruct") {
        model = L"qwen2.5-coder:3b";
    }
}

static void Ai_DoSend(HWND hwnd);

static HFONT Ai_MakeDlgFont(HWND hwnd, bool bold = false)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
    ncm.lfMessageFont.lfHeight = -MulDiv(12, dpi > 0 ? dpi : 96, 72);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    if (bold) ncm.lfMessageFont.lfWeight = FW_BOLD;
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

static HFONT Ai_MakePaneFont(HWND hwnd)
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
    ncm.lfMessageFont.lfHeight = -MulDiv(11, dpi > 0 ? dpi : 96, 72);
    ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
    lstrcpyW(ncm.lfMessageFont.lfFaceName, L"Consolas");
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

static int Ai_MeasureButtonWidth(const std::wstring& text)
{
    HDC hdc = GetDC(NULL);
    if (!hdc) return S(120);
    HFONT hf = Ai_MakeDlgFont(NULL, true);
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    if (old) SelectObject(hdc, old);
    if (hf) DeleteObject(hf);
    ReleaseDC(NULL, hdc);
    int w = S(16) + S(8) + sz.cx + S(24);
    return std::max(w, S(120));
}

static COLORREF Ai_ToneColor(AiBtnTone tone, bool pressed, bool hover)
{
    switch (tone) {
    case AiBtnTone::Green:
        return pressed ? RGB(100, 160, 100) : hover ? RGB(155, 205, 155) : RGB(175, 215, 175);
    case AiBtnTone::Red:
        return pressed ? RGB(190, 100, 100) : hover ? RGB(225, 145, 145) : RGB(235, 175, 175);
    default:
        return pressed ? RGB(90, 155, 215) : hover ? RGB(130, 185, 230) : RGB(160, 205, 240);
    }
}

static int Ai_ButtonIndexById(const AiDialogData* dd, int id)
{
    if (!dd) return -1;
    for (int i = 0; i < dd->buttonCount; ++i) {
        if (dd->buttons[i].id == id) return i;
    }
    return -1;
}

static void Ai_DrawButton(const DRAWITEMSTRUCT* dis, const AiDialogData* dd)
{
    if (!dis || !dd) return;
    int idx = Ai_ButtonIndexById(dd, dis->CtlID);
    if (idx < 0) return;

    const AiButtonSpec& b = dd->buttons[idx];
    RECT rc = dis->rcItem;
    HDC hdc = dis->hDC;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool hover = (GetPropW(dis->hwndItem, L"AiHover") != NULL);
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    COLORREF bgCol = disabled ? RGB(210, 210, 210) : Ai_ToneColor(b.tone, pressed, hover);
    HBRUSH hb = CreateSolidBrush(bgCol);
    FillRect(hdc, &rc, hb);
    DeleteObject(hb);
    FrameRect(hdc, &rc, GetSysColorBrush(COLOR_3DSHADOW));

    HFONT hf = Ai_MakeDlgFont(NULL, true);
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;

    SIZE ts = {};
    GetTextExtentPoint32W(hdc, b.text.c_str(), (int)b.text.size(), &ts);
    int contentW = S(16) + S(8) + ts.cx;
    int startX = rc.left + ((rc.right - rc.left) - contentW) / 2;
    int iconY = rc.top + ((rc.bottom - rc.top) - S(16)) / 2;

    if (b.useOllamaImage) {
        Gdiplus::Image* aiImg = Ai_GetOllamaButtonImage();
        if (aiImg) {
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            int iconSz = std::min(rc.right - rc.left, rc.bottom - rc.top) - S(8);
            if (iconSz > S(18)) iconSz = S(18);
            if (iconSz < S(12)) iconSz = S(12);
            int x = startX + (S(16) - iconSz) / 2;
            int y = iconY + (S(16) - iconSz) / 2;
            g.DrawImage(aiImg, x, y, iconSz, iconSz);
        }
    } else if (b.useCopyGlyph) {
        COLORREF edge = disabled ? RGB(150, 150, 150) : RGB(70, 70, 70);
        COLORREF fill = disabled ? RGB(235, 235, 235) : RGB(250, 250, 250);
        HPEN hPen = CreatePen(PS_SOLID, 1, edge);
        HBRUSH hFill = CreateSolidBrush(fill);
        HPEN oldPen = (HPEN)SelectObject(hdc, hPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hFill);

        RECT back = { startX + S(4), iconY + S(3), startX + S(14), iconY + S(13) };
        Rectangle(hdc, back.left, back.top, back.right, back.bottom);
        RECT front = { startX, iconY, startX + S(10), iconY + S(10) };
        Rectangle(hdc, front.left, front.top, front.right, front.bottom);
        MoveToEx(hdc, front.left + S(2), front.top + S(3), NULL);
        LineTo(hdc, front.right - S(2), front.top + S(3));
        MoveToEx(hdc, front.left + S(2), front.top + S(5), NULL);
        LineTo(hdc, front.right - S(2), front.top + S(5));

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hFill);
        DeleteObject(hPen);
    } else {
        HICON hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(b.id == IDC_AI_CLEAR_BTN ? kSystemIconError : kSystemIconInfo));
        if (hIcon) DrawIconEx(hdc, startX, iconY, hIcon, S(16), S(16), 0, NULL, DI_NORMAL);
    }

    RECT tr = rc;
    tr.left = startX + S(16) + S(8);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, disabled ? RGB(150, 150, 150) : RGB(25, 25, 25));
    DrawTextW(hdc, b.text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

    if (dis->itemState & ODS_FOCUS) {
        RECT fr = rc;
        InflateRect(&fr, -S(4), -S(4));
        DrawFocusRect(hdc, &fr);
    }

    if (old) SelectObject(hdc, old);
    if (hf) DeleteObject(hf);
}

static void Ai_SavePrefs(const AiWindowState* st)
{
    if (!st) return;
    NeProfiles_SetStrSetting("ai.model", Ai_WideToUtf8(st->model));
    NeProfiles_SetStrSetting("ai.fallback", Ai_WideToUtf8(st->fallback));
    NeProfiles_SetIntSetting("ai.cloud_mode", st->cloudMode ? 1 : 0);
    NeProfiles_SetIntSetting("ai.signed_in", st->signedIn ? 1 : 0);
}

static void Ai_LoadPrefs(AiWindowState* st)
{
    if (!st) return;
    st->model = Ai_DefaultModelName();
    st->fallback = Ai_FallbackModelName();

    std::string saved;
    if (NeProfiles_GetStrSetting("ai.model", "", saved) && !saved.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, saved.c_str(), -1, NULL, 0);
        if (wlen > 1) {
            std::wstring wide((size_t)wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, saved.c_str(), -1, &wide[0], wlen);
            wide.resize((size_t)wlen - 1);
            st->model = std::move(wide);
        }
    }

    saved.clear();
    if (NeProfiles_GetStrSetting("ai.fallback", "", saved) && !saved.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, saved.c_str(), -1, NULL, 0);
        if (wlen > 1) {
            std::wstring wide((size_t)wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, saved.c_str(), -1, &wide[0], wlen);
            wide.resize((size_t)wlen - 1);
            st->fallback = std::move(wide);
        }
    }

    Ai_NormalizeModelName(st->model);
    Ai_NormalizeModelName(st->fallback);

    int mode = 0, signedIn = 0;
    NeProfiles_GetIntSetting("ai.cloud_mode", 0, mode);
    NeProfiles_GetIntSetting("ai.signed_in", 0, signedIn);
    st->cloudMode = (mode != 0);
    st->signedIn = (signedIn != 0);
}

static std::wstring Ai_BuildSummary(const AiWindowState* st)
{
    std::wstring summary;
    summary += L"Ollama is answering on localhost.\r\n";
    summary += L"Model in use: ";
    summary += ((st && !st->model.empty()) ? st->model : Ai_DefaultModelName());
    summary += L"\r\nFallback model: ";
    summary += ((st && !st->fallback.empty()) ? st->fallback : Ai_FallbackModelName());
    summary += L"\r\nCloud: ";
    summary += ((st && st->cloudMode) ? L"Cloud subscription" : L"Local only");
    summary += L"\r\nCloud account: ";
    summary += ((st && st->signedIn) ? L"signed in" : L"not signed in");
    return summary;
}

static void Ai_RefreshUi(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;

    std::wstring title = L"NSBEdit AI - ";
    title += (st->model.empty() ? Ai_DefaultModelName() : st->model);
    SetWindowTextW(hwnd, title.c_str());

    HWND hHdr = GetDlgItem(hwnd, IDC_AI_HEADER);
    if (hHdr) SetWindowTextW(hHdr, Ai_BuildSummary(st).c_str());

    HWND hStatus = GetDlgItem(hwnd, IDC_AI_STATUS);
    if (hStatus) {
        std::wstring status = L"Cloud: ";
        status += (st->cloudMode ? L"Cloud subscription" : L"Local only");
        status += L" | Sign-in: ";
        status += (st->signedIn ? L"yes" : L"no");
        status += L" | Current model: ";
        status += (st->model.empty() ? Ai_DefaultModelName() : st->model);
        SetWindowTextW(hStatus, status.c_str());
    }

    HMENU hMenu = GetMenu(hwnd);
    if (hMenu) {
        HMENU hModel = GetSubMenu(hMenu, 0);
        HMENU hCloud = GetSubMenu(hMenu, 1);
        if (hModel) {
            CheckMenuItem(hModel, IDM_AI_MODEL_DEFAULT,
                MF_BYCOMMAND | ((st->model == Ai_DefaultModelName()) ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(hModel, IDM_AI_MODEL_FALLBACK,
                MF_BYCOMMAND | ((st->model == Ai_FallbackModelName()) ? MF_CHECKED : MF_UNCHECKED));
        }
        if (hCloud) {
            CheckMenuItem(hCloud, IDM_AI_MODE_LOCAL,
                MF_BYCOMMAND | (st->cloudMode ? MF_UNCHECKED : MF_CHECKED));
            CheckMenuItem(hCloud, IDM_AI_MODE_CLOUD,
                MF_BYCOMMAND | (st->cloudMode ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(hCloud, IDM_AI_SIGN_IN,
                MF_BYCOMMAND | (st->signedIn ? MF_CHECKED : MF_UNCHECKED));
            CheckMenuItem(hCloud, IDM_AI_SIGN_OUT,
                MF_BYCOMMAND | (st->signedIn ? MF_UNCHECKED : MF_CHECKED));
        }
        DrawMenuBar(hwnd);
    }
}

static void Ai_AppendLog(HWND hwnd, const std::wstring& line)
{
    HWND hLog = GetDlgItem(hwnd, IDC_AI_LOG);
    if (!hLog) return;
    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    if (len > 0) SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

static LRESULT CALLBACK Ai_InputSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR dwRefData)
{
    bool plainEnter = !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000);
    if (msg == WM_KEYDOWN && wParam == VK_RETURN && plainEnter) {
        HWND hParent = (HWND)dwRefData;
        if (hParent) {
            Ai_DoSend(hParent);
            return 0;
        }
    }
    if (msg == WM_CHAR && wParam == L'\r' && plainEnter) {
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void Ai_ApplyButtons(AiWindowState* st)
{
    if (!st || !st->dd) return;
    st->dd->buttonCount = 4;
    st->dd->buttons[0] = AiButtonSpec{ IDC_AI_SEND_BTN,  L"Send",  AiBtnTone::Green, Ai_MeasureButtonWidth(L"Send"),  true };
    st->dd->buttons[1] = AiButtonSpec{ IDC_AI_COPY_BTN,  L"Copy",  AiBtnTone::Blue,  Ai_MeasureButtonWidth(L"Copy"),  false, true };
    st->dd->buttons[2] = AiButtonSpec{ IDC_AI_CLEAR_BTN, L"Clear", AiBtnTone::Red,   Ai_MeasureButtonWidth(L"Clear"), true };
    st->dd->buttons[3] = AiButtonSpec{ IDC_AI_CLOSE_BTN, L"Close", AiBtnTone::Red,   Ai_MeasureButtonWidth(L"Close"), true };
}

static HMENU Ai_BuildMenu()
{
    HMENU hMenu = CreateMenu();
    HMENU hModel = CreatePopupMenu();
    HMENU hCloud = CreatePopupMenu();
    HMENU hLog = CreatePopupMenu();
    HMENU hHelp = CreatePopupMenu();

    std::wstring modelLabel = L"Suggestion: ";
    modelLabel += Ai_DefaultModelName();
    AppendMenuW(hModel, MF_STRING, IDM_AI_MODEL_DEFAULT, modelLabel.c_str());

    std::wstring fbLabel = L"Agent: ";
    fbLabel += Ai_FallbackModelName();
    AppendMenuW(hModel, MF_STRING, IDM_AI_MODEL_FALLBACK, fbLabel.c_str());

    AppendMenuW(hCloud, MF_STRING, IDM_AI_MODE_LOCAL, L"Local only");
    AppendMenuW(hCloud, MF_STRING, IDM_AI_MODE_CLOUD, L"Cloud subscription");
    AppendMenuW(hCloud, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hCloud, MF_STRING, IDM_AI_SIGN_IN, L"Sign in to Ollama");
    AppendMenuW(hCloud, MF_STRING, IDM_AI_SIGN_OUT, L"Sign out");
    AppendMenuW(hCloud, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hCloud, MF_STRING, IDM_AI_OPEN_PROVIDER, L"Open Ollama web site");

    AppendMenuW(hLog, MF_STRING, IDM_AI_SEND, L"Send prompt");
    AppendMenuW(hLog, MF_STRING, IDM_AI_LOG_CLEAR, L"Clear log");
    AppendMenuW(hLog, MF_STRING, IDM_AI_LOG_COPY, L"Copy log");

    AppendMenuW(hHelp, MF_STRING, IDM_AI_ABOUT, L"About this window");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hModel, L"Model");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hCloud, L"Cloud");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hLog, L"Log");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hHelp, L"Help");
    return hMenu;
}

static void Ai_DoSend(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;

    HWND hInput = GetDlgItem(hwnd, IDC_AI_INPUT);
    wchar_t prompt[2048] = {};
    if (hInput) GetWindowTextW(hInput, prompt, 2048);
    if (!prompt[0]) return;

    if (hInput) SetWindowTextW(hInput, L"");

    std::wstring userLine = L"You: ";
    userLine += prompt;
    Ai_AppendLog(hwnd, userLine);

    std::wstring formattedPrompt = L"Answer in Markdown. Preserve indentation and use fenced code blocks for any code.\r\n\r\n";
    formattedPrompt += prompt;

    std::wstring reply;
    std::wstring error;
    std::wstring selectedModel = st->model.empty() ? Ai_DefaultModelName() : st->model;
    if (NeAiClient_AskOllama(selectedModel, formattedPrompt, reply, error)) {
        Ai_AppendLog(hwnd, L"Ollama: " + reply);
    } else {
        std::wstring fallbackModel = st->fallback.empty() ? Ai_FallbackModelName() : st->fallback;
        bool modelMissing = !error.empty() &&
            (error.find(L"not found") != std::wstring::npos ||
             error.find(L"does not exist") != std::wstring::npos ||
             error.find(L"pull the model") != std::wstring::npos);
        if (modelMissing && fallbackModel != selectedModel) {
            Ai_AppendLog(hwnd, L"Ollama: selected model not found, retrying with fallback model.");
            error.clear();
            if (NeAiClient_AskOllama(fallbackModel, formattedPrompt, reply, error)) {
                Ai_AppendLog(hwnd, L"Ollama: " + reply);
                return;
            }
        }
        Ai_AppendLog(hwnd, L"Ollama: " + (error.empty() ? L"No response." : error));
    }
}

static LRESULT CALLBACK Ai_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lParam;
        st = (AiWindowState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        if (!st) return -1;

        st->dd = new AiDialogData();
        if (!st->dd) return -1;

        st->hFont = Ai_MakeDlgFont(hwnd, false);
        st->hPaneFont = Ai_MakePaneFont(hwnd);
        Ai_ApplyButtons(st);

        LoadLibraryW(L"Msftedit.dll");
        const wchar_t* reClass = L"EDIT";
        HMODULE hMsft = GetModuleHandleW(L"Msftedit.dll");
        if (hMsft) {
            WNDCLASSEXW wce = { sizeof(wce) };
            if (GetClassInfoExW(hMsft, L"RICHEDIT50W", &wce)) {
                reClass = L"RICHEDIT50W";
            }
        }

        SetMenu(hwnd, Ai_BuildMenu());

        HWND hHdr = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(10), S(10), S(800), S(72), hwnd, (HMENU)(UINT_PTR)IDC_AI_HEADER, GetModuleHandleW(NULL), NULL);
        if (hHdr && st->hFont) SendMessageW(hHdr, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        HWND hLog = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL | ES_READONLY | ES_NOHIDESEL,
            S(10), S(86), S(820), S(340), hwnd, (HMENU)(UINT_PTR)IDC_AI_LOG, GetModuleHandleW(NULL), NULL);
        if (hLog && st->hPaneFont) SendMessageW(hLog, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
        if (hLog) {
            SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
        }

        HWND hInput = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
            S(10), S(438), S(640), S(24), hwnd, (HMENU)(UINT_PTR)IDC_AI_INPUT, GetModuleHandleW(NULL), NULL);
        if (hInput && st->hPaneFont) SendMessageW(hInput, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
        if (hInput) SetWindowSubclass(hInput, Ai_InputSubclassProc, 1, (DWORD_PTR)hwnd);

        int totalBtnW = st->dd->buttons[0].width + S(10) + st->dd->buttons[1].width + S(10) + st->dd->buttons[2].width + S(10) + st->dd->buttons[3].width;
        int btnY = S(438);
        int btnX = S(10) + S(820) - totalBtnW;
        HWND hSend = CreateWindowExW(0, L"BUTTON", st->dd->buttons[0].text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            btnX, btnY, st->dd->buttons[0].width, S(34), hwnd, (HMENU)(UINT_PTR)IDC_AI_SEND_BTN, GetModuleHandleW(NULL), NULL);
        if (hSend && st->hFont) SendMessageW(hSend, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        HWND hCopy = CreateWindowExW(0, L"BUTTON", st->dd->buttons[1].text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            btnX + st->dd->buttons[0].width + S(10), btnY, st->dd->buttons[1].width, S(34), hwnd, (HMENU)(UINT_PTR)IDC_AI_COPY_BTN, GetModuleHandleW(NULL), NULL);
        if (hCopy && st->hFont) SendMessageW(hCopy, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        HWND hClear = CreateWindowExW(0, L"BUTTON", st->dd->buttons[2].text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            btnX + st->dd->buttons[0].width + S(10) + st->dd->buttons[1].width + S(10), btnY, st->dd->buttons[2].width, S(34), hwnd, (HMENU)(UINT_PTR)IDC_AI_CLEAR_BTN, GetModuleHandleW(NULL), NULL);
        if (hClear && st->hFont) SendMessageW(hClear, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        HWND hClose = CreateWindowExW(0, L"BUTTON", st->dd->buttons[3].text.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            btnX + st->dd->buttons[0].width + S(10) + st->dd->buttons[1].width + S(10) + st->dd->buttons[2].width + S(10), btnY, st->dd->buttons[3].width, S(34), hwnd, (HMENU)(UINT_PTR)IDC_AI_CLOSE_BTN, GetModuleHandleW(NULL), NULL);
        if (hClose && st->hFont) SendMessageW(hClose, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        HWND hStatus = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            S(10), S(470), S(820), S(22), hwnd, (HMENU)(UINT_PTR)IDC_AI_STATUS, GetModuleHandleW(NULL), NULL);
        if (hStatus && st->hFont) SendMessageW(hStatus, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        st->hHeader = hHdr;
        st->hLog = hLog;
        st->hInput = hInput;
        st->hStatus = hStatus;
        st->hSendBtn = hSend;
        st->hCopyBtn = hCopy;
        st->hClearBtn = hClear;
        st->hCloseBtn = hClose;

        Ai_RefreshUi(hwnd);
        Ai_AppendLog(hwnd, L"AI window opened.");
        SetFocus(hInput);
        return 0;
    }
    case WM_SIZE: {
        if (!st) break;
        RECT rc; GetClientRect(hwnd, &rc);
        int pad = S(10);
        int hdrH = S(72);
        int statusH = S(18);
        int buttonH = S(34);
        int gap = S(6);
        HWND hHdr = GetDlgItem(hwnd, IDC_AI_HEADER);
        HWND hLog = GetDlgItem(hwnd, IDC_AI_LOG);
        HWND hInput = GetDlgItem(hwnd, IDC_AI_INPUT);
        HWND hSend = GetDlgItem(hwnd, IDC_AI_SEND_BTN);
        HWND hCopy = GetDlgItem(hwnd, IDC_AI_COPY_BTN);
        HWND hClear = GetDlgItem(hwnd, IDC_AI_CLEAR_BTN);
        HWND hClose = GetDlgItem(hwnd, IDC_AI_CLOSE_BTN);
        HWND hStatus = GetDlgItem(hwnd, IDC_AI_STATUS);
        int contentTop = pad + hdrH + gap;
        int contentBottom = std::max(contentTop, (int)rc.bottom - pad - statusH - gap);
        int contentH = std::max(0, contentBottom - contentTop);
        int availableForEditors = std::max(0, contentH - buttonH - gap);
        int inputH = std::max(S(96), availableForEditors / 3);
        int logH = std::max(0, availableForEditors - inputH);
        int inputW = std::max(0, (int)rc.right - 2 * pad);
        int logY = contentTop + inputH + gap + buttonH + gap;
        int btnY = contentTop + inputH + gap;
        int totalBtnW = st->dd->buttons[0].width + S(10) + st->dd->buttons[1].width + S(10) + st->dd->buttons[2].width + S(10) + st->dd->buttons[3].width;
        int btnX = pad + std::max(0, (inputW - totalBtnW) / 2);
        if (hHdr) SetWindowPos(hHdr, NULL, pad, pad, rc.right - 2 * pad, hdrH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hInput) SetWindowPos(hInput, NULL, pad, contentTop, inputW, inputH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hSend) SetWindowPos(hSend, NULL, btnX, btnY, st->dd->buttons[0].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hCopy) SetWindowPos(hCopy, NULL, btnX + st->dd->buttons[0].width + S(10), btnY, st->dd->buttons[1].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hClear) SetWindowPos(hClear, NULL, btnX + st->dd->buttons[0].width + S(10) + st->dd->buttons[1].width + S(10), btnY, st->dd->buttons[2].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hClose) SetWindowPos(hClose, NULL, btnX + st->dd->buttons[0].width + S(10) + st->dd->buttons[1].width + S(10) + st->dd->buttons[2].width + S(10), btnY, st->dd->buttons[3].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hLog) SetWindowPos(hLog, NULL, pad, logY, rc.right - 2 * pad, logH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hStatus) SetWindowPos(hStatus, NULL, pad, rc.bottom - pad - statusH, rc.right - 2 * pad, statusH, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_AI_MODEL_DEFAULT:
            if (st) {
                st->model = Ai_DefaultModelName();
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                Ai_AppendLog(hwnd, L"Model switched to suggestion.");
            }
            return 0;
        case IDM_AI_MODEL_FALLBACK:
            if (st) {
                st->model = Ai_FallbackModelName();
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                Ai_AppendLog(hwnd, L"Model switched to agent.");
            }
            return 0;
        case IDM_AI_MODE_LOCAL:
            if (st) {
                st->cloudMode = false;
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                Ai_AppendLog(hwnd, L"Cloud mode switched to local only.");
            }
            return 0;
        case IDM_AI_MODE_CLOUD:
            if (st) {
                st->cloudMode = true;
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                Ai_AppendLog(hwnd, L"Cloud subscription mode selected.");
            }
            return 0;
        case IDM_AI_SIGN_IN:
            if (st) {
                st->signedIn = true;
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                ShellExecuteW(hwnd, L"open", L"https://ollama.com", NULL, NULL, SW_SHOWNORMAL);
                Ai_AppendLog(hwnd, L"Opened Ollama sign-in / subscription page.");
            }
            return 0;
        case IDM_AI_SIGN_OUT:
            if (st) {
                st->signedIn = false;
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                Ai_AppendLog(hwnd, L"Signed out locally.");
            }
            return 0;
        case IDM_AI_OPEN_PROVIDER:
            ShellExecuteW(hwnd, L"open", L"https://ollama.com", NULL, NULL, SW_SHOWNORMAL);
            return 0;
        case IDM_AI_LOG_CLEAR:
            if (st && st->hLog) SetWindowTextW(st->hLog, L"");
            return 0;
        case IDM_AI_LOG_COPY:
            if (st && st->hLog) {
                int len = GetWindowTextLengthW(st->hLog);
                if (len > 0 && OpenClipboard(hwnd)) {
                    EmptyClipboard();
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (size_t)(len + 1) * sizeof(wchar_t));
                    if (hMem) {
                        wchar_t* p = (wchar_t*)GlobalLock(hMem);
                        if (p) {
                            GetWindowTextW(st->hLog, p, len + 1);
                            GlobalUnlock(hMem);
                            SetClipboardData(CF_UNICODETEXT, hMem);
                        } else {
                            GlobalFree(hMem);
                        }
                    }
                    CloseClipboard();
                }
            }
            return 0;
        case IDM_AI_SEND:
        case IDC_AI_SEND_BTN:
            Ai_DoSend(hwnd);
            return 0;
        case IDC_AI_COPY_BTN:
            SendMessageW(hwnd, WM_COMMAND, IDM_AI_LOG_COPY, 0);
            return 0;
        case IDC_AI_CLEAR_BTN:
            SendMessageW(hwnd, WM_COMMAND, IDM_AI_LOG_CLEAR, 0);
            return 0;
        case IDC_AI_CLOSE_BTN:
            DestroyWindow(hwnd);
            return 0;
        case IDM_AI_ABOUT:
            Ai_AppendLog(hwnd, L"This is the AI shell window. Full Ollama chat wiring will follow.");
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DRAWITEM:
        if (st && st->dd && st->dd->buttonCount > 0) {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis && Ai_ButtonIndexById(st->dd, dis->CtlID) >= 0) {
                Ai_DrawButton(dis, st->dd);
                return TRUE;
            }
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        return 1;
    }
    case WM_DESTROY:
        if (st) {
            if (st->hFont) DeleteObject(st->hFont);
            if (st->hPaneFont) DeleteObject(st->hPaneFont);
            delete st->dd;
            delete st;
        }
        if (s_hwndAiWindow == hwnd) s_hwndAiWindow = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void Ne_ShowAiWindow(HWND parent)
{
    if (s_hwndAiWindow && IsWindow(s_hwndAiWindow)) {
        ShowWindow(s_hwndAiWindow, SW_SHOW);
        SetForegroundWindow(s_hwndAiWindow);
        return;
    }

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = Ai_WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(kSystemArrowCursor));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"NSBEditAIWindow";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc))
        RegisterClassW(&wc);

    RECT pr = {};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    const int clientW = S(920);
    const int clientH = S(620);
    RECT wrc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wrc, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE);
    const int W = wrc.right - wrc.left;
    const int H = wrc.bottom - wrc.top;
    int x = pr.left + std::max(0, (int)((pr.right - pr.left - W) / 2));
    int y = pr.top + std::max(0, (int)((pr.bottom - pr.top - H) / 2));

    auto* st = new AiWindowState();
    Ai_LoadPrefs(st);

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        wc.lpszClassName, L"NSBEdit AI",
        WS_OVERLAPPEDWINDOW,
        x, y, W, H, parent, NULL, hi, st);
    if (!hwnd) {
        delete st;
        return;
    }

    s_hwndAiWindow = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
}
