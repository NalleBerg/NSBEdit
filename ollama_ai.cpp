#include "ollama_ai.h"
#include "NSBEdit.h"
#include "copy_ai_code.h"
#include "ai_markdown_helper.h"

#include "dpi.h"
#include "ne_ai_client.h"
#include "ne_ai_bootstrap.h"
#include "ne_profiles.h"
#include "ne_projects.h"
#include "scroll/my_scrollbar_vscroll.h"

#include <gdiplus.h>
#include <commctrl.h>
#include <richedit.h>
#include "ILexer.h"
#include "Scintilla.h"
#include "ScintillaMessages.h"
#include "SciLexer.h"
#include "Lexilla.h"
#include "third_party/cmark-gfm/src/cmark-gfm.h"
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cwctype>
#include <regex>

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
#define IDC_AI_STATUS_TIMER       1960
#define IDC_AI_STOP_BTN           1961
#define IDC_AI_PREV_BTN           1962
#define IDC_AI_NEXT_BTN           1963

#define IDM_AI_MODEL_DEFAULT      1901
#define IDM_AI_MODEL_FALLBACK     1902
#define IDM_AI_MODE_LOCAL         1910
#define IDM_AI_MODE_CLOUD         1911
#define IDM_AI_SIGN_IN            1920
#define IDM_AI_SIGN_OUT           1921
#define IDM_AI_OPEN_PROVIDER      1922
#define IDM_AI_CHECK_MODELS       1923
#define IDM_AI_SET_API_KEY        1924
#define IDM_AI_LOG_CLEAR          1930
#define IDM_AI_LOG_COPY           1931
#define IDM_AI_SEND               1932
#define IDM_AI_ABOUT              1933
#define IDM_AI_CODE_COPY          1934
#define WM_AI_SEND_COMPLETE       (WM_APP + 71)
#define WM_AI_MODELCHECK_APPEND   (WM_APP + 72)
#define WM_AI_MODELCHECK_DONE     (WM_APP + 73)
#define WM_AI_SEND_APPEND         (WM_APP + 74)
#define WM_AI_PROGRESS            (WM_APP + 75)
#define WM_AI_CLOUDMODELS_READY   (WM_APP + 76)  // lParam = new std::vector<std::wstring>*

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
    AiButtonSpec buttons[7];
};

// ── Image helpers: clipboard image → PNG, base64/inline-RTF for the AI query ──

// Standard base64 encode of raw bytes (used for the Ollama images[] array and
// for persisting pasted images in the DB). Base64 is JSON- and TEXT-safe.
static std::string Ai_Base64Encode(const unsigned char* data, size_t len)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        unsigned n = ((unsigned)data[i] << 16) | ((unsigned)data[i + 1] << 8) | data[i + 2];
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += tbl[n & 63];
    }
    if (i < len) {
        unsigned n = (unsigned)data[i] << 16;
        bool two = (i + 1 < len);
        if (two) n |= (unsigned)data[i + 1] << 8;
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += two ? tbl[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}
static std::string Ai_Base64Encode(const std::string& s) {
    return Ai_Base64Encode((const unsigned char*)s.data(), s.size());
}

// Base64 decode back to raw bytes (used to redisplay images loaded from the DB).
static std::string Ai_Base64Decode(const std::string& in)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); }
    }
    return out;
}

// Locate the GDI+ PNG encoder CLSID.
static bool Ai_GetPngEncoderClsid(CLSID* clsid)
{
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<unsigned char> buf(size);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)buf.data();
    if (Gdiplus::GetImageEncoders(num, size, codecs) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (codecs[i].MimeType && wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            *clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// Grab the current clipboard image and re-encode it as PNG bytes. Returns an
// empty string when there is no image or the encode fails.
static std::string Ai_CaptureClipboardImagePng(HWND owner)
{
    std::string png;
    if (!IsClipboardFormatAvailable(CF_DIB) && !IsClipboardFormatAvailable(CF_BITMAP))
        return png;
    if (!OpenClipboard(owner)) return png;

    Gdiplus::Bitmap* bmp = nullptr;

    // Prefer CF_BITMAP (a DDB): FromHBITMAP gives correct top-down orientation.
    if (HANDLE hbm = GetClipboardData(CF_BITMAP)) {
        bmp = Gdiplus::Bitmap::FromHBITMAP((HBITMAP)hbm, NULL);
        if (bmp && bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; bmp = nullptr; }
    }
    // Fall back to a packed CF_DIB.
    if (!bmp) {
        if (HANDLE hDib = GetClipboardData(CF_DIB)) {
            if (void* p = GlobalLock(hDib)) {
                BITMAPINFO* bi = (BITMAPINFO*)p;
                BITMAPINFOHEADER* bih = &bi->bmiHeader;
                DWORD clrUsed = bih->biClrUsed;
                if (clrUsed == 0 && bih->biBitCount <= 8) clrUsed = 1u << bih->biBitCount;
                size_t paletteBytes = 0;
                if (bih->biBitCount <= 8) paletteBytes = (size_t)clrUsed * sizeof(RGBQUAD);
                else if (bih->biCompression == BI_BITFIELDS) paletteBytes = 3 * sizeof(DWORD);
                unsigned char* bits = (unsigned char*)p + bih->biSize + paletteBytes;
                bmp = new Gdiplus::Bitmap(bi, bits);
                if (bmp && bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; bmp = nullptr; }
                GlobalUnlock(hDib);
            }
        }
    }
    CloseClipboard();
    if (!bmp) return png;

    CLSID pngClsid;
    if (Ai_GetPngEncoderClsid(&pngClsid)) {
        IStream* stream = NULL;
        if (CreateStreamOnHGlobal(NULL, TRUE, &stream) == S_OK && stream) {
            if (bmp->Save(stream, &pngClsid, NULL) == Gdiplus::Ok) {
                HGLOBAL hg = NULL;
                if (GetHGlobalFromStream(stream, &hg) == S_OK && hg) {
                    SIZE_T sz = GlobalSize(hg);
                    if (void* mem = GlobalLock(hg)) {
                        if (sz) png.assign((const char*)mem, (size_t)sz);
                        GlobalUnlock(hg);
                    }
                }
            }
            stream->Release();
        }
    }
    delete bmp;
    return png;
}

// Read a PNG's pixel dimensions from its IHDR chunk.
static void Ai_PngDimensions(const std::string& png, int& w, int& h)
{
    w = 0; h = 0;
    if (png.size() >= 24 && (unsigned char)png[0] == 0x89 &&
        png[1] == 'P' && png[2] == 'N' && png[3] == 'G') {
        auto rd = [&](size_t off) -> unsigned {
            return ((unsigned)(unsigned char)png[off] << 24) |
                   ((unsigned)(unsigned char)png[off + 1] << 16) |
                   ((unsigned)(unsigned char)png[off + 2] << 8) |
                   ((unsigned)(unsigned char)png[off + 3]);
        };
        w = (int)rd(16);
        h = (int)rd(20);
    }
}

// EM_STREAMIN callback that feeds a std::string RTF buffer into a RichEdit.
struct AiRtfStreamBuf { const std::string* s; size_t pos; };
static DWORD CALLBACK Ai_RtfStreamInCb(DWORD_PTR cookie, LPBYTE buff, LONG cb, LONG* pcb)
{
    AiRtfStreamBuf* b = (AiRtfStreamBuf*)cookie;
    size_t remain = b->s->size() - b->pos;
    LONG n = (LONG)((size_t)cb < remain ? (size_t)cb : remain);
    if (n > 0) { memcpy(buff, b->s->data() + b->pos, (size_t)n); b->pos += (size_t)n; }
    *pcb = n;
    return 0;
}

// Insert a PNG image inline (in the text flow) at the current selection of a
// RichEdit, as an RTF \pict. The display size is capped so the image is big
// enough to read but not overwhelming.
static void Ai_InsertInlinePng(HWND hEdit, const std::string& png)
{
    if (!hEdit || png.empty()) return;
    int w = 0, h = 0;
    Ai_PngDimensions(png, w, h);
    if (w <= 0) w = 320;
    if (h <= 0) h = 240;

    // Twips at 96 dpi: 1 px ≈ 15 twips. Cap the width to ~420 px.
    int goalW = w * 15;
    int goalH = h * 15;
    const int kMaxW = 420 * 15;
    if (goalW > kMaxW) { goalH = (int)((long long)kMaxW * h / w); goalW = kMaxW; }

    static const char hx[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(png.size() * 2 + png.size() / 64 + 2);
    for (size_t i = 0; i < png.size(); ++i) {
        unsigned char b = (unsigned char)png[i];
        hex += hx[(b >> 4) & 0xF];
        hex += hx[b & 0xF];
        if ((i + 1) % 64 == 0) hex += '\n';
    }

    std::string snippet = "{\\rtf1\\ansi {\\pict\\pngblip";
    snippet += "\\picw"     + std::to_string(w);
    snippet += "\\pich"     + std::to_string(h);
    snippet += "\\picwgoal" + std::to_string(goalW);
    snippet += "\\pichgoal" + std::to_string(goalH);
    snippet += "\r\n" + hex + "}}";

    AiRtfStreamBuf sb = { &snippet, 0 };
    EDITSTREAM es = {};
    es.dwCookie = (DWORD_PTR)&sb;
    es.pfnCallback = Ai_RtfStreamInCb;
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
}

// Append a PNG image inline at the very end of a RichEdit's text.
static void Ai_AppendInlinePng(HWND hEdit, const std::string& png)
{
    if (!hEdit || png.empty()) return;
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    Ai_InsertInlinePng(hEdit, png);
}

// One turn of the accumulated conversation: the user's prompt, the raw markdown
// reply, and any images (base64-encoded PNG) the user attached to that query.
struct AiConversationTurn {
    std::wstring prompt;
    std::wstring replyMd;
    std::vector<std::string> images;
};

struct AiWindowState {
    HWND hHeader = NULL;
    HWND hLog = NULL;
    HWND hAnswerHost = NULL;
    HWND hInput = NULL;
    HWND hStatus = NULL;
    HWND hStatusTimer = NULL;
    HWND hSendBtn = NULL;
    HWND hStopBtn = NULL;
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
    std::wstring cloudModel;   // selected Ollama cloud model (served via the signed-in local daemon)
    std::wstring cloudApiKey;  // ollama.com API key (browser-free cloud sign-in); stored only in the local profile
    AiMenuRole role = AiMenuRole::Suggest;
    bool cloudMode = false;
    bool signedIn = false;
    int historyIndex = -1;
    std::wstring historyDraft;
    int replyBaseStart = 0;
    std::wstring liveReply;
    std::wstring liveRawReply;
    std::wstring liveTypingQueue;
    bool liveTypingStartReady = false;
    bool liveTypingStartTimerRunning = false;
    bool liveTypingTimerRunning = false;
    bool liveTypingDone = false;
    bool liveTypingFinalRendered = false;
    bool thinkingTimerRunning = false;
    ULONGLONG thinkingStartMs = 0;
    std::wstring lastThinkingElapsedText;
    bool chunkedActive = false;   // batch map-reduce running: spinner shows real batch %
    bool sendCanceled = false;    // Stop pressed: ignore the worker's completion post
    std::wstring lastPrompt;      // raw prompt of the in-flight send, restored on Stop
    std::vector<std::string> pendingImages; // base64 PNG images pasted into the current input
    std::vector<std::string> lastImages;    // images of the in-flight send, restored on Stop
    SpinnerDialog* spinner = NULL;
    std::wstring answerCopyText;
    std::vector<HWND> answerBlocks;
    std::wstring answerIntroText;
    std::wstring answerRawMarkdown;
    int answerWheelAccum = 0;
    int answerScrollY = 0;
    int answerContentHeight = 0;
    // ── Conversation history (accumulated answers, persisted to the DB) ───────
    // Each entry is one turn: the user's prompt and the raw markdown reply. The
    // whole conversation is re-rendered into hLog on each new answer, separated
    // by dividers. turnAnchors holds the char offset in hLog where each turn's
    // "You:" line begins, for the Prev/Next navigation buttons.
    std::vector<AiConversationTurn> conversation;
    std::vector<int> turnAnchors;
    int navTurn = -1;
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
    // Base64-encoded PNG images attached to this query (multimodal send).
    std::vector<std::string> images;
    // When mapChunks is non-empty the worker runs a chunked map-reduce over a
    // large file: each chunk is analysed, then reduceHeader + notes + prompt is
    // streamed as the final answer.
    std::vector<std::wstring> mapChunks;
    std::wstring reduceHeader;
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
// True while the query input's WM_PASTE handler is inserting text. Multi-line
// inserts fire EN_CHANGE synchronously mid-insert; letting the custom scrollbar
// reflow (which calls EM_SETRECT back on the RichEdit) run at that point
// re-enters the control while it is still mutating and crashes. The flag makes
// EN_CHANGE skip the reflow so the paste handler can do it once, safely, after
// the insert completes.
static bool s_aiInputPasting = false;

static std::wstring Ai_GetExeDir()
{
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) return {};
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (!slash) return {};
    *(slash + 1) = L'\0';
    return exePath;
}

static void Ai_RefreshUi(HWND hwnd);
static std::wstring Ai_FormatThinkingStatusText(const AiWindowState* st);
static void Ai_LoadPrefs(AiWindowState* st);
static void Ai_SavePrefs(const AiWindowState* st);
static void Ai_AppendLog(HWND hwnd, const std::wstring& line);
static bool Ai_CopyAnswerText(HWND hwndLog);
static void Ai_HistoryRemember(AiWindowState* st, const std::wstring& prompt);
static bool Ai_HistoryBrowse(HWND hwnd, AiWindowState* st, int delta);
static void Ai_DoSend(HWND hwnd);
static void Ai_StopSend(HWND hwnd);
static void Ai_AppendMenuOD(HMENU hMenu, UINT flags, UINT_PTR id, const wchar_t* text, bool isBar);
static std::wstring Ai_CurrentModeLabel(const AiWindowState* st);
static std::wstring Ai_FormatLocaleText(const wchar_t* fmt, const std::wstring& value);
static std::wstring Ai_FormatThinkingStatusText(const AiWindowState* st);
static HMENU Ai_BuildMenu(const AiWindowState* st);
static int Ai_ButtonIndexById(const AiDialogData* dd, int id);
static void Ai_DrawButton(const DRAWITEMSTRUCT* dis, const AiDialogData* dd);
static HFONT Ai_MakeDlgFont(HWND hwnd, bool bold);
static HFONT Ai_MakePaneFont(HWND hwnd);
static int Ai_MeasureButtonWidth(const std::wstring& text);
static void Ai_StreamChunkCallback(void* context, const std::wstring& chunk);
static void Ai_NormalizeModelNameLocal(std::wstring& model);
static void Ai_AddUniqueModel(std::vector<std::wstring>& models, const std::wstring& model);
static std::vector<std::wstring> Ai_GetModelCandidates();
static void Ai_SetWrapToWindow(HWND hwndEdit);
static void Ai_RenderConversation(HWND hwnd);
static bool Ai_CommitLiveReply(HWND hwnd);
static void Ai_FinalizeLiveReply(HWND hwnd);
static void Ai_SetThinkingStatusText(HWND hwnd);
static void Ai_ClearRenderedAnswer(HWND hwnd);
static HWND Ai_CreateAnswerHost(HWND hwndParent, int x, int y, int w, int h);
static LRESULT CALLBACK Ai_AnswerHostSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK Ai_AnswerChildSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR);
static HWND Ai_CreateAnswerRichEdit(HWND hwndParent, int x, int y, int w, int h);
static std::vector<AiAnswerBlockDesc> Ai_ParseAnswerBlocks(const std::wstring& reply);
static void Ai_LayoutAnswerHost(HWND hwndHost);
static void Ai_SaveHistory();
static std::wstring Ai_DefaultModelName();
static std::wstring Ai_FallbackModelName();
static std::wstring Ai_DefaultCloudModelName();
static void Ai_NormalizeModelName(std::wstring& model);
static void Ai_LoadHistory();
static Gdiplus::Image* Ai_GetOllamaButtonImage();
static void Ai_OpenOllamaSignUp(HWND hwnd);
static LRESULT CALLBACK Ai_InputSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR dwRefData);
static void Ai_AppendMarkupLine(HWND hLog, const std::wstring& line, const CHARFORMAT2W* normalFmt, const CHARFORMAT2W* boldFmt, const CHARFORMAT2W* italicFmt, const CHARFORMAT2W* codeFmt, const CHARFORMAT2W* resetFmt);
static void Ai_AppendMarkdownBlocks(HWND hLog, const std::wstring& reply);
static void Ai_ApplyButtons(AiWindowState* st);
static void Ai_ShowModelCheckDialog(HWND parent);


static void Ai_SavePrefs(const AiWindowState* st)
{
    if (!st) return;
    NeProfiles_SetStrSetting("ai.model", Ai_WideToUtf8(st->model));
    NeProfiles_SetStrSetting("ai.fallback", Ai_WideToUtf8(st->fallback));
    NeProfiles_SetStrSetting("ai.cloud_model", Ai_WideToUtf8(st->cloudModel));
    NeProfiles_SetIntSetting("ai.role", (int)st->role);
    NeProfiles_SetIntSetting("ai.cloud_mode", st->cloudMode ? 1 : 0);
    NeProfiles_SetIntSetting("ai.signed_in", st->signedIn ? 1 : 0);
    NeProfiles_SetSecretSetting("ai.cloud_api_key", st->cloudApiKey);
    Ai_SaveHistory();
}

// Reconcile the persisted sign-in flag with reality.  `ai.signed_in` is only a
// cached hint that can go stale (e.g. `ollama signout` from a terminal while
// NSBEdit was closed, or an API key revoked).
//   * With an API key configured, sign-in state == key validity, and we never
//     force a switch to a local model (the user explicitly opted into cloud).
//   * Without a key, we ask the local daemon (/api/me); on signed-out we also
//     drop out of cloud mode and fall back to the last-used local model (or the
//     first local model if that is unavailable).
// Returns true when any persisted state changed.
static bool Ai_ReconcileSignInState(AiWindowState* st)
{
    if (!st) return false;

    if (!st->cloudApiKey.empty()) {
        bool valid = NeAiClient_ValidateCloudApiKey(st->cloudApiKey);
        if (st->signedIn != valid) {
            st->signedIn = valid;
            NeProfiles_SetIntSetting("ai.signed_in", st->signedIn ? 1 : 0);
            return true;
        }
        return false;
    }

    int live = NeAiClient_QueryOllamaSignIn();
    if (live < 0) return false;   // unknown — leave the cached value untouched
    bool real = (live == 1);
    bool changed = false;

    if (st->signedIn != real) {
        st->signedIn = real;
        NeProfiles_SetIntSetting("ai.signed_in", st->signedIn ? 1 : 0);
        changed = true;
    }

    // Signed out: cloud models are unreachable, so fall back to a local model.
    if (!st->signedIn && st->cloudMode) {
        st->cloudMode = false;
        NeProfiles_SetIntSetting("ai.cloud_mode", 0);

        // Prefer the last-used local model (st->model).  If it is empty or no
        // longer offered by the daemon, use the first model in the local list.
        std::vector<std::wstring> locals = Ai_GetModelCandidates();
        bool haveLast = false;
        if (!st->model.empty()) {
            if (locals.empty()) {
                haveLast = true;   // daemon list unknown — keep remembered model
            } else {
                for (const std::wstring& m : locals) {
                    if (m == st->model) { haveLast = true; break; }
                }
            }
        }
        if (!haveLast) {
            st->model = locals.empty() ? Ai_DefaultModelName() : locals.front();
        }
        NeProfiles_SetStrSetting("ai.model", Ai_WideToUtf8(st->model));
        changed = true;
    }

    return changed;
}

static void Ai_LoadPrefs(AiWindowState* st)
{
    if (!st) return;
    st->model = Ai_DefaultModelName();
    st->fallback = Ai_FallbackModelName();
    st->cloudModel = Ai_DefaultCloudModelName();

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

    saved.clear();
    if (NeProfiles_GetStrSetting("ai.cloud_model", "", saved) && !saved.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, saved.c_str(), -1, NULL, 0);
        if (wlen > 1) {
            std::wstring wide((size_t)wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, saved.c_str(), -1, &wide[0], wlen);
            wide.resize((size_t)wlen - 1);
            st->cloudModel = std::move(wide);
        }
    }
    if (st->cloudModel.empty()) st->cloudModel = Ai_DefaultCloudModelName();

    // The API key is stored encrypted (NeProfiles secret setting).
    st->cloudApiKey.clear();
    NeProfiles_GetSecretSetting("ai.cloud_api_key", st->cloudApiKey);
    // Hand the key to the client so cloud sends can use it right away.
    NeAiClient_SetCloudApiKey(st->cloudApiKey);

    int mode = 0, signedIn = 0;
    NeProfiles_GetIntSetting("ai.cloud_mode", 0, mode);
    NeProfiles_GetIntSetting("ai.signed_in", 0, signedIn);
    int role = 0;
    NeProfiles_GetIntSetting("ai.role", 0, role);
    st->cloudMode = (mode != 0);
    st->signedIn = (signedIn != 0);
    st->role = (role == (int)AiMenuRole::Agent) ? AiMenuRole::Agent : AiMenuRole::Suggest;
    // The cached flag above can be stale (e.g. `ollama signout` while the app
    // was closed); ask the daemon and adopt its real sign-in state.
    Ai_ReconcileSignInState(st);
    Ai_LoadHistory();
}

