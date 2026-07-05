#include "copy_ai_code.h"

#include "tooltip/tooltip.h"

#include <commctrl.h>
#include <richedit.h>

#include <map>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

namespace {

constexpr UINT_PTR kCopiedTooltipTimerId = 0xA1C0;
constexpr UINT kCopiedTooltipHideMs = 500;

struct AiCodeBlockCopyInfo {
    int headerStartChar = -1;
    int headerEndChar = -1;
    int codeStartChar = -1;
    int codeEndChar = -1;
    std::wstring headerText;
};

struct AiCopyCodeState {
    std::vector<AiCodeBlockCopyInfo> blocks;
};

static std::map<HWND, AiCopyCodeState> s_states;

static int AiCopyCode_Scale(HWND hwnd, int value)
{
    UINT dpi = 96;
    if (hwnd) {
        dpi = GetDpiForWindow(hwnd);
    }
    return MulDiv(value, (int)dpi, 96);
}

static RECT AiCopyCode_MeasureLabelRect(HWND hwndLog, int startChar, const std::wstring& text)
{
    RECT rc = { 0, 0, 0, 0 };
    if (!hwndLog || startChar < 0 || text.empty()) return rc;

    POINT pt = {};
    SendMessageW(hwndLog, EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)startChar);

    HDC hdc = GetDC(hwndLog);
    if (!hdc) {
        rc.left = pt.x;
        rc.top = pt.y;
        return rc;
    }

    HFONT hFont = (HFONT)SendMessageW(hwndLog, WM_GETFONT, 0, 0);
    HFONT oldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
    SIZE size = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size);
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(hwndLog, hdc);

    rc.left = pt.x;
    rc.top = pt.y;
    rc.right = pt.x + size.cx;
    rc.bottom = pt.y + tm.tmHeight + tm.tmExternalLeading;
    return rc;
}

static AiCopyCodeState& AiCopyCode_GetState(HWND hwndLog)
{
    return s_states[hwndLog];
}

static void AiCopyCode_HideCopiedTooltip(HWND hwndLog)
{
    KillTimer(hwndLog, kCopiedTooltipTimerId);
    HideTooltip();
}

static void AiCopyCode_ShowCopiedTooltip(HWND hwndLog, POINT ptClient)
{
    if (!hwndLog) return;

    AiCopyCode_HideCopiedTooltip(hwndLog);

    POINT ptScreen = ptClient;
    ClientToScreen(hwndLog, &ptScreen);

    std::vector<TooltipEntry> entries = { { L"", L"Copied" } };
    int gap = AiCopyCode_Scale(hwndLog, 12);
    ShowMultilingualTooltip(entries, ptScreen.x + gap, ptScreen.y + gap, GetParent(hwndLog));
    SetTimer(hwndLog, kCopiedTooltipTimerId, kCopiedTooltipHideMs, NULL);
}

static bool AiCopyCode_BlockHitTest(HWND hwndLog, const AiCodeBlockCopyInfo& block, POINT ptClient)
{
    if (!hwndLog || block.headerStartChar < 0 || block.headerText.empty()) return false;

    RECT headerRect = AiCopyCode_MeasureLabelRect(hwndLog, block.headerStartChar, block.headerText);
    return headerRect.right > headerRect.left && headerRect.bottom > headerRect.top &&
        ptClient.x >= headerRect.left && ptClient.x <= headerRect.right &&
        ptClient.y >= headerRect.top && ptClient.y <= headerRect.bottom;
}

} // namespace

