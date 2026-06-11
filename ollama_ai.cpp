#include "ollama_ai.h"

#include "dpi.h"
#include "ne_ai_client.h"
#include "ne_ai_bootstrap.h"
#include "ne_profiles.h"
#include "scroll/my_scrollbar_vscroll.h"

#include <algorithm>
#include <gdiplus.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <vector>

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
enum class AiMenuRole { Suggest, Agent };

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
    HMSB hLogSb = NULL;
    HMSB hInputSb = NULL;
    AiDialogData* dd = NULL;
    std::wstring model;
    std::wstring fallback;
    AiMenuRole role = AiMenuRole::Suggest;
    bool cloudMode = false;
    bool signedIn = false;
};

struct AiMenuItemData {
    std::wstring text;
    bool isSeparator = false;
    bool isBar = false;
};

struct AiModelMenuItem {
    UINT id = 0;
    std::wstring model;
    AiMenuRole role = AiMenuRole::Suggest;
    bool isCloud = false;
};

static HWND s_hwndAiWindow = NULL;
static HFONT s_hAiMenuFont = NULL;
static std::vector<AiMenuItemData*> s_aiMenuItems;
static std::vector<AiModelMenuItem> s_aiModelMenuItems;
static UINT s_aiNextModelMenuId = IDM_AI_MODEL_DEFAULT;

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
static void Ai_AppendMenuOD(HMENU hMenu, UINT flags, UINT_PTR id, const wchar_t* text, bool isBar = false);
static HMENU Ai_BuildMenu(const AiWindowState* st);
static std::wstring Ai_CurrentModeLabel(const AiWindowState* st);

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
    NeProfiles_SetIntSetting("ai.role", (int)st->role);
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
    int role = 0;
    NeProfiles_GetIntSetting("ai.role", 0, role);
    st->cloudMode = (mode != 0);
    st->signedIn = (signedIn != 0);
    st->role = (role == (int)AiMenuRole::Agent) ? AiMenuRole::Agent : AiMenuRole::Suggest;
}

static std::wstring Ai_BuildSummary(const AiWindowState* st)
{
    std::wstring summary;
    summary += L"Ollama on localhost | Current: ";
    summary += ((st && !st->model.empty()) ? st->model : Ai_DefaultModelName());
    summary += L" ";
    summary += (st && st->role == AiMenuRole::Agent) ? L"Agent" : L"Suggest";
    summary += L" | Cloud: ";
    summary += ((st && st->cloudMode) ? L"Cloud subscription" : L"Local only");
    summary += L" | Account: ";
    summary += ((st && st->signedIn) ? L"signed in" : L"not signed in");
    return summary;
}

static void Ai_RefreshUi(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;

    std::wstring title = L"NSBEdit AI - ";
    title += Ai_CurrentModeLabel(st);
    SetWindowTextW(hwnd, title.c_str());

    HWND hHdr = GetDlgItem(hwnd, IDC_AI_HEADER);
    if (hHdr) SetWindowTextW(hHdr, Ai_BuildSummary(st).c_str());

    HWND hStatus = GetDlgItem(hwnd, IDC_AI_STATUS);
    if (hStatus) {
        std::wstring status = L"Cloud: ";
        status += (st->cloudMode ? L"Cloud subscription" : L"Local only");
        status += L" | Sign-in: ";
        status += (st->signedIn ? L"yes" : L"no");
        status += L" | Current: ";
        status += Ai_CurrentModeLabel(st);
        SetWindowTextW(hStatus, status.c_str());
    }

    HMENU oldMenu = GetMenu(hwnd);
    HMENU newMenu = Ai_BuildMenu(st);
    if (newMenu) {
        SetMenu(hwnd, newMenu);
        if (oldMenu) {
            DestroyMenu(oldMenu);
        }
        DrawMenuBar(hwnd);
    }
}

static void Ai_AppendLog(HWND hwnd, const std::wstring& line)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    HWND hLog = GetDlgItem(hwnd, IDC_AI_LOG);
    if (!hLog) return;
    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    if (len > 0) SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
    if (st && st->hLogSb) msb_notify_content_changed(st->hLogSb);
}

