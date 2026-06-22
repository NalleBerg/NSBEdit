#include "ollama_ai.h"
#include "NSBEdit.h"
#include "copy_ai_code.h"
#include "ai_markdown_helper.h"

#include "dpi.h"
#include "ne_ai_client.h"
#include "ne_ai_bootstrap.h"
#include "ne_profiles.h"
#include "scroll/my_scrollbar_vscroll.h"

#include <gdiplus.h>
#include <commctrl.h>
#include <richedit.h>
#include "ILexer.h"
#include "Scintilla.h"
#include "ScintillaMessages.h"
#include "Lexilla.h"
#include "third_party/cmark-gfm/src/cmark-gfm.h"
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <thread>

#include "spinner/spinner_dialog.h"

#define IDC_AI_HEADER             1951
#define IDC_AI_LOG                1952
#define IDC_AI_INPUT              1953
#define IDC_AI_SEND_BTN           1954
#define IDC_AI_COPY_BTN           1955
#define IDC_AI_CLEAR_BTN          1956
#define IDC_AI_STATUS             1957
#define IDC_AI_CLOSE_BTN          1958
#define IDC_AI_ANSWER_TEXT        1959

#define IDM_AI_MODEL_DEFAULT      1901
#define IDM_AI_MODEL_FALLBACK     1902
#define IDM_AI_MODE_LOCAL         1910
#define IDM_AI_MODE_CLOUD         1911
#define IDM_AI_SIGN_IN            1920
#define IDM_AI_SIGN_OUT           1921
#define IDM_AI_OPEN_PROVIDER      1922
#define IDM_AI_CHECK_MODELS       1923
#define IDM_AI_LOG_CLEAR          1930
#define IDM_AI_LOG_COPY           1931
#define IDM_AI_SEND               1932
#define IDM_AI_ABOUT              1933
#define WM_AI_SEND_COMPLETE       (WM_APP + 71)
#define WM_AI_MODELCHECK_APPEND   (WM_APP + 72)
#define WM_AI_MODELCHECK_DONE     (WM_APP + 73)
#define WM_AI_SEND_APPEND         (WM_APP + 74)

static constexpr UINT_PTR kAiLiveTypingTimerId = 0xA11E;
static constexpr UINT_PTR kAiLiveTypingStartDelayTimerId = 0xA11F;
static constexpr UINT_PTR kAiThinkingTimerId = 0xA120;
static constexpr UINT kAiLiveTypingStartDelayMs = 3000;
static constexpr UINT kAiLiveTypingTickMs = 15;
static constexpr size_t kAiLiveTypingSlice = 4;

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
    HWND hAnswerHost = NULL;
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
    int historyIndex = -1;
    std::wstring historyDraft;
    int replyBaseStart = 0;
    std::wstring liveReply;
    std::wstring liveTypingQueue;
    bool liveTypingStartReady = false;
    bool liveTypingStartTimerRunning = false;
    bool liveTypingTimerRunning = false;
    bool liveTypingDone = false;
    bool liveTypingFinalRendered = false;
    bool thinkingTimerRunning = false;
    ULONGLONG thinkingStartMs = 0;
    SpinnerDialog* spinner = NULL;
    std::wstring answerCopyText;
    std::vector<HWND> answerBlocks;
    std::wstring answerRawMarkdown;
    int answerScrollY = 0;
    int answerContentHeight = 0;
};

struct AiAnswerBlockDesc {
    bool isCode = false;
    std::wstring text;
    std::wstring language;
};

struct AiSendResult {
    bool ok = false;
    bool usedFallback = false;
    bool streamed = false;
    std::wstring reply;
    std::wstring error;
};

struct AiSendWorkItem {
    HWND hwnd = NULL;
    std::wstring model;
    std::wstring fallback;
    std::wstring prompt;
    int answerStart = 0;
};

struct AiModelCheckState {
    HWND hwnd = NULL;
    HWND parent = NULL;
    HWND hLog = NULL;
    HWND hClose = NULL;
    HFONT hFont = NULL;
    AiDialogData* dd = NULL;
    bool done = false;
};

struct AiModelCheckProgressContext {
    HWND hwnd = NULL;
    std::wstring model;
    bool announcedDownload = false;
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
static std::vector<std::wstring> s_aiInputHistory;
static constexpr size_t kAiInputHistoryLimit = 500;

static void Ai_RefreshUi(HWND hwnd);
static void Ai_LoadPrefs(AiWindowState* st);
static void Ai_SavePrefs(const AiWindowState* st);
static void Ai_AppendLog(HWND hwnd, const std::wstring& line);
static bool Ai_CopyAnswerText(HWND hwndLog);
static void Ai_HistoryRemember(AiWindowState* st, const std::wstring& prompt);
static bool Ai_HistoryBrowse(HWND hwnd, AiWindowState* st, int delta);
static void Ai_DoSend(HWND hwnd);
static void Ai_AppendMenuOD(HMENU hMenu, UINT flags, UINT_PTR id, const wchar_t* text, bool isBar);
static std::wstring Ai_CurrentModeLabel(const AiWindowState* st);
static std::wstring Ai_FormatLocaleText(const wchar_t* fmt, const std::wstring& value);
static HMENU Ai_BuildMenu(const AiWindowState* st);
static int Ai_ButtonIndexById(const AiDialogData* dd, int id);
static void Ai_DrawButton(const DRAWITEMSTRUCT* dis, const AiDialogData* dd);
static HFONT Ai_MakeDlgFont(HWND hwnd, bool bold);
static HFONT Ai_MakePaneFont(HWND hwnd);
static int Ai_MeasureButtonWidth(const std::wstring& text);
static void Ai_StreamChunkCallback(void* context, const std::wstring& chunk);
static void Ai_NormalizeModelNameLocal(std::wstring& model);
static void Ai_AddUniqueModel(std::vector<std::wstring>& models, const std::wstring& model);
static std::wstring Ai_TrimCopy(const std::wstring& text);
static void Ai_ApplyRichFormatRange(HWND hLog, int start, int end, const CHARFORMAT2W* format);
static void Ai_AppendMarkupLine(HWND hLog, const std::wstring& line, const CHARFORMAT2W* normalFmt, const CHARFORMAT2W* boldFmt, const CHARFORMAT2W* italicFmt);
static void Ai_SetWrapToWindow(HWND hwndEdit);
static bool Ai_RenderMarkdownReply(HWND hwnd, const std::wstring& reply);
static void Ai_FinalizeLiveReply(HWND hwnd);
static void Ai_SetThinkingStatusText(HWND hwnd);
static void Ai_ClearRenderedAnswer(HWND hwnd);
static HWND Ai_CreateAnswerHost(HWND hwndParent, int x, int y, int w, int h);
static HWND Ai_CreateAnswerRichEdit(HWND hwndParent, int x, int y, int w, int h);
static HWND Ai_CreateAnswerScintilla(HWND hwndParent, int x, int y, int w, int h);
static const char* Ai_CodeLexerNameForLanguage(const std::wstring& language);
static std::vector<AiAnswerBlockDesc> Ai_ParseAnswerBlocks(const std::wstring& reply);
static void Ai_LayoutAnswerHost(HWND hwndHost);
static void Ai_RenderAnswerBlocks(HWND hwnd);
static LRESULT CALLBACK Ai_AnswerHostSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR dwRefData);
static LRESULT CALLBACK Ai_InputSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR dwRefData);
static void Ai_ApplyButtons(AiWindowState* st);
static void Ai_ShowModelCheckDialog(HWND parent);

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

static void Ai_OpenOllamaSignUp(HWND hwnd)
{
    ShellExecuteW(hwnd, L"open", L"https://ollama.com/signup", NULL, NULL, SW_SHOWNORMAL);
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
    while (!model.empty() && iswspace(model.back())) {
        model.pop_back();
    }
    const wchar_t* kAgentSuffix = L" Agent";
    const wchar_t* kSuggestSuffix = L" Suggest";
    if (model.size() > 6 && model.compare(model.size() - 6, 6, kAgentSuffix) == 0) {
        model.erase(model.size() - 6);
    } else if (model.size() > 8 && model.compare(model.size() - 8, 8, kSuggestSuffix) == 0) {
        model.erase(model.size() - 8);
    }
    if (model == L"qwen2.5-coder:7b-instruct" || model == L"qwen2.5-coder") {
        model = L"qwen2.5-coder:7b";
    } else if (model == L"qwen2.5-coder:3b-instruct") {
        model = L"qwen2.5-coder:3b";
    }
}

static void Ai_SaveHistory()
{
    NeProfiles_SetIntSetting("ai.history.count", (int)s_aiInputHistory.size());
    for (size_t i = 0; i < s_aiInputHistory.size(); ++i) {
        std::string key = "ai.history." + std::to_string(i);
        NeProfiles_SetStrSetting(key.c_str(), Ai_WideToUtf8(s_aiInputHistory[i]));
    }
}

static void Ai_LoadHistory()
{
    s_aiInputHistory.clear();

    int count = 0;
    if (!NeProfiles_GetIntSetting("ai.history.count", 0, count) || count <= 0) {
        return;
    }

    count = std::min(count, (int)kAiInputHistoryLimit);
    s_aiInputHistory.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        std::string saved;
        std::string key = "ai.history." + std::to_string(i);
        if (NeProfiles_GetStrSetting(key.c_str(), "", saved) && !saved.empty()) {
            s_aiInputHistory.push_back(Ai_Utf8ToWide(saved));
        }
    }
}