static std::wstring Ai_BuildSummary(const AiWindowState* st)
{
    std::wstring summary;
    summary += Ne_Ls(L"AI_SUMMARY_LOCALHOST");
    summary += L" | ";
    summary += Ne_Ls(L"AI_SUMMARY_CURRENT");
    summary += L" ";
    if (st && st->cloudMode)
        summary += (st->cloudModel.empty() ? Ai_DefaultCloudModelName() : st->cloudModel);
    else
        summary += ((st && !st->model.empty()) ? st->model : Ai_DefaultModelName());
    summary += L" ";
    summary += (st && st->role == AiMenuRole::Agent) ? Ne_Ls(L"AI_ROLE_AGENT") : Ne_Ls(L"AI_ROLE_SUGGEST");
    summary += L" | ";
    summary += Ne_Ls(L"AI_SUMMARY_CLOUD");
    summary += L" ";
    summary += ((st && st->cloudMode) ? Ne_Ls(L"AI_MODE_CLOUD_SUBSCRIPTION") : Ne_Ls(L"AI_MODE_LOCAL_ONLY"));
    summary += L" | ";
    summary += Ne_Ls(L"AI_SUMMARY_ACCOUNT");
    summary += L" ";
    summary += ((st && st->signedIn) ? Ne_Ls(L"AI_SIGNED_IN") : Ne_Ls(L"AI_NOT_SIGNED_IN"));
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
        std::wstring current;
        if (hInput) {
            int inLen = GetWindowTextLengthW(hInput);
            if (inLen > 0) {
                // Size for CRLF expansion so a multi-line draft is not truncated.
                std::wstring buf((size_t)inLen * 2 + 2, L'\0');
                int copied = GetWindowTextW(hInput, &buf[0], (int)buf.size());
                buf.resize((size_t)copied);
                current = std::move(buf);
            }
        }
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
        HICON hIcon = LoadIconW(NULL, MAKEINTRESOURCEW((b.id == IDC_AI_CLEAR_BTN || b.id == IDC_AI_STOP_BTN) ? kSystemIconError : kSystemIconInfo));
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

// Default Ollama cloud model.  Cloud models are served through the SAME local
// endpoint once the user has run `ollama signin`; the daemon proxies the request
// to Ollama's cloud.  Model names carry a "-cloud" suffix.
static std::wstring Ai_DefaultCloudModelName()
{
    return L"qwen3-coder:480b-cloud";
}

// Curated list of well-known Ollama cloud models, merged with any "-cloud"
// models the local daemon already reports.  Used to populate the Model menu so
// the user can pick which cloud model to use.

// Live cloud model list, populated asynchronously from ollama.com/api/tags when
// the AI window opens (see Ai_RefreshCloudModelsAsync). Empty until the first
// successful fetch — the curated fallback below is used until then. Only ever
// read/written on the UI thread (the worker hands it over via PostMessage).
static std::vector<std::wstring> s_cloudModelsLive;

static std::vector<std::wstring> Ai_CloudModelCandidates()
{
    // Prefer the LIVE list fetched from ollama.com/api/tags: retired models are
    // already gone from it and newly-released ones are present. Fall back to the
    // curated list below when no API key is set / the fetch has not completed.
    if (!s_cloudModelsLive.empty()) {
        std::vector<std::wstring> live;
        for (std::wstring m : s_cloudModelsLive) {
            // The app routes cloud models by the "-cloud" suffix; normalise so
            // both the menu and the send path agree on the name.
            if (m.find(L"-cloud") == std::wstring::npos) m += L"-cloud";
            bool present = false;
            for (const std::wstring& e : live) { if (e == m) { present = true; break; } }
            if (!present) live.push_back(m);
        }
        if (!live.empty()) return live;
    }

    std::vector<std::wstring> models = {
        L"qwen3-coder:480b-cloud",
        L"gpt-oss:120b-cloud",
        L"gpt-oss:20b-cloud",
        L"deepseek-v3.1:671b-cloud",
    };
    // Merge in any cloud models the local Ollama already knows about.
    std::vector<std::wstring> installed;
    if (NeAiClient_ListOllamaModels(installed)) {
        for (const std::wstring& m : installed) {
            if (m.find(L"cloud") == std::wstring::npos) continue;
            bool present = false;
            for (const std::wstring& e : models) { if (e == m) { present = true; break; } }
            if (!present) models.push_back(m);
        }
    }
    return models;
}

// Fetch the current cloud model list in the background and, when it differs from
// what we already have, hand it to the UI thread (WM_AI_CLOUDMODELS_READY) which
// updates s_cloudModelsLive and rebuilds the menu. No-op without an API key.
static void Ai_RefreshCloudModelsAsync(HWND hwnd)
{
    if (!hwnd) return;
    std::thread([hwnd]() {
        std::vector<std::wstring> models;
        if (NeAiClient_ListCloudModels(models) && !models.empty()) {
            if (IsWindow(hwnd))
                PostMessageW(hwnd, WM_AI_CLOUDMODELS_READY, 0,
                             (LPARAM)new std::vector<std::wstring>(std::move(models)));
        }
    }).detach();
}

static void Ai_NormalizeModelName(std::wstring& model)
{
    if (model == L"qwen2.5-coder:7b-instruct" || model == L"qwen2.5-coder") {
        model = L"qwen2.5-coder:7b";
    } else if (model == L"qwen2.5-coder:3b-instruct") {
        model = L"qwen2.5-coder:3b";
    }
}

static void Ai_OpenOllamaSignUp(HWND hwnd)
{
    ShellExecuteW(hwnd, L"open", L"https://ollama.com/signup", NULL, NULL, SW_SHOWNORMAL);
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

    // Always use the fence-splitting parser: it keeps prose as raw markdown (so the
    // inline renderer can style headings/bold/italic/inline-code) and lifts fenced
    // ``` code blocks into their own syntax-highlighted blocks.
    if (true) {
        std::wstring prose;
        std::wstring code;
        std::wstring codeLanguage;
        bool inCode = false;

        auto flushProse = [&]() {
            while (!prose.empty() && (prose.back() == L'\n' || prose.back() == L'\r')) {
                prose.pop_back();
            }
            if (!prose.empty()) {
                blocks.push_back(AiAnswerBlockDesc{ false, prose, L"" });
                prose.clear();
            }
        };

        auto flushCode = [&]() {
            while (!code.empty() && (code.back() == L'\n' || code.back() == L'\r')) {
                code.pop_back();
            }
            if (!code.empty()) {
                blocks.push_back(AiAnswerBlockDesc{ true, code, codeLanguage });
                code.clear();
            }
            codeLanguage.clear();
        };

        size_t pos = 0;
        while (pos <= reply.size()) {
            size_t lineEnd = reply.find(L'\n', pos);
            bool hasNewline = lineEnd != std::wstring::npos;
            size_t len = hasNewline ? lineEnd - pos : reply.size() - pos;
            std::wstring line = reply.substr(pos, len);

            auto ltrim = [](std::wstring& text) {
                while (!text.empty() && iswspace(text.front())) {
                    text.erase(text.begin());
                }
            };
            std::wstring trimmed = line;
            ltrim(trimmed);

            if (trimmed.rfind(L"```", 0) == 0) {
                if (inCode) {
                    flushCode();
                    inCode = false;
                } else {
                    flushProse();
                    codeLanguage = trimmed.substr(3);
                    ltrim(codeLanguage);
                    inCode = true;
                }
            } else {
                if (inCode) {
                    code += line;
                    if (hasNewline) code += L"\n";
                } else {
                    prose += line;
                    if (hasNewline) prose += L"\n";
                }
            }

            if (!hasNewline) break;
            pos = lineEnd + 1;
        }

        if (inCode) {
            flushCode();
        } else {
            flushProse();
        }

        if (!blocks.empty()) {
            return blocks;
        }
    }

    std::string utf8 = Ai_WideToUtf8(reply);
    cmark_parser* parser = cmark_parser_new(CMARK_OPT_DEFAULT);
    if (!parser) return blocks;
    cmark_parser_feed(parser, utf8.c_str(), utf8.size());
    cmark_node* doc = cmark_parser_finish(parser);
    if (!doc) return blocks;

    std::wstring prose;

    auto flushProse = [&]() {
        while (!prose.empty() && (prose.back() == L'\n' || prose.back() == L'\r')) {
            prose.pop_back();
        }
        if (!prose.empty()) {
            blocks.push_back(AiAnswerBlockDesc{ false, prose, L"" });
            prose.clear();
        }
    };

    for (cmark_node* node = cmark_node_first_child(doc); node; node = cmark_node_next(node)) {
        cmark_node_type type = cmark_node_get_type(node);
        if (type == CMARK_NODE_CODE_BLOCK) {
            flushProse();
            std::wstring code = Ai_Utf8ToWide(cmark_node_get_literal(node));
            std::wstring language = Ai_Utf8ToWide(cmark_node_get_fence_info(node));
            blocks.push_back(AiAnswerBlockDesc{ true, code, language });
        } else {
            Ai_CollectMarkdownText(node, prose);
        }
    }

    flushProse();
    cmark_node_free(doc);
    cmark_parser_free(parser);
    return blocks;
}

static int Ai_AnswerHostPageHeight(HWND hwndHost)
{
    RECT rc = {};
    GetClientRect(hwndHost, &rc);
    return std::max(1, (int)rc.bottom - (int)rc.top);
}

static void Ai_LayoutAnswerHost(HWND hwndHost)
{
    if (!hwndHost || !IsWindow(hwndHost)) return;

    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(GetParent(hwndHost), GWLP_USERDATA);
    if (!st) return;

    RECT rc = {};
    GetClientRect(hwndHost, &rc);
    int pageWidth = std::max(1, (int)rc.right - (int)rc.left);
    int pageHeight = std::max(1, (int)rc.bottom - (int)rc.top);
    int innerWidth = std::max(1, pageWidth - S(16));

    std::vector<AiAnswerBlockDesc> blocks = Ai_ParseAnswerBlocks(st->answerRawMarkdown);
    if (blocks.empty() && !st->answerRawMarkdown.empty()) {
        blocks.push_back(AiAnswerBlockDesc{ false, st->answerRawMarkdown, L"" });
    }

    int measuredHeight = S(16);
    HDC hdc = GetDC(hwndHost);
    HFONT oldFont = NULL;
    if (hdc && st->hPaneFont) {
        oldFont = (HFONT)SelectObject(hdc, st->hPaneFont);
    }

    for (const AiAnswerBlockDesc& desc : blocks) {
        if (desc.isCode) {
            int codeLines = 1;
            for (wchar_t ch : desc.text) {
                if (ch == L'\n') ++codeLines;
            }
            int codeHeight = std::max((int)S(80), codeLines * (int)S(18));
            measuredHeight += S(32) + codeHeight + S(8);
        } else if (!desc.text.empty() && hdc) {
            RECT measure = { 0, 0, std::max(1, innerWidth), 0 };
            DrawTextW(hdc, desc.text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
            int blockHeight = std::max((int)S(40), (int)(measure.bottom - measure.top) + (int)S(18));
            measuredHeight += blockHeight + S(8);
        }
    }

    if (hdc && oldFont) {
        SelectObject(hdc, oldFont);
    }
    if (hdc) {
        ReleaseDC(hwndHost, hdc);
    }

    st->answerContentHeight = measuredHeight + S(8);
    int maxScroll = std::max(0, st->answerContentHeight - pageHeight);
    if (st->answerScrollY < 0) st->answerScrollY = 0;
    if (st->answerScrollY > maxScroll) st->answerScrollY = maxScroll;

    int y = S(8) - st->answerScrollY;
    size_t childIndex = 0;
    hdc = GetDC(hwndHost);
    oldFont = NULL;
    if (hdc && st->hPaneFont) {
        oldFont = (HFONT)SelectObject(hdc, st->hPaneFont);
    }

    for (const AiAnswerBlockDesc& desc : blocks) {
        if (desc.isCode) {
            HWND hHeader = childIndex < st->answerBlocks.size() ? st->answerBlocks[childIndex] : NULL;
            HWND hSci = (childIndex + 1) < st->answerBlocks.size() ? st->answerBlocks[childIndex + 1] : NULL;

            int codeLines = 1;
            for (wchar_t ch : desc.text) {
                if (ch == L'\n') ++codeLines;
            }
            int codeHeight = std::max((int)S(80), codeLines * (int)S(18));

            if (hHeader && IsWindow(hHeader)) {
                SetWindowPos(hHeader, NULL, S(8), y, S(140), S(28), SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (hSci && IsWindow(hSci)) {
                SetWindowPos(hSci, NULL, S(8), y + S(32), innerWidth, codeHeight, SWP_NOZORDER | SWP_NOACTIVATE);
            }

            y += S(32) + codeHeight + S(8);
            childIndex += 2;
        } else {
            HWND hText = childIndex < st->answerBlocks.size() ? st->answerBlocks[childIndex] : NULL;
            int blockHeight = S(40);
            if (hdc && !desc.text.empty()) {
                RECT measure = { 0, 0, std::max(1, innerWidth), 0 };
                DrawTextW(hdc, desc.text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
                blockHeight = std::max((int)S(40), (int)(measure.bottom - measure.top) + (int)S(18));
            }

            if (hText && IsWindow(hText)) {
                SetWindowPos(hText, NULL, S(8), y, innerWidth, blockHeight, SWP_NOZORDER | SWP_NOACTIVATE);
            }

            y += blockHeight + S(8);
            ++childIndex;
        }
    }

    if (hdc && oldFont) {
        SelectObject(hdc, oldFont);
    }
    if (hdc) {
        ReleaseDC(hwndHost, hdc);
    }

    if (st->hLogSb) {
        msb_notify_content_changed(st->hLogSb);
        msb_sync(st->hLogSb);
    }
}

static HWND Ai_CreateAnswerRichEdit(HWND hwndParent, int x, int y, int w, int h)
{
    HWND hEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_NOHIDESEL | ES_READONLY,
        x, y, std::max(1, w), std::max(1, h), hwndParent, NULL, GetModuleHandleW(NULL), NULL);
    if (hEdit) {
        SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
        SendMessageW(hEdit, EM_AUTOURLDETECT, TRUE, 0);
        SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_MOUSEEVENTS | ENM_LINK | ENM_KEYEVENTS | ENM_CHANGE);
        SetWindowSubclass(hEdit, Ai_AnswerChildSubclassProc, 1, 0);
    }
    return hEdit;
}

static void Ai_ClearRenderedAnswer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;

    for (HWND hChild : st->answerBlocks) {
        if (hChild && IsWindow(hChild)) {
            DestroyWindow(hChild);
        }
    }
    st->answerBlocks.clear();

    if (st->hLog && IsWindow(st->hLog)) {
        SetWindowTextW(st->hLog, L"");
    }

    st->answerRawMarkdown.clear();
    st->answerCopyText.clear();
    st->answerContentHeight = 0;
    st->answerScrollY = 0;
    st->answerWheelAccum = 0;

    if (st->hLogSb) {
        msb_notify_content_changed(st->hLogSb);
        msb_sync(st->hLogSb);
    }
}

static void Ai_AnswerHostScrollTo(HWND hwndHost, int scrollY)
{
    if (!hwndHost || !IsWindow(hwndHost)) return;

    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(GetParent(hwndHost), GWLP_USERDATA);
    if (!st) return;

    int pageHeight = Ai_AnswerHostPageHeight(hwndHost);
    int maxScroll = std::max(0, st->answerContentHeight - pageHeight);
    int clamped = scrollY;
    if (clamped < 0) clamped = 0;
    if (clamped > maxScroll) clamped = maxScroll;

    if (clamped == st->answerScrollY) {
        if (st->hLogSb) msb_sync(st->hLogSb);
        return;
    }

    st->answerScrollY = clamped;
    Ai_LayoutAnswerHost(hwndHost);
}

static void Ai_AppendMarkupLine(HWND hLog, const std::wstring& line, const CHARFORMAT2W* normalFmt, const CHARFORMAT2W* boldFmt, const CHARFORMAT2W* italicFmt, const CHARFORMAT2W* codeFmt, const CHARFORMAT2W* resetFmt)
{
    if (!hLog) return;

    size_t pos = 0;
    while (pos < line.size()) {
        size_t boldStart   = line.find(L"**", pos);
        size_t italicStart = line.find(L"*", pos);
        size_t codeStart   = line.find(L"`", pos);

        // Pick the earliest inline marker.  1 = bold, 2 = italic, 3 = code.
        size_t next = std::wstring::npos;
        int kind = 0;
        auto consider = [&](size_t p, int k) {
            if (p != std::wstring::npos && (next == std::wstring::npos || p < next)) { next = p; kind = k; }
        };
        consider(codeStart, 3);
        consider(boldStart, 1);
        // A lone '*' only counts as italic when it is not the '**' we already saw.
        if (italicStart != boldStart) consider(italicStart, 2);

        if (next == std::wstring::npos) {
            Ai_AppendRichRun(hLog, line.substr(pos), normalFmt, resetFmt);
            break;
        }
        if (next > pos) {
            Ai_AppendRichRun(hLog, line.substr(pos, next - pos), normalFmt, resetFmt);
        }

        if (kind == 1) {
            size_t end = line.find(L"**", next + 2);
            if (end == std::wstring::npos) {
                Ai_AppendRichRun(hLog, line.substr(next), normalFmt, resetFmt);
                break;
            }
            Ai_AppendRichRun(hLog, line.substr(next + 2, end - (next + 2)), boldFmt ? boldFmt : normalFmt, resetFmt);
            pos = end + 2;
        } else if (kind == 3) {
            size_t end = line.find(L"`", next + 1);
            if (end == std::wstring::npos) {
                Ai_AppendRichRun(hLog, line.substr(next), normalFmt, resetFmt);
                break;
            }
            Ai_AppendRichRun(hLog, line.substr(next + 1, end - (next + 1)), codeFmt ? codeFmt : normalFmt, resetFmt);
            pos = end + 1;
        } else {
            size_t end = line.find(L"*", next + 1);
            if (end == std::wstring::npos) {
                Ai_AppendRichRun(hLog, line.substr(next), normalFmt, resetFmt);
                break;
            }
            Ai_AppendRichRun(hLog, line.substr(next + 1, end - (next + 1)), italicFmt ? italicFmt : normalFmt, resetFmt);
            pos = end + 1;
        }
    }
}

static std::string Ai_RtfEscapeCellText(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size() * 4);
    for (wchar_t ch : text) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            out += "\\line ";
            continue;
        }
        if (ch == L'\\' || ch == L'{' || ch == L'}') {
            out.push_back('\\');
            out.push_back((char)ch);
            continue;
        }
        if (ch >= 0x20 && ch <= 0x7E) {
            out.push_back((char)ch);
            continue;
        }

        short code = (short)ch;
        out += "\\u" + std::to_string((int)code) + "?";
    }
    return out;
}