static void Ai_SetWrapToWindow(HWND hwndEdit)
{
    if (hwndEdit) {
        SendMessageW(hwndEdit, EM_SETTARGETDEVICE, (WPARAM)NULL, 0);
    }
}

static void Ai_ClearMenuStorage()
{
    for (AiMenuItemData* item : s_aiMenuItems) {
        delete item;
    }
    s_aiMenuItems.clear();
    s_aiModelMenuItems.clear();
    s_aiNextModelMenuId = IDM_AI_MODEL_DEFAULT;
}

static void Ai_NormalizeModelNameLocal(std::wstring& model)
{
    if (model == L"qwen2.5-coder:7b-instruct" || model == L"qwen2.5-coder") {
        model = L"qwen2.5-coder:7b";
    } else if (model == L"qwen2.5-coder:3b-instruct") {
        model = L"qwen2.5-coder:3b";
    }
}

static void Ai_AddUniqueModel(std::vector<std::wstring>& models, const std::wstring& model)
{
    if (model.empty()) return;
    for (const std::wstring& existing : models) {
        if (existing == model) return;
    }
    models.push_back(model);
}

static std::vector<std::wstring> Ai_GetModelCandidates()
{
    std::vector<std::wstring> installed;
    std::vector<std::wstring> models;
    if (NeAiClient_ListOllamaModels(installed)) {
        for (std::wstring& model : installed) {
            Ai_NormalizeModelNameLocal(model);
        }
    }

    std::wstring primary = Ai_DefaultModelName();
    std::wstring secondary = Ai_FallbackModelName();
    Ai_NormalizeModelNameLocal(primary);
    Ai_NormalizeModelNameLocal(secondary);
    Ai_AddUniqueModel(models, primary);
    Ai_AddUniqueModel(models, secondary);

    for (const std::wstring& model : installed) {
        Ai_AddUniqueModel(models, model);
    }

    return models;
}

static std::wstring Ai_ModelMenuLabel(const std::wstring& model, AiMenuRole role)
{
    std::wstring label = model;
    label += (role == AiMenuRole::Suggest) ? L" Suggest" : L" Agent";
    return label;
}

static std::wstring Ai_CurrentModeLabel(const AiWindowState* st)
{
    if (!st) return Ai_DefaultModelName() + L" Suggest";
    if (st->cloudMode) {
        return std::wstring(L"Cloud ") + ((st->role == AiMenuRole::Suggest) ? L"Suggest" : L"Agent");
    }
    std::wstring label = st->model.empty() ? Ai_DefaultModelName() : st->model;
    label += (st->role == AiMenuRole::Suggest) ? L" Suggest" : L" Agent";
    return label;
}

static void Ai_AddModelMenuItem(HMENU hMenu, const std::wstring& model, AiMenuRole role, bool isCloud = false, bool enabled = true)
{
    AiModelMenuItem entry;
    entry.id = s_aiNextModelMenuId++;
    entry.model = model;
    entry.role = role;
    entry.isCloud = isCloud;
    s_aiModelMenuItems.push_back(entry);
    UINT flags = MF_STRING;
    if (!enabled) {
        flags |= MF_GRAYED;
    }
    Ai_AppendMenuOD(hMenu, flags, entry.id, Ai_ModelMenuLabel(model, role).c_str());
}

static const AiModelMenuItem* Ai_FindModelMenuItem(UINT id)
{
    for (const AiModelMenuItem& item : s_aiModelMenuItems) {
        if (item.id == id) return &item;
    }
    return NULL;
}

static HFONT Ai_MakeMenuFont(HWND hwnd)
{
    int dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
    return CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
}

static void Ai_AppendMenuOD(HMENU hMenu, UINT flags, UINT_PTR id, const wchar_t* text, bool isBar)
{
    AiMenuItemData* d = new AiMenuItemData();
    d->text = text ? text : L"";
    d->isSeparator = (flags & MF_SEPARATOR) != 0;
    d->isBar = isBar;
    s_aiMenuItems.push_back(d);
    AppendMenuW(hMenu, flags | MF_OWNERDRAW, id, (LPCWSTR)d);
}