static void Ai_SavePrefs(const AiWindowState* st)
{
    if (!st) return;
    NeProfiles_SetStrSetting("ai.model", Ai_WideToUtf8(st->model));
    NeProfiles_SetStrSetting("ai.fallback", Ai_WideToUtf8(st->fallback));
    NeProfiles_SetIntSetting("ai.role", (int)st->role);
    NeProfiles_SetIntSetting("ai.cloud_mode", st->cloudMode ? 1 : 0);
    NeProfiles_SetIntSetting("ai.signed_in", st->signedIn ? 1 : 0);
    Ai_SaveHistory();
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
    Ai_LoadHistory();
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

static HFONT Ai_MakeDlgFont(HWND hwnd, bool bold)
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

static void Ai_HistorySetInput(HWND hwnd, const std::wstring& text)
{
    HWND hInput = GetDlgItem(hwnd, IDC_AI_INPUT);
    if (!hInput) return;
    SetWindowTextW(hInput, text.c_str());
    int len = (int)GetWindowTextLengthW(hInput);
    SendMessageW(hInput, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hInput, EM_SCROLLCARET, 0, 0);
}

static void Ai_HistoryRemember(AiWindowState* st, const std::wstring& text)
{
    if (!st || text.empty()) return;
    if (!s_aiInputHistory.empty() && s_aiInputHistory.back() == text) {
        st->historyIndex = -1;
        st->historyDraft.clear();
        return;
    }
    s_aiInputHistory.push_back(text);
    if (s_aiInputHistory.size() > kAiInputHistoryLimit) {
        s_aiInputHistory.erase(s_aiInputHistory.begin());
    }
    st->historyIndex = -1;
    st->historyDraft.clear();
}

static bool Ai_HistoryBrowse(HWND hwnd, AiWindowState* st, int direction)
{
    if (!st || s_aiInputHistory.empty()) return false;

    if (st->historyIndex < 0) {
        HWND hInput = GetDlgItem(hwnd, IDC_AI_INPUT);
        wchar_t current[2048] = {};
        if (hInput) GetWindowTextW(hInput, current, 2048);
        st->historyDraft = current;
        st->historyIndex = (int)s_aiInputHistory.size() - 1;
    } else if (direction < 0) {
        if (st->historyIndex > 0) {
            --st->historyIndex;
        }
    } else {
        if (st->historyIndex + 1 < (int)s_aiInputHistory.size()) {
            ++st->historyIndex;
        } else {
            st->historyIndex = -1;
            Ai_HistorySetInput(hwnd, st->historyDraft);
            return true;
        }
    }

    if (st->historyIndex >= 0 && st->historyIndex < (int)s_aiInputHistory.size()) {
        Ai_HistorySetInput(hwnd, s_aiInputHistory[(size_t)st->historyIndex]);
        return true;
    }

    return false;
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

static void Ai_CollectMarkdownText(cmark_node* node, std::wstring& out);

static void Ai_AppendMarkdownLiteral(const char* literal, std::wstring& out)
{
    if (!literal || !*literal) return;
    out += Ai_Utf8ToWide(literal);
}

static void Ai_CollectMarkdownText(cmark_node* node, std::wstring& out)
{
    for (cmark_node* cur = node; cur; cur = cmark_node_next(cur)) {
        cmark_node_type type = cmark_node_get_type(cur);
        if (type == CMARK_NODE_TEXT || type == CMARK_NODE_CODE || type == CMARK_NODE_HTML_INLINE) {
            Ai_AppendMarkdownLiteral(cmark_node_get_literal(cur), out);
        } else if (type == CMARK_NODE_SOFTBREAK || type == CMARK_NODE_LINEBREAK) {
            out += L"\n";
        } else if (type == CMARK_NODE_CODE_BLOCK) {
            Ai_AppendMarkdownLiteral(cmark_node_get_literal(cur), out);
        } else if (type == CMARK_NODE_ITEM) {
            out += L"• ";
            Ai_CollectMarkdownText(cmark_node_first_child(cur), out);
            out += L"\n";
        } else {
            Ai_CollectMarkdownText(cmark_node_first_child(cur), out);
            if (type == CMARK_NODE_PARAGRAPH || type == CMARK_NODE_HEADING || type == CMARK_NODE_BLOCK_QUOTE) {
                out += L"\n";
            }
        }
    }
}

static std::vector<AiAnswerBlockDesc> Ai_ParseAnswerBlocks(const std::wstring& reply)
{
    std::vector<AiAnswerBlockDesc> blocks;
    if (reply.empty()) return blocks;

    std::string utf8 = Ai_WideToUtf8(reply);
    cmark_parser* parser = cmark_parser_new(CMARK_OPT_DEFAULT);
    if (!parser) return blocks;
    cmark_parser_feed(parser, utf8.c_str(), utf8.size());
    cmark_node* doc = cmark_parser_finish(parser);
    if (!doc) return blocks;

    for (cmark_node* node = cmark_node_first_child(doc); node; node = cmark_node_next(node)) {
        cmark_node_type type = cmark_node_get_type(node);
        if (type == CMARK_NODE_CODE_BLOCK) {
            AiAnswerBlockDesc block;
            block.isCode = true;
            const char* info = cmark_node_get_fence_info(node);
            if (info) block.language = Ai_Utf8ToWide(info);
            const char* literal = cmark_node_get_literal(node);
            if (literal) block.text = Ai_Utf8ToWide(literal);
            blocks.push_back(std::move(block));
            continue;
        }

        AiAnswerBlockDesc block;
        block.isCode = false;
        switch (type) {
        case CMARK_NODE_ITEM:
            block.text = L"• ";
            Ai_CollectMarkdownText(cmark_node_first_child(node), block.text);
            break;
        default:
            Ai_CollectMarkdownText(cmark_node_first_child(node), block.text);
            break;
        }

        while (!block.text.empty() && (block.text.back() == L'\n' || block.text.back() == L'\r')) {
            block.text.pop_back();
        }
        if (!block.text.empty()) {
            blocks.push_back(std::move(block));
        }
    }

    cmark_node_free(doc);
    return blocks;
}

static const char* Ai_CodeLexerNameForLanguage(const std::wstring& language)
{
    std::wstring lower = language;
    for (wchar_t& ch : lower) ch = (wchar_t)towlower(ch);
    if (lower == L"cpp" || lower == L"c++" || lower == L"cc" || lower == L"cxx" || lower == L"c") return "cpp";
    if (lower == L"bash" || lower == L"sh" || lower == L"shell" || lower == L"zsh") return "bash";
    if (lower == L"python" || lower == L"py") return "python";
    if (lower == L"javascript" || lower == L"js" || lower == L"mjs" || lower == L"ts" || lower == L"typescript") return "javascript";
    if (lower == L"sql") return "sql";
    if (lower == L"html" || lower == L"xml") return "hypertext";
    if (lower == L"css") return "css";
    if (lower == L"json") return "json";
    if (lower == L"yaml" || lower == L"yml") return "yaml";
    if (lower == L"markdown" || lower == L"md") return "markdown";
    if (lower == L"php") return "php";
    if (lower == L"powershell" || lower == L"ps1") return "powershell";
    if (lower == L"rust") return "rust";
    if (lower == L"java") return "java";
    if (lower == L"csharp" || lower == L"cs" || lower == L"c#") return "csharp";
    if (lower == L"lua") return "lua";
    if (lower == L"perl") return "perl";
    if (lower == L"ruby") return "ruby";
    return nullptr;
}

static HWND Ai_CreateAnswerRichEdit(HWND hwndParent, int x, int y, int w, int h)
{
    LoadLibraryW(L"Msftedit.dll");
    const wchar_t* reClass = L"EDIT";
    HMODULE hMsft = GetModuleHandleW(L"Msftedit.dll");
    if (hMsft) {
        WNDCLASSEXW wce = { sizeof(wce) };
        if (GetClassInfoExW(hMsft, L"RICHEDIT50W", &wce)) {
            reClass = L"RICHEDIT50W";
        }
    }

    HWND hEdit = CreateWindowExW(0, reClass, L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
        x, y, std::max(1, w), std::max(1, h), hwndParent, (HMENU)(UINT_PTR)IDC_AI_ANSWER_TEXT, GetModuleHandleW(NULL), NULL);
    if (hEdit) {
        SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = RGB(20, 20, 20);
        SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    }
    return hEdit;
}

static HWND Ai_CreateAnswerScintilla(HWND hwndParent, int x, int y, int w, int h)
{
    HWND hSci = CreateWindowExW(0, L"Scintilla", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, std::max(1, w), std::max(1, h), hwndParent, NULL, GetModuleHandleW(NULL), NULL);
    if (!hSci) return NULL;
    SendMessageW(hSci, SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT, 0);
    SendMessageW(hSci, SCI_USEPOPUP, SC_POPUP_NEVER, 0);
    SendMessageW(hSci, SCI_SETREADONLY, TRUE, 0);
    SendMessageW(hSci, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)L"Consolas");
    SendMessageW(hSci, SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    SendMessageW(hSci, SCI_STYLESETFORE, STYLE_DEFAULT, RGB(30, 30, 30));
    SendMessageW(hSci, SCI_STYLESETBACK, STYLE_DEFAULT, RGB(246, 246, 246));
    SendMessageW(hSci, SCI_STYLECLEARALL, 0, 0);
    SendMessageW(hSci, SCI_SETTABWIDTH, 4, 0);
    return hSci;
}

static void Ai_DestroyWindowVector(std::vector<HWND>& windows)
{
    for (HWND hwnd : windows) {
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
    }
    windows.clear();
}

static void Ai_ClearRenderedAnswer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    Ai_DestroyWindowVector(st->answerBlocks);
    st->answerCopyText.clear();
    HWND hHost = st->hAnswerHost;
    if (hHost && IsWindow(hHost)) {
        SetScrollPos(hHost, SB_VERT, 0, TRUE);
        SendMessageW(hHost, WM_ERASEBKGND, 0, 0);
    }
}

static void Ai_PositionAnswerChild(HWND hwnd, int top, int height)
{
    if (!IsWindow(hwnd)) return;
    SetWindowPos(hwnd, NULL, S(8), top, std::max(1, (int)std::max(0L, 1L)), std::max(1, height), SWP_NOZORDER | SWP_NOACTIVATE);
}

static void Ai_LayoutAnswerHost(HWND hwndHost)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(GetParent(hwndHost), GWLP_USERDATA);
    if (!st) return;

    RECT rc = {};
    GetClientRect(hwndHost, &rc);
    int width = std::max(0, (int)rc.right - S(16));
    int y = S(8) - st->answerScrollY;

    if (st->answerBlocks.empty() && st->hLog && IsWindow(st->hLog)) {
        SetWindowPos(st->hLog, NULL, S(8), S(8), width, std::max(1, (int)rc.bottom - S(16)), SWP_NOZORDER | SWP_NOACTIVATE);
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = std::max(0, (int)rc.bottom);
        si.nPage = (UINT)std::max(0, (int)rc.bottom);
        si.nPos = 0;
        SetScrollInfo(hwndHost, SB_VERT, &si, TRUE);
        if (st->hLogSb) msb_notify_content_changed(st->hLogSb);
        return;
    }

    for (HWND child : st->answerBlocks) {
        if (!IsWindow(child)) continue;
        RECT cr = {};
        GetWindowRect(child, &cr);
        int height = cr.bottom - cr.top;
        SetWindowPos(child, NULL, S(8), y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        y += height + S(8);
    }

    st->answerContentHeight = std::max(0, y + st->answerScrollY);
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, st->answerContentHeight);
    si.nPage = (UINT)std::max(0, (int)rc.bottom);
    si.nPos = std::max(0, st->answerScrollY);
    SetScrollInfo(hwndHost, SB_VERT, &si, TRUE);
    if (st->hLogSb) msb_notify_content_changed(st->hLogSb);
}