static std::string Ai_BuildCodeTableRtf(const std::wstring& codeText, int availablePx, HWND hLog,
                                        const std::wstring& language)
{
    RECT rc = {};
    GetClientRect(hLog, &rc);
    int dpi = hLog ? GetDpiForWindow(hLog) : 96;
    int clientPx = std::max(1, availablePx > 0 ? availablePx : (int)rc.right - S(24));

    std::vector<std::wstring> lines;
    std::wstring line;
    size_t pos = 0;
    while (pos <= codeText.size()) {
        size_t nl = codeText.find(L'\n', pos);
        if (nl == std::wstring::npos) {
            line = codeText.substr(pos);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            lines.push_back(line);
            break;
        }
        line = codeText.substr(pos, nl - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(line);
        pos = nl + 1;
    }
    if (lines.empty()) lines.push_back(L"");
    if (!lines.front().empty()) {
        lines.insert(lines.begin(), L"");
    }
    if (!lines.back().empty()) {
        lines.push_back(L"");
    }

    HFONT hMono = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    HDC hdc = GetDC(hLog);
    HFONT old = hdc && hMono ? (HFONT)SelectObject(hdc, hMono) : NULL;
    SIZE maxSize = {};
    for (const std::wstring& l : lines) {
        SIZE sz = {};
        GetTextExtentPoint32W(hdc, l.c_str(), (int)l.size(), &sz);
        if (sz.cx > maxSize.cx) maxSize.cx = sz.cx;
        if (sz.cy > maxSize.cy) maxSize.cy = sz.cy;
    }
    if (old) SelectObject(hdc, old);
    if (hdc) ReleaseDC(hLog, hdc);
    if (hMono) DeleteObject(hMono);

    int contentPx = std::max(clientPx, (int)maxSize.cx + (int)S(40));
    int cellTwips = MulDiv(contentPx, 1440, dpi);
    if (cellTwips < 1440) cellTwips = 1440;

    // Syntax-highlight the code (Scintilla/Lexilla) into colored runs. Fixed
    // colors: 1 = border, 2 = cell background, 3 = default text; syntax colors
    // are appended to the table and referenced per run.
    std::wstring codeForHi = codeText;
    while (!codeForHi.empty() && (codeForHi.back() == L'\n' || codeForHi.back() == L'\r')) {
        codeForHi.pop_back();
    }
    std::vector<NsbCodeStyleRun> hiRuns = NsbAi_HighlightCode(codeForHi, language);

    std::string colorTable =
        "{\\colortbl;\\red96\\green120\\blue160;\\red245\\green247\\blue250;\\red30\\green30\\blue30;";
    std::map<COLORREF, int> colorIndex;
    int nextColor = 4; // first three are the fixed entries above
    auto rtfColorIndex = [&](COLORREF c) -> int {
        if (c == RGB(30, 30, 30)) return 3; // default text reuses fixed slot
        auto it = colorIndex.find(c);
        if (it != colorIndex.end()) return it->second;
        int idx = nextColor++;
        colorIndex[c] = idx;
        colorTable += "\\red" + std::to_string(GetRValue(c)) +
                      "\\green" + std::to_string(GetGValue(c)) +
                      "\\blue" + std::to_string(GetBValue(c)) + ";";
        return idx;
    };
    // Pre-assign indices in run order so the color table lists them all.
    std::vector<int> runColorIdx;
    runColorIdx.reserve(hiRuns.size());
    for (const NsbCodeStyleRun& r : hiRuns) {
        runColorIdx.push_back(rtfColorIndex(r.color));
    }
    colorTable += "}";

    std::string rtf;
    rtf += "{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0 Consolas;}}";
    rtf += colorTable;
    rtf += "\\viewkind4\\pard\\sa0\\sb0\\sl0\\slmult1";
    rtf += "\\trowd\\trgaph0\\trleft0\\trpaddl75\\trpaddr120\\trpaddt90\\trpaddb90";
    rtf += "\\clbrdrt\\brdrs\\brdrw18\\brdrcf1";
    rtf += "\\clbrdrl\\brdrs\\brdrw18\\brdrcf1";
    rtf += "\\clbrdrb\\brdrs\\brdrw18\\brdrcf1";
    rtf += "\\clbrdrr\\brdrs\\brdrw18\\brdrcf1";
    rtf += "\\clcbpat2\\cellx" + std::to_string(cellTwips);
    rtf += "\\pard\\intbl\\li75\\fi0\\f0\\fs20\\highlight2 ";
    rtf += "\\cf3\\line "; // top padding line
    for (size_t i = 0; i < hiRuns.size(); ++i) {
        rtf += "\\cf" + std::to_string(runColorIdx[i]) + " ";
        rtf += Ai_RtfEscapeCellText(hiRuns[i].text);
    }
    rtf += "\\cf3\\line "; // bottom padding line
    rtf += "\\cell\\row\\pard\\par}";
    return rtf;
}

// Returns the caret position at the very end of the control in the same
// character-index space used by EM_EXSETSEL/EM_EXGETSEL. GetWindowTextLengthW
// counts line breaks as CRLF (2 chars) which drifts away from the RichEdit
// internal position space (CR only), so it must not be used to record ranges.
static int Ai_GetTextEndPos(HWND hLog)
{
    CHARRANGE cr = { 0x7FFFFFFF, 0x7FFFFFFF };
    SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARRANGE got = {};
    SendMessageW(hLog, EM_EXGETSEL, 0, (LPARAM)&got);
    return got.cpMax;
}

static void Ai_AppendMarkdownBlocks(HWND hLog, const std::wstring& reply)
{
    if (!hLog || reply.empty()) return;

    static const CHARFORMAT2W kHeaderFmt = []() {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_COLOR | CFM_BACKCOLOR | CFM_FACE;
        cf.dwEffects = CFE_BOLD;
        cf.crTextColor = RGB(0, 120, 215);
        cf.crBackColor = RGB(255, 255, 255);
        lstrcpyW(cf.szFaceName, L"Segoe UI Emoji");
        return cf;
    }();

    static const CHARFORMAT2W kInlineNormalFmt = []() {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC;
        cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(0, 0, 0);
        cf.crBackColor = RGB(255, 255, 255);
        lstrcpyW(cf.szFaceName, L"Segoe UI Emoji");
        return cf;
    }();

    static const CHARFORMAT2W kInlineBoldFmt = []() {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC;
        cf.dwEffects = CFE_BOLD;
        cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(0, 0, 0);
        cf.crBackColor = RGB(255, 255, 255);
        lstrcpyW(cf.szFaceName, L"Segoe UI Emoji");
        return cf;
    }();

    static const CHARFORMAT2W kInlineItalicFmt = []() {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC;
        cf.dwEffects = CFE_ITALIC;
        cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(0, 0, 0);
        cf.crBackColor = RGB(255, 255, 255);
        lstrcpyW(cf.szFaceName, L"Segoe UI Emoji");
        return cf;
    }();

    static const CHARFORMAT2W kInlineCodeFmt = []() {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC;
        cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(170, 40, 50);
        cf.crBackColor = RGB(238, 238, 238);
        lstrcpyW(cf.szFaceName, L"Consolas");
        return cf;
    }();

    std::vector<AiAnswerBlockDesc> blocks = Ai_ParseAnswerBlocks(reply);
    for (const AiAnswerBlockDesc& block : blocks) {
        if (!block.isCode) {
            // Split the prose block into individual lines so headings, block
            // quotes and bullets are detected per line (not just the first).
            std::wstring text = block.text;
            size_t start = 0;
            while (start <= text.size()) {
                size_t nl = text.find(L'\n', start);
                std::wstring line = (nl == std::wstring::npos)
                    ? text.substr(start) : text.substr(start, nl - start);
                while (!line.empty() && line.back() == L'\r') line.pop_back();

                bool isHeading = false;
                size_t hashes = 0;
                while (hashes < line.size() && line[hashes] == L'#') ++hashes;
                if (hashes >= 1 && hashes <= 6 && hashes < line.size() && line[hashes] == L' ') {
                    isHeading = true;
                    line.erase(0, hashes + 1);
                }
                if (!isHeading && line.rfind(L"> ", 0) == 0) line.erase(0, 2);
                if (!isHeading && (line.rfind(L"- ", 0) == 0 || line.rfind(L"+ ", 0) == 0))
                    line = L"\u2022 " + line.substr(2);

                if (isHeading)
                    Ai_AppendMarkupLine(hLog, line, &kHeaderFmt, &kHeaderFmt, &kHeaderFmt, &kInlineCodeFmt, &kInlineNormalFmt);
                else
                    Ai_AppendMarkupLine(hLog, line, &kInlineNormalFmt, &kInlineBoldFmt, &kInlineItalicFmt, &kInlineCodeFmt, &kInlineNormalFmt);
                Ai_AppendRichRun(hLog, L"\r\n", nullptr, &kInlineNormalFmt, false);

                if (nl == std::wstring::npos) break;
                start = nl + 1;
            }
            continue;
        }

        Ai_AppendRichRun(hLog, L"\r\n\r\n", nullptr, &kInlineNormalFmt, false);

        std::wstring codeText = block.text;
        if (!codeText.empty()) {
            Ai_AppendRichRun(hLog, L"\r\n", nullptr, &kInlineNormalFmt, false);
            int codeStart = Ai_GetTextEndPos(hLog);
            AiCopyCode_BeginBlock(hLog, -1, L"", codeStart);
            CHARRANGE insertRange = { codeStart, codeStart };
            SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&insertRange);
            std::string rtf = Ai_BuildCodeTableRtf(codeText, 0, hLog, block.language);
            Ai_StreamRtfIntoSelection(hLog, rtf);
            AiCopyCode_EndBlock(hLog, Ai_GetTextEndPos(hLog));
        }

        Ai_AppendRichRun(hLog, L"\r\n", nullptr, &kInlineNormalFmt, false);
    }

    CHARRANGE endRange = { GetWindowTextLengthW(hLog), GetWindowTextLengthW(hLog) };
    SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&endRange);
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

static void Ai_WriteRawReplyFile(const std::wstring& rawReply)
{
    // Release build: no raw-reply debug file is written.
    (void)rawReply;
}

static LRESULT CALLBACK Ai_AnswerHostSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    switch (msg) {
    case WM_SIZE: {
        if (st) {
            Ai_SetWrapToWindow(hwnd);
            if (st->hLogSb) {
                msb_reposition(st->hLogSb);
            }
            Ai_LayoutAnswerHost(hwnd);
            if (st->hLogSb) {
                msb_sync(st->hLogSb);
            }
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL:
        if (st) {
            int delta = (short)GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta != 0) {
                int step = std::max((int)S(32), Ai_AnswerHostPageHeight(hwnd) / 4);
                Ai_AnswerHostScrollTo(hwnd, st->answerScrollY - ((delta > 0) ? step : -step));
                return 0;
            }
        }
        break;
    case WM_VSCROLL:
        if (st) {
            int pageHeight = Ai_AnswerHostPageHeight(hwnd);
            int step = std::max((int)S(24), pageHeight / 8);
            int target = st->answerScrollY;
            switch (LOWORD(wParam)) {
            case SB_LINEUP:
                target -= step;
                break;
            case SB_LINEDOWN:
                target += step;
                break;
            case SB_PAGEUP:
                target -= pageHeight;
                break;
            case SB_PAGEDOWN:
                target += pageHeight;
                break;
            case SB_TOP:
                target = 0;
                break;
            case SB_BOTTOM:
                target = st->answerContentHeight;
                break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: {
                SCROLLINFO si = {};
                si.cbSize = sizeof(si);
                si.fMask = SIF_TRACKPOS;
                if (GetScrollInfo(hwnd, SB_VERT, &si)) {
                    target = si.nTrackPos;
                }
                break;
            }
            default:
                break;
            }
            Ai_AnswerHostScrollTo(hwnd, target);
            return 0;
        }
        break;
    case WM_LBUTTONUP: {
        POINT pt = { (LONG)(short)LOWORD(lParam), (LONG)(short)HIWORD(lParam) };
        if (AiCopyCode_HandleClick(hwnd, pt)) {
            return 0;
        }
        break;
    }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void Ai_CopyRichRangeText(HWND hLog, int start, int end)
{
    if (!hLog || end <= start) return;
    std::wstring text((size_t)(end - start) + 1, L'\0');
    TEXTRANGEW tr = {};
    tr.chrg.cpMin = start;
    tr.chrg.cpMax = end;
    tr.lpstrText = text.data();
    LRESULT copied = SendMessageW(hLog, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    if (copied > 0) {
        text.resize((size_t)copied);
        // Strip the empty buffer lines rendered before/after the code so the
        // dev can paste the snippet straight into existing code.
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
            text.pop_back();
        }
        while (!text.empty() && (text.front() == L'\r' || text.front() == L'\n')) {
            text.erase(text.begin());
        }
        if (!text.empty()) {
            Ai_CopyTextToClipboard(hLog, text);
        }
    }
}

// Given a raw code-cell range, tighten it so only the code text is covered,
// trimming the padding newlines added around the RTF table.
static void Ai_TrimCodeRange(HWND hLog, int* start, int* end)
{
    if (!start || !end || *end <= *start) return;
    int len = *end - *start;
    std::wstring buf((size_t)len + 1, L'\0');
    TEXTRANGEW tr = {};
    tr.chrg.cpMin = *start;
    tr.chrg.cpMax = *end;
    tr.lpstrText = buf.data();
    LRESULT got = SendMessageW(hLog, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    if (got <= 0) return;
    buf.resize((size_t)got);
    while (!buf.empty() && (buf.back() == L'\r' || buf.back() == L'\n')) {
        buf.pop_back();
        --(*end);
    }
    while (*end > *start && (buf.front() == L'\r' || buf.front() == L'\n')) {
        buf.erase(buf.begin());
        ++(*start);
    }
}

// If charIndex lands inside a rendered code cell, select just its code text
// (as double-click does) and return true.
static bool Ai_SelectCodeCellAt(HWND hLog, int charIndex)
{
    int cellStart = 0, cellEnd = 0;
    if (!AiCopyCode_GetCodeRangeAt(hLog, charIndex, &cellStart, &cellEnd)) {
        return false;
    }
    Ai_TrimCodeRange(hLog, &cellStart, &cellEnd);
    CHARRANGE crg = { cellStart, cellEnd };
    SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&crg);
    return true;
}

static int Ai_CharIndexFromClientPoint(HWND hLog, POINT ptClient)
{
    POINTL pl = { ptClient.x, ptClient.y };
    return (int)SendMessageW(hLog, EM_CHARFROMPOS, 0, (LPARAM)&pl);
}

static void Ai_ShowCodeCellMenu(HWND hLog, int screenX, int screenY, int cellStart, int cellEnd)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;
    Ai_AppendMenuOD(hMenu, MF_STRING, IDM_AI_CODE_COPY, Ne_Ls(L"BTN_COPY"), false);

    HWND hOwner = GetParent(GetParent(hLog));
    if (!hOwner) hOwner = GetParent(hLog);

    int cmd = (int)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
        screenX, screenY, 0, hOwner, NULL);
    DestroyMenu(hMenu);

    if (cmd == IDM_AI_CODE_COPY) {
        CHARRANGE sel = {};
        SendMessageW(hLog, EM_EXGETSEL, 0, (LPARAM)&sel);
        if (sel.cpMax > sel.cpMin) {
            Ai_CopyRichRangeText(hLog, sel.cpMin, sel.cpMax);
        } else {
            Ai_CopyRichRangeText(hLog, cellStart, cellEnd);
        }
    }
}

static LRESULT CALLBACK Ai_AnswerChildSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR)
{
    switch (msg) {
    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl && wParam == 'A') {
            // Ctrl+A inside a code cell selects that cell's code (like
            // double-click); elsewhere it falls back to the default select-all.
            CHARRANGE sel = {};
            SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            if (Ai_SelectCodeCellAt(hwnd, sel.cpMin)) {
                return 0;
            }
        } else if (ctrl && wParam == 'C') {
            // Ctrl+C copies the selection without the surrounding buffer lines.
            CHARRANGE sel = {};
            SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            if (sel.cpMax > sel.cpMin) {
                Ai_CopyRichRangeText(hwnd, sel.cpMin, sel.cpMax);
                return 0;
            }
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        POINT pt = { (LONG)(short)LOWORD(lParam), (LONG)(short)HIWORD(lParam) };
        int ch = Ai_CharIndexFromClientPoint(hwnd, pt);
        if (Ai_SelectCodeCellAt(hwnd, ch)) {
            return 0;
        }
        break;
    }
    case WM_CONTEXTMENU: {
        int screenX = (int)(short)LOWORD(lParam);
        int screenY = (int)(short)HIWORD(lParam);
        POINT ptClient;
        int ch;
        if (screenX == -1 && screenY == -1) {
            CHARRANGE sel = {};
            SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            ch = sel.cpMin;
            POINTL pl = {};
            SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&pl, (LPARAM)ch);
            ptClient.x = pl.x;
            ptClient.y = pl.y;
            POINT ptScreen = ptClient;
            ClientToScreen(hwnd, &ptScreen);
            screenX = ptScreen.x;
            screenY = ptScreen.y;
        } else {
            ptClient.x = screenX;
            ptClient.y = screenY;
            ScreenToClient(hwnd, &ptClient);
            ch = Ai_CharIndexFromClientPoint(hwnd, ptClient);
        }
        int cellStart = 0, cellEnd = 0;
        if (AiCopyCode_GetCodeRangeAt(hwnd, ch, &cellStart, &cellEnd)) {
            Ai_ShowCodeCellMenu(hwnd, screenX, screenY, cellStart, cellEnd);
            return 0;
        }
        // Not inside a code cell: still offer a Copy menu when text is selected,
        // so right-click Copy works on the prose/intro just like in code blocks.
        {
            CHARRANGE sel = {};
            SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&sel);
            if (sel.cpMax > sel.cpMin) {
                Ai_ShowCodeCellMenu(hwnd, screenX, screenY, sel.cpMin, sel.cpMax);
                return 0;
            }
        }
        break;
    }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static HWND Ai_CreateAnswerHost(HWND hwndParent, int x, int y, int w, int h)
{
    // No WS_VSCROLL here: the answer RichEdit inside fills this host and its
    // custom MSB scrollbar (attached to st->hLog) is the only vertical bar.
    HWND hHost = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x, y, std::max(1, w), std::max(1, h), hwndParent, (HMENU)(UINT_PTR)IDC_AI_LOG, GetModuleHandleW(NULL), NULL);
    if (hHost) {
        SetWindowSubclass(hHost, Ai_AnswerHostSubclassProc, 1, (DWORD_PTR)hwndParent);
    }
    return hHost;
}

static bool Ai_CopyAnswerText(HWND hwndLog)
{
    if (!hwndLog) return false;
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(GetParent(hwndLog), GWLP_USERDATA);
    if (!st || st->answerCopyText.empty()) return false;
    Ai_CopyTextToClipboard(hwndLog, st->answerCopyText);
    return true;
}

// Re-render every stored conversation turn into hLog, separated by a divider,
// recording each turn's "You:" char offset in st->turnAnchors for Prev/Next.
static void Ai_RenderConversation(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    HWND hLog = st->hLog;
    if (!hLog || !IsWindow(hLog)) return;

    static const CHARFORMAT2W kNormal = []() {
        CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC;
        cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(0, 0, 0); cf.crBackColor = RGB(255, 255, 255);
        lstrcpyW(cf.szFaceName, L"Segoe UI Emoji"); return cf;
    }();
    static const CHARFORMAT2W kYou = []() {
        CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC;
        cf.dwEffects = CFE_BOLD; cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(59, 125, 59); cf.crBackColor = RGB(255, 255, 255);
        lstrcpyW(cf.szFaceName, L"Segoe UI Emoji"); return cf;
    }();
    static const CHARFORMAT2W kOllama = []() {
        CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC;
        cf.dwEffects = CFE_BOLD; cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(0, 120, 215); cf.crBackColor = RGB(255, 255, 255);
        lstrcpyW(cf.szFaceName, L"Segoe UI Emoji"); return cf;
    }();
    static const CHARFORMAT2W kSep = []() {
        CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BACKCOLOR | CFM_BOLD | CFM_ITALIC;
        cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(170, 170, 170); cf.crBackColor = RGB(255, 255, 255);
        lstrcpyW(cf.szFaceName, L"Segoe UI"); return cf;
    }();

    Ai_ClearRenderedAnswer(hwnd);   // wipes hLog + legacy blocks
    AiCopyCode_Clear(hLog);         // reset code-copy registry ONCE for the whole doc
    st->turnAnchors.clear();
    st->answerRawMarkdown.clear();

    const std::wstring youLabel    = Ne_Ls(L"AI_LOG_USER_PREFIX");
    const std::wstring ollamaLabel = Ne_Ls(L"AI_LOG_OLLAMA_PREFIX");
    const std::wstring sepLine(48, L'\u2500');

    std::wstring copyAll;
    for (size_t i = 0; i < st->conversation.size(); ++i) {
        const AiConversationTurn& turn = st->conversation[i];
        const std::wstring& prompt  = turn.prompt;
        const std::wstring& replyMd = turn.replyMd;

        if (i > 0) {
            Ai_AppendRichRun(hLog, L"\r\n", nullptr, &kNormal, false);
            Ai_AppendRichRun(hLog, sepLine, &kSep, &kNormal, false);
            Ai_AppendRichRun(hLog, L"\r\n\r\n", nullptr, &kNormal, false);
        }

        st->turnAnchors.push_back(GetWindowTextLengthW(hLog));

        Ai_AppendRichRun(hLog, youLabel, &kYou, &kNormal, false);
        Ai_AppendRichRun(hLog, prompt + L"\r\n", &kNormal, &kNormal, false);
        for (const std::string& b64 : turn.images) {
            std::string png = Ai_Base64Decode(b64);
            if (!png.empty()) {
                Ai_AppendInlinePng(hLog, png);
                Ai_AppendRichRun(hLog, L"\r\n", &kNormal, &kNormal, false);
            }
        }
        Ai_AppendRichRun(hLog, L"\r\n", &kNormal, &kNormal, false);
        Ai_AppendRichRun(hLog, ollamaLabel + L"\r\n", &kOllama, &kNormal, false);

        std::wstring md = replyMd;
        Ai_ReplaceAll(md, L"\r\n", L"\n");
        Ai_ReplaceAll(md, L"\r", L"\n");
        Ai_UnescapeModelText(md);
        Ai_AppendMarkdownBlocks(hLog, md);

        copyAll += youLabel + prompt + L"\n\n" + ollamaLabel + L"\n" + replyMd + L"\n\n";
    }

    st->answerCopyText = copyAll;
    s_aiAnswerCopyRanges[hLog] = CHARRANGE{ 0, GetWindowTextLengthW(hLog) };

    // Show the newest turn (its "You:" line near the top of the pane).
    st->navTurn = (int)st->conversation.size() - 1;
    if (st->navTurn >= 0 && st->navTurn < (int)st->turnAnchors.size()) {
        int off = st->turnAnchors[st->navTurn];
        int line = (int)SendMessageW(hLog, EM_LINEFROMCHAR, (WPARAM)off, 0);
        int firstVis = (int)SendMessageW(hLog, EM_GETFIRSTVISIBLELINE, 0, 0);
        SendMessageW(hLog, EM_LINESCROLL, 0, line - firstVis);
    }
    if (st->hLogSb) { msb_notify_content_changed(st->hLogSb); msb_sync(st->hLogSb); }
}