void Ai_CopyTextToClipboard(HWND hwnd, const std::wstring& text)
{
    if (text.empty()) return;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        wchar_t* dst = (wchar_t*)GlobalLock(hMem);
        if (dst) {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

void AiCopyCode_Clear(HWND hwndLog)
{
    if (!hwndLog) return;
    auto it = s_states.find(hwndLog);
    if (it != s_states.end()) {
        it->second.blocks.clear();
    }
    AiCopyCode_HideCopiedTooltip(hwndLog);
}

void AiCopyCode_BeginBlock(HWND hwndLog, int headerStartChar, const std::wstring& headerText, int codeStartChar)
{
    if (!hwndLog) return;

    AiCopyCodeState& state = AiCopyCode_GetState(hwndLog);
    AiCodeBlockCopyInfo block;
    block.headerStartChar = headerStartChar;
    block.headerEndChar = headerStartChar + (int)headerText.size();
    block.codeStartChar = codeStartChar;
    block.codeEndChar = codeStartChar;
    block.headerText = headerText;
    state.blocks.push_back(std::move(block));
}

void AiCopyCode_AppendCodeLine(HWND hwndLog, const std::wstring& line)
{
    if (!hwndLog || line.empty()) return;

    auto it = s_states.find(hwndLog);
    if (it == s_states.end() || it->second.blocks.empty()) return;
    it->second.blocks.back().codeEndChar += (int)line.size();
}

void AiCopyCode_EndBlock(HWND hwndLog, int codeEndChar)
{
    if (!hwndLog) return;

    auto it = s_states.find(hwndLog);
    if (it == s_states.end() || it->second.blocks.empty()) return;

    it->second.blocks.back().codeEndChar = codeEndChar;
}

bool AiCopyCode_GetCodeRangeAt(HWND hwndLog, int charIndex, int* outStart, int* outEnd)
{
    auto it = s_states.find(hwndLog);
    if (it == s_states.end()) return false;

    for (const AiCodeBlockCopyInfo& block : it->second.blocks) {
        if (block.codeStartChar >= 0 && block.codeEndChar > block.codeStartChar &&
            charIndex >= block.codeStartChar && charIndex <= block.codeEndChar) {
            if (outStart) *outStart = block.codeStartChar;
            if (outEnd) *outEnd = block.codeEndChar;
            return true;
        }
    }
    return false;
}

bool AiCopyCode_IsHot(HWND hwndLog, POINT ptClient)
{
    auto it = s_states.find(hwndLog);
    if (it == s_states.end()) return false;

    for (const AiCodeBlockCopyInfo& block : it->second.blocks) {
        if (AiCopyCode_BlockHitTest(hwndLog, block, ptClient)) {
            return true;
        }
    }
    return false;
}

bool AiCopyCode_HandleClick(HWND hwndLog, POINT ptClient)
{
    auto it = s_states.find(hwndLog);
    if (it == s_states.end()) return false;

    for (const AiCodeBlockCopyInfo& block : it->second.blocks) {
        if (AiCopyCode_BlockHitTest(hwndLog, block, ptClient)) {
            if (block.codeStartChar >= 0 && block.codeEndChar > block.codeStartChar) {
                std::wstring text((size_t)(block.codeEndChar - block.codeStartChar) + 1, L'\0');
                TEXTRANGEW tr = {};
                tr.chrg.cpMin = block.codeStartChar;
                tr.chrg.cpMax = block.codeEndChar;
                tr.lpstrText = text.data();
                LRESULT copied = SendMessageW(hwndLog, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
                if (copied > 0) {
                    text.resize((size_t)copied);
                    Ai_CopyTextToClipboard(hwndLog, text);
                }
            }
            AiCopyCode_ShowCopiedTooltip(hwndLog, ptClient);
            return true;
        }
    }
    return false;
}

void AiCopyCode_HandleTimer(HWND hwndLog, UINT_PTR timerId)
{
    if (!hwndLog || timerId != kCopiedTooltipTimerId) return;
    AiCopyCode_HideCopiedTooltip(hwndLog);
}

void AiCopyCode_HandleDestroy(HWND hwndLog)
{
    if (!hwndLog) return;
    AiCopyCode_HideCopiedTooltip(hwndLog);
    s_states.erase(hwndLog);
}