static void Ai_RenderAnswerBlocks(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || !st->hAnswerHost || !IsWindow(st->hAnswerHost)) return;

    Ai_DestroyWindowVector(st->answerBlocks);

    RECT rc = {};
    GetClientRect(st->hAnswerHost, &rc);
    int width = std::max(0, (int)rc.right - S(16));
    int y = S(8);
    std::vector<AiAnswerBlockDesc> blocks = Ai_ParseAnswerBlocks(st->answerRawMarkdown);
    if (blocks.empty() && !st->answerRawMarkdown.empty()) {
        blocks.push_back(AiAnswerBlockDesc{ false, st->answerRawMarkdown, L"" });
    }

    for (const AiAnswerBlockDesc& desc : blocks) {
        if (desc.isCode) {
            std::wstring headerText = L"📋 Copy code";
            HWND hHeader = CreateWindowExW(0, L"BUTTON", headerText.c_str(),
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                S(8), y, S(140), S(28), st->hAnswerHost, (HMENU)(UINT_PTR)IDC_AI_COPY_BTN, GetModuleHandleW(NULL), NULL);
            if (hHeader && st->hFont) SendMessageW(hHeader, WM_SETFONT, (WPARAM)st->hFont, TRUE);

            std::wstring codeText = desc.text;
            int codeLines = 1;
            for (wchar_t ch : codeText) {
                if (ch == L'\n') ++codeLines;
            }
            int codeHeight = std::max((int)S(80), codeLines * (int)S(18));
            HWND hSci = Ai_CreateAnswerScintilla(st->hAnswerHost, S(8), y + S(32), width, codeHeight);
            if (hSci) {
                const char* lexer = Ai_CodeLexerNameForLanguage(desc.language);
                if (lexer) {
                    ILexer5* pLex = CreateLexer(lexer);
                    if (pLex) SendMessageW(hSci, SCI_SETILEXER, 0, (LPARAM)pLex);
                }
                std::string utf8 = Ai_WideToUtf8(codeText);
                SendMessageA(hSci, SCI_SETTEXT, 0, (LPARAM)utf8.c_str());
                if (hHeader) {
                    SetPropW(hHeader, L"AiCodeSci", (HANDLE)hSci);
                }
                if (hHeader) {
                    st->answerBlocks.push_back(hHeader);
                }
                st->answerBlocks.push_back(hSci);
                y += S(32) + codeHeight + S(8);
            } else if (hHeader) {
                DestroyWindow(hHeader);
            }
        } else {
            std::wstring text = desc.text;
            if (text.empty()) continue;
            RECT measure = { 0, 0, std::max(1, width), 0 };
            HDC hdc = GetDC(st->hAnswerHost);
            HFONT hf = st->hPaneFont;
            HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
            DrawTextW(hdc, text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            if (old) SelectObject(hdc, old);
            ReleaseDC(st->hAnswerHost, hdc);
            int h = std::max((int)S(32), (int)(measure.bottom - measure.top) + (int)S(10));
            HWND hText = Ai_CreateAnswerRichEdit(st->hAnswerHost, S(8), y, width, h);
            if (hText) {
                if (st->hPaneFont) SendMessageW(hText, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
                SetWindowTextW(hText, text.c_str());
                int textLen = GetWindowTextLengthW(hText);
                if (textLen > 0) {
                    CHARFORMAT2W cf = {};
                    cf.cbSize = sizeof(cf);
                    cf.dwMask = CFM_COLOR;
                    cf.crTextColor = RGB(20, 20, 20);
                    CHARRANGE range = { 0, textLen };
                    SendMessageW(hText, EM_EXSETSEL, 0, (LPARAM)&range);
                    SendMessageW(hText, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
                    range.cpMin = textLen;
                    range.cpMax = textLen;
                    SendMessageW(hText, EM_EXSETSEL, 0, (LPARAM)&range);
                }
                st->answerBlocks.push_back(hText);
                y += h + S(8);
            }
        }
    }

    st->answerContentHeight = y + S(8);
    Ai_LayoutAnswerHost(st->hAnswerHost);
}

static HWND Ai_CreateAnswerHost(HWND hwndParent, int x, int y, int w, int h)
{
    HWND hHost = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        x, y, std::max(1, w), std::max(1, h), hwndParent, (HMENU)(UINT_PTR)IDC_AI_LOG, GetModuleHandleW(NULL), NULL);
    if (hHost) {
        SetWindowSubclass(hHost, Ai_AnswerHostSubclassProc, 1, (DWORD_PTR)hwndParent);
    }
    return hHost;
}

static LRESULT CALLBACK Ai_AnswerHostSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR dwRefData)
{
    HWND hwndParent = (HWND)dwRefData;
    AiWindowState* st = hwndParent ? (AiWindowState*)GetWindowLongPtrW(hwndParent, GWLP_USERDATA) : NULL;
    switch (msg) {
    case WM_SIZE:
        Ai_LayoutAnswerHost(hwnd);
        return 0;
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
        if (st) {
            int delta = 0;
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            int maxScroll = std::max(0, st->answerContentHeight - (int)rc.bottom);
            if (msg == WM_MOUSEWHEEL) {
                delta = -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * S(48);
            } else {
                switch (LOWORD(wParam)) {
                case SB_LINEUP: delta = -S(32); break;
                case SB_LINEDOWN: delta = S(32); break;
                case SB_PAGEUP: delta = -S(120); break;
                case SB_PAGEDOWN: delta = S(120); break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:
                    st->answerScrollY = std::min(maxScroll, std::max(0, (int)HIWORD(wParam)));
                    Ai_LayoutAnswerHost(hwnd);
                    return 0;
                default: break;
                }
            }
            st->answerScrollY = std::min(maxScroll, std::max(0, st->answerScrollY + delta));
            Ai_LayoutAnswerHost(hwnd);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_AI_COPY_BTN && lParam) {
            HWND hCopy = (HWND)lParam;
            HWND hSci = (HWND)GetPropW(hCopy, L"AiCodeSci");
            if (hSci && IsWindow(hSci)) {
                int len = (int)SendMessageW(hSci, SCI_GETLENGTH, 0, 0);
                if (len > 0) {
                    std::string utf8((size_t)len + 1, '\0');
                    SendMessageA(hSci, SCI_GETTEXT, (WPARAM)utf8.size(), (LPARAM)utf8.data());
                    utf8.resize((size_t)len);
                    Ai_CopyTextToClipboard(hwndParent, Ai_Utf8ToWide(utf8));
                }
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        RemoveWindowSubclass(hwnd, Ai_AnswerHostSubclassProc, 1);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static std::wstring Ai_GetTextRange(HWND hwnd, const CHARRANGE& range)
{
    if (!hwnd || range.cpMin >= range.cpMax) return {};

    std::wstring text((size_t)(range.cpMax - range.cpMin) + 1, L'\0');
    TEXTRANGEW tr = {};
    tr.chrg = range;
    tr.lpstrText = text.data();
    LRESULT copied = SendMessageW(hwnd, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    if (copied <= 0) return {};
    text.resize((size_t)copied);
    return text;
}

static bool Ai_CopyAnswerText(HWND hwndLog)
{
    if (!hwndLog) return false;
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(GetParent(hwndLog), GWLP_USERDATA);
    if (!st || st->answerCopyText.empty()) return false;
    Ai_CopyTextToClipboard(hwndLog, st->answerCopyText);
    return true;
}

static std::wstring Ai_TrimCopy(const std::wstring& text)
{
    size_t first = 0;
    while (first < text.size() && iswspace(text[first])) {
        ++first;
    }
    size_t last = text.size();
    while (last > first && iswspace(text[last - 1])) {
        --last;
    }
    return text.substr(first, last - first);
}

static void Ai_ApplyRichFormatRange(HWND hLog, int start, int end, const CHARFORMAT2W* format)
{
    if (!hLog || !format || start >= end) return;
    CHARRANGE range = { start, end };
    SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&range);
    SendMessageW(hLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)format);
}

static void Ai_AppendStyledRun(HWND hLog, const std::wstring& text, const CHARFORMAT2W* baseFmt,
    COLORREF color, bool bold, bool italic, bool underline, bool strike)
{
    if (text.empty()) return;

    CHARFORMAT2W fmt = {};
    if (baseFmt) fmt = *baseFmt;
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask |= CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
    if (color != RGB(0, 0, 0)) fmt.crTextColor = color;
    if (bold) fmt.dwEffects |= CFE_BOLD;
    if (italic) fmt.dwEffects |= CFE_ITALIC;
    if (underline) fmt.dwEffects |= CFE_UNDERLINE;
    if (strike) fmt.dwEffects |= CFE_STRIKEOUT;
    Ai_AppendRichRun(hLog, text, &fmt);
}

static COLORREF Ai_ParseHtmlColorValue(const std::wstring& value)
{
    std::wstring v = value;
    for (wchar_t& ch : v) {
        ch = (wchar_t)towlower(ch);
    }

    if (v == L"red") return RGB(220, 50, 47);
    if (v == L"green") return RGB(38, 139, 78);
    if (v == L"blue") return RGB(38, 112, 191);
    if (v == L"yellow") return RGB(181, 137, 0);
    if (v == L"purple") return RGB(108, 113, 196);
    if (v == L"orange") return RGB(203, 75, 22);
    if (v == L"cyan" || v == L"aqua") return RGB(42, 161, 152);
    if (v == L"magenta" || v == L"fuchsia") return RGB(211, 54, 130);
    if (v == L"lime") return RGB(133, 153, 0);
    if (v == L"teal") return RGB(42, 161, 152);
    if (v == L"navy") return RGB(38, 112, 191);
    if (v == L"maroon") return RGB(220, 50, 47);
    if (v == L"olive") return RGB(181, 137, 0);
    if (v == L"silver") return RGB(147, 161, 161);
    if (v == L"brown") return RGB(133, 53, 0);
    if (v == L"pink") return RGB(211, 54, 130);
    if (v == L"gold") return RGB(181, 137, 0);
    if (v == L"coral") return RGB(203, 75, 22);
    if (v == L"salmon") return RGB(203, 75, 22);
    if (v == L"crimson") return RGB(220, 50, 47);
    if (v == L"indigo") return RGB(108, 113, 196);
    if (v == L"violet") return RGB(108, 113, 196);
    if (v == L"black") return RGB(0, 0, 0);
    if (v == L"gray" || v == L"grey") return RGB(128, 128, 128);
    if (v == L"white") return RGB(255, 255, 255);

    if (v.rfind(L"rgb(", 0) == 0 || v.rfind(L"rgba(", 0) == 0) {
        int r = 0, g = 0, b = 0;
        if (swscanf_s(v.c_str(), L"rgb(%d,%d,%d)", &r, &g, &b) == 3 ||
            swscanf_s(v.c_str(), L"rgba(%d,%d,%d", &r, &g, &b) == 3) {
            return RGB((BYTE)r, (BYTE)g, (BYTE)b);
        }
    }

    if (!v.empty() && v[0] == L'#') {
        unsigned int rgb = 0;
        if (swscanf_s(v.c_str() + 1, L"%x", &rgb) == 1) {
            if (v.size() == 7) {
                return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
            }
            if (v.size() == 4) {
                unsigned int r = ((rgb >> 8) & 0xf) * 17;
                unsigned int g = ((rgb >> 4) & 0xf) * 17;
                unsigned int b = (rgb & 0xf) * 17;
                return RGB(r, g, b);
            }
            if (v.size() == 9) {
                return RGB((rgb >> 24) & 0xff, (rgb >> 16) & 0xff, (rgb >> 8) & 0xff);
            }
        }
    }

    return RGB(0, 0, 0);
}

static std::wstring Ai_GetHtmlAttributeValue(const std::wstring& tag, const std::wstring& attrName)
{
    size_t pos = tag.find(attrName);
    if (pos == std::wstring::npos) return {};
    pos = tag.find(L'=', pos + attrName.size());
    if (pos == std::wstring::npos) return {};
    ++pos;
    while (pos < tag.size() && iswspace(tag[pos])) ++pos;
    if (pos >= tag.size()) return {};

    wchar_t quote = 0;
    if (tag[pos] == L'"' || tag[pos] == L'\'') {
        quote = tag[pos++];
    }
    size_t end = pos;
    while (end < tag.size() && ((quote && tag[end] != quote) || (!quote && !iswspace(tag[end]) && tag[end] != L'>'))) {
        ++end;
    }
    return tag.substr(pos, end - pos);
}

static void Ai_ParseSpanStyleFlags(const std::wstring& openTag, COLORREF& color, bool& bold, bool& italic, bool& underline, bool& strike)
{
    std::wstring style = Ai_GetHtmlAttributeValue(openTag, L"style");
    if (style.empty()) return;

    std::wstring lower = style;
    for (wchar_t& ch : lower) {
        ch = (wchar_t)towlower(ch);
    }

    size_t colorPos = lower.find(L"color:");
    if (colorPos != std::wstring::npos) {
        size_t start = colorPos + 6;
        while (start < style.size() && iswspace(style[start])) ++start;
        size_t end = start;
        while (end < style.size() && style[end] != L';' && !iswspace(style[end])) ++end;
        color = Ai_ParseHtmlColorValue(style.substr(start, end - start));
    }

    size_t weightPos = lower.find(L"font-weight:");
    if (weightPos != std::wstring::npos) {
        size_t start = weightPos + 12;
        while (start < lower.size() && iswspace(lower[start])) ++start;
        if (lower.find(L"normal", start) == start || lower.find(L"400", start) == start) {
            bold = false;
        } else if (lower.find(L"bold", start) == start || lower.find(L"600", start) == start ||
            lower.find(L"700", start) == start || lower.find(L"800", start) == start ||
            lower.find(L"900", start) == start) {
            bold = true;
        }
    }

    size_t fontStylePos = lower.find(L"font-style:");
    if (fontStylePos != std::wstring::npos) {
        size_t start = fontStylePos + 11;
        while (start < lower.size() && iswspace(lower[start])) ++start;
        if (lower.find(L"normal", start) == start) {
            italic = false;
        } else if (lower.find(L"italic", start) == start || lower.find(L"oblique", start) == start) {
            italic = true;
        }
    }

    size_t decorationPos = lower.find(L"text-decoration:");
    if (decorationPos != std::wstring::npos) {
        size_t start = decorationPos + 16;
        while (start < lower.size() && iswspace(lower[start])) ++start;
        if (lower.find(L"none", start) == start) {
            underline = false;
            strike = false;
        }
        if (lower.find(L"underline", start) != std::wstring::npos) underline = true;
        if (lower.find(L"line-through", start) != std::wstring::npos || lower.find(L"strike", start) != std::wstring::npos) strike = true;
    }
}

static void Ai_AppendMarkupLine(HWND hLog, const std::wstring& line, const CHARFORMAT2W* normalFmt, const CHARFORMAT2W* boldFmt, const CHARFORMAT2W* italicFmt)
{
    size_t pos = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    COLORREF color = RGB(0, 0, 0);

    while (pos < line.size()) {
        size_t open = line.find(L'<', pos);
        size_t star = line.find(L'*', pos);
        size_t under = line.find(L'_', pos);
        size_t tilde = line.find(L'~', pos);
        size_t next = std::wstring::npos;
        if (open != std::wstring::npos) next = open;
        if (star != std::wstring::npos) next = (next == std::wstring::npos) ? star : std::min(next, star);
        if (under != std::wstring::npos) next = (next == std::wstring::npos) ? under : std::min(next, under);
        if (tilde != std::wstring::npos) next = (next == std::wstring::npos) ? tilde : std::min(next, tilde);

        if (next == std::wstring::npos) {
            Ai_AppendStyledRun(hLog, line.substr(pos), normalFmt, color, bold, italic, underline, strike);
            break;
        }

        if (next > pos) {
            Ai_AppendStyledRun(hLog, line.substr(pos, next - pos), normalFmt, color, bold, italic, underline, strike);
        }

        if (line[next] == L'*' || line[next] == L'_') {
            bool doubleMarker = (next + 1 < line.size() && line[next + 1] == line[next]);
            if (doubleMarker) {
                bold = !bold;
                pos = next + 2;
            } else {
                italic = !italic;
                pos = next + 1;
            }
            continue;
        }

        if (line[next] == L'~') {
            bool doubleMarker = (next + 1 < line.size() && line[next + 1] == L'~');
            if (doubleMarker) {
                strike = !strike;
                pos = next + 2;
            } else {
                Ai_AppendStyledRun(hLog, line.substr(next, 1), normalFmt, color, bold, italic, underline, strike);
                pos = next + 1;
            }
            continue;
        }

        size_t close = line.find(L'>', next + 1);
        if (close == std::wstring::npos) {
            Ai_AppendStyledRun(hLog, line.substr(next), normalFmt, color, bold, italic, underline, strike);
            break;
        }

        std::wstring tag = line.substr(next + 1, close - next - 1);
        std::wstring lowerTag = tag;
        for (wchar_t& ch : lowerTag) ch = (wchar_t)towlower(ch);

        if (!tag.empty() && tag[0] == L'/') {
            if (lowerTag == L"/span") color = RGB(0, 0, 0);
            else if (lowerTag == L"/font") color = RGB(0, 0, 0);
            else if (lowerTag == L"/b" || lowerTag == L"/strong") bold = false;
            else if (lowerTag == L"/i" || lowerTag == L"/em") italic = false;
            else if (lowerTag == L"/u") underline = false;
            else if (lowerTag == L"/s" || lowerTag == L"/strike" || lowerTag == L"/del") strike = false;
        } else if (lowerTag.rfind(L"span", 0) == 0) {
            Ai_ParseSpanStyleFlags(tag, color, bold, italic, underline, strike);
        } else if (lowerTag.rfind(L"font", 0) == 0) {
            std::wstring fontColor = Ai_GetHtmlAttributeValue(tag, L"color");
            if (!fontColor.empty()) {
                color = Ai_ParseHtmlColorValue(fontColor);
            }
        } else if (lowerTag == L"b" || lowerTag == L"strong") {
            bold = true;
        } else if (lowerTag == L"i" || lowerTag == L"em") {
            italic = true;
        } else if (lowerTag == L"u") {
            underline = true;
        } else if (lowerTag == L"s" || lowerTag == L"strike" || lowerTag == L"del") {
            strike = true;
        } else {
            Ai_AppendStyledRun(hLog, line.substr(next, close - next + 1), normalFmt, color, bold, italic, underline, strike);
        }

        pos = close + 1;
    }
}

static bool Ai_RenderMarkdownReply(HWND hwnd, const std::wstring& reply)
{
    if (reply.empty()) return false;

    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return false;

    std::wstring text = reply;
    Ai_ReplaceAll(text, L"\r\n", L"\n");
    Ai_ReplaceAll(text, L"\r", L"\n");
    Ai_UnescapeModelText(text);

    st->answerRawMarkdown = text;
    st->answerCopyText = text;
    Ai_ClearRenderedAnswer(hwnd);
    if (!st->hAnswerHost || !IsWindow(st->hAnswerHost)) {
        return false;
    }

    if (st->hLog && IsWindow(st->hLog)) {
        DestroyWindow(st->hLog);
        st->hLog = NULL;
    }

    Ai_RenderAnswerBlocks(hwnd);
    if (st->hLogSb) msb_notify_content_changed(st->hLogSb);
    return true;
}

static void Ai_FinalizeLiveReply(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->liveTypingFinalRendered || !st->liveTypingDone || !st->liveTypingQueue.empty() || st->liveReply.empty()) {
        return;
    }

    if (Ai_RenderMarkdownReply(hwnd, st->liveReply)) {
        st->liveTypingFinalRendered = true;
        Ai_SetThinkingStatusText(hwnd);
    }
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

static std::wstring Ai_FormatThinkingStatusText(const AiWindowState* st)
{
    std::wstring timeText = L"00:00:00";
    if (st && st->thinkingStartMs != 0) {
        ULONGLONG elapsedMs = GetTickCount64() - st->thinkingStartMs;
        unsigned long long totalSeconds = elapsedMs / 1000ULL;
        unsigned long long hours = totalSeconds / 3600ULL;
        unsigned long long minutes = (totalSeconds % 3600ULL) / 60ULL;
        unsigned long long seconds = totalSeconds % 60ULL;

        wchar_t buffer[32] = {};
        swprintf_s(buffer, L"%02llu:%02llu:%02llu", hours, minutes, seconds);
        timeText = buffer;
    }

    return Ai_FormatLocaleText(Ne_Ls(L"AI_THINKING_STATUS"), timeText);
}

static void Ai_SetBaseStatusText(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;

    HWND hStatus = GetDlgItem(hwnd, IDC_AI_STATUS);
    if (!hStatus) return;

    std::wstring status = L"Cloud: ";
    status += (st->cloudMode ? L"Cloud subscription" : L"Local only");
    status += L" | Sign-in: ";
    status += (st->signedIn ? L"yes" : L"no");
    status += L" | Current: ";
    status += Ai_CurrentModeLabel(st);
    SetWindowTextW(hStatus, status.c_str());
}

static void Ai_SetThinkingStatusText(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    HWND hStatus = GetDlgItem(hwnd, IDC_AI_STATUS);
    if (!hStatus) return;
    SetWindowTextW(hStatus, Ai_FormatThinkingStatusText(st).c_str());
}

static void Ai_StopThinkingTimer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || !st->thinkingTimerRunning) return;
    KillTimer(hwnd, kAiThinkingTimerId);
    st->thinkingTimerRunning = false;
}

static void Ai_StartThinkingTimer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    st->thinkingStartMs = GetTickCount64();
    st->thinkingTimerRunning = true;
    SetTimer(hwnd, kAiThinkingTimerId, 1000, NULL);
    Ai_SetThinkingStatusText(hwnd);
}

