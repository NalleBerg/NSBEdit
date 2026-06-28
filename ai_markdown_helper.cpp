#include "ai_markdown_helper.h"

#include <richedit.h>
#include <string>
#include <cstring>
#include <vector>

namespace {

struct AiStreamBuf {
    const std::string* src = nullptr;
    size_t pos = 0;
};

static DWORD CALLBACK Ai_ReadStreamCb(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* pcb)
{
    AiStreamBuf* state = (AiStreamBuf*)cookie;
    if (!state || !state->src) {
        *pcb = 0;
        return 0;
    }

    size_t remaining = state->src->size() - state->pos;
    LONG count = (LONG)(remaining < (size_t)cb ? remaining : (size_t)cb);
    if (count > 0) {
        memcpy(buf, state->src->data() + state->pos, (size_t)count);
        state->pos += (size_t)count;
    }
    *pcb = count;
    return 0;
}

} // namespace

std::map<HWND, CHARRANGE> s_aiAnswerCopyRanges;

void Ai_ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to)
{
    if (from.empty()) return;

    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

void Ai_UnescapeModelText(std::wstring& text)
{
    Ai_ReplaceAll(text, L"\\u003c", L"<");
    Ai_ReplaceAll(text, L"\\u003e", L">");
    Ai_ReplaceAll(text, L"\\u0026", L"&");
    Ai_ReplaceAll(text, L"u003c", L"<");
    Ai_ReplaceAll(text, L"u003e", L">");
    Ai_ReplaceAll(text, L"u0026", L"&");
    Ai_ReplaceAll(text, L"&lt;", L"<");
    Ai_ReplaceAll(text, L"&gt;", L">");
    Ai_ReplaceAll(text, L"&amp;", L"&");
}

std::string Ai_WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();

    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (needed <= 0) return std::string();

    std::string result((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), result.data(), needed, NULL, NULL);
    return result;
}

std::wstring Ai_Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();

    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (needed <= 0) return std::wstring();

    std::wstring result((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), needed);
    return result;
}

void Ai_StreamRtfIntoSelection(HWND hEdit, const std::string& rtf)
{
    if (!hEdit || rtf.empty()) return;

    AiStreamBuf state = { &rtf, 0 };
    EDITSTREAM es = { (DWORD_PTR)&state, 0, Ai_ReadStreamCb };
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
}

void Ai_AppendRichRun(HWND hLog, const std::wstring& text, const CHARFORMAT2W* format, const CHARFORMAT2W* resetFormat, bool scrollCaret)
{
    if (!hLog || text.empty()) return;

    int start = GetWindowTextLengthW(hLog);
    CHARRANGE range = { start, start };
    SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&range);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());

    int end = GetWindowTextLengthW(hLog);
    if (format && end > start) {
        CHARRANGE styled = { start, end };
        SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&styled);
        SendMessageW(hLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)format);
    }

    if (resetFormat) {
        CHARRANGE caret = { end, end };
        SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&caret);
        SendMessageW(hLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)resetFormat);
    }

    range.cpMin = end;
    range.cpMax = end;
    SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&range);
    if (scrollCaret) {
        SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
    }
}