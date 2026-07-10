#include "ollama_ai_copycode.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct AiCodeCopyBlock {
    int headerStartChar = -1;
    int headerEndChar = -1;
    std::wstring headerText;
    std::wstring code;
    bool open = false;
};

static std::map<HWND, std::vector<AiCodeCopyBlock>> s_codeBlocks;

static void Ai_CopyTextToClipboard(HWND hwnd, const std::wstring& text)
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

static RECT Ai_MeasureTextRect(HWND hLog, int startChar, const std::wstring& text)
{
    RECT rc = { 0, 0, 0, 0 };
    if (!hLog || startChar < 0 || text.empty()) return rc;

    LRESULT pos = SendMessageW(hLog, EM_POSFROMCHAR, (WPARAM)startChar, 0);
    POINT topLeft = { (LONG)(short)LOWORD(pos), (LONG)(short)HIWORD(pos) };

    HFONT hFont = (HFONT)SendMessageW(hLog, WM_GETFONT, 0, 0);
    HDC hdc = GetDC(hLog);
    if (!hdc) {
        rc.left = topLeft.x;
        rc.top = topLeft.y;
        return rc;
    }

    HFONT oldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;
    SIZE size = {};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &size);
    TEXTMETRICW tm = {};
    GetTextMetricsW(hdc, &tm);
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(hLog, hdc);

    int textHeight = tm.tmHeight + tm.tmExternalLeading;
    int lowerLeftY = topLeft.y + textHeight;

    rc.left = topLeft.x;
    rc.right = topLeft.x + size.cx;
    rc.bottom = lowerLeftY;
    rc.top = lowerLeftY - textHeight;
    InflateRect(&rc, 2, 1);
    return rc;
}

} // namespace

void AiCodeCopy_Clear(HWND hwndLog)
{
    if (!hwndLog) return;
    s_codeBlocks[hwndLog].clear();
}

void AiCodeCopy_BeginBlock(HWND hwndLog, int headerStartChar, int headerEndChar, const std::wstring& headerText)
{
    if (!hwndLog) return;
    AiCodeCopyBlock block;
    block.headerStartChar = headerStartChar;
    block.headerEndChar = headerEndChar;
    block.headerText = headerText;
    block.open = true;
    s_codeBlocks[hwndLog].push_back(std::move(block));
}

void AiCodeCopy_AppendCodeLine(HWND hwndLog, const std::wstring& line)
{
    if (!hwndLog || line.empty()) return;
    auto it = s_codeBlocks.find(hwndLog);
    if (it == s_codeBlocks.end() || it->second.empty()) return;
    AiCodeCopyBlock& block = it->second.back();
    if (!block.open) return;
    block.code += line;
}

void AiCodeCopy_EndBlock(HWND hwndLog)
{
    if (!hwndLog) return;
    auto it = s_codeBlocks.find(hwndLog);
    if (it == s_codeBlocks.end() || it->second.empty()) return;
    it->second.back().open = false;
}

bool AiCodeCopy_HandleClick(HWND hwndLog, POINT pt)
{
    auto it = s_codeBlocks.find(hwndLog);
    if (it == s_codeBlocks.end()) return false;

    for (const AiCodeCopyBlock& block : it->second) {
        if (block.headerStartChar >= 0 && block.headerEndChar > block.headerStartChar) {
            long clickPos = (long)SendMessageW(hwndLog, EM_CHARFROMPOS, 0, MAKELPARAM(pt.x, pt.y));
            if (clickPos >= block.headerStartChar && clickPos < block.headerEndChar) {
                Ai_CopyTextToClipboard(hwndLog, block.code);
                return true;
            }
        }

        RECT headerRect = Ai_MeasureTextRect(hwndLog, block.headerStartChar, block.headerText);
        bool inRect = headerRect.right > headerRect.left && headerRect.bottom > headerRect.top &&
            pt.x >= headerRect.left && pt.x <= headerRect.right &&
            pt.y >= headerRect.top && pt.y <= headerRect.bottom;
        if (inRect) {
            Ai_CopyTextToClipboard(hwndLog, block.code);
            return true;
        }
    }

    return false;
}

bool AiCodeCopy_IsOverLink(HWND hwndLog, POINT pt)
{
    auto it = s_codeBlocks.find(hwndLog);
    if (it == s_codeBlocks.end()) return false;

    for (const AiCodeCopyBlock& block : it->second) {
        if (block.headerStartChar >= 0 && block.headerEndChar > block.headerStartChar) {
            long hoverPos = (long)SendMessageW(hwndLog, EM_CHARFROMPOS, 0, MAKELPARAM(pt.x, pt.y));
            if (hoverPos >= block.headerStartChar && hoverPos < block.headerEndChar) {
                return true;
            }
        }

        RECT headerRect = Ai_MeasureTextRect(hwndLog, block.headerStartChar, block.headerText);
        bool inRect = headerRect.right > headerRect.left && headerRect.bottom > headerRect.top &&
            pt.x >= headerRect.left && pt.x <= headerRect.right &&
            pt.y >= headerRect.top && pt.y <= headerRect.bottom;
        if (inRect) {
            return true;
        }
    }

    return false;
}

bool AiCodeCopy_HandleLink(HWND hwndLog, const ENLINK* link)
{
    if (!hwndLog || !link) return false;
    if (link->msg != WM_LBUTTONDOWN && link->msg != WM_LBUTTONUP) return false;

    auto it = s_codeBlocks.find(hwndLog);
    if (it == s_codeBlocks.end()) return false;

    for (const AiCodeCopyBlock& block : it->second) {
        if (block.headerStartChar >= 0 && block.headerEndChar > block.headerStartChar) {
            if (link->chrg.cpMax >= block.headerStartChar && link->chrg.cpMin < block.headerEndChar) {
                Ai_CopyTextToClipboard(hwndLog, block.code);
                return true;
            }
        }
    }

    return false;
}

void AiCodeCopy_Remove(HWND hwndLog)
{
    if (!hwndLog) return;
    s_codeBlocks.erase(hwndLog);
}