static void Ai_AppendLog(HWND hwnd, const std::wstring& line)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (st && (!st->hLog || !IsWindow(st->hLog)) && st->hAnswerHost && IsWindow(st->hAnswerHost)) {
        RECT rc = {};
        GetClientRect(st->hAnswerHost, &rc);
        st->hLog = Ai_CreateAnswerRichEdit(st->hAnswerHost, S(8), S(8), std::max(1, (int)rc.right - S(16)), std::max(1, (int)rc.bottom - S(16)));
        if (st->hLog && st->hPaneFont) SendMessageW(st->hLog, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
        if (st->hLog) {
            SendMessageW(st->hLog, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
            Ai_SetWrapToWindow(st->hLog);
        }
    }
    HWND hLog = st ? st->hLog : NULL;
    if (!hLog) return;
    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    if (len > 0) SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
    if (st && st->hLogSb) msb_notify_content_changed(st->hLogSb);
}

static void Ai_StreamChunkCallback(void* context, const std::wstring& chunk)
{
    auto* chunkInfo = (AiStreamChunk*)context;
    if (!chunkInfo || chunk.empty()) return;

    chunkInfo->streamed = true;
    if (IsWindow(chunkInfo->hwnd)) {
        auto* queued = new AiStreamChunk(*chunkInfo);
        queued->chunk = chunk;
        if (!PostMessageW(chunkInfo->hwnd, WM_AI_SEND_APPEND, 0, (LPARAM)queued)) {
            delete queued;
        }
    }
}

static LRESULT CALLBACK Ai_LogSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR dwRefData)
{
    HWND hwndParent = (HWND)dwRefData;
    switch (msg) {
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt = {};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (AiCopyCode_IsHot(hwnd, pt)) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_TIMER:
        AiCopyCode_HandleTimer(hwnd, (UINT_PTR)wParam);
        return 0;
    case WM_CONTEXTMENU: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        if (pt.x == -1 && pt.y == -1) {
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            pt.x = rc.left + S(12);
            pt.y = rc.top + S(12);
            ClientToScreen(hwnd, &pt);
        }
        HMENU hMenu = CreatePopupMenu();
        if (hMenu) {
            AppendMenuW(hMenu, MF_STRING, IDM_AI_LOG_COPY, Ne_Ls(L"BTN_COPY"));
            SetForegroundWindow(hwndParent);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwndParent, NULL);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void Ai_BeginBusyState(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    if (!st->spinner) {
        st->spinner = new SpinnerDialog(hwnd);
    }
    if (st->spinner) {
        st->spinner->Show(Ne_Ls(L"AI_WAKING_OLLAMA"));
    }
    if (st->hSendBtn) {
        EnableWindow(st->hSendBtn, FALSE);
    }
}

static void Ai_EndBusyState(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    if (st->spinner) {
        st->spinner->Hide();
    }
    if (st->hSendBtn) {
        EnableWindow(st->hSendBtn, TRUE);
    }
}

static void Ai_StopLiveTypingTimer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || !st->liveTypingTimerRunning) return;
    KillTimer(hwnd, kAiLiveTypingTimerId);
    st->liveTypingTimerRunning = false;
}