static bool Ai_DrawMenuItem(HWND hwnd, const DRAWITEMSTRUCT* dis)
{
    if (!dis || dis->CtlType != ODT_MENU) return false;
    AiMenuItemData* d = (AiMenuItemData*)(ULONG_PTR)dis->itemData;
    if (!d) return false;

    if (!s_hAiMenuFont) s_hAiMenuFont = Ai_MakeMenuFont(hwnd);

    RECT rc = dis->rcItem;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;
    bool disabled = (dis->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    bool checked = (dis->itemState & ODS_CHECKED) != 0;

    if (d->isSeparator) {
        FillRect(dis->hDC, &rc, GetSysColorBrush(d->isBar ? COLOR_MENUBAR : COLOR_MENU));
        int mid = (rc.top + rc.bottom) / 2;
        RECT sep = { rc.left + S(4), mid, rc.right - S(4), mid + 1 };
        FillRect(dis->hDC, &sep, GetSysColorBrush(COLOR_3DSHADOW));
        return true;
    }

    HBRUSH hBg = CreateSolidBrush(selected ? GetSysColor(COLOR_HIGHLIGHT)
        : d->isBar ? GetSysColor(COLOR_MENUBAR)
        : RGB(255, 255, 255));
    FillRect(dis->hDC, &rc, hBg);
    DeleteObject(hBg);

    HFONT oldFont = s_hAiMenuFont ? (HFONT)SelectObject(dis->hDC, s_hAiMenuFont) : NULL;
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT)
        : disabled ? GetSysColor(COLOR_GRAYTEXT)
        : checked ? RGB(0, 140, 0)
        : GetSysColor(COLOR_MENUTEXT));

    const wchar_t* tab = wcschr(d->text.c_str(), L'\t');
    std::wstring main = tab ? std::wstring(d->text.c_str(), tab - d->text.c_str()) : d->text;
    std::wstring accel = tab ? std::wstring(tab + 1) : std::wstring();

    RECT textRc = rc;
    if (d->isBar) {
        textRc.left += S(10);
        textRc.right -= S(10);
        DrawTextW(dis->hDC, main.c_str(), -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    } else {
        if (checked) {
            COLORREF chkFg = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(0, 140, 0);
            SetTextColor(dis->hDC, chkFg);
            HFONT oldChkFont = (HFONT)SelectObject(dis->hDC, s_hAiMenuFont);
            RECT mark = { rc.left, rc.top, rc.left + S(24), rc.bottom };
            DrawTextW(dis->hDC, L"\u2713", -1, &mark, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
            if (oldChkFont) SelectObject(dis->hDC, oldChkFont);
        }
        textRc.left += S(20);
        textRc.right -= S(14);
        DrawTextW(dis->hDC, main.c_str(), -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        if (!accel.empty()) {
            RECT accelRc = rc;
            accelRc.left += S(18);
            accelRc.right -= S(14);
            DrawTextW(dis->hDC, accel.c_str(), -1, &accelRc, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        }
    }

    if (dis->itemState & ODS_FOCUS) {
        RECT focus = rc;
        InflateRect(&focus, -S(2), -S(2));
        DrawFocusRect(dis->hDC, &focus);
    }

    if (oldFont) SelectObject(dis->hDC, oldFont);
    return true;
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

static HMENU Ai_BuildMenu(const AiWindowState* st)
{
    Ai_ClearMenuStorage();
    HMENU hMenu = CreateMenu();
    HMENU hModel = CreatePopupMenu();
    HMENU hCloud = CreatePopupMenu();
    HMENU hLog = CreatePopupMenu();
    HMENU hHelp = CreatePopupMenu();

    std::vector<std::wstring> models = Ai_GetModelCandidates();
    for (const std::wstring& model : models) {
        Ai_AddModelMenuItem(hModel, model, AiMenuRole::Suggest);
        Ai_AddModelMenuItem(hModel, model, AiMenuRole::Agent);
    }

    Ai_AppendMenuOD(hModel, MF_SEPARATOR, 0, NULL);
    Ai_AddModelMenuItem(hModel, L"Cloud", AiMenuRole::Suggest, true, st && st->signedIn);
    Ai_AddModelMenuItem(hModel, L"Cloud", AiMenuRole::Agent, true, st && st->signedIn);

    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_MODE_LOCAL, L"Local only");
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_MODE_CLOUD, L"Cloud subscription");
    Ai_AppendMenuOD(hCloud, MF_SEPARATOR, 0, NULL);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_SIGN_IN, L"Sign in to Ollama");
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_SIGN_OUT, L"Sign out");
    Ai_AppendMenuOD(hCloud, MF_SEPARATOR, 0, NULL);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_OPEN_PROVIDER, L"Open Ollama web site");

    Ai_AppendMenuOD(hLog, MF_STRING, IDM_AI_SEND, L"Send prompt");
    Ai_AppendMenuOD(hLog, MF_STRING, IDM_AI_LOG_CLEAR, L"Clear log");
    Ai_AppendMenuOD(hLog, MF_STRING, IDM_AI_LOG_COPY, L"Copy log");

    Ai_AppendMenuOD(hHelp, MF_STRING, IDM_AI_ABOUT, L"About this window");

    Ai_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hModel, L"Model", true);
    Ai_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hCloud, L"Cloud", true);
    Ai_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hLog, L"Log", true);
    Ai_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hHelp, L"Help", true);

    if (st) {
        for (const AiModelMenuItem& item : s_aiModelMenuItems) {
            bool checked = item.isCloud
                ? (st->cloudMode && st->role == item.role)
                : (!st->cloudMode && st->model == item.model && st->role == item.role);
            if (checked) {
                CheckMenuItem(hModel, item.id, MF_BYCOMMAND | MF_CHECKED);
            }
        }
    }

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
    if (st->cloudMode) {
        if (!st->signedIn) {
            Ai_AppendLog(hwnd, L"Cloud mode is selected, but you are not signed in yet.");
        } else {
            Ai_AppendLog(hwnd, L"Cloud mode is selected; local Ollama is not used in this path yet.");
        }
        return;
    }

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

        SetMenu(hwnd, Ai_BuildMenu(st));

        HWND hHdr = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
            S(10), S(10), S(800), S(24), hwnd, (HMENU)(UINT_PTR)IDC_AI_HEADER, GetModuleHandleW(NULL), NULL);
        if (hHdr && st->hFont) SendMessageW(hHdr, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        HWND hLog = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY | ES_NOHIDESEL,
            S(10), S(86), S(820), S(340), hwnd, (HMENU)(UINT_PTR)IDC_AI_LOG, GetModuleHandleW(NULL), NULL);
        if (hLog && st->hPaneFont) SendMessageW(hLog, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
        if (hLog) {
            SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
            Ai_SetWrapToWindow(hLog);
            st->hLogSb = msb_attach(hLog, MSB_VERTICAL);
        }

        HWND hInput = CreateWindowExW(WS_EX_CLIENTEDGE, reClass, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            S(10), S(438), S(640), S(24), hwnd, (HMENU)(UINT_PTR)IDC_AI_INPUT, GetModuleHandleW(NULL), NULL);
        if (hInput && st->hPaneFont) SendMessageW(hInput, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
        if (hInput) Ai_SetWrapToWindow(hInput);
        if (hInput) st->hInputSb = msb_attach(hInput, MSB_VERTICAL);
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
        int hdrH = S(24);
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
        if (hInput) Ai_SetWrapToWindow(hInput);
        if (st->hInputSb) msb_reposition(st->hInputSb);
        if (hSend) SetWindowPos(hSend, NULL, btnX, btnY, st->dd->buttons[0].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hCopy) SetWindowPos(hCopy, NULL, btnX + st->dd->buttons[0].width + S(10), btnY, st->dd->buttons[1].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hClear) SetWindowPos(hClear, NULL, btnX + st->dd->buttons[0].width + S(10) + st->dd->buttons[1].width + S(10), btnY, st->dd->buttons[2].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hClose) SetWindowPos(hClose, NULL, btnX + st->dd->buttons[0].width + S(10) + st->dd->buttons[1].width + S(10) + st->dd->buttons[2].width + S(10), btnY, st->dd->buttons[3].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hLog) SetWindowPos(hLog, NULL, pad, logY, rc.right - 2 * pad, logH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hLog) Ai_SetWrapToWindow(hLog);
        if (st->hLogSb) msb_reposition(st->hLogSb);
        if (hStatus) SetWindowPos(hStatus, NULL, pad, rc.bottom - pad - statusH, rc.right - 2 * pad, statusH, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_COMMAND:
        if (st && HIWORD(wParam) == EN_CHANGE) {
            if (LOWORD(wParam) == IDC_AI_INPUT && st->hInputSb) {
                msb_notify_content_changed(st->hInputSb);
                return 0;
            }
        }
        switch (LOWORD(wParam)) {
        default: {
            const AiModelMenuItem* modelItem = Ai_FindModelMenuItem((UINT)LOWORD(wParam));
            if (modelItem && st) {
                st->role = modelItem->role;
                st->cloudMode = modelItem->isCloud;
                if (!modelItem->isCloud) {
                    st->model = modelItem->model;
                    Ai_NormalizeModelNameLocal(st->model);
                }
                Ai_AppendLog(hwnd, modelItem->isCloud
                    ? ((modelItem->role == AiMenuRole::Suggest)
                        ? L"Cloud suggestion mode selected."
                        : L"Cloud agent mode selected.")
                    : ((modelItem->role == AiMenuRole::Suggest)
                        ? L"Model switched to suggestion mode."
                        : L"Model switched to agent mode."));
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                return 0;
            }
            break;
        }
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
    case WM_SETFOCUS:
        Ai_RefreshUi(hwnd);
        return 0;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlType == ODT_MENU) {
            AiMenuItemData* d = (AiMenuItemData*)(ULONG_PTR)mis->itemData;
            if (!d) break;
            if (!s_hAiMenuFont) s_hAiMenuFont = Ai_MakeMenuFont(hwnd);
            if (d->isSeparator) {
                mis->itemHeight = S(8);
                mis->itemWidth = S(12);
                return TRUE;
            }
            HDC hdc = GetDC(hwnd);
            HFONT oldFont = s_hAiMenuFont ? (HFONT)SelectObject(hdc, s_hAiMenuFont) : NULL;
            const wchar_t* tab = wcschr(d->text.c_str(), L'\t');
            std::wstring main = tab ? std::wstring(d->text.c_str(), tab - d->text.c_str()) : d->text;
            RECT rc = {};
            DrawTextW(hdc, main.c_str(), -1, &rc, DT_CALCRECT | DT_SINGLELINE);
            int accelW = 0;
            if (tab && !d->isBar) {
                RECT accel = {};
                DrawTextW(hdc, tab + 1, -1, &accel, DT_CALCRECT | DT_SINGLELINE);
                accelW = accel.right + S(24);
            }
            if (oldFont) SelectObject(hdc, oldFont);
            ReleaseDC(hwnd, hdc);
            mis->itemHeight = (rc.bottom - rc.top) + (d->isBar ? S(6) : S(8));
            mis->itemWidth = (rc.right - rc.left) + (d->isBar ? S(20) : S(40)) + accelW;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM:
        if (Ai_DrawMenuItem(hwnd, (const DRAWITEMSTRUCT*)lParam)) {
            return TRUE;
        }
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
            if (st->hLogSb) msb_detach(st->hLogSb);
            if (st->hInputSb) msb_detach(st->hInputSb);
            if (st->hFont) DeleteObject(st->hFont);
            if (st->hPaneFont) DeleteObject(st->hPaneFont);
            if (s_hAiMenuFont) DeleteObject(s_hAiMenuFont);
            Ai_ClearMenuStorage();
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
        Ai_RefreshUi(s_hwndAiWindow);
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