// Commit the just-finished streamed reply as a new conversation turn, persist it
// to the DB, and re-render the whole conversation.
static bool Ai_CommitLiveReply(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->liveReply.empty()) return false;
    AiConversationTurn turn;
    turn.prompt  = st->lastPrompt;
    turn.replyMd = st->liveReply;
    turn.images  = st->lastImages;
    st->conversation.push_back(turn);
    NeProfiles_AiAnswersAppend(st->lastPrompt, st->liveReply, st->lastImages);
    Ai_RenderConversation(hwnd);
    return true;
}

// Scroll the answer pane to conversation turn `idx` (its "You:" line).
static void Ai_ScrollToTurn(HWND hwnd, int idx)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || !st->hLog || !IsWindow(st->hLog)) return;
    if (idx < 0 || idx >= (int)st->turnAnchors.size()) return;
    st->navTurn = idx;
    int off = st->turnAnchors[idx];
    int line = (int)SendMessageW(st->hLog, EM_LINEFROMCHAR, (WPARAM)off, 0);
    int firstVis = (int)SendMessageW(st->hLog, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(st->hLog, EM_LINESCROLL, 0, line - firstVis);
    if (st->hLogSb) msb_sync(st->hLogSb);
}

static void Ai_FinalizeLiveReply(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->liveTypingFinalRendered || !st->liveTypingDone || !st->liveTypingQueue.empty() || st->liveReply.empty()) {
        return;
    }

    if (Ai_CommitLiveReply(hwnd)) {
        st->liveTypingFinalRendered = true;
        Ai_SetThinkingStatusText(hwnd);
    }
}

static void Ai_RefreshUi(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;

    std::wstring title = Ne_Ls(L"AI_WINDOW_TITLE_PREFIX");
    title += L" ";
    title += Ai_CurrentModeLabel(st);
    SetWindowTextW(hwnd, title.c_str());

    HWND hHdr = GetDlgItem(hwnd, IDC_AI_HEADER);
    if (hHdr) SetWindowTextW(hHdr, Ai_BuildSummary(st).c_str());

    HWND hStatus = GetDlgItem(hwnd, IDC_AI_STATUS);
    HWND hStatusTimer = st->hStatusTimer ? st->hStatusTimer : GetDlgItem(hwnd, IDC_AI_STATUS_TIMER);
    if (hStatus) {
        std::wstring status = Ne_Ls(L"AI_STATUS_CLOUD");
        status += L" ";
        status += (st->cloudMode ? Ne_Ls(L"AI_MODE_CLOUD_SUBSCRIPTION") : Ne_Ls(L"AI_MODE_LOCAL_ONLY"));
        status += L" | ";
        status += Ne_Ls(L"AI_STATUS_SIGNIN");
        status += L" ";
        status += (st->signedIn ? Ne_Ls(L"BTN_YES") : Ne_Ls(L"BTN_NO"));
        status += L" | ";
        status += Ne_Ls(L"AI_STATUS_CURRENT");
        status += L" ";
        status += Ai_CurrentModeLabel(st);
        SetWindowTextW(hStatus, status.c_str());
    }
    if (hStatusTimer) {
        SetWindowTextW(hStatusTimer, Ai_FormatThinkingStatusText(st).c_str());
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

static std::wstring Ai_FormatElapsedTimeText(ULONGLONG elapsedMs)
{
    std::wstring timeText = L"00:00:00";
    unsigned long long totalSeconds = elapsedMs / 1000ULL;
    unsigned long long hours = totalSeconds / 3600ULL;
    unsigned long long minutes = (totalSeconds % 3600ULL) / 60ULL;
    unsigned long long seconds = totalSeconds % 60ULL;

    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%02llu:%02llu:%02llu", hours, minutes, seconds);
    timeText = buffer;
    return timeText;
}

static std::wstring Ai_FormatThinkingStatusText(const AiWindowState* st)
{
    if (!st) {
        return std::wstring(Ne_Ls(L"AI_LAST_QUERY_USED")) + L" 00:00:00";
    }

    if (st->thinkingTimerRunning && st->thinkingStartMs != 0) {
        return std::wstring(Ne_Ls(L"AI_TIME_USED")) + L" " + Ai_FormatElapsedTimeText(GetTickCount64() - st->thinkingStartMs);
    }

    if (!st->lastThinkingElapsedText.empty()) {
        return std::wstring(Ne_Ls(L"AI_LAST_QUERY_USED")) + L" " + st->lastThinkingElapsedText;
    }

    return std::wstring(Ne_Ls(L"AI_LAST_QUERY_USED")) + L" 00:00:00";
}

static void Ai_SetThinkingStatusText(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    HWND hStatus = st->hStatusTimer ? st->hStatusTimer : GetDlgItem(hwnd, IDC_AI_STATUS_TIMER);
    if (!hStatus) return;
    SetWindowTextW(hStatus, Ai_FormatThinkingStatusText(st).c_str());
}

// While the model is being queried (spinner visible, nothing streamed yet), show a
// rising best-guess percentage so the developer sees activity.  Skipped in chunked
// mode, where the spinner already shows the real batch progress.
static void Ai_UpdateSpinnerProgress(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || st->chunkedActive || !st->spinner) return;
    if (!st->spinner->IsVisible() || st->thinkingStartMs == 0) return;
    unsigned long long e = (GetTickCount64() - st->thinkingStartMs) / 1000ULL;   // seconds
    // Asymptotic curve approaching ~95%: pct = 95*e/(e+25).
    int pct = (int)((95ULL * e) / (e + 25ULL));
    if (pct < 1)  pct = 1;
    if (pct > 95) pct = 95;
    std::wstring t = std::wstring(Ne_Ls(L"AI_WAKING_OLLAMA")) + L"\r\n(" + std::to_wstring(pct) + L"%)";
    st->spinner->SetText(t);
}

static void Ai_StopThinkingTimer(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st || !st->thinkingTimerRunning) return;
    if (st->thinkingStartMs != 0) {
        st->lastThinkingElapsedText = Ai_FormatElapsedTimeText(GetTickCount64() - st->thinkingStartMs);
    }
    KillTimer(hwnd, kAiThinkingTimerId);
    st->thinkingTimerRunning = false;
    st->thinkingStartMs = 0;
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
            SetWindowSubclass(st->hLog, Ai_AnswerChildSubclassProc, 1, 0);
            SendMessageW(st->hLog, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
            Ai_SetWrapToWindow(st->hLog);
            if (!st->hLogSb) {
                st->hLogSb = msb_attach(st->hLog, MSB_VERTICAL);
            }
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

static void Ai_BeginBusyState(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    if (!st->spinner) {
        st->spinner = new SpinnerDialog(hwnd);
    }
    if (st->spinner) {
        // Mirror the window's Stop button inside the spinner, below the animation.
        st->spinner->SetTitle(Ne_Ls(L"AI_WAIT_TITLE"));
        st->spinner->SetStopButton(Ne_Ls(L"BTN_STOP"), [hwnd]() { Ai_StopSend(hwnd); });
        st->spinner->Show(Ne_Ls(L"AI_WAKING_OLLAMA"));
    }
    if (st->hSendBtn) {
        EnableWindow(st->hSendBtn, FALSE);
    }
    if (st->hStopBtn) {
        EnableWindow(st->hStopBtn, TRUE);
    }
}

static void Ai_EndBusyState(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    st->chunkedActive = false;
    if (st->spinner) {
        st->spinner->Hide();
    }
    if (st->hSendBtn) {
        EnableWindow(st->hSendBtn, TRUE);
    }
    if (st->hStopBtn) {
        EnableWindow(st->hStopBtn, FALSE);
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

    HWND hLog = st->hLog;
    if (!hLog || !IsWindow(hLog)) return;

    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    if (st->hLogSb) {
        msb_notify_content_changed(st->hLogSb);
    }

    st->liveReply += text;
    s_aiAnswerCopyRanges[hLog] = CHARRANGE{ st->replyBaseStart, GetWindowTextLengthW(hLog) };
    st->liveRawReply += text;
    Ai_WriteRawReplyFile(st->liveRawReply);
    if (st->hAnswerHost && IsWindow(st->hAnswerHost)) {
        Ai_AnswerHostScrollTo(st->hAnswerHost, 0x7fffffff);
    }
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
    for (wchar_t& ch : lower) ch = (wchar_t)towlower(ch);

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

// ── AI menu hover tooltip (owned by the AI window) ────────────────────────────
// A dedicated yellow tooltip that looks identical to the shared multilingual
// tooltip, but is OWNED BY THE AI WINDOW.  The shared singleton (tooltip.cpp) is
// owned by whichever top-level window created it first — normally the main
// editor — so showing it from the separate AI window pulled the editor's window
// group forward and sent the AI window behind.  A tooltip owned by the AI window
// stays in the AI window's group and never steals activation.
static HWND  s_hAiTipWnd = NULL;
static HFONT s_hAiTipFont = NULL;
static std::wstring s_aiTipText;

static LRESULT CALLBACK Ai_MenuTipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_INFOBK));
        HFONT old = s_hAiTipFont ? (HFONT)SelectObject(hdc, s_hAiTipFont) : NULL;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        RECT tr = rc;
        tr.left += S(8); tr.top += S(6); tr.right -= S(8); tr.bottom -= S(6);
        DrawTextW(hdc, s_aiTipText.c_str(), -1, &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        if (old) SelectObject(hdc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void Ai_HideMenuTip()
{
    if (s_hAiTipWnd && IsWindowVisible(s_hAiTipWnd)) ShowWindow(s_hAiTipWnd, SW_HIDE);
}

static void Ai_DestroyMenuTip()
{
    if (s_hAiTipWnd) { DestroyWindow(s_hAiTipWnd); s_hAiTipWnd = NULL; }
    if (s_hAiTipFont) { DeleteObject(s_hAiTipFont); s_hAiTipFont = NULL; }
    s_aiTipText.clear();
}

static void Ai_ShowMenuTip(HWND owner, const std::wstring& text)
{
    if (!owner || text.empty()) return;

    if (!s_hAiTipFont) {
        NONCLIENTMETRICSW ncm = {}; ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        LOGFONTW lf = ncm.lfMessageFont;
        UINT dpi = GetDpiForWindow(owner);
        lf.lfHeight = -MulDiv(12, dpi > 0 ? dpi : 96, 72);
        lf.lfWeight = FW_BOLD;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        s_hAiTipFont = CreateFontIndirectW(&lf);
    }

    s_aiTipText = text;

    HINSTANCE hi = GetModuleHandleW(NULL);
    if (!s_hAiTipWnd) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = Ai_MenuTipWndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(kSystemArrowCursor));
        wc.hbrBackground = GetSysColorBrush(COLOR_INFOBK);
        wc.lpszClassName = L"NSBEditAiMenuTip";
        if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) RegisterClassW(&wc);
        s_hAiTipWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"NSBEditAiMenuTip", L"", WS_POPUP | WS_BORDER,
            0, 0, 10, 10, owner, NULL, hi, NULL);
    }
    if (!s_hAiTipWnd) return;

    // Measure (word-wrapped, capped like the shared tooltip).
    HDC hdc = GetDC(s_hAiTipWnd);
    HFONT old = s_hAiTipFont ? (HFONT)SelectObject(hdc, s_hAiTipFont) : NULL;
    RECT calc = { 0, 0, S(480) - S(16), 0 };
    DrawTextW(hdc, text.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
    ReleaseDC(s_hAiTipWnd, hdc);
    int w = (calc.right - calc.left) + S(16);
    int h = (calc.bottom - calc.top) + S(12);

    POINT pt; GetCursorPos(&pt);
    int x = pt.x + S(18), y = pt.y + S(12);
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(hm, &mi)) {
        if (x + w > mi.rcWork.right - 8)  x = mi.rcWork.right - w - 8;
        if (x < mi.rcWork.left + 8)       x = mi.rcWork.left + 8;
        if (y + h > mi.rcWork.bottom - 8) y = mi.rcWork.bottom - h - 8;
        if (y < mi.rcWork.top + 8)        y = mi.rcWork.top + 8;
    }
    SetWindowPos(s_hAiTipWnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(s_hAiTipWnd, NULL, TRUE);
}

// Prompt for the Ollama cloud API key, validate it, and store it ONLY in the
// local profile.  On success the client is told to use the key and the UI flips
// to "Signed in" — no browser / `ollama signin` needed.  An empty entry clears
// the stored key.  When a key already exists the field is pre-filled with a
// fixed mask sentinel (never the real secret); leaving it unchanged keeps the
// stored key, typing a new value replaces it, clearing it removes it.
static void Ai_PromptAndSetApiKey(HWND hwnd, AiWindowState* st)
{
    if (!st) return;
    Ai_HideMenuTip();   // menu-hover tooltip: clear before disabling the window

    // Sentinel shown (as password dots) when a key is already stored, so the
    // real key never enters the dialog control.
    static const wchar_t* kApiKeyMask = L"********************";
    const bool hadKey = !st->cloudApiKey.empty();

    std::wstring key = hadKey ? kApiKeyMask : std::wstring();
    if (!Ne_ShowInputDialog(hwnd, Ne_Ls(L"AI_APIKEY_TITLE"), Ne_Ls(L"AI_APIKEY_PROMPT"),
                            key, (hadKey ? std::wstring(kApiKeyMask) : std::wstring()), true)) {
        return;   // cancelled
    }

    // Trim whitespace/newlines that ride along with a paste.
    while (!key.empty() && (key.front() == L' ' || key.front() == L'\t' ||
                            key.front() == L'\r' || key.front() == L'\n')) key.erase(key.begin());
    while (!key.empty() && (key.back() == L' ' || key.back() == L'\t' ||
                            key.back() == L'\r' || key.back() == L'\n')) key.pop_back();

    // Left the mask untouched → keep the existing key, do nothing.
    if (hadKey && key == kApiKeyMask) return;

    st->cloudApiKey = key;
    NeAiClient_SetCloudApiKey(st->cloudApiKey);

    if (st->cloudApiKey.empty()) {
        st->signedIn = false;
        Ai_SavePrefs(st);
        Ai_RefreshUi(hwnd);
        Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_API_KEY_CLEARED"));
        return;
    }

    bool valid = NeAiClient_ValidateCloudApiKey(st->cloudApiKey);
    st->signedIn = valid;
    if (valid) st->cloudMode = true;   // a valid key means the user wants cloud
    Ai_SavePrefs(st);
    Ai_RefreshUi(hwnd);
    Ai_AppendLog(hwnd, valid ? Ne_Ls(L"AI_LOG_API_KEY_OK") : Ne_Ls(L"AI_LOG_API_KEY_BAD"));
}

static void Ai_StartSendWorker(HWND hwnd, std::wstring prompt, std::wstring model, std::wstring fallback, int answerStart,
                               std::vector<std::wstring> mapChunks = {}, std::wstring reduceHeader = {},
                               std::vector<std::string> images = {})
{
    AiSendWorkItem work;
    work.hwnd = hwnd;
    work.prompt = std::move(prompt);
    work.model = std::move(model);
    work.fallback = std::move(fallback);
    work.answerStart = answerStart;
    work.mapChunks = std::move(mapChunks);
    work.reduceHeader = std::move(reduceHeader);
    work.images = std::move(images);

    std::thread([work = std::move(work)]() mutable {
        auto* result = new AiSendResult();

        // ── Chunked map-reduce path for large files ──────────────────────────
        if (!work.mapChunks.empty()) {
            auto postProgress = [&](const std::wstring& text) {
                if (IsWindow(work.hwnd))
                    PostMessageW(work.hwnd, WM_AI_PROGRESS, 0, (LPARAM)new std::wstring(text));
            };

            // Run the per-file/part requests with a SMALL amount of parallelism.
            // A local model shares one GPU/CPU, so flooding it with many big
            // requests just splits the compute — each one then finishes far later
            // (why 14-way stalled at 0/15).  A few in flight overlaps I/O without
            // starving prefill, and progress advances steadily.  True N-way speed
            // also needs the Ollama server's OLLAMA_NUM_PARALLEL raised.
            const int total = (int)work.mapChunks.size();
            const int kBatchCtx = 16384;                 // enough for a whole ~35 KB file
            int nThreads = 3;
            if (nThreads > total) nThreads = total;
            if (nThreads < 1) nThreads = 1;

            const std::wstring readingLabel = Ne_Ls(L"AI_PROGRESS_READING");
            std::vector<std::wstring> partResults(total);
            std::atomic<int> nextIdx{ 0 };
            std::atomic<int> completed{ 0 };

            auto mapWorker = [&]() {
                for (;;) {
                    if (NeAiClient_IsCancelRequested()) break;
                    int i = nextIdx.fetch_add(1);
                    if (i >= total) break;
                    std::wstring partReply, partErr;
                    if (NeAiClient_AskOllama(work.model, work.mapChunks[i], partReply, partErr, kBatchCtx)) {
                        while (!partReply.empty() && (partReply.back() == L'\n'
                            || partReply.back() == L'\r' || partReply.back() == L' '))
                            partReply.pop_back();
                        if (!partReply.empty() &&
                            partReply.find(L"nothing relevant") == std::wstring::npos) {
                            partResults[i] = std::move(partReply);
                        }
                    }
                    int done = completed.fetch_add(1) + 1;
                    int pct = (int)((double)done / (double)(total + 1) * 100.0);
                    postProgress(readingLabel + L"\r\n" + std::to_wstring(done)
                        + L"/" + std::to_wstring(total) + L"  (" + std::to_wstring(pct) + L"%)");
                }
            };

            std::vector<std::thread> pool;
            pool.reserve(nThreads);
            // Show progress immediately (0/total) — the first request can take a
            // while, and we only post again on each completion.
            postProgress(readingLabel + L"\r\n0/" + std::to_wstring(total) + L"  (0%)");
            for (int t = 0; t < nThreads; ++t) pool.emplace_back(mapWorker);
            for (auto& th : pool) th.join();

            // If the user hit Stop during the map phase, don't waste time on reduce.
            if (NeAiClient_IsCancelRequested()) {
                if (IsWindow(work.hwnd))
                    PostMessageW(work.hwnd, WM_AI_SEND_COMPLETE, 0, (LPARAM)result);
                else
                    delete result;
                return;
            }

            std::wstring notes;
            for (int i = 0; i < total; ++i)
                if (!partResults[i].empty())
                    notes += L"\r\n[Part " + std::to_wstring(i + 1) + L"]\r\n" + partResults[i] + L"\r\n";

            postProgress(std::wstring(Ne_Ls(L"AI_PROGRESS_SYNTH")) + L"\r\n(95%)");

            std::wstring reducePrompt = work.reduceHeader;
            reducePrompt += L"Notes extracted from the file batches:\r\n";
            reducePrompt += notes.empty() ? L"(no relevant notes were found)\r\n" : notes;
            reducePrompt += L"\r\nUser question: " + work.prompt + L"\r\n";

            AiStreamChunk chunkInfo = {};
            chunkInfo.hwnd = work.hwnd;
            chunkInfo.answerStart = work.answerStart;
            if (NeAiClient_AskOllamaStream(work.model, reducePrompt, &chunkInfo,
                    Ai_StreamChunkCallback, result->reply, result->error, kBatchCtx)) {
                result->ok = true;
            }
            result->streamed = result->streamed || chunkInfo.streamed;
            if (result->reply.empty() && !chunkInfo.streamed) {
                std::wstring rr, re;
                if (NeAiClient_AskOllama(work.model, reducePrompt, rr, re, kBatchCtx) && !rr.empty()) {
                    result->ok = true; result->reply = rr; result->error.clear();
                }
            }
            if (IsWindow(work.hwnd))
                PostMessageW(work.hwnd, WM_AI_SEND_COMPLETE, 0, (LPARAM)result);
            else
                delete result;
            return;
        }

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

        if (NeAiClient_AskOllamaStream(work.model, formattedPrompt, &chunkInfo, Ai_StreamChunkCallback, result->reply, result->error, 0, work.images)) {
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
                if (NeAiClient_AskOllamaStream(work.fallback, formattedPrompt, &chunkInfo, Ai_StreamChunkCallback, result->reply, fallbackError, 0, work.images)) {
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
        std::wstring label = st->cloudModel.empty() ? Ai_DefaultCloudModelName() : st->cloudModel;
        label += L" ";
        label += (st->role == AiMenuRole::Suggest) ? Ne_Ls(L"AI_ROLE_SUGGEST") : Ne_Ls(L"AI_ROLE_AGENT");
        return label;
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

// True for characters that must never reach the query RichEdit: invisible or
// format/control code points that a Windows console (CMD/PowerShell/Windows
// Terminal) can place on the clipboard.  Some of these — notably an UNPAIRED
// UTF-16 surrogate — make RichEdit hang and then crash a moment later.  Valid
// surrogate PAIRS are handled by the caller and must not be stripped here.
static bool Ai_IsStripPasteChar(wchar_t c)
{
    if (c == 0x7F) return true;                       // DEL
    if (c >= 0x80  && c <= 0x9F) return true;         // C1 controls
    if (c >= 0x200B && c <= 0x200F) return true;      // zero-width + LRM/RLM
    if (c == 0x2028 || c == 0x2029) return true;      // line / paragraph separators
    if (c >= 0x202A && c <= 0x202E) return true;      // bidi embedding/override
    if (c == 0x2060) return true;                     // word joiner
    if (c >= 0x2066 && c <= 0x2069) return true;      // bidi isolates
    if (c == 0xFEFF) return true;                     // BOM / zero-width no-break space
    if (c >= 0xFFF9 && c <= 0xFFFB) return true;      // interlinear annotation
    return false;
}

// Normalise arbitrary clipboard text (often a Windows console selection) into
// clean, insertable plain text.  Console payloads carry invisible/ill-formed
// code units that destabilise the query RichEdit; this repairs them and keeps
// only genuine readable characters so a paste can never crash the control.
static std::wstring Ai_CleanPasteText(const std::wstring& raw)
{
    if (raw.empty()) return std::wstring();

    // 1) UTF-8 round-trip: any unpaired surrogate or other ill-formed unit is
    //    replaced with U+FFFD, guaranteeing valid UTF-16 for the filter below.
    std::wstring norm;
    int n8 = WideCharToMultiByte(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0, nullptr, nullptr);
    if (n8 > 0) {
        std::string u8((size_t)n8, '\0');
        WideCharToMultiByte(CP_UTF8, 0, raw.c_str(), (int)raw.size(), &u8[0], n8, nullptr, nullptr);
        int nw = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), nullptr, 0);
        if (nw > 0) {
            norm.resize((size_t)nw);
            MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), &norm[0], nw);
        }
    }
    if (norm.empty()) norm = raw;   // conversion failed → fall back to the raw text

    // 2) Whitelist: keep tab, collapse CRLF/CR/LF to the bare CR RichEdit uses,
    //    and keep only real characters — dropping controls, format/bidi marks,
    //    private-use and noncharacter code points.  Language letters such as
    //    ØÆÅøæå are ordinary code points and pass through untouched.
    std::wstring clean;
    clean.reserve(norm.size());
    for (size_t i = 0; i < norm.size(); ++i) {
        wchar_t c = norm[i];
        if (c == L'\r') { clean.push_back(L'\r'); if (i + 1 < norm.size() && norm[i + 1] == L'\n') ++i; continue; }
        if (c == L'\n') { clean.push_back(L'\r'); continue; }
        if (c == L'\t') { clean.push_back(L'\t'); continue; }
        if (c < 0x20) continue;                       // C0 controls
        if (c == 0x7F) continue;                      // DEL
        if (c >= 0x80 && c <= 0x9F) continue;         // C1 controls
        if (c == 0xA0) { clean.push_back(L' '); continue; }   // NBSP → normal space
        if (c >= 0xD800 && c <= 0xDBFF) {             // high surrogate
            if (i + 1 < norm.size() && norm[i + 1] >= 0xDC00 && norm[i + 1] <= 0xDFFF) {
                unsigned cp = 0x10000u + (((unsigned)(c - 0xD800)) << 10) + (unsigned)(norm[i + 1] - 0xDC00);
                bool nonchar = (cp & 0xFFFE) == 0xFFFE;
                bool priv    = cp >= 0xF0000u;        // planes 15-16 private use
                if (!nonchar && !priv) { clean.push_back(c); clean.push_back(norm[i + 1]); }
                ++i;
            }
            continue;                                 // lone high surrogate → dropped
        }
        if (c >= 0xDC00 && c <= 0xDFFF) continue;     // lone low surrogate
        if (Ai_IsStripPasteChar(c)) continue;         // zero-width / bidi / BOM …
        if (c >= 0xE000 && c <= 0xF8FF) continue;     // BMP private use
        if (c >= 0xFDD0 && c <= 0xFDEF) continue;     // noncharacters
        if ((c & 0xFFFE) == 0xFFFE) continue;         // U+FFFE / U+FFFF
        clean.push_back(c);
    }
    return clean;
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
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE && st) {
        HWND hParent = (HWND)dwRefData;
        // Esc mirrors the Stop button: interrupt an in-progress request so the
        // user can add more info before it runs again. Only acts while busy.
        if (hParent && st->hStopBtn && IsWindowEnabled(st->hStopBtn)) {
            Ai_StopSend(hParent);
            return 0;
        }
    }
    // Route Ctrl+V / Shift+Insert through our own sanitizing WM_PASTE handler
    // below. RichEdit's built-in paste otherwise inserts the clipboard's rich
    // content directly, which means:
    //   • text copied from a dark editor tab keeps its light colour and is
    //     nearly invisible on the white input box, and
    //   • some console (CMD/PowerShell) payloads corrupt the control and crash
    //     the app a moment later (e.g. on the next Send).
    // Swallowing the key here (and the matching Ctrl+V WM_CHAR 0x16 below)
    // guarantees the safe path runs exactly once — no double paste.
    if (msg == WM_KEYDOWN &&
        ((wParam == L'V' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) ||
         (wParam == VK_INSERT && (GetKeyState(VK_SHIFT) & 0x8000)))) {
        SendMessageW(hwnd, WM_PASTE, 0, 0);
        return 0;
    }
    if (msg == WM_CHAR && wParam == 0x16) {
        // Ctrl+V control char — already handled in WM_KEYDOWN; swallow so the
        // control never performs (or double-performs) its own paste.
        return 0;
    }
    if (msg == WM_CHAR && (wParam == L'\r') && plainEnter) {
        return 0;
    }
    if (msg == WM_CHAR && wParam == VK_ESCAPE) {
        // Swallow the Esc WM_CHAR so the RichEdit doesn't beep after we handle it.
        return 0;
    }
    if (msg == WM_COPY || msg == WM_CUT) {
        // The query box is a plain input, but RichEdit's built-in copy/cut export
        // rich text.  That made Ctrl+C, Ctrl+X and the right-click menu disagree
        // with our plain-text paste, so the same content behaved differently
        // depending on how it was copied.  Export PLAIN text here so every
        // copy/cut/paste combination is identical.
        CHARRANGE cr = {}; SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&cr);
        if (cr.cpMin == cr.cpMax) return 0;             // nothing selected
        std::wstring sel((size_t)(cr.cpMax - cr.cpMin) + 1, L'\0');
        LRESULT got = SendMessageW(hwnd, EM_GETSELTEXT, 0, (LPARAM)&sel[0]);
        sel.resize((size_t)(got < 0 ? 0 : got));
        // RichEdit reports line breaks as bare CR; give the clipboard CRLF.
        std::wstring out;
        out.reserve(sel.size() + 8);
        for (wchar_t c : sel) {
            if (c == L'\n') continue;
            if (c == L'\r') out += L"\r\n";
            else out.push_back(c);
        }
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            size_t bytes = (out.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                if (void* p = GlobalLock(hMem)) {
                    memcpy(p, out.c_str(), bytes);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                } else {
                    GlobalFree(hMem);
                }
            }
            CloseClipboard();
        }
        if (msg == WM_CUT) {
            s_aiInputPasting = true;
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)L"");
            s_aiInputPasting = false;
            if (st && st->hInputSb) msb_notify_content_changed(st->hInputSb);
        }
        return 0;
    }
    if (msg == WM_PASTE) {
        // Image on the clipboard → capture it as PNG, remember it for the next
        // send (multimodal query), and drop it inline into the query box so the
        // user can see what will be sent.
        if (st && (IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_BITMAP))) {
            std::string png = Ai_CaptureClipboardImagePng(hwnd);
            if (!png.empty()) {
                st->pendingImages.push_back(Ai_Base64Encode(png));
                s_aiInputPasting = true;
                Ai_InsertInlinePng(hwnd, png);
                s_aiInputPasting = false;
                if (st->hInputSb) msb_notify_content_changed(st->hInputSb);
                SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
                return 0;
            }
        }
        // Read the clipboard text ourselves and insert it with EM_REPLACESEL
        // instead of relying on EM_PASTESPECIAL. EM_PASTESPECIAL crashes the
        // control on some multi-line plain-text payloads that originate outside
        // the app (e.g. a selection copied from a PowerShell/CMD console); a
        // manual insert of a sanitized buffer is both safe and lets us normalize
        // line endings and force the input box's own readable formatting. Rich
        // sources such as a dark editor tab otherwise paste light-coloured text
        // that is invisible on the white input box.
        std::wstring text;
        bool haveText = false;
        if (OpenClipboard(hwnd)) {
            if (HANDLE hUni = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* p = (const wchar_t*)GlobalLock(hUni)) {
                    // Bound by the allocation: a non-NUL-terminated console block
                    // would otherwise be read past its end and crash.
                    size_t maxChars = GlobalSize(hUni) / sizeof(wchar_t);
                    size_t n = 0;
                    while (n < maxChars && p[n] != L'\0') ++n;
                    text.assign(p, n);
                    GlobalUnlock(hUni);
                    haveText = true;
                }
            } else if (HANDLE hAnsi = GetClipboardData(CF_TEXT)) {
                if (const char* pa = (const char*)GlobalLock(hAnsi)) {
                    int need = MultiByteToWideChar(CP_ACP, 0, pa, -1, NULL, 0);
                    if (need > 1) {
                        text.resize((size_t)need);
                        MultiByteToWideChar(CP_ACP, 0, pa, -1, &text[0], need);
                        if (!text.empty() && text.back() == L'\0') text.pop_back();
                    }
                    GlobalUnlock(hAnsi);
                    haveText = true;
                }
            }
            CloseClipboard();
        }
        if (haveText) {
            // Repair ill-formed units and strip every control/format/invisible
            // code point a console selection may carry, so the insert is safe.
            std::wstring clean = Ai_CleanPasteText(text);
            CHARRANGE before = {}; SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&before);
            s_aiInputPasting = true;
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)clean.c_str());
            // Force the pasted run to solid black so text copied from a dark
            // source stays visible on the white input box.
            CHARRANGE after = {}; SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&after);
            if (after.cpMax > before.cpMin) {
                CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
                cf.dwMask      = CFM_COLOR;
                cf.dwEffects   = 0;
                cf.crTextColor = RGB(0, 0, 0);
                CHARRANGE sel = { before.cpMin, after.cpMax };
                SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)&sel);
                SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
                CHARRANGE caret = { after.cpMax, after.cpMax };
                SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)&caret);
            }
            s_aiInputPasting = false;
            // Reflow the custom scrollbar exactly once, now that the insert is
            // complete, so the reflow never re-enters the RichEdit mid-change.
            if (st && st->hInputSb) msb_notify_content_changed(st->hInputSb);
            SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
        }
        return 0;
    }
    if (msg == WM_CONTEXTMENU) {
        // RichEdit suppresses its own popup here; provide our own Cut/Copy/
        // Paste/Select All menu so the query box has a working right-click menu.
        DWORD selStart = 0, selEnd = 0;
        SendMessageW(hwnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        bool hasSel = (selStart != selEnd);
        bool canPaste = IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                        IsClipboardFormatAvailable(CF_TEXT);
        int  len = GetWindowTextLengthW(hwnd);
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING | (hasSel ? 0 : MF_GRAYED),   1, Ne_Ls(L"MENU_CUT"));
        AppendMenuW(hMenu, MF_STRING | (hasSel ? 0 : MF_GRAYED),   2, Ne_Ls(L"MENU_COPY"));
        AppendMenuW(hMenu, MF_STRING | (canPaste ? 0 : MF_GRAYED), 3, Ne_Ls(L"MENU_PASTE"));
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING | (len > 0 ? 0 : MF_GRAYED),  4, Ne_Ls(L"MENU_SELECTALL"));
        int x = (int)(short)LOWORD(lParam);
        int y = (int)(short)HIWORD(lParam);
        if (x == -1 && y == -1) {
            RECT rc = {}; GetWindowRect(hwnd, &rc);
            x = rc.left + 12; y = rc.top + 12;
        }
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL);
        DestroyMenu(hMenu);
        switch (cmd) {
            case 1: SendMessageW(hwnd, WM_CUT,    0, 0);          break;
            case 2: SendMessageW(hwnd, WM_COPY,   0, 0);          break;
            case 3: SendMessageW(hwnd, WM_PASTE,  0, 0);          break;  // routed through the plain-text handler above
            case 4: SendMessageW(hwnd, EM_SETSEL, 0, (LPARAM)-1); break;
        }
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void Ai_ApplyButtons(AiWindowState* st)
{
    if (!st || !st->dd) return;
    st->dd->buttonCount = 7;
    st->dd->buttons[0] = AiButtonSpec{ IDC_AI_SEND_BTN,  Ne_Ls(L"BTN_SEND"),  AiBtnTone::Green, Ai_MeasureButtonWidth(Ne_Ls(L"BTN_SEND")),  true };
    st->dd->buttons[1] = AiButtonSpec{ IDC_AI_STOP_BTN,  Ne_Ls(L"BTN_STOP"),  AiBtnTone::Red,   Ai_MeasureButtonWidth(Ne_Ls(L"BTN_STOP")),  false };
    st->dd->buttons[2] = AiButtonSpec{ IDC_AI_COPY_BTN,  Ne_Ls(L"BTN_COPY"),  AiBtnTone::Blue,  Ai_MeasureButtonWidth(Ne_Ls(L"BTN_COPY")),  false, true };
    st->dd->buttons[3] = AiButtonSpec{ IDC_AI_PREV_BTN,  Ne_Ls(L"BTN_PREV"),  AiBtnTone::Blue,  Ai_MeasureButtonWidth(Ne_Ls(L"BTN_PREV")),  false };
    st->dd->buttons[4] = AiButtonSpec{ IDC_AI_NEXT_BTN,  Ne_Ls(L"BTN_NEXT"),  AiBtnTone::Blue,  Ai_MeasureButtonWidth(Ne_Ls(L"BTN_NEXT")),  false };
    st->dd->buttons[5] = AiButtonSpec{ IDC_AI_CLEAR_BTN, Ne_Ls(L"BTN_CLEAR"), AiBtnTone::Red,   Ai_MeasureButtonWidth(Ne_Ls(L"BTN_CLEAR")), true };
    st->dd->buttons[6] = AiButtonSpec{ IDC_AI_CLOSE_BTN, Ne_Ls(L"BTN_CLOSE"), AiBtnTone::Red,   Ai_MeasureButtonWidth(Ne_Ls(L"BTN_CLOSE")), true };
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
    // "Local:" section header (greyed, non-selectable).
    Ai_AppendMenuOD(hModel, MF_STRING | MF_GRAYED | MF_DISABLED, 0, Ne_Ls(L"AI_MENU_LOCAL_HEADER"), false);
    for (const std::wstring& model : models) {
        Ai_AddModelMenuItem(hModel, model, AiMenuRole::Suggest);
    }

    // “Check local models” belongs with the Local models: place it between the
    // Local and Cloud groups, with a divider above and below it.
    Ai_AppendMenuOD(hModel, MF_SEPARATOR, 0, NULL, false);
    Ai_AppendMenuOD(hModel, MF_STRING, IDM_AI_CHECK_MODELS, Ne_Ls(L"MENU_AI_CHECK_MODELS"), false);
    Ai_AppendMenuOD(hModel, MF_SEPARATOR, 0, NULL, false);
    // "Cloud" section header, then the cloud models — served through the
    // signed-in local Ollama daemon.  Greyed until the user signs in
    // (Cloud → Sign in to Ollama).
    Ai_AppendMenuOD(hModel, MF_STRING | MF_GRAYED | MF_DISABLED, 0, Ne_Ls(L"AI_MENU_CLOUD_HEADER"), false);
    {
        bool cloudEnabled = st && st->signedIn;
        for (const std::wstring& cm : Ai_CloudModelCandidates()) {
            Ai_AddModelMenuItem(hModel, cm, AiMenuRole::Suggest, true, cloudEnabled);
        }
    }

    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_MODE_LOCAL, Ne_Ls(L"AI_MENU_LOCAL_ONLY"), false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_MODE_CLOUD, Ne_Ls(L"AI_MENU_CLOUD_SUBSCRIPTION"), false);
    Ai_AppendMenuOD(hCloud, MF_SEPARATOR, 0, NULL, false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_SET_API_KEY, Ne_Ls(L"AI_MENU_ADD_API_KEY"), false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_SIGN_IN, Ne_Ls(L"AI_MENU_SIGN_IN"), false);
    Ai_AppendMenuOD(hCloud, MF_STRING, IDM_AI_SIGN_OUT, Ne_Ls(L"AI_MENU_SIGN_OUT"), false);
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
                ? (st->cloudMode && st->cloudModel == item.model && st->role == item.role)
                : (!st->cloudMode && st->model == item.model && st->role == item.role);
            if (checked) {
                CheckMenuItem(hModel, item.id, MF_BYCOMMAND | MF_CHECKED);
            }
        }
    }

    return hMenu;
}