static void Ai_StopLiveTypingStartDelayTimer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || !st->liveTypingStartTimerRunning) return;
    KillTimer(hwnd, kAiLiveTypingStartDelayTimerId);
    st->liveTypingStartTimerRunning = false;
}

static void Ai_StartLiveTypingStartDelayTimer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->liveTypingStartTimerRunning) return;
    SetTimer(hwnd, kAiLiveTypingStartDelayTimerId, kAiLiveTypingStartDelayMs, NULL);
    st->liveTypingStartTimerRunning = true;
    st->liveTypingStartReady = false;
}

static void Ai_StartLiveTypingTimer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    if (!st->liveTypingTimerRunning) {
        SetTimer(hwnd, kAiLiveTypingTimerId, kAiLiveTypingTickMs, NULL);
        st->liveTypingTimerRunning = true;
    }
}

static void Ai_AppendLiveTypingText(HWND hwnd, const std::wstring& text)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || text.empty()) return;

    if (st->spinner) {
        st->spinner->Hide();
        delete st->spinner;
        st->spinner = NULL;
    }

    HWND hLog = GetDlgItem(hwnd, IDC_AI_LOG);
    if (!hLog) return;

    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
    if (st->hLogSb) {
        msb_notify_content_changed(st->hLogSb);
    }

    st->liveReply += text;
    s_aiAnswerCopyRanges[hLog] = CHARRANGE{ st->replyBaseStart, GetWindowTextLengthW(hLog) };
}