// ── Project-context helpers ───────────────────────────────────────────────────
static std::wstring Ai_LowerW(std::wstring s)
{
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

// Case-insensitive Levenshtein edit distance (inputs assumed already lowercased).
static int Ai_EditDistance(const std::wstring& a, const std::wstring& b)
{
    const size_t n = a.size(), m = b.size();
    if (n == 0) return (int)m;
    if (m == 0) return (int)n;
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = (int)j;
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= m; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = cur[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            cur[j] = std::min(del, std::min(ins, sub));
        }
        prev.swap(cur);
    }
    return prev[m];
}

// Pull candidate file references out of the prompt (tokens that contain a path
// separator or end in a dotted extension).  Surrounding quotes/backticks/brackets
// and trailing punctuation are trimmed.
static std::vector<std::wstring> Ai_ExtractFileRefs(const std::wstring& prompt)
{
    std::vector<std::wstring> refs;
    std::wstring tok;
    auto flush = [&]() {
        if (tok.empty()) return;
        // Trim leading/trailing wrapper characters.
        const std::wstring junk = L"`'\"()[]{}<>,;:!?*";
        size_t b = 0, e = tok.size();
        while (b < e && junk.find(tok[b]) != std::wstring::npos) ++b;
        while (e > b && (junk.find(tok[e - 1]) != std::wstring::npos || tok[e - 1] == L'.')) --e;
        std::wstring t = tok.substr(b, e - b);
        tok.clear();
        if (t.size() < 2) return;
        bool hasSep = t.find(L'/') != std::wstring::npos || t.find(L'\\') != std::wstring::npos;
        bool hasExt = false;
        size_t dot = t.find_last_of(L'.');
        if (dot != std::wstring::npos && dot + 1 < t.size() && dot > 0) {
            hasExt = true;
            for (size_t i = dot + 1; i < t.size(); ++i)
                if (!iswalnum(t[i])) { hasExt = false; break; }
            if (t.size() - dot - 1 > 8) hasExt = false;
        }
        if (hasSep || hasExt) refs.push_back(t);
    };
    for (wchar_t ch : prompt) {
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') flush();
        else tok += ch;
    }
    flush();
    // A real query names a few files; a large paste (e.g. console output) can
    // yield thousands of path-like tokens.  Each ref later drives disk scans +
    // typo correction, so cap the count to keep Send from freezing/crashing.
    if (refs.size() > 40) refs.resize(40);
    return refs;
}

// Find the best-matching child entry of dir for seg, comparing against what is
// actually on disk (case-insensitive exact wins; otherwise closest edit distance).
static bool Ai_FindBestChild(const std::wstring& dir, const std::wstring& seg,
                             bool wantDirOnly, std::wstring& outName, bool& outExact)
{
    std::wstring segLow = Ai_LowerW(seg);
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    int bestDist = 1 << 30;
    std::wstring bestName;
    bool bestIsFile = false;
    bool found = false;
    do {
        const wchar_t* nm = fd.cFileName;
        if (wcscmp(nm, L".") == 0 || wcscmp(nm, L"..") == 0) continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (wantDirOnly && !isDir) continue;
        std::wstring nmLow = Ai_LowerW(nm);
        if (nmLow == segLow) { outName = nm; outExact = true; FindClose(h); return true; }
        int d = Ai_EditDistance(nmLow, segLow);
        bool better = d < bestDist ||
                      (d == bestDist && !wantDirOnly && !isDir && !bestIsFile);
        if (better) { bestDist = d; bestName = nm; bestIsFile = !isDir; found = true; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!found) return false;
    int threshold = std::max(2, (int)segLow.size() / 2 + 1);
    if (bestDist > threshold) return false;
    outName = bestName;
    outExact = false;
    return true;
}

// Resolve a path-qualified reference against disk, correcting typos per segment.
// Follows junctions/symlinks naturally because it reads real directory entries.
static bool Ai_ResolveOnDisk(const std::wstring& root, const std::wstring& norm,
                             std::wstring& outRel, std::wstring& outFull, bool& outCorrected)
{
    std::vector<std::wstring> segs;
    std::wstring cur;
    for (wchar_t ch : norm) {
        if (ch == L'\\') { if (!cur.empty()) segs.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    if (!cur.empty()) segs.push_back(cur);
    if (segs.empty()) return false;

    std::wstring dir = root;
    std::wstring rel;
    outCorrected = false;
    for (size_t i = 0; i < segs.size(); ++i) {
        bool wantDirOnly = (i + 1 < segs.size());
        std::wstring match;
        bool exact = false;
        if (!Ai_FindBestChild(dir, segs[i], wantDirOnly, match, exact)) return false;
        if (!exact) outCorrected = true;
        dir += L"\\" + match;
        rel += (rel.empty() ? L"" : L"/") + match;
    }
    DWORD attr = GetFileAttributesW(dir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
    outRel = rel;
    outFull = dir;
    return true;
}

// Resolve a single reference (exact first, then fuzzy) to an on-disk file inside
// the project.  Bare names are matched against the collected file list by base
// name; path-qualified names are resolved segment-by-segment against disk.
static bool Ai_ResolveRef(const std::wstring& root, const std::wstring& ref,
                          const std::vector<NeProjectFile>& files,
                          std::wstring& outRel, std::wstring& outFull, bool& outCorrected)
{
    // Normalise: '/'→'\', strip leading ".\" and stray leading separators.
    std::wstring norm = ref;
    for (auto& c : norm) if (c == L'/') c = L'\\';
    while (norm.size() >= 2 && norm[0] == L'.' && norm[1] == L'\\') norm.erase(0, 2);
    while (!norm.empty() && norm[0] == L'\\') norm.erase(0, 1);
    if (norm.empty()) return false;

    bool hasSep = norm.find(L'\\') != std::wstring::npos;
    bool isAbsolute = norm.size() >= 2 && norm[1] == L':';

    if (isAbsolute) {
        std::wstring rootLow = Ai_LowerW(root);
        std::wstring normLow = Ai_LowerW(norm);
        if (normLow.compare(0, rootLow.size(), rootLow) == 0) {
            // Make the path relative to the root.  When the reference IS the root
            // itself (a console prompt like "PS …\Project>" pastes exactly that),
            // there is no file part — substr(root.size()+1) would run past the end
            // and throw std::out_of_range, crashing the app.  Treat it as no file.
            size_t cut = root.size() + (root.back() == L'\\' ? 0 : 1);
            if (cut >= norm.size()) return false;
            norm = norm.substr(cut);
        }
        else
            return false; // outside project — handled by one-off access later
        hasSep = norm.find(L'\\') != std::wstring::npos;
    }

    if (hasSep)
        return Ai_ResolveOnDisk(root, norm, outRel, outFull, outCorrected);

    // Bare name: search collected files by base name.
    std::wstring baseLow = Ai_LowerW(norm);
    // Exact base-name match first.
    for (const auto& f : files) {
        std::wstring fb = f.relPath;
        size_t s = fb.find_last_of(L'/');
        if (s != std::wstring::npos) fb = fb.substr(s + 1);
        if (Ai_LowerW(fb) == baseLow) {
            outRel = f.relPath; outFull = f.fullPath; outCorrected = false; return true;
        }
    }
    // Fuzzy base-name match.
    int bestDist = 1 << 30; size_t bestIdx = (size_t)-1;
    for (size_t i = 0; i < files.size(); ++i) {
        std::wstring fb = files[i].relPath;
        size_t s = fb.find_last_of(L'/');
        if (s != std::wstring::npos) fb = fb.substr(s + 1);
        int d = Ai_EditDistance(Ai_LowerW(fb), baseLow);
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    int threshold = std::max(2, (int)baseLow.size() / 2 + 1);
    if (bestIdx != (size_t)-1 && bestDist <= threshold) {
        outRel = files[bestIdx].relPath; outFull = files[bestIdx].fullPath;
        outCorrected = true; return true;
    }
    return false;
}

// A file (on disk or a DB knowledge record) the user asked the AI to work on.
struct AiRequestedItem {
    std::wstring rel;      // display path: relative file path, or "db://kind/title"
    std::wstring full;     // absolute path on disk, empty for DB docs
    std::wstring body;     // preloaded content for DB docs, empty for on-disk files
    bool         isDoc = false;
};

// Translate a glob (with * ? and ** ) into an ECMAScript regex string, anchored,
// matched case-insensitively against a relative path that uses '/' separators.
static std::wstring Ai_GlobToRegex(const std::wstring& glob)
{
    std::wstring re = L"^";
    for (size_t i = 0; i < glob.size(); ++i) {
        wchar_t c = glob[i];
        if (c == L'*') {
            if (i + 1 < glob.size() && glob[i + 1] == L'*') {   // ** = any depth
                re += L".*";
                ++i;
                if (i + 1 < glob.size() && glob[i + 1] == L'/') ++i;  // swallow **/ 
            } else {
                re += L"[^/]*";                                   // * = within a segment
            }
        } else if (c == L'?') {
            re += L"[^/]";
        } else if (wcschr(L".^$+(){}[]|\\", c)) {
            re += L'\\'; re += c;                                 // escape regex metachars
        } else {
            re += c;
        }
    }
    re += L"$";
    return re;
}

// Is this token a pattern (glob wildcard or an explicit regex: / re: form)?
static bool Ai_TokenLooksLikePattern(const std::wstring& t)
{
    if (t.find(L'*') != std::wstring::npos || t.find(L'?') != std::wstring::npos) return true;
    if (t.size() > 6 && Ai_LowerW(t.substr(0, 6)) == L"regex:") return true;
    if (t.size() > 3 && Ai_LowerW(t.substr(0, 3)) == L"re:") return true;
    if (t.size() >= 2 && t.front() == L'/' && t.back() == L'/') return true;
    return false;
}

// Match one relative path against a compiled matcher (case-insensitive).
static bool Ai_PathMatchesRegex(const std::wstring& relPath, const std::wregex& re)
{
    try { return std::regex_match(relPath, re); }
    catch (...) { return false; }
}

// Tokenise the prompt preserving path/pattern characters ( * ? / \ . : ), so
// globs and regex survive (Ai_ExtractFileRefs strips wildcards and can't).
static std::vector<std::wstring> Ai_ExtractPatternTokens(const std::wstring& prompt)
{
    std::vector<std::wstring> toks;
    std::wstring cur;
    auto flush = [&]() {
        // Trim surrounding quotes/brackets but KEEP wildcards and separators.
        const std::wstring junk = L"`'\"()[]{}<>,;!?";
        size_t b = 0, e = cur.size();
        while (b < e && junk.find(cur[b]) != std::wstring::npos) ++b;
        while (e > b && junk.find(cur[e - 1]) != std::wstring::npos) --e;
        std::wstring t = cur.substr(b, e - b);
        cur.clear();
        if (t.size() >= 2) toks.push_back(t);
    };
    for (wchar_t ch : prompt) {
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') flush();
        else cur += ch;
    }
    flush();
    // Each returned token is later expanded into a glob/regex and matched against
    // every project file; an unbounded token list from a large paste would make
    // that O(tokens × files) work freeze then crash the app on Send.  A genuine
    // file/pattern query never needs this many tokens.
    if (toks.size() > 64) toks.resize(64);
    return toks;
}

// Normalise a directory-ish reference to a relative, '/'-separated prefix.
// Returns empty if it does not correspond to a directory inside the project.
static std::wstring Ai_NormalizeDirPrefix(const std::wstring& root, const std::wstring& tokenIn)
{
    std::wstring t = tokenIn;
    for (auto& c : t) if (c == L'\\') c = L'/';
    while (t.size() >= 2 && t[0] == L'.' && t[1] == L'/') t.erase(0, 2);
    while (!t.empty() && t.front() == L'/') t.erase(0, 1);
    while (!t.empty() && t.back() == L'/') t.pop_back();
    if (t.empty()) return L"";
    // Must exist on disk as a directory under the root.
    std::wstring full = root;
    if (!full.empty() && full.back() != L'\\') full += L'\\';
    std::wstring win = t;
    for (auto& c : win) if (c == L'/') c = L'\\';
    full += win;
    DWORD attr = GetFileAttributesW(full.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return L"";
    return Ai_LowerW(t);
}

// Expand every file reference in the prompt into concrete project items.
// Handles: plain names/paths (with typo correction), directory references
// (locale, .\locale\, locale/ → all files under it), glob wildcards
// (locale/*, *.txt, **/*.cpp) and explicit regex (regex:PATTERN, /PATTERN/).
// Also pulls in the project's DB knowledge records (project_docs) when the
// prompt refers to notes/db, or always as low-cost extra context.
static std::vector<AiRequestedItem> Ai_ExpandFileRefs(
    const std::wstring& root, const std::vector<NeProjectFile>& files,
    const std::wstring& prompt, bool& outBulk)
{
    outBulk = false;
    std::vector<AiRequestedItem> out;
    std::set<std::wstring> seen;
    auto add = [&](const std::wstring& rel, const std::wstring& full) {
        std::wstring key = Ai_LowerW(full.empty() ? rel : full);
        if (seen.count(key)) return;
        seen.insert(key);
        AiRequestedItem it; it.rel = rel; it.full = full; out.push_back(std::move(it));
    };

    // 1) Explicit patterns (globs / regex) and directory references.
    for (const auto& tok : Ai_ExtractPatternTokens(prompt)) {
        std::wstring patternRel;   // '/'-separated, lower-cased, for matching
        bool isRegex = false;

        std::wstring low = Ai_LowerW(tok);
        if (low.compare(0, 6, L"regex:") == 0) { patternRel = tok.substr(6); isRegex = true; }
        else if (low.compare(0, 3, L"re:") == 0) { patternRel = tok.substr(3); isRegex = true; }
        else if (tok.size() >= 2 && tok.front() == L'/' && tok.back() == L'/') {
            patternRel = tok.substr(1, tok.size() - 2); isRegex = true;
        } else if (Ai_TokenLooksLikePattern(tok)) {
            // Glob: normalise separators and strip leading ./ and root.
            std::wstring g = tok;
            for (auto& c : g) if (c == L'\\') c = L'/';
            while (g.size() >= 2 && g[0] == L'.' && g[1] == L'/') g.erase(0, 2);
            while (!g.empty() && g.front() == L'/') g.erase(0, 1);
            patternRel = Ai_GlobToRegex(Ai_LowerW(g));
            isRegex = true;   // now a regex string
        } else {
            // Maybe a bare/qualified directory → include everything beneath it.
            std::wstring dir = Ai_NormalizeDirPrefix(root, tok);
            if (!dir.empty()) {
                std::wstring pre = dir + L"/";
                int before = (int)out.size();
                for (const auto& f : files) {
                    std::wstring rl = Ai_LowerW(f.relPath);
                    if (rl == dir || rl.compare(0, pre.size(), pre) == 0)
                        add(f.relPath, f.fullPath);
                }
                if ((int)out.size() - before > 1) outBulk = true;
            }
            continue;
        }

        if (isRegex && !patternRel.empty()) {
            std::wregex re;
            bool ok = true;
            try { re.assign(patternRel, std::regex::ECMAScript | std::regex::icase); }
            catch (...) { ok = false; }
            if (ok) {
                int before = (int)out.size();
                for (const auto& f : files)
                    if (Ai_PathMatchesRegex(f.relPath, re)) add(f.relPath, f.fullPath);
                if ((int)out.size() - before > 1) outBulk = true;
            }
        }
    }

    // 2) Plain named files (existing resolver, with typo auto-correction).
    for (const auto& ref : Ai_ExtractFileRefs(prompt)) {
        std::wstring rel, full; bool corrected = false;
        if (Ai_ResolveRef(root, ref, files, rel, full, corrected))
            add(rel, full);
    }

    // 3) DB knowledge records.  Included when the user refers to notes/db, or
    //    when a bulk request is under way (so "everything in the project" truly
    //    means files + DB).  Kept generic so the store can evolve.
    {
        std::wstring pl = Ai_LowerW(prompt);
        bool wantsDb = pl.find(L"note") != std::wstring::npos ||
                       pl.find(L"db") != std::wstring::npos ||
                       pl.find(L"database") != std::wstring::npos ||
                       pl.find(L"everything") != std::wstring::npos ||
                       pl.find(L"all files") != std::wstring::npos ||
                       outBulk;
        if (wantsDb) {
            int64_t pid = NeProjects_GetActiveId();
            std::vector<NeProjectDoc> docs;
            if (pid && NeProjects_CollectDocs(pid, docs) && !docs.empty()) {
                for (const auto& d : docs) {
                    std::wstring rel = L"db://" + (d.kind.empty() ? L"note" : d.kind)
                                     + L"/" + (d.title.empty() ? std::to_wstring(d.id) : d.title);
                    std::wstring key = Ai_LowerW(rel);
                    if (seen.count(key)) continue;
                    seen.insert(key);
                    AiRequestedItem it; it.rel = rel; it.isDoc = true; it.body = d.body;
                    out.push_back(std::move(it));
                }
                if (docs.size() > 1) outBulk = true;
            }
        }
    }

    return out;
}


// plus identifier-like tokens (containing '_' or camelCase) so the AI can find a
// symbol/function by name without reading whole files.
static std::vector<std::wstring> Ai_ExtractSearchTerms(const std::wstring& prompt)
{
    std::vector<std::wstring> terms;
    auto addTerm = [&](std::wstring t) {
        while (!t.empty() && iswspace(t.front())) t.erase(t.begin());
        while (!t.empty() && iswspace(t.back()))  t.pop_back();
        if (t.size() < 3) return;
        for (const auto& e : terms) if (Ai_LowerW(e) == Ai_LowerW(t)) return;
        terms.push_back(std::move(t));
    };
    // `backtick`-quoted spans.
    size_t p = 0;
    while ((p = prompt.find(L'`', p)) != std::wstring::npos) {
        size_t e = prompt.find(L'`', p + 1);
        if (e == std::wstring::npos) break;
        addTerm(prompt.substr(p + 1, e - p - 1));
        p = e + 1;
    }
    // Identifier-like tokens.
    std::wstring cur;
    auto flush = [&]() {
        if (cur.size() >= 4) {
            bool hasUnderscore = cur.find(L'_') != std::wstring::npos;
            bool camel = false;
            for (size_t i = 1; i < cur.size(); ++i)
                if (iswupper(cur[i]) && iswlower(cur[i - 1])) { camel = true; break; }
            if (hasUnderscore || camel) addTerm(cur);
        }
        cur.clear();
    };
    for (wchar_t ch : prompt) {
        if (iswalnum(ch) || ch == L'_') cur += ch;
        else flush();
    }
    flush();
    if (terms.size() > 10) terms.resize(10);
    return terms;
}

// Grep the project for the search terms and return matching code snippets with a
// few lines of surrounding context (so a whole small function is captured).  This
// lets the model rewrite a function without ingesting the entire file.
static bool Ai_SearchSnippets(const std::vector<NeProjectFile>& files,
                              const std::vector<std::wstring>& terms,
                              const std::set<std::wstring>& referencedLower,
                              std::wstring& outBlock)
{
    if (terms.empty() || files.empty()) return false;
    std::vector<std::wstring> termsLow;
    for (const auto& t : terms) termsLow.push_back(Ai_LowerW(t));

    const size_t kBudget      = 16000;    // wide chars of snippets total
    const size_t kMaxSnippets = 12;
    const int    kBefore      = 6;
    const int    kAfter       = 34;
    const int    kFileScanCap = 2500;
    const size_t kFileReadCap = 800 * 1024;
    const size_t kPerSnippet  = 3200;

    // Scan referenced files first, then the rest.
    std::vector<size_t> order;
    order.reserve(files.size());
    for (size_t i = 0; i < files.size(); ++i)
        if (referencedLower.count(Ai_LowerW(files[i].fullPath))) order.push_back(i);
    for (size_t i = 0; i < files.size(); ++i)
        if (!referencedLower.count(Ai_LowerW(files[i].fullPath))) order.push_back(i);

    std::wstring block;
    size_t used = 0, snippets = 0; int scanned = 0;
    for (size_t oi = 0; oi < order.size() && used < kBudget
                     && snippets < kMaxSnippets && scanned < kFileScanCap; ++oi) {
        std::wstring content;
        if (!NeProjects_ReadTextFile(files[order[oi]].fullPath, content, kFileReadCap)) continue;
        ++scanned;
        if (content.empty()) continue;
        std::wstring contentLow = Ai_LowerW(content);
        bool anyTerm = false;
        for (const auto& tl : termsLow) if (contentLow.find(tl) != std::wstring::npos) { anyTerm = true; break; }
        if (!anyTerm) continue;

        // Split into lines.
        std::vector<std::wstring> lines;
        size_t s = 0;
        while (s <= content.size()) {
            size_t nl = content.find(L'\n', s);
            std::wstring ln = (nl == std::wstring::npos) ? content.substr(s) : content.substr(s, nl - s);
            if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
            lines.push_back(std::move(ln));
            if (nl == std::wstring::npos) break;
            s = nl + 1;
        }

        int lastEmittedEnd = -1000;
        for (int li = 0; li < (int)lines.size() && used < kBudget && snippets < kMaxSnippets; ++li) {
            std::wstring lnLow = Ai_LowerW(lines[li]);
            bool match = false;
            for (const auto& tl : termsLow) if (lnLow.find(tl) != std::wstring::npos) { match = true; break; }
            if (!match) continue;
            int start = std::max(0, li - kBefore);
            int end   = std::min((int)lines.size() - 1, li + kAfter);
            if (start <= lastEmittedEnd) continue;   // skip overlapping match
            std::wstring snip;
            for (int k = start; k <= end; ++k) { snip += lines[k]; snip += L"\r\n"; }
            if (snip.size() > kPerSnippet) { snip.resize(kPerSnippet); snip += L"\r\n... (snippet truncated)\r\n"; }
            if (used + snip.size() > kBudget) break;
            block += L"=== " + files[order[oi]].relPath + L"  (near line "
                   + std::to_wstring(li + 1) + L") ===\r\n" + snip + L"\r\n";
            used += snip.size();
            ++snippets;
            lastEmittedEnd = end;
        }
    }
    if (block.empty()) return false;
    outBlock = L"Matching code snippets (searched the project for: ";
    for (size_t i = 0; i < terms.size(); ++i) { if (i) outBlock += L", "; outBlock += terms[i]; }
    outBlock += L"):\r\n" + block + L"\r\n";
    return true;
}

// Extract explicit regex patterns the user wants to search file CONTENTS for:
//   regex:PATTERN   re:PATTERN   /PATTERN/
// (The same tokens also select file paths in Ai_ExpandFileRefs; here they drive a
// content grep so "search all files for <pattern>" works regardless of path.)
static std::vector<std::wstring> Ai_ExtractContentRegexes(const std::wstring& prompt)
{
    std::vector<std::wstring> pats;
    for (const auto& tok : Ai_ExtractPatternTokens(prompt)) {
        std::wstring low = Ai_LowerW(tok);
        std::wstring pat;
        if (low.compare(0, 6, L"regex:") == 0)      pat = tok.substr(6);
        else if (low.compare(0, 3, L"re:") == 0)    pat = tok.substr(3);
        else if (tok.size() >= 2 && tok.front() == L'/' && tok.back() == L'/')
            pat = tok.substr(1, tok.size() - 2);
        if (pat.size() >= 2) {
            bool dup = false;
            for (const auto& e : pats) if (e == pat) { dup = true; break; }
            if (!dup) pats.push_back(pat);
        }
    }
    return pats;
}

// Grep EVERY project file line-by-line for one or more regexes and emit matching
// snippets with surrounding context.  Reads are capped per file so huge files
// (20000+ lines) stay bounded; the scan is bounded by a total budget and a file
// cap.  Returns false when nothing matched.
static bool Ai_RegexSearchSnippets(const std::vector<NeProjectFile>& files,
                                   const std::vector<std::wstring>& patterns,
                                   std::wstring& outBlock)
{
    if (patterns.empty() || files.empty()) return false;

    std::vector<std::wregex> res;
    std::vector<std::wstring> reText;
    for (const auto& p : patterns) {
        try {
            res.emplace_back(p, std::regex::ECMAScript | std::regex::icase | std::regex::optimize);
            reText.push_back(p);
        } catch (...) { /* skip invalid regex */ }
    }
    if (res.empty()) return false;

    const size_t kBudget      = 16000;    // wide chars of snippets total
    const size_t kMaxSnippets = 20;
    const int    kBefore      = 3;
    const int    kAfter       = 12;
    const int    kFileScanCap = 5000;
    const size_t kFileReadCap = 2u * 1024 * 1024;   // 2 MB head per file
    const size_t kPerSnippet  = 2400;

    std::wstring block;
    size_t used = 0, snippets = 0; int scanned = 0; int filesMatched = 0;
    for (size_t fi = 0; fi < files.size() && used < kBudget
                     && snippets < kMaxSnippets && scanned < kFileScanCap; ++fi) {
        std::wstring content;
        if (!NeProjects_ReadTextFile(files[fi].fullPath, content, kFileReadCap)) continue;
        ++scanned;
        if (content.empty()) continue;

        // Split into lines.
        std::vector<std::wstring> lines;
        size_t s = 0;
        while (s <= content.size()) {
            size_t nl = content.find(L'\n', s);
            std::wstring ln = (nl == std::wstring::npos) ? content.substr(s) : content.substr(s, nl - s);
            if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
            lines.push_back(std::move(ln));
            if (nl == std::wstring::npos) break;
            s = nl + 1;
        }

        int lastEmittedEnd = -1000;
        bool fileHeaderNeeded = true;
        for (int li = 0; li < (int)lines.size() && used < kBudget && snippets < kMaxSnippets; ++li) {
            bool match = false;
            for (const auto& re : res) {
                try { if (std::regex_search(lines[li], re)) { match = true; break; } }
                catch (...) {}
            }
            if (!match) continue;
            int start = std::max(0, li - kBefore);
            int end   = std::min((int)lines.size() - 1, li + kAfter);
            if (start <= lastEmittedEnd) continue;   // skip overlapping match
            std::wstring snip;
            for (int k = start; k <= end; ++k) {
                snip += std::to_wstring(k + 1); snip += L": "; snip += lines[k]; snip += L"\r\n";
            }
            if (snip.size() > kPerSnippet) { snip.resize(kPerSnippet); snip += L"\r\n... (snippet truncated)\r\n"; }
            if (used + snip.size() > kBudget) break;
            if (fileHeaderNeeded) { filesMatched++; fileHeaderNeeded = false; }
            block += L"=== " + files[fi].relPath + L"  (line "
                   + std::to_wstring(li + 1) + L") ===\r\n" + snip + L"\r\n";
            used += snip.size();
            ++snippets;
            lastEmittedEnd = end;
        }
    }
    if (block.empty()) return false;
    outBlock = L"Regex search matches (patterns: ";
    for (size_t i = 0; i < reText.size(); ++i) { if (i) outBlock += L", "; outBlock += reText[i]; }
    outBlock += L") across " + std::to_wstring(filesMatched) + L" file(s):\r\n" + block + L"\r\n";
    return true;
}

// Build a "suggestion mode" project-context block for the active project, if any.
// Runs on the UI thread (SQLite handle is single-threaded) before the send worker
// is spawned.  Returns an empty string when no project is active.
// Upper bound on the project-file walk done on the UI thread for every send.
// The walk is synchronous, so an unbounded root (e.g. one holding a vcpkg /
// node_modules tree) would freeze the window; this caps the worst case while
// still covering any realistic source project.
static constexpr int kAiMaxProjectFiles = 20000;

static std::wstring Ai_BuildProjectContext(const std::wstring& userPrompt)
{
    int64_t pid = NeProjects_GetActiveId();
    if (!pid) return L"";
    NeProject proj;
    if (!NeProjects_GetById(pid, proj) || proj.rootPath.empty()) return L"";

    std::vector<NeProjectFile> files;
    NeProjects_CollectFiles(proj.rootPath, files, kAiMaxProjectFiles);

    // Expand every reference the user made — plain names (with typo correction),
    // directories, glob wildcards, explicit regex — plus any DB knowledge records,
    // into concrete items.  These are always included in full (budget permitting).
    struct Requested { std::wstring rel; std::wstring full; std::wstring body; bool isDoc; };
    std::vector<Requested> requested;
    bool bulk = false;
    {
        auto items = Ai_ExpandFileRefs(proj.rootPath, files, userPrompt, bulk);
        for (const auto& it : items)
            requested.push_back({ it.rel, it.full, it.body, it.isDoc });
    }


    // Tokenize the prompt into lowercase words (>= 3 chars) for relevance ranking.
    std::vector<std::wstring> tokens;
    {
        std::wstring cur;
        for (wchar_t ch : userPrompt) {
            if (iswalnum(ch) || ch == L'_') {
                cur += (wchar_t)towlower(ch);
            } else {
                if (cur.size() >= 3) tokens.push_back(cur);
                cur.clear();
            }
        }
        if (cur.size() >= 3) tokens.push_back(cur);
    }

    // Score each file by filename / path matches against the prompt tokens.
    struct Scored { int score; size_t idx; };
    std::vector<Scored> scored;
    scored.reserve(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        std::wstring pathLow = Ai_LowerW(files[i].relPath);
        std::wstring baseLow = pathLow;
        size_t slash = baseLow.find_last_of(L'/');
        if (slash != std::wstring::npos) baseLow = baseLow.substr(slash + 1);
        int score = 0;
        for (const auto& t : tokens) {
            if (baseLow.find(t) != std::wstring::npos)      score += 10;
            else if (pathLow.find(t) != std::wstring::npos) score += 4;
        }
        scored.push_back({ score, i });
    }
    std::stable_sort(scored.begin(), scored.end(),
        [&](const Scored& a, const Scored& b) {
            if (a.score != b.score) return a.score > b.score;
            return files[a.idx].relPath.size() < files[b.idx].relPath.size();
        });

    std::wstring ctx;
    ctx += L"[PROJECT CONTEXT \u2014 SUGGESTION MODE]\r\n";
    ctx += L"Active project: " + proj.name + L"\r\n";
    ctx += L"Project root: " + proj.rootPath + L"\r\n\r\n";
    ctx += L"You are assisting with this project. Follow these rules:\r\n";
    ctx += L"- First, directly and specifically answer the user's question. Then add supporting detail only if it helps.\r\n";
    ctx += L"- Base your answer on the project files below. Ignore build logs and stale error output.\r\n";
    ctx += L"- When you propose code, name the exact file (relative path listed below) and the precise location (function, class, or nearby line) where the developer should place it.\r\n";
    ctx += L"- Only reference files that exist in this project. Do not invent file paths.\r\n";
    ctx += L"- If a change belongs in a new file, say so and give a suggested relative path.\r\n\r\n";

    // Full file listing so the model knows the whole project structure.  Sorted
    // SHALLOW-FIRST (fewest path separators, then alphabetical) so the developer's
    // own files — root files and things like locale/*.txt — always appear before
    // deep vendored trees (curl/, scintilla_src/, third_party/…).  Previously the
    // collection order walked those big libraries first, so with a small cap the
    // real project files (e.g. all 15 locale files) never got listed at all.
    ctx += L"Project files (relative to root):\r\n";
    {
        std::vector<const std::wstring*> listing;
        listing.reserve(files.size());
        for (const auto& f : files) listing.push_back(&f.relPath);
        auto depthOf = [](const std::wstring& p) {
            int d = 0; for (wchar_t c : p) if (c == L'/') ++d; return d;
        };
        std::stable_sort(listing.begin(), listing.end(),
            [&](const std::wstring* a, const std::wstring* b) {
                int da = depthOf(*a), db = depthOf(*b);
                if (da != db) return da < db;
                return Ai_LowerW(*a) < Ai_LowerW(*b);
            });
        // Bound the listing by BOTH an entry count and a character budget so a
        // project with large vendored trees (curl/, scintilla_src/, third_party/…)
        // can never explode the prompt past the model's context window — the whole
        // listing used to reach ~800k chars and the cloud now rejects it outright.
        // Shallow-first ordering means the developer's own files are listed first
        // and the deep library files are what get dropped when the budget runs out.
        const int    kMaxListed      = 2000;    // hard entry cap
        const size_t kListingBudget  = 24000;   // wide chars (~6k tokens)
        size_t listChars = 0;
        int listed = 0;
        for (const std::wstring* p : listing) {
            if (listed >= kMaxListed || listChars >= kListingBudget) break;
            ctx += L"  " + *p + L"\r\n";
            listChars += p->size() + 4;   // "  " + path + "\r\n"
            ++listed;
        }
        if (listed < (int)listing.size())
            ctx += L"  ... (" + std::to_wstring((int)listing.size() - listed) + L" more)\r\n";
    }
    ctx += L"\r\n";

    // File contents.  Local models prefill the ENTIRE prompt before emitting the
    // first token, so the total is kept modest (a few thousand tokens) to stay
    // responsive; a large named file is truncated to its head, which is normally
    // enough to describe it.  Files the user named get budget priority AND are
    // placed LAST (closest to the question) so they survive tail-truncation.
    const size_t kTotalBudget = 12000;   // wide chars (~3k tokens) — keeps prefill fast
    const size_t kPerFile     = 10000;
    const int    kMaxFiles    = 5;
    size_t used = 0;
    int included = 0;
    std::set<std::wstring> includedPaths;
    std::wstring refStr, relStr;

    auto emitInto = [&](std::wstring& dst, const std::wstring& rel, const std::wstring& full) {
        std::wstring key = Ai_LowerW(full);
        if (includedPaths.count(key)) return;
        if (used >= kTotalBudget || included >= kMaxFiles) return;
        std::wstring content;
        if (!NeProjects_ReadTextFile(full, content, kPerFile * 4 + 64)) return;
        size_t room = kTotalBudget - used;
        size_t cap = std::min(kPerFile, room);
        if (content.size() > cap) { content.resize(cap); content += L"\r\n... (truncated)"; }
        includedPaths.insert(key);
        dst += L"=== FILE: " + rel + L" ===\r\n" + content + L"\r\n\r\n";
        used += content.size();
        ++included;
    };

    // Emit a preloaded DB knowledge record (no disk read).
    auto emitDoc = [&](std::wstring& dst, const std::wstring& rel, const std::wstring& body) {
        std::wstring key = Ai_LowerW(rel);
        if (includedPaths.count(key)) return;
        if (used >= kTotalBudget || included >= kMaxFiles) return;
        std::wstring content = body;
        size_t room = kTotalBudget - used;
        size_t cap = std::min(kPerFile, room);
        if (content.size() > cap) { content.resize(cap); content += L"\r\n... (truncated)"; }
        includedPaths.insert(key);
        dst += L"=== DB RECORD: " + rel + L" ===\r\n" + content + L"\r\n\r\n";
        used += content.size();
        ++included;
    };

    // Referenced items first (consume budget with priority), rendered last.
    for (const auto& r : requested) {
        if (r.isDoc) emitDoc(refStr, r.rel, r.body);
        else         emitInto(refStr, r.rel, r.full);
    }

    // Regex content search: grep EVERY file for any explicit regex the user gave
    // (regex:… / re:… / /…/) and include the matching lines with context.
    std::wstring regexBlock;
    bool haveRegex = Ai_RegexSearchSnippets(files, Ai_ExtractContentRegexes(userPrompt), regexBlock);


    // Code search: grep the project for symbols/terms in the prompt and include
    // matching snippets (a function can be captured without reading whole files).
    // When snippets are found they replace the broad whole-file relevance dump.
    std::set<std::wstring> refLower;
    for (const auto& r : requested) refLower.insert(Ai_LowerW(r.full));
    std::wstring snippetBlock;
    bool haveSnippets = Ai_SearchSnippets(files, Ai_ExtractSearchTerms(userPrompt),
                                          refLower, snippetBlock);


    if (!haveSnippets && !haveRegex) {
        // Relevant files fill whatever budget remains.
        for (const auto& s : scored) {
            if (s.score <= 0 || included >= kMaxFiles || used >= kTotalBudget) break;
            emitInto(relStr, files[s.idx].relPath, files[s.idx].fullPath);
        }
    }

    if (haveRegex)
        ctx += regexBlock;
    if (haveSnippets)
        ctx += snippetBlock;
    else if (!haveRegex && !relStr.empty())
        ctx += L"Other relevant file contents:\r\n" + relStr;
    if (!refStr.empty())
        ctx += L"Referenced file contents (the exact files you named, read from disk):\r\n" + refStr;

    ctx += L"[END PROJECT CONTEXT]\r\n\r\n";
    return ctx;
}

// Plan for reading project content in batches (map-reduce).
struct AiChunkPlan {
    bool chunked = false;
    std::vector<std::wstring> mapChunks;   // one analysis prompt per content chunk
    std::wstring reduceHeader;             // preamble for the final synthesis prompt
};

// Build a batched plan when the request touches more content than fits the model
// context in one shot: a single big file (20000+ lines), OR a bulk selection
// (directory / glob / regex matching many files), OR DB knowledge records.  Each
// source is split into line-bounded chunks; every chunk becomes one map prompt,
// and the reduce step combines the per-chunk results.  Returns chunked == false
// when everything fits inline (the normal single-call path then handles it).
static AiChunkPlan Ai_PlanBigFileChunks(const std::wstring& userPrompt)
{
    AiChunkPlan plan;
    int64_t pid = NeProjects_GetActiveId();
    if (!pid) return plan;
    NeProject proj;
    if (!NeProjects_GetById(pid, proj) || proj.rootPath.empty()) return plan;

    std::vector<NeProjectFile> files;
    NeProjects_CollectFiles(proj.rootPath, files, kAiMaxProjectFiles);

    // Expand all references (plain/dir/glob/regex) + DB docs into concrete items.
    bool bulk = false;
    std::vector<AiRequestedItem> items = Ai_ExpandFileRefs(proj.rootPath, files, userPrompt, bulk);
    if (items.empty()) return plan;

    // A whole file/record is processed as ONE request (no splitting) as long as it
    // fits the batch context window (num_ctx 32768 ≈ this many wide chars).  Only
    // genuinely huge files are split, into kSplitChars line-bounded pieces.
    const size_t kWholeMax    = 80000;
    const size_t kSplitChars  = 60000;
    const int    kMaxChunks   = 400;     // safety cap across all sources
    const size_t kFileReadCap = 8ull * 1024 * 1024;   // 8 MB head per file

    std::vector<std::wstring> searchTerms = Ai_ExtractSearchTerms(userPrompt);

    // Gather the content of each source (files from disk, docs from memory).
    struct Src { std::wstring rel; std::wstring content; };
    std::vector<Src> sources;
    size_t totalSize = 0;
    for (const auto& it : items) {
        std::wstring content;
        if (it.isDoc) {
            content = it.body;
        } else {
            if (!NeProjects_ReadTextFile(it.full, content, kFileReadCap)) continue;
            // For a targeted (non-bulk) symbol lookup, let the snippet search pull
            // just the relevant lines instead of ingesting the whole file.
            if (!bulk && !searchTerms.empty()) {
                std::wstring low = Ai_LowerW(content);
                bool symbolHere = false;
                for (const auto& t : searchTerms)
                    if (low.find(Ai_LowerW(t)) != std::wstring::npos) { symbolHere = true; break; }
                if (symbolHere) continue;
            }
        }
        if (content.empty()) continue;
        totalSize += content.size();
        sources.push_back({ it.rel, std::move(content) });
    }
    if (sources.empty()) return plan;

    // Slow batch map-reduce is a LAST RESORT: it re-runs the model over every
    // source, so for many normal files it is unusably slow (and pointless for an
    // ordinary question).  Only use it when a SINGLE file is so large it cannot
    // fit one request.  Everything else — even 15 files totalling hundreds of KB —
    // goes through the fast single streamed request (file list + budgeted content),
    // which answers questions in seconds.
    bool anyHuge = false;
    for (const auto& s : sources) if (s.content.size() > kWholeMax) { anyHuge = true; break; }
    if (!anyHuge) return plan;

    for (const auto& sc : sources) {
        // Keep each file/record whole (one request) unless it is too big for the
        // batch context; only then split it into line-bounded pieces.
        std::vector<std::wstring> pieces;
        if (sc.content.size() <= kWholeMax) {
            pieces.push_back(sc.content);
        } else {
            size_t pos = 0;
            while (pos < sc.content.size()) {
                size_t end = std::min(sc.content.size(), pos + kSplitChars);
                if (end < sc.content.size()) {
                    size_t nl = sc.content.rfind(L'\n', end);
                    if (nl != std::wstring::npos && nl > pos + kSplitChars / 2) end = nl + 1;
                }
                pieces.push_back(sc.content.substr(pos, end - pos));
                pos = end;
            }
        }
        int total = (int)pieces.size();
        for (int i = 0; i < total && (int)plan.mapChunks.size() < kMaxChunks; ++i) {
            std::wstring p;
            if (total == 1) {
                p += L"Apply the user's request to the file \"" + sc.rel + L"\" below and "
                     L"report the full result. Preserve formatting. If it is not relevant, "
                     L"reply exactly \"nothing relevant\".\r\n\r\n";
                p += L"User request: " + userPrompt + L"\r\n\r\n";
                p += L"=== FILE: " + sc.rel + L" ===\r\n";
            } else {
                p += L"You are reading a large file in parts. This is part "
                   + std::to_wstring(i + 1) + L" of " + std::to_wstring(total)
                   + L" of \"" + sc.rel + L"\".\r\n";
                p += L"Apply the user's request to THIS part only and report the result "
                     L"for this part. Preserve formatting. If nothing here is relevant, "
                     L"reply exactly \"nothing relevant\".\r\n\r\n";
                p += L"User request: " + userPrompt + L"\r\n\r\n";
                p += L"=== PART " + std::to_wstring(i + 1) + L"/" + std::to_wstring(total)
                   + L" OF " + sc.rel + L" ===\r\n";
            }
            p += pieces[i];
            plan.mapChunks.push_back(std::move(p));
        }
        if ((int)plan.mapChunks.size() >= kMaxChunks) break;
    }
    if (plan.mapChunks.empty()) return plan;

    plan.chunked = true;
    plan.reduceHeader  = L"[PROJECT CONTEXT \u2014 SUGGESTION MODE]\r\n";
    plan.reduceHeader += L"Active project: " + proj.name + L"\r\n";
    plan.reduceHeader += L"Project root: " + proj.rootPath + L"\r\n\r\n";
    plan.reduceHeader += L"The project was read in batches (files and/or DB records). Below are "
                         L"the per-part results. Combine them into a single, complete answer to "
                         L"the user's request, preserving formatting. When you propose code, name "
                         L"the exact file and where in it the developer should place the change.\r\n\r\n";
    return plan;
}

static void Ai_DoSend(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;

    HWND hInput = GetDlgItem(hwnd, IDC_AI_INPUT);
    std::wstring prompt;
    if (hInput) {
        int inLen = GetWindowTextLengthW(hInput);
        if (inLen > 0) {
            // RichEdit reports the length with single-CR line breaks but emits
            // CRLF from GetWindowText, so size the buffer for that expansion
            // (worst case doubles) to avoid truncating multi-line prompts.
            std::wstring buf((size_t)inLen * 2 + 2, L'\0');
            int copied = GetWindowTextW(hInput, &buf[0], (int)buf.size());
            buf.resize((size_t)copied);
            prompt = std::move(buf);
        }
    }
    if (prompt.empty() && st->pendingImages.empty()) return;

    st->lastPrompt = prompt;
    st->lastImages = st->pendingImages;
    st->pendingImages.clear();
    st->sendCanceled = false;
    NeAiClient_ResetCancel();

    Ai_HistoryRemember(st, prompt);
    Ai_SavePrefs(st);

    Ai_StopThinkingTimer(hwnd);
    Ai_StartThinkingTimer(hwnd);

    // Re-render the accumulated conversation so previous answers stay visible;
    // the new prompt echo + streamed reply are appended below and become part of
    // the conversation once the answer completes (Ai_CommitLiveReply).
    Ai_RenderConversation(hwnd);

    if (hInput) SetWindowTextW(hInput, L"");

    std::wstring userLine = Ne_Ls(L"AI_LOG_USER_PREFIX");
    userLine += prompt;
    st->answerIntroText = userLine;
    Ai_AppendLog(hwnd, userLine);
    if (st->cloudMode) {
        if (!st->signedIn) {
            Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_CLOUD_NEEDS_SIGNIN"));
            Ai_OpenOllamaSignUp(hwnd);
            Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_OPENED_SIGNUP"));
            return;
        }
        Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_CLOUD_SENDING"));
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
        Ai_AppendRichRun(hLog, L"\r\n", nullptr);
        for (const std::string& b64 : st->lastImages) {
            std::string png = Ai_Base64Decode(b64);
            if (!png.empty()) {
                Ai_AppendInlinePng(hLog, png);
                Ai_AppendRichRun(hLog, L"\r\n", nullptr);
            }
        }
        Ai_AppendRichRun(hLog, L"Ollama:\r\n", nullptr);
    }

    std::wstring selectedModel;
    std::wstring fallbackModel;
    if (st->cloudMode) {
        // Cloud model is served through the signed-in local Ollama daemon.  No
        // silent fallback to a local model — if the cloud model is unavailable
        // the error is surfaced instead of quietly answering from a small local one.
        selectedModel = st->cloudModel.empty() ? Ai_DefaultCloudModelName() : st->cloudModel;
        fallbackModel.clear();
    } else {
        selectedModel = st->model.empty() ? Ai_DefaultModelName() : st->model;
        fallbackModel = st->fallback.empty() ? Ai_FallbackModelName() : st->fallback;
        Ai_NormalizeModelNameLocal(selectedModel);
        Ai_NormalizeModelNameLocal(fallbackModel);
    }
    // Decide between the normal single call and a chunked map-reduce over a large
    // named file.  Both built here on the UI thread (SQLite handle is single-threaded).
    AiChunkPlan chunkPlan = Ai_PlanBigFileChunks(prompt);
    st->chunkedActive = chunkPlan.chunked;
    std::wstring promptForModel;
    if (chunkPlan.chunked) {
        promptForModel = prompt;   // raw question; file context via batches
    } else {
        std::wstring projectCtx = Ai_BuildProjectContext(prompt);
        promptForModel = projectCtx.empty() ? prompt : (projectCtx + prompt);
    }

    if (hLog) {
        st->replyBaseStart = GetWindowTextLengthW(hLog);
        st->historyDraft = L"";
        st->liveReply.clear();
        st->liveRawReply.clear();
        st->liveTypingQueue.clear();
        st->liveTypingStartReady = true;
        st->liveTypingStartTimerRunning = false;
        st->liveTypingDone = false;
        st->liveTypingFinalRendered = false;
        Ai_WriteRawReplyFile(L"");
        Ai_StopLiveTypingTimer(hwnd);
        Ai_StopLiveTypingStartDelayTimer(hwnd);
        AiSendWorkItem work;
        work.answerStart = st->replyBaseStart;
        Ai_StartSendWorker(hwnd, promptForModel, selectedModel, fallbackModel, work.answerStart,
                           chunkPlan.mapChunks, chunkPlan.reduceHeader, st->lastImages);
    } else {
        st->replyBaseStart = 0;
        st->liveReply.clear();
        st->liveRawReply.clear();
        st->liveTypingQueue.clear();
        st->liveTypingStartReady = true;
        st->liveTypingStartTimerRunning = false;
        st->liveTypingDone = false;
        st->liveTypingFinalRendered = false;
        Ai_WriteRawReplyFile(L"");
        Ai_StopLiveTypingTimer(hwnd);
        Ai_StopLiveTypingStartDelayTimer(hwnd);
        Ai_StartSendWorker(hwnd, promptForModel, selectedModel, fallbackModel, 0,
                           chunkPlan.mapChunks, chunkPlan.reduceHeader, st->lastImages);
    }
}

// Stop an in-progress request so the developer can add more information and
// resend.  Aborts the streaming transfer (cooperative cancel in the curl write
// callback), stops the busy UI, and restores the prompt text for editing.  The
// worker thread still posts WM_AI_SEND_COMPLETE later; sendCanceled makes that
// handler discard the partial result.
static void Ai_StopSend(HWND hwnd)
{
    AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    if (!st->hStopBtn || !IsWindowEnabled(st->hStopBtn)) return;   // nothing running

    st->sendCanceled = true;
    NeAiClient_RequestCancel();

    Ai_StopThinkingTimer(hwnd);
    Ai_StopLiveTypingTimer(hwnd);
    Ai_StopLiveTypingStartDelayTimer(hwnd);
    Ai_EndBusyState(hwnd);
    Ai_SetThinkingStatusText(hwnd);

    Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_STOPPED"));

    // Put the prompt back so the developer can extend it and send again.
    HWND hInput = GetDlgItem(hwnd, IDC_AI_INPUT);
    if (hInput && !st->lastPrompt.empty()) {
        SetWindowTextW(hInput, st->lastPrompt.c_str());
        int len = GetWindowTextLengthW(hInput);
        SendMessageW(hInput, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SetFocus(hInput);
    }
}

// ── Window geometry persistence (AI window) ───────────────────────────────────
// Saved to the settings table on every size/move change so the AI window reopens
// exactly as it was left (normal/maximized).  Minimized is never persisted.
static bool s_aiClosing = false;   // teardown guard: block late WM_SIZE overwrite
static bool s_aiGeometryReady = false;  // false until restore is done, so the creation
                                        // WM_SIZE can't clobber the saved geometry
static void Ai_SaveWinPlacement(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    if (IsIconic(hwnd)) return;
    WINDOWPLACEMENT wp = {}; wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp)) return;
    bool maximized = (IsZoomed(hwnd) != FALSE) ||
                     (wp.showCmd == SW_SHOWMAXIMIZED) || (wp.flags & WPF_RESTORETOMAXIMIZED);
    RECT r = wp.rcNormalPosition;
    NeProfiles_SetIntSetting("ai_win_valid", 1);
    NeProfiles_SetIntSetting("ai_win_max",   maximized ? 1 : 0);
    NeProfiles_SetIntSetting("ai_win_x",     r.left);
    NeProfiles_SetIntSetting("ai_win_y",     r.top);
    NeProfiles_SetIntSetting("ai_win_w",     r.right - r.left);
    NeProfiles_SetIntSetting("ai_win_h",     r.bottom - r.top);
}

static bool Ai_RestoreWinPlacement(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return false;
    int valid = 0;
    if (!NeProfiles_GetIntSetting("ai_win_valid", 0, valid) || !valid) return false;
    int x = 0, y = 0, w = 0, h = 0, mx = 0;
    NeProfiles_GetIntSetting("ai_win_x",   0, x);
    NeProfiles_GetIntSetting("ai_win_y",   0, y);
    NeProfiles_GetIntSetting("ai_win_w",   0, w);
    NeProfiles_GetIntSetting("ai_win_h",   0, h);
    NeProfiles_GetIntSetting("ai_win_max", 0, mx);
    if (w < 200 || h < 150) return false;
    RECT rc = { x, y, x + w, y + h };
    if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) return false;
    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ShowWindow(hwnd, mx ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    return true;
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
            HWND hLog = Ai_CreateAnswerRichEdit(hAnswerHost, S(8), S(8), S(800), S(300));
            if (hLog && st->hPaneFont) SendMessageW(hLog, WM_SETFONT, (WPARAM)st->hPaneFont, TRUE);
            if (hLog) {
                SetWindowSubclass(hLog, Ai_AnswerChildSubclassProc, 1, 0);
                SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
                Ai_SetWrapToWindow(hLog);
                st->hLogSb = msb_attach(hLog, MSB_VERTICAL);
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
        // Allow very large prompts (whole files pasted for translation etc.).
        // RichEdit defaults to ~32K characters; raise it to the full 32-bit range
        // (4294967295 chars) so even huge pastes are never truncated.
        if (hInput) {
            SendMessageW(hInput, EM_EXLIMITTEXT, 0, (LPARAM)0xFFFFFFFFu);
            SendMessageW(hInput, EM_LIMITTEXT, (WPARAM)0xFFFFFFFFu, 0);
        }
        if (hInput) st->hInputSb = msb_attach(hInput, MSB_VERTICAL);
        if (hInput) SetWindowSubclass(hInput, Ai_InputSubclassProc, 1, (DWORD_PTR)hwnd);

        int totalBtnW = 0;
        for (int i = 0; i < st->dd->buttonCount; ++i) {
            totalBtnW += st->dd->buttons[i].width;
            if (i > 0) totalBtnW += S(10);
        }
        int btnY = S(438);
        int btnX = S(10) + S(820) - totalBtnW;
        HWND btnHwnds[7] = {};
        int bx = btnX;
        for (int i = 0; i < st->dd->buttonCount; ++i) {
            const AiButtonSpec& b = st->dd->buttons[i];
            btnHwnds[i] = CreateWindowExW(0, L"BUTTON", b.text.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                bx, btnY, b.width, S(34), hwnd, (HMENU)(UINT_PTR)b.id, GetModuleHandleW(NULL), NULL);
            if (btnHwnds[i] && st->hFont) SendMessageW(btnHwnds[i], WM_SETFONT, (WPARAM)st->hFont, TRUE);
            bx += b.width + S(10);
        }
        HWND hSend  = btnHwnds[0];
        HWND hStop  = btnHwnds[1];
        HWND hCopy  = btnHwnds[2];
        HWND hClear = btnHwnds[5];
        HWND hClose = btnHwnds[6];

        HWND hStatus = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
            S(10), S(470), S(820), S(22), hwnd, (HMENU)(UINT_PTR)IDC_AI_STATUS, GetModuleHandleW(NULL), NULL);
        if (hStatus && st->hFont) SendMessageW(hStatus, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        HWND hStatusTimer = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            S(10), S(470), S(240), S(22), hwnd, (HMENU)(UINT_PTR)IDC_AI_STATUS_TIMER, GetModuleHandleW(NULL), NULL);
        if (hStatusTimer && st->hFont) SendMessageW(hStatusTimer, WM_SETFONT, (WPARAM)st->hFont, TRUE);

        st->hHeader = hHdr;
        st->hInput = hInput;
        st->hStatus = hStatus;
        st->hStatusTimer = hStatusTimer;
        st->hSendBtn = hSend;
        st->hStopBtn = hStop;
        st->hCopyBtn = hCopy;
        st->hClearBtn = hClear;
        st->hCloseBtn = hClose;
        if (st->hStopBtn) EnableWindow(st->hStopBtn, FALSE);

        Ai_RefreshUi(hwnd);
        Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_WINDOW_OPENED"));
        // Load any persisted conversation history and render it. Accumulated
        // answers survive across restarts until the user presses Clear.
        {
            std::vector<NeAiAnswerRow> rows;
            if (NeProfiles_AiAnswersLoad(rows) && !rows.empty()) {
                st->conversation.reserve(rows.size());
                for (auto& r : rows) {
                    AiConversationTurn turn;
                    turn.prompt  = r.prompt;
                    turn.replyMd = r.replyMd;
                    turn.images  = r.images;
                    st->conversation.push_back(std::move(turn));
                }
                Ai_RenderConversation(hwnd);
            }
        }
        // Refresh the cloud model list from ollama.com in the background; when it
        // arrives (WM_AI_CLOUDMODELS_READY) the menu is rebuilt so retired models
        // drop off and newly-released ones appear.
        Ai_RefreshCloudModelsAsync(hwnd);
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
        HWND hStatus = GetDlgItem(hwnd, IDC_AI_STATUS);
        HWND hStatusTimer = st->hStatusTimer ? st->hStatusTimer : GetDlgItem(hwnd, IDC_AI_STATUS_TIMER);
        int contentTop = pad + hdrH + gap;
        int contentBottom = std::max(contentTop, (int)rc.bottom - pad - statusH - gap);
        int contentH = std::max(0, contentBottom - contentTop);
        int availableForEditors = std::max(0, contentH - buttonH - gap);
        int inputH = std::max(S(96), availableForEditors / 3);
        int logH = std::max(0, availableForEditors - inputH);
        int inputW = std::max(0, (int)rc.right - 2 * pad);
        int logY = contentTop + inputH + gap + buttonH + gap;
        int btnY = contentTop + inputH + gap;
        int totalBtnW = 0;
        for (int i = 0; i < st->dd->buttonCount; ++i) {
            totalBtnW += st->dd->buttons[i].width;
            if (i > 0) totalBtnW += S(10);
        }
        int btnX = pad + std::max(0, (inputW - totalBtnW) / 2);
        if (hHdr) SetWindowPos(hHdr, NULL, pad, pad, rc.right - 2 * pad, hdrH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hInput) SetWindowPos(hInput, NULL, pad, contentTop, inputW, inputH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hInput) Ai_SetWrapToWindow(hInput);
        if (st->hInputSb) msb_reposition(st->hInputSb);
        {
            int bx = btnX;
            for (int i = 0; i < st->dd->buttonCount; ++i) {
                HWND hBtn = GetDlgItem(hwnd, st->dd->buttons[i].id);
                if (hBtn) SetWindowPos(hBtn, NULL, bx, btnY, st->dd->buttons[i].width, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
                bx += st->dd->buttons[i].width + S(10);
            }
        }
        if (hLog) SetWindowPos(hLog, NULL, pad, logY, rc.right - 2 * pad, logH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (st->hLog && IsWindow(st->hLog) && st->hAnswerHost) {
            // Fill the host client area so the custom MSB scrollbar sits flush
            // against the host's right (client-edge) border.
            RECT hrc = {};
            GetClientRect(st->hAnswerHost, &hrc);
            SetWindowPos(st->hLog, NULL, 0, 0, std::max(1, (int)hrc.right), std::max(1, (int)hrc.bottom), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (st->hLogSb) msb_reposition(st->hLogSb);
        if (hStatus) SetWindowPos(hStatus, NULL, pad, rc.bottom - pad - statusH, std::max(0, (int)rc.right - 2 * pad - S(250)), statusH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (hStatusTimer) SetWindowPos(hStatusTimer, NULL, std::max(pad, (int)rc.right - pad - S(240)), rc.bottom - pad - statusH, S(240), statusH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)
            if (s_aiGeometryReady && !s_aiClosing) Ai_SaveWinPlacement(hwnd);
        return 0;
    }
    case WM_EXITSIZEMOVE:
        if (s_aiGeometryReady && !s_aiClosing) Ai_SaveWinPlacement(hwnd);
        return 0;
    case WM_MENUSELECT: {
        // Hover help for the "Add API key" menu item, using a tooltip OWNED by
        // the AI window so it never steals activation to the editor.
        UINT item = LOWORD(wParam);
        UINT flags = HIWORD(wParam);
        if (flags != 0xFFFF && !(flags & MF_POPUP) && item == IDM_AI_SET_API_KEY) {
            std::wstring msg = Ne_Ls(L"AI_TIP_ADD_API_KEY");
            size_t p;
            while ((p = msg.find(L"\\n")) != std::wstring::npos) msg.replace(p, 2, L"\n");
            Ai_ShowMenuTip(hwnd, msg);
        } else {
            Ai_HideMenuTip();
        }
        return 0;
    }
    case WM_EXITMENULOOP:
        Ai_HideMenuTip();
        break;
    case WM_COMMAND:
        if (st && HIWORD(wParam) == EN_CHANGE) {
            if (LOWORD(wParam) == IDC_AI_INPUT && st->hInputSb) {
                // Suppress the reflow while a paste is inserting text: it would
                // re-enter the RichEdit (EM_SETRECT) mid-insert and crash. The
                // paste handler reflows once after the insert finishes.
                if (s_aiInputPasting) return 0;
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
                if (modelItem->isCloud) {
                    st->cloudModel = modelItem->model;
                } else {
                    st->model = modelItem->model;
                    Ai_NormalizeModelNameLocal(st->model);
                }
                Ai_AppendLog(hwnd, modelItem->isCloud
                    ? (Ne_Ls(L"AI_LOG_CLOUD_MODEL_SELECTED") + (std::wstring(L" ") + modelItem->model))
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
        case IDM_AI_SET_API_KEY:
            Ai_PromptAndSetApiKey(hwnd, st);
            return 0;
        case IDM_AI_SIGN_IN:
            if (st) {
                if (st->cloudApiKey.empty()) {
                    // Browser-free path: ask for an API key and validate it.
                    Ai_PromptAndSetApiKey(hwnd, st);
                } else {
                    bool valid = NeAiClient_ValidateCloudApiKey(st->cloudApiKey);
                    st->signedIn = valid;
                    if (valid) st->cloudMode = true;
                    Ai_SavePrefs(st);
                    Ai_RefreshUi(hwnd);
                    Ai_AppendLog(hwnd, valid ? Ne_Ls(L"AI_LOG_API_KEY_OK") : Ne_Ls(L"AI_LOG_API_KEY_BAD"));
                }
            }
            return 0;
        case IDM_AI_SIGN_OUT:
            if (st) {
                // Forget the API key (local only) and drop any daemon session,
                // then fall back to a local model.
                st->cloudApiKey.clear();
                NeAiClient_SetCloudApiKey(L"");
                NeAiClient_OllamaSignout();
                st->signedIn = false;
                st->cloudMode = false;
                std::vector<std::wstring> locals = Ai_GetModelCandidates();
                bool haveLast = false;
                if (!st->model.empty()) {
                    if (locals.empty()) haveLast = true;
                    else for (const std::wstring& m : locals) { if (m == st->model) { haveLast = true; break; } }
                }
                if (!haveLast) st->model = locals.empty() ? Ai_DefaultModelName() : locals.front();
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
                // Confirm before wiping the conversation — but only when there is
                // actually something to clear.
                bool hasContent = !st->conversation.empty() ||
                    (st->hLog && IsWindow(st->hLog) && GetWindowTextLengthW(st->hLog) > 0);
                if (hasContent &&
                    !Ne_ShowConfirmDialog(hwnd, Ne_Ls(L"AI_CLEAR_CONFIRM_TITLE"),
                                          Ne_Ls(L"AI_CLEAR_CONFIRM_MSG"))) {
                    return 0;
                }
                st->conversation.clear();
                st->turnAnchors.clear();
                st->navTurn = -1;
                st->pendingImages.clear();
                NeProfiles_AiAnswersClear();
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
        case IDC_AI_STOP_BTN:
            Ai_StopSend(hwnd);
            return 0;
        case IDC_AI_COPY_BTN:
            SendMessageW(hwnd, WM_COMMAND, IDM_AI_LOG_COPY, 0);
            return 0;
        case IDC_AI_PREV_BTN:
            if (st && !st->turnAnchors.empty()) {
                int idx = (st->navTurn < 0) ? (int)st->turnAnchors.size() - 1 : st->navTurn - 1;
                if (idx < 0) idx = 0;
                Ai_ScrollToTurn(hwnd, idx);
            }
            return 0;
        case IDC_AI_NEXT_BTN:
            if (st && !st->turnAnchors.empty()) {
                int idx = (st->navTurn < 0) ? 0 : st->navTurn + 1;
                if (idx > (int)st->turnAnchors.size() - 1) idx = (int)st->turnAnchors.size() - 1;
                Ai_ScrollToTurn(hwnd, idx);
            }
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
        Ai_SaveWinPlacement(hwnd);   // capture final geometry (incl. maximized)
        s_aiClosing = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_SETFOCUS:
        Ai_RefreshUi(hwnd);
        return 0;
    case WM_MOUSEWHEEL: {
        if (st && st->hAnswerHost && IsWindow(st->hAnswerHost)) {
            POINT pt = { (LONG)(short)LOWORD(lParam), (LONG)(short)HIWORD(lParam) };
            HWND hUnder = WindowFromPoint(pt);
            if (hUnder && (hUnder == st->hAnswerHost || IsChild(st->hAnswerHost, hUnder))) {
                SendMessageW(st->hAnswerHost, WM_MOUSEWHEEL, wParam, lParam);
                return 0;
            }
        }
        break;
    }
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
            Ai_UpdateSpinnerProgress(hwnd);
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
    case WM_AI_PROGRESS: {
        auto* text = (std::wstring*)lParam;
        if (text) {
            if (st && st->spinner) st->spinner->SetText(*text);
            delete text;
        }
        return 0;
    }
    case WM_AI_CLOUDMODELS_READY: {
        // Live cloud model list arrived from ollama.com. Adopt it and rebuild the
        // menu if it changed so retired models disappear and new ones show up.
        auto* list = (std::vector<std::wstring>*)lParam;
        if (list) {
            if (*list != s_cloudModelsLive) {
                s_cloudModelsLive = std::move(*list);
                if (st) Ai_RefreshUi(hwnd);   // rebuilds the menu from the new list
            }
            delete list;
        }
        return 0;
    }
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
                st->liveRawReply += liveText;
                Ai_WriteRawReplyFile(st->liveRawReply);
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
        auto* result = (AiSendResult*)lParam;
        if (st && st->sendCanceled) {
            // The user pressed Stop (or Esc): discard whatever the worker
            // produced and leave the restored prompt untouched.
            st->sendCanceled = false;
            delete result;
            return 0;
        }
        Ai_EndBusyState(hwnd);
        if (result) {
            if (st) {
                st->liveTypingDone = true;
            }
            Ai_StopThinkingTimer(hwnd);
            Ai_SetThinkingStatusText(hwnd);
            if (result->usedFallback) {
                Ai_AppendLog(hwnd, Ne_Ls(L"AI_LOG_RETRY_FALLBACK"));
            }
            if (result->ok) {
                // Use the worker's complete reply as the authoritative text.
                // The live-typing queue may not be fully pumped when this
                // completion arrives (fast cloud responses especially), so
                // st->liveReply can be partial — rendering it would cut the
                // answer mid-line and the timer would then append the tail as
                // raw text below the code cell.  Adopt the full reply, clear the
                // pending queue and stop the typing timer before the final render.
                if (st) {
                    if (!result->reply.empty()) st->liveReply = result->reply;
                    st->liveTypingQueue.clear();
                    Ai_StopLiveTypingTimer(hwnd);
                }
                if (st && !st->liveReply.empty() && !st->liveTypingFinalRendered) {
                    Ai_CommitLiveReply(hwnd);
                    st->liveTypingFinalRendered = true;
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
        Ai_DestroyMenuTip();
        if (st) {
            if (st->hLogSb) msb_detach(st->hLogSb);
            if (st->hInputSb) msb_detach(st->hInputSb);
            if (st->spinner) { st->spinner->Hide(); delete st->spinner; st->spinner = NULL; }
            if (st->hFont) DeleteObject(st->hFont);
            if (st->hPaneFont) DeleteObject(st->hPaneFont);
            if (s_hAiMenuFont) { DeleteObject(s_hAiMenuFont); s_hAiMenuFont = NULL; }
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
        AiWindowState* st = (AiWindowState*)GetWindowLongPtrW(s_hwndAiWindow, GWLP_USERDATA);
        if (st) Ai_ReconcileSignInState(st);   // pick up terminal signin/signout
        Ai_RefreshUi(s_hwndAiWindow);
        Ai_RefreshCloudModelsAsync(s_hwndAiWindow); // refresh cloud model list on reopen
        ShowWindow(s_hwndAiWindow, SW_SHOW);
        SetForegroundWindow(s_hwndAiWindow);
        return;
    }

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = Ai_WndProc;
    wc.hInstance = hi;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));   // app icon (NSBEdit.ico) for taskbar
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
    s_aiClosing = false;   // fresh window — allow geometry saves again
    s_aiGeometryReady = false;  // block clobber until restore has run

    // Independent top-level window (NO owner) + WS_EX_APPWINDOW so it gets its own
    // taskbar button and Alt+Tab entry — the AI window and editor act as two apps.
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_APPWINDOW,
        wc.lpszClassName, Ne_Ls(L"AI_WINDOW_TITLE_PREFIX"),
        WS_OVERLAPPEDWINDOW,
        x, y, W, H, NULL, NULL, hi, st);
    if (!hwnd) {
        delete st;
        return;
    }

    if (HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(1))) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIco);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
    }

    s_hwndAiWindow = hwnd;
    // Restore the last saved geometry (size/position/maximized); else show normally.
    if (!Ai_RestoreWinPlacement(hwnd))
        ShowWindow(hwnd, SW_SHOW);
    s_aiGeometryReady = true;   // from now on, size/move changes are persisted
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
}