static void Ai_RestoreOwnerOnClose(HWND hwnd)
{
    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (!owner || !IsWindow(owner)) return;

    SetWindowPos(owner, HWND_TOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(owner, HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(owner);
}

static void Ai_PumpLiveTyping(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;

    if (st->liveTypingQueue.empty()) {
        Ai_StopLiveTypingTimer(hwnd);
        Ai_FinalizeLiveReply(hwnd);
        return;
    }

    size_t count = std::min(kAiLiveTypingSlice, st->liveTypingQueue.size());
    std::wstring slice = st->liveTypingQueue.substr(0, count);
    st->liveTypingQueue.erase(0, count);
    Ai_AppendLiveTypingText(hwnd, slice);

    if (st->liveTypingQueue.empty() && st->liveTypingDone) {
        Ai_StopLiveTypingTimer(hwnd);
        Ai_FinalizeLiveReply(hwnd);
    }
}

static bool Ai_IsCodeRelatedPrompt(const std::wstring& prompt)
{
    std::wstring lower = prompt;
    for (wchar_t& ch : lower) {
        ch = (wchar_t)towlower(ch);
    }

    return lower.find(L"code") != std::wstring::npos ||
        lower.find(L"program") != std::wstring::npos ||
        lower.find(L"function") != std::wstring::npos ||
        lower.find(L"class") != std::wstring::npos ||
        lower.find(L"compile") != std::wstring::npos ||
        lower.find(L"debug") != std::wstring::npos ||
        lower.find(L"syntax") != std::wstring::npos ||
        lower.find(L"markdown") != std::wstring::npos ||
        lower.find(L"snippet") != std::wstring::npos;
}

static void Ai_ModelCheckPostLine(HWND hwnd, const std::wstring& line)
{
    if (!IsWindow(hwnd)) return;
    auto* text = new std::wstring(line);
    PostMessageW(hwnd, WM_AI_MODELCHECK_APPEND, 0, (LPARAM)text);
}
static std::wstring Ai_FormatLocaleText(const wchar_t* fmt, const std::wstring& value)
{
    std::wstring text = fmt ? fmt : L"";
    size_t pos = text.find(L"%s");
    if (pos != std::wstring::npos) {
        text.replace(pos, 2, value);
    }
    return text;
}

static void Ai_ModelCheckProgress(void* context, const std::wstring& status,
    unsigned long long completed, unsigned long long total)
{
    auto* pc = (AiModelCheckProgressContext*)context;
    if (!pc || !IsWindow(pc->hwnd)) return;
    std::wstring lower = status;
    for (wchar_t& ch : lower) ch = (wchar_t)towlower(ch);
    if (!pc->announcedDownload && ((completed > 0 && total > 0) || lower.find(L"downloading") != std::wstring::npos || lower.find(L"pulling") != std::wstring::npos)) {
        pc->announcedDownload = true;
        Ai_ModelCheckPostLine(pc->hwnd, Ai_FormatLocaleText(Ne_Ls(L"AI_MODEL_DOWNLOADING_MODEL"), pc->model));
    }
}

static void Ai_ModelCheckWorker(HWND hwnd)
{
    std::thread([hwnd]() {
        auto post = [hwnd](const std::wstring& line) {
            Ai_ModelCheckPostLine(hwnd, line);
        };

        std::vector<std::wstring> models;
        if (NeAiClient_ListOllamaModels(models)) {
            for (std::wstring& model : models) {
                Ai_NormalizeModelNameLocal(model);
            }
        }
        Ai_AddUniqueModel(models, Ai_DefaultModelName());
        Ai_AddUniqueModel(models, Ai_FallbackModelName());

        bool anyError = false;
        if (models.empty()) {
            post(Ne_Ls(L"AI_MODEL_CHECK_NO_MODELS"));
        } else {
            for (const std::wstring& model : models) {
                post(Ai_FormatLocaleText(Ne_Ls(L"AI_MODEL_CHECKING_MODEL"), model));
                AiModelCheckProgressContext progress = {};
                progress.hwnd = hwnd;
                progress.model = model;
                std::wstring error;
                if (NeAiClient_PullOllamaModel(model, &progress, Ai_ModelCheckProgress, error)) {
                    post(Ai_FormatLocaleText(Ne_Ls(L"AI_MODEL_UPTODATE_MODEL"), model));
                } else {
                    anyError = true;
                    if (error.empty()) {
                        post(Ai_FormatLocaleText(Ne_Ls(L"AI_MODEL_CHECK_FAILED_SIMPLE"), model));
                    } else {
                        post(Ai_FormatLocaleText(Ne_Ls(L"AI_MODEL_CHECK_FAILED"), model) + L" " + error);
                    }
                }
            }
        }

        post(anyError ? Ne_Ls(L"AI_MODEL_CHECK_FINISHED_ERROR") : Ne_Ls(L"AI_MODEL_CHECK_ALL_DONE"));
        if (IsWindow(hwnd)) {
            PostMessageW(hwnd, WM_AI_MODELCHECK_DONE, (WPARAM)(anyError ? 1 : 0), 0);
        }
    }).detach();
}

static LRESULT CALLBACK Ai_ModelCheckWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AiModelCheckState* st = (AiModelCheckState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lParam;
        st = (AiModelCheckState*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        if (!st) return -1;
        st->hwnd = hwnd;
        st->hFont = Ai_MakeDlgFont(hwnd, false);
        st->dd = new AiDialogData();
        if (!st->dd) return -1;
        st->dd->buttonCount = 1;
        st->dd->buttons[0] = AiButtonSpec{ IDC_AI_CLOSE_BTN, Ne_Ls(L"BTN_CLOSE"), AiBtnTone::Red, Ai_MeasureButtonWidth(Ne_Ls(L"BTN_CLOSE")), true };

        HWND hTitle = CreateWindowExW(0, L"STATIC", Ne_Ls(L"AI_MODEL_CHECK_TITLE"),
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
            S(12), S(12), S(500), S(22), hwnd, NULL, GetModuleHandleW(NULL), NULL);
        if (hTitle && st->hFont) SendMessageW(hTitle, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        st->hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            S(12), S(40), S(620), S(280), hwnd, NULL, GetModuleHandleW(NULL), NULL);
        if (st->hLog && st->hFont) SendMessageW(st->hLog, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        st->hClose = CreateWindowExW(0, L"BUTTON", Ne_Ls(L"BTN_CLOSE"),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | WS_DISABLED,
            S(12), S(332), st->dd->buttons[0].width, S(34), hwnd, (HMENU)(UINT_PTR)IDC_AI_CLOSE_BTN, GetModuleHandleW(NULL), NULL);
        if (st->hClose && st->hFont) SendMessageW(st->hClose, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        Ai_ModelCheckPostLine(hwnd, Ne_Ls(L"AI_MODEL_CHECK_START"));
        Ai_ModelCheckWorker(hwnd);
        return 0;
    }
    case WM_SIZE: {
        if (!st) break;
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        int pad = S(12);
        int btnW = st->dd ? st->dd->buttons[0].width : S(100);
        int btnH = S(34);
        if (st->hLog) SetWindowPos(st->hLog, NULL, pad, S(40), std::max(0, (int)rc.right - pad * 2), std::max(0, (int)rc.bottom - S(40) - pad - btnH - S(10)), SWP_NOZORDER | SWP_NOACTIVATE);
        if (st->hClose) SetWindowPos(st->hClose, NULL, std::max(pad, ((int)rc.right - btnW) / 2), rc.bottom - pad - btnH, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_COMMAND:
        if (st && st->done && (LOWORD(wParam) == IDC_AI_CLOSE_BTN || LOWORD(wParam) == IDCANCEL)) {
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (st && !st->done) {
            MessageBeep(MB_ICONASTERISK);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_AI_MODELCHECK_APPEND: {
        auto* line = (std::wstring*)lParam;
        if (st && st->hLog && line) {
            int len = GetWindowTextLengthW(st->hLog);
            SendMessageW(st->hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            if (len > 0) SendMessageW(st->hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
            SendMessageW(st->hLog, EM_REPLACESEL, FALSE, (LPARAM)line->c_str());
            SendMessageW(st->hLog, EM_SCROLLCARET, 0, 0);
        }
        delete line;
        return 0;
    }
    case WM_AI_MODELCHECK_DONE:
        if (st) {
            st->done = true;
            if (st->hClose) EnableWindow(st->hClose, TRUE);
        }
        return 0;
    case WM_DRAWITEM:
        if (st && st->dd) {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis && Ai_ButtonIndexById(st->dd, dis->CtlID) >= 0) {
                Ai_DrawButton(dis, st->dd);
                return TRUE;
            }
        }
        break;
    case WM_DESTROY:
        if (st && st->hFont) {
            DeleteObject(st->hFont);
            st->hFont = NULL;
        }
        if (st && st->dd) {
            delete st->dd;
            st->dd = NULL;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ai_ShowModelCheckDialog(HWND parent)
{
    if (!parent || !IsWindow(parent)) return;
    auto* st = new AiModelCheckState();
    st->parent = parent;
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = Ai_ModelCheckWndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(kSystemArrowCursor));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"NSBEditAiModelCheckDialog";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) {
        RegisterClassW(&wc);
    }

    RECT pr = {};
    GetWindowRect(parent, &pr);
    const int clientW = S(640);
    const int clientH = S(380);
    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;
    int x = pr.left + std::max(0, (int)((pr.right - pr.left - winW) / 2));
    int y = pr.top + std::max(0, (int)((pr.bottom - pr.top - winH) / 2));

    EnableWindow(parent, FALSE);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        wc.lpszClassName, Ne_Ls(L"AI_MODEL_CHECK_TITLE"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, winW, winH, parent, NULL, hi, st);
    if (!hwnd) {
        EnableWindow(parent, TRUE);
        delete st;
        return;
    }
    st->hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    MSG msg = {};
    while (IsWindow(hwnd) && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

static void Ai_StartSendWorker(HWND hwnd, std::wstring prompt, std::wstring model, std::wstring fallback, int answerStart)
{
    AiSendWorkItem work;
    work.hwnd = hwnd;
    work.prompt = std::move(prompt);
    work.model = std::move(model);
    work.fallback = std::move(fallback);
    work.answerStart = answerStart;

    std::thread([work = std::move(work)]() mutable {
        auto* result = new AiSendResult();
        std::wstring formattedPrompt = L"Answer plainly. Preserve indentation.\r\n\r\n";
        if (Ai_IsCodeRelatedPrompt(work.prompt)) {
            formattedPrompt +=
                L"If you include code, keep it readable and do not add extra markup around it.\r\n\r\n";
        }
        formattedPrompt += L"Keep the response concise unless the task needs detail.\r\n\r\n";
        formattedPrompt += work.prompt;

        AiStreamChunk chunkInfo = {};
        chunkInfo.hwnd = work.hwnd;
        chunkInfo.answerStart = work.answerStart;

        if (NeAiClient_AskOllamaStream(work.model, formattedPrompt, &chunkInfo, Ai_StreamChunkCallback, result->reply, result->error)) {
            result->ok = true;
        } else {
            bool modelMissing = !result->error.empty() &&
                (result->error.find(L"not found") != std::wstring::npos ||
                 result->error.find(L"does not exist") != std::wstring::npos ||
                 result->error.find(L"pull the model") != std::wstring::npos);
            if (modelMissing && work.fallback != work.model) {
                result->usedFallback = true;
                std::wstring fallbackError;
                chunkInfo.streamed = false;
                if (NeAiClient_AskOllamaStream(work.fallback, formattedPrompt, &chunkInfo, Ai_StreamChunkCallback, result->reply, fallbackError)) {
                    result->ok = true;
                    result->error.clear();
                } else {
                    result->error = fallbackError.empty() ? result->error : fallbackError;
                }
            }
        }

        if (result->reply.empty() && !chunkInfo.streamed) {
            std::wstring recoveryModel = work.fallback.empty() ? work.model : work.fallback;
            if (recoveryModel.empty()) {
                recoveryModel = Ai_DefaultModelName();
            }

            std::wstring recoveryReply;
            std::wstring recoveryError;
            if (NeAiClient_AskOllama(recoveryModel, formattedPrompt, recoveryReply, recoveryError) && !recoveryReply.empty()) {
                result->ok = true;
                result->reply = recoveryReply;
                result->error.clear();
            } else if (result->error.empty() && !recoveryError.empty()) {
                result->error = recoveryError;
            }
        }

        result->streamed = result->streamed || chunkInfo.streamed;

        if (IsWindow(work.hwnd)) {
            PostMessageW(work.hwnd, WM_AI_SEND_COMPLETE, 0, (LPARAM)result);
        } else {
            delete result;
        }
    }).detach();
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
    while (!model.empty() && iswspace(model.back())) {
        model.pop_back();
    }
    const wchar_t* kAgentSuffix = L" Agent";
    const wchar_t* kSuggestSuffix = L" Suggest";
    if (model.size() > 6 && model.compare(model.size() - 6, 6, kAgentSuffix) == 0) {
        model.erase(model.size() - 6);
    } else if (model.size() > 8 && model.compare(model.size() - 8, 8, kSuggestSuffix) == 0) {
        model.erase(model.size() - 8);
    }
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
    label += L" ";
    label += (role == AiMenuRole::Suggest) ? Ne_Ls(L"AI_ROLE_SUGGEST") : Ne_Ls(L"AI_ROLE_AGENT");
    return label;
}

static std::wstring Ai_CurrentModeLabel(const AiWindowState* st)
{
    if (!st) return Ai_DefaultModelName() + L" " + Ne_Ls(L"AI_ROLE_SUGGEST");
    if (st->cloudMode) {
        return std::wstring(Ne_Ls(L"AI_MENU_CLOUD")) + L" " + ((st->role == AiMenuRole::Suggest) ? Ne_Ls(L"AI_ROLE_SUGGEST") : Ne_Ls(L"AI_ROLE_AGENT"));
    }
    std::wstring label = st->model.empty() ? Ai_DefaultModelName() : st->model;
    label += L" ";
    label += (st->role == AiMenuRole::Suggest) ? Ne_Ls(L"AI_ROLE_SUGGEST") : Ne_Ls(L"AI_ROLE_AGENT");
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
    Ai_AppendMenuOD(hMenu, flags, entry.id, Ai_ModelMenuLabel(model, role).c_str(), false);
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
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    ncm.lfMenuFont.lfHeight = -MulDiv(12, dpi > 0 ? dpi : 96, 72);
    ncm.lfMenuFont.lfWeight = FW_NORMAL;
    ncm.lfMenuFont.lfQuality = CLEARTYPE_QUALITY;
    lstrcpyW(ncm.lfMenuFont.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&ncm.lfMenuFont);
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
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW((HWND)dwRefData, GWLP_USERDATA);
    bool plainEnter = !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000);
    if (msg == WM_KEYDOWN && wParam == VK_UP && st) {
        DWORD selStart = 0;
        DWORD selEnd = 0;
        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        int line = (int)SendMessageW(hwnd, EM_LINEFROMCHAR, (WPARAM)selStart, 0);
        if (line <= 0 && Ai_HistoryBrowse((HWND)dwRefData, st, -1)) {
            return 0;
        }
    }
    if (msg == WM_KEYDOWN && wParam == VK_DOWN && st) {
        DWORD selStart = 0;
        DWORD selEnd = 0;
        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        int line = (int)SendMessageW(hwnd, EM_LINEFROMCHAR, (WPARAM)selStart, 0);
        if (st->historyIndex >= 0 && line <= 0 && Ai_HistoryBrowse((HWND)dwRefData, st, +1)) {
            return 0;
        }
    }
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
    st->dd->buttons[0] = AiButtonSpec{ IDC_AI_SEND_BTN,  Ne_Ls(L"BTN_SEND"),  AiBtnTone::Green, Ai_MeasureButtonWidth(Ne_Ls(L"BTN_SEND")),  true };
    st->dd->buttons[1] = AiButtonSpec{ IDC_AI_COPY_BTN,  Ne_Ls(L"BTN_COPY"),  AiBtnTone::Blue,  Ai_MeasureButtonWidth(Ne_Ls(L"BTN_COPY")),  false, true };
    st->dd->buttons[2] = AiButtonSpec{ IDC_AI_CLEAR_BTN, Ne_Ls(L"BTN_CLEAR"), AiBtnTone::Red,   Ai_MeasureButtonWidth(Ne_Ls(L"BTN_CLEAR")), true };
    st->dd->buttons[3] = AiButtonSpec{ IDC_AI_CLOSE_BTN, Ne_Ls(L"BTN_CLOSE"), AiBtnTone::Red,   Ai_MeasureButtonWidth(Ne_Ls(L"BTN_CLOSE")), true };
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

    Ai_AppendMenuOD(hModel, MF_SEPARATOR, 0, NULL, false);
    Ai_AddModelMenuItem(hModel, Ne_Ls(L"AI_MENU_CLOUD"), AiMenuRole::Suggest, true, st && st->signedIn);
    Ai_AddModelMenuItem(hModel, Ne_Ls(L"AI_MENU_CLOUD"), AiMenuRole::Agent, true, st && st->signedIn);

    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_MODE_LOCAL, Ne_Ls(L"AI_MENU_LOCAL_ONLY"), false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_MODE_CLOUD, Ne_Ls(L"AI_MENU_CLOUD_SUBSCRIPTION"), false);
    Ai_AppendMenuOD(hCloud, MF_SEPARATOR, 0, NULL, false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_SIGN_IN, Ne_Ls(L"AI_MENU_SIGN_IN"), false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_SIGN_OUT, Ne_Ls(L"AI_MENU_SIGN_OUT"), false);
    Ai_AppendMenuOD(hCloud, MF_SEPARATOR, 0, NULL, false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_CHECK_MODELS, Ne_Ls(L"MENU_AI_CHECK_MODELS"), false);
    Ai_AppendMenuOD(hCloud, MF_SEPARATOR, 0, NULL, false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_OPEN_PROVIDER, Ne_Ls(L"AI_MENU_OPEN_PROVIDER"), false);

    Ai_AppendMenuOD(hLog, MF_STRING, IDM_AI_SEND, Ne_Ls(L"AI_MENU_SEND_PROMPT"), false);
    Ai_AppendMenuOD(hLog, MF_STRING, IDM_AI_LOG_CLEAR, Ne_Ls(L"AI_MENU_CLEAR_LOG"), false);
    Ai_AppendMenuOD(hLog, MF_STRING, IDM_AI_LOG_COPY, Ne_Ls(L"AI_MENU_COPY_ANSWER"), false);

    Ai_AppendMenuOD(hHelp, MF_STRING, IDM_AI_ABOUT, Ne_Ls(L"AI_MENU_ABOUT_WINDOW"), false);

    Ai_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hModel, Ne_Ls(L"AI_MENU_MODEL"), true);
    Ai_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hCloud, Ne_Ls(L"AI_MENU_CLOUD"), true);
    Ai_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hLog, Ne_Ls(L"AI_MENU_LOG"), true);
    Ai_AppendMenuOD(hMenu, MF_POPUP, (UINT_PTR)hHelp, Ne_Ls(L"AI_MENU_HELP"), true);

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

    Ai_HistoryRemember(st, prompt);
    Ai_SavePrefs(st);

    Ai_StopThinkingTimer(hwnd);
    Ai_StartThinkingTimer(hwnd);

    if (hInput) SetWindowTextW(hInput, L"");

    std::wstring userLine = Ne_Ls(L"AI_LOG_USER_PREFIX");
    userLine += prompt;
    Ai_AppendLog(hwnd, userLine);
    if (st->cloudMode) {
        if (!st->signedIn) {
            Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_CLOUD_NEEDS_SIGNIN"));
            Ai_OpenOllamaSignUp(hwnd);
            Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_OPENED_SIGNUP"));
        } else {
            Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_CLOUD_NOT_USED_YET"));
        }
        return;
    }

    Ai_BeginBusyState(hwnd);

    HWND hLog = st->hLog;
    if (!hLog || !IsWindow(hLog)) {
        HWND hHost = st->hAnswerHost;
        if (hHost && IsWindow(hHost)) {
            RECT rc = {};
            GetClientRect(hHost, &rc);
            hLog = Ai_CreateAnswerRichEdit(hHost, S(8), S(8), std::max(1, (int)rc.right - S(16)), std::max(1, (int)rc.bottom - S(16)));
            if (hLog && st->hPaneFont) SendMessageW(hLog, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
            if (hLog) {
                SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
                Ai_SetWrapToWindow(hLog);
            }
            st->hLog = hLog;
        }
    }
    if (hLog) {
        st->historyDraft.clear();
        st->historyIndex = -1;
        Ai_ClearRenderedAnswer(hwnd);
        Ai_AppendRichRun(hLog, L"\r\n", nullptr);
        Ai_AppendRichRun(hLog, L"Ollama:\r\n", nullptr);
    }

    std::wstring selectedModel = st->model.empty() ? Ai_DefaultModelName() : st->model;
    std::wstring fallbackModel = st->fallback.empty() ? Ai_FallbackModelName() : st->fallback;
    Ai_NormalizeModelNameLocal(selectedModel);
    Ai_NormalizeModelNameLocal(fallbackModel);
    if (hLog) {
        st->replyBaseStart = GetWindowTextLengthW(hLog);
        st->historyDraft = L"";
        st->liveReply.clear();
        st->liveTypingQueue.clear();
        st->liveTypingStartReady = true;
        st->liveTypingStartTimerRunning = false;
        st->liveTypingDone = false;
        st->liveTypingFinalRendered = false;
        Ai_StopLiveTypingTimer(hwnd);
        Ai_StopLiveTypingStartDelayTimer(hwnd);
        AiSendWorkItem work;
        work.answerStart = st->replyBaseStart;
        Ai_StartSendWorker(hwnd, prompt, selectedModel, fallbackModel, work.answerStart);
    } else {
        st->replyBaseStart = 0;
        st->liveReply.clear();
        st->liveTypingQueue.clear();
        st->liveTypingStartReady = true;
        st->liveTypingStartTimerRunning = false;
        st->liveTypingDone = false;
        st->liveTypingFinalRendered = false;
        Ai_StopLiveTypingTimer(hwnd);
        Ai_StopLiveTypingStartDelayTimer(hwnd);
        Ai_StartSendWorker(hwnd, prompt, selectedModel, fallbackModel, 0);
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

        SetMenu(hwnd, Ai_BuildMenu(st));

        HWND hHdr = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
            S(10), S(10), S(800), S(24), hwnd, (HMENU)(UINT_PTR)IDC_AI_HEADER, GetModuleHandleW(NULL), NULL);
        if (hHdr && st->hFont) SendMessageW(hHdr, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        HWND hAnswerHost = Ai_CreateAnswerHost(hwnd, S(10), S(86), S(820), S(340));
        if (hAnswerHost) {
            st->hAnswerHost = hAnswerHost;
            st->hLogSb = msb_attach(hAnswerHost, MSB_VERTICAL);
            HWND hLog = Ai_CreateAnswerRichEdit(hAnswerHost, S(8), S(8), S(800), S(300));
            if (hLog && st->hPaneFont) SendMessageW(hLog, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
            if (hLog) {
                SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
                Ai_SetWrapToWindow(hLog);
            }
            st->hLog = hLog;
        }

        LoadLibraryW(L"Msftedit.dll");
        const wchar_t* reClass = L"EDIT";
        HMODULE hMsft = GetModuleHandleW(L"Msftedit.dll");
        if (hMsft) {
            WNDCLASSEXW wce = { sizeof(wce) };
            if (GetClassInfoExW(hMsft, L"RICHEDIT50W", &wce)) {
                reClass = L"RICHEDIT50W";
            }
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
        st->hInput = hInput;
        st->hStatus = hStatus;
        st->hSendBtn = hSend;
        st->hCopyBtn = hCopy;
        st->hClearBtn = hClear;
        st->hCloseBtn = hClose;

        Ai_RefreshUi(hwnd);
        Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_WINDOW_OPENED"));
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
        HWND hLog = st->hAnswerHost;
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
        if (st->hLog && IsWindow(st->hLog)) {
            SetWindowPos(st->hLog, NULL, S(8), S(8), std::max(1, (int)rc.right - S(16)), std::max(1, logH - S(16)), SWP_NOZORDER | SWP_NOACTIVATE);
        }
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
                        ? Ne_Ls(L"AI_LOG_CLOUD_SUGGEST_SELECTED")
                        : Ne_Ls(L"AI_LOG_CLOUD_AGENT_SELECTED"))
                    : ((modelItem->role == AiMenuRole::Suggest)
                        ? Ne_Ls(L"AI_LOG_MODEL_SWITCHED_SUGGESTION")
                        : Ne_Ls(L"AI_LOG_MODEL_SWITCHED_AGENT")));
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
                Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_CLOUD_LOCAL_ONLY"));
            }
            return 0;
        case IDM_AI_MODE_CLOUD:
            if (st) {
                st->cloudMode = true;
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_CLOUD_SUB_SELECTED"));
            }
            return 0;
        case IDM_AI_SIGN_IN:
            if (st) {
                st->signedIn = true;
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                ShellExecuteW(hwnd, L"open", L"https://ollama.com", NULL, NULL, SW_SHOWNORMAL);
                Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_OPENED_SIGNUP"));
            }
            return 0;
        case IDM_AI_SIGN_OUT:
            if (st) {
                st->signedIn = false;
                Ai_SavePrefs(st);
                Ai_RefreshUi(hwnd);
                Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_SIGNED_OUT_LOCALLY"));
            }
            return 0;
        case IDM_AI_OPEN_PROVIDER:
            ShellExecuteW(hwnd, L"open", L"https://ollama.com", NULL, NULL, SW_SHOWNORMAL);
            return 0;
        case IDM_AI_CHECK_MODELS:
            Ai_ShowModelCheckDialog(hwnd);
            return 0;
        case IDM_AI_LOG_CLEAR:
            if (st) {
                if (st->hLog && IsWindow(st->hLog)) {
                    SetWindowTextW(st->hLog, L"");
                }
                Ai_ClearRenderedAnswer(hwnd);
                st->answerRawMarkdown.clear();
                st->answerCopyText.clear();
            }
            return 0;
        case IDM_AI_LOG_COPY:
            if (st && st->hAnswerHost) {
                Ai_CopyAnswerText(st->hAnswerHost);
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
            Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_ABOUT_WINDOW"));
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_SETFOCUS:
        Ai_RefreshUi(hwnd);
        return 0;
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr && hdr->idFrom == IDC_AI_LOG && hdr->code == EN_LINK) {
            ENLINK* link = (ENLINK*)lParam;
            if (link && link->msg == WM_LBUTTONUP) {
                POINT pt = { (LONG)(short)LOWORD(link->lParam), (LONG)(short)HIWORD(link->lParam) };
                if (AiCopyCode_HandleClick(hdr->hwndFrom, pt)) {
                    return 0;
                }
            }
        }
        break;
    }
    case WM_TIMER:
        if (wParam == kAiThinkingTimerId) {
            Ai_SetThinkingStatusText(hwnd);
            return 0;
        }
        if (wParam == kAiLiveTypingStartDelayTimerId) {
            if (st) {
                st->liveTypingStartTimerRunning = false;
                st->liveTypingStartReady = true;
            }
            Ai_StopLiveTypingStartDelayTimer(hwnd);
            Ai_SetThinkingStatusText(hwnd);
            Ai_StartLiveTypingTimer(hwnd);
            return 0;
        }
        if (wParam == kAiLiveTypingTimerId) {
            Ai_PumpLiveTyping(hwnd);
            return 0;
        }
        break;
    case WM_AI_SEND_APPEND: {
        auto* chunk = (AiStreamChunk*)lParam;
        if (st && chunk && !chunk->chunk.empty()) {
            if (st->liveReply.empty() && st->liveTypingQueue.empty()) {
                Ai_SetThinkingStatusText(hwnd);
            }
            if (st->spinner) {
                st->spinner->Hide();
                delete st->spinner;
                st->spinner = NULL;
            }
            std::wstring liveText = chunk->chunk;
            Ai_UnescapeModelText(liveText);
            if (!liveText.empty()) {
                st->liveTypingQueue += liveText;
                Ai_StartLiveTypingTimer(hwnd);
            }
        }
        if (chunk) {
            delete chunk;
        }
        return 0;
    }
    case WM_AI_SEND_COMPLETE: {
        Ai_EndBusyState(hwnd);
        auto* result = (AiSendResult*)lParam;
        if (result) {
            if (st) {
                st->liveTypingDone = true;
            }
            Ai_StopThinkingTimer(hwnd);
            if (result->usedFallback) {
                Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_RETRY_FALLBACK"));
            }
            if (result->ok) {
                if (st && !result->streamed && st->liveReply.empty() && !result->reply.empty() && !st->liveTypingFinalRendered) {
                    st->liveReply = result->reply;
                    Ai_RenderMarkdownReply(hwnd, st->liveReply);
                    st->liveTypingFinalRendered = true;
                } else {
                    Ai_FinalizeLiveReply(hwnd);
                }
            }
            if (!result->ok && result->reply.empty()) {
                Ai_AppendLog(hwnd, std::wstring(Ne_Ls(L"AI_LOG_OLLAMA_PREFIX")) + L" " + (result->error.empty() ? Ne_Ls(L"AI_LOG_NO_RESPONSE") : result->error));
            } else {
                if (!result->ok && !result->error.empty()) {
                    Ai_AppendLog(hwnd, std::wstring(Ne_Ls(L"AI_LOG_OLLAMA_PREFIX")) + L" " + result->error);
                }
            }
            delete result;
        }
        return 0;
    }
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
        Ai_StopLiveTypingTimer(hwnd);
        Ai_StopThinkingTimer(hwnd);
        if (st) {
            if (st->hLogSb) msb_detach(st->hLogSb);
            if (st->hInputSb) msb_detach(st->hInputSb);
            if (st->spinner) { st->spinner->Hide(); delete st->spinner; st->spinner = NULL; }
            if (st->hFont) DeleteObject(st->hFont);
            if (st->hPaneFont) DeleteObject(st->hPaneFont);
            if (s_hAiMenuFont) DeleteObject(s_hAiMenuFont);
            Ai_ClearMenuStorage();
            if (st->hLog) {
                AiCopyCode_HandleDestroy(st->hLog);
                s_aiAnswerCopyRanges.erase(st->hLog);
            }
            delete st->dd;
            delete st;
        }
        Ai_RestoreOwnerOnClose(hwnd);
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
        wc.lpszClassName, Ne_Ls(L"AI_WINDOW_TITLE_PREFIX"),
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
