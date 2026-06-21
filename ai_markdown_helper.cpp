#include "ai_markdown_helper.h"

#include "copy_ai_code.h"

#include <algorithm>
#include <cwctype>
#include <map>
#include <richedit.h>
#include <string>

#include "ILexer.h"
#include "Lexilla.h"
#include "Scintilla.h"
#include "ScintillaMessages.h"

std::map<HWND, CHARRANGE> s_aiAnswerCopyRanges;

namespace {

static constexpr COLORREF kAiCodeTextColor = RGB(34, 34, 34);

static const char* Ai_MapFenceLangToLexer(const std::wstring& lang)
{
    std::wstring lower;
    lower.reserve(lang.size());
    for (wchar_t ch : lang) {
        if (!iswspace(ch)) lower.push_back((wchar_t)towlower(ch));
    }

    if (lower.empty()) return nullptr;
    if (lower == L"c" || lower == L"cc" || lower == L"cpp" || lower == L"cxx" || lower == L"h" || lower == L"hh" || lower == L"hpp" || lower == L"hxx" || lower == L"c++") return "cpp";
    if (lower == L"cs" || lower == L"csharp") return "csharp";
    if (lower == L"js" || lower == L"javascript" || lower == L"jsx" || lower == L"mjs") return "javascript";
    if (lower == L"ts" || lower == L"typescript" || lower == L"tsx") return "typescript";
    if (lower == L"py" || lower == L"python") return "python";
    if (lower == L"html" || lower == L"htm" || lower == L"xhtml") return "hypertext";
    if (lower == L"xml" || lower == L"svg" || lower == L"rss") return "xml";
    if (lower == L"json" || lower == L"jsonc") return "json";
    if (lower == L"css") return "css";
    if (lower == L"sh" || lower == L"bash" || lower == L"zsh" || lower == L"shell") return "bash";
    if (lower == L"ps1" || lower == L"powershell" || lower == L"pwsh") return "powershell";
    if (lower == L"sql") return "sql";
    if (lower == L"php") return "php";
    if (lower == L"md" || lower == L"markdown") return "markdown";
    if (lower == L"diff" || lower == L"patch") return "diff";
    if (lower == L"ini" || lower == L"conf" || lower == L"cfg" || lower == L"toml" || lower == L"properties") return "props";
    return nullptr;
}

static void Ai_AppendCodeStyledRun(HWND hLog, const std::wstring& text, COLORREF color)
{
    if (!hLog || text.empty()) return;

    CHARFORMAT2W fmt = {};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    fmt.yHeight = MulDiv(12, 20, 1);
    fmt.crTextColor = color;
    lstrcpyW(fmt.szFaceName, L"Consolas");
    Ai_AppendRichRun(hLog, text, &fmt, nullptr);
}

static void Ai_AppendScintillaColoredCode(HWND hLog, const std::wstring& text, const std::wstring& lang, COLORREF fallbackColor)
{
    if (!hLog || text.empty()) return;

    const char* lexerName = Ai_MapFenceLangToLexer(lang);
    if (!lexerName) {
        Ai_AppendCodeRun(hLog, text, fallbackColor);
        return;
    }

    HWND hSci = CreateWindowExW(0, L"Scintilla", L"",
        WS_CHILD,
        0, 0, 1, 1, hLog, NULL, GetModuleHandleW(NULL), NULL);
    if (!hSci) {
        Ai_AppendCodeRun(hLog, text, fallbackColor);
        return;
    }

    SendMessageW(hSci, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
    SendMessageW(hSci, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Consolas");
    SendMessageW(hSci, SCI_STYLESETSIZE, STYLE_DEFAULT, 10);
    SendMessageW(hSci, SCI_STYLESETFORE, STYLE_DEFAULT, (LPARAM)fallbackColor);
    SendMessageW(hSci, SCI_STYLESETBACK, STYLE_DEFAULT, (LPARAM)RGB(255, 255, 255));
    SendMessageW(hSci, SCI_STYLECLEARALL, 0, 0);

    ILexer5* pLex = CreateLexer(lexerName);
    if (pLex) {
        SendMessageW(hSci, SCI_SETILEXER, 0, (LPARAM)pLex);
    }

    std::string utf8 = Ai_WideToUtf8(text);
    SendMessageW(hSci, SCI_SETTEXT, 0, (LPARAM)utf8.c_str());
    SendMessageW(hSci, SCI_COLOURISE, 0, -1);

    size_t pos = 0;
    while (pos < utf8.size()) {
        int style = (int)SendMessageW(hSci, SCI_GETSTYLEAT, (WPARAM)pos, 0);
        size_t next = pos + 1;
        while (next < utf8.size() && (int)SendMessageW(hSci, SCI_GETSTYLEAT, (WPARAM)next, 0) == style) {
            ++next;
        }

        COLORREF color = (COLORREF)SendMessageW(hSci, SCI_STYLEGETFORE, (WPARAM)style, 0);
        std::string slice = utf8.substr(pos, next - pos);
        std::wstring wideSlice = Ai_Utf8ToWide(slice);
        if (!wideSlice.empty()) {
            Ai_AppendCodeStyledRun(hLog, wideSlice, color);
        }
        pos = next;
    }

    DestroyWindow(hSci);
}
static bool Ai_IsGeneratedCopyHeaderLine(const std::wstring& line)
{
    std::wstring trimmed = line;
    while (!trimmed.empty() && iswspace(trimmed.front())) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && iswspace(trimmed.back())) trimmed.pop_back();
    return !trimmed.empty() && trimmed.find(L"Copy code") != std::wstring::npos &&
        trimmed.find(L"\U0001F4CB") != std::wstring::npos;
}

} // namespace

std::string Ai_WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 1) return {};
    std::string out((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, NULL, NULL);
    return out;
}

std::wstring Ai_Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (wlen <= 1) return {};
    std::wstring out((size_t)wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], wlen);
    return out;
}

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

COLORREF Ai_ParseHtmlColorValue(const std::wstring& value)
{
    std::wstring lower = value;
    for (wchar_t& ch : lower) {
        ch = (wchar_t)towlower(ch);
    }

    if (lower == L"darkgreen" || lower == L"dark green") return RGB(0, 100, 0);
    if (lower == L"green") return RGB(38, 139, 78);
    if (lower == L"red") return RGB(220, 50, 47);
    if (lower == L"blue") return RGB(38, 112, 191);
    if (lower == L"yellow") return RGB(181, 137, 0);
    if (lower == L"purple") return RGB(108, 113, 196);
    if (lower == L"orange") return RGB(203, 75, 22);
    if (lower == L"cyan" || lower == L"aqua") return RGB(42, 161, 152);
    if (lower == L"magenta" || lower == L"fuchsia") return RGB(211, 54, 130);
    if (lower == L"lime") return RGB(133, 153, 0);
    if (lower == L"teal") return RGB(42, 161, 152);
    if (lower == L"navy") return RGB(38, 112, 191);
    if (lower == L"maroon") return RGB(220, 50, 47);
    if (lower == L"olive") return RGB(181, 137, 0);
    if (lower == L"silver") return RGB(147, 161, 161);
    if (lower == L"brown") return RGB(133, 53, 0);
    if (lower == L"pink") return RGB(211, 54, 130);
    if (lower == L"gold") return RGB(181, 137, 0);
    if (lower == L"coral") return RGB(203, 75, 22);
    if (lower == L"salmon") return RGB(203, 75, 22);
    if (lower == L"crimson") return RGB(220, 50, 47);
    if (lower == L"indigo") return RGB(108, 113, 196);
    if (lower == L"violet") return RGB(108, 113, 196);
    if (lower == L"black") return RGB(0, 0, 0);
    if (lower == L"gray" || lower == L"grey") return RGB(128, 128, 128);
    if (lower == L"white") return RGB(255, 255, 255);

    if (lower.rfind(L"rgb(", 0) == 0 || lower.rfind(L"rgba(", 0) == 0) {
        int r = 0, g = 0, b = 0;
        if (swscanf(lower.c_str(), L"rgb(%d,%d,%d)", &r, &g, &b) == 3 ||
            swscanf(lower.c_str(), L"rgba(%d,%d,%d", &r, &g, &b) == 3) {
            return RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
        }
    }

    if (lower.size() == 7 && lower[0] == L'#') {
        int r = 0, g = 0, b = 0;
        if (swscanf(lower.c_str(), L"#%02x%02x%02x", &r, &g, &b) == 3) {
            return RGB(r, g, b);
        }
    }

    return RGB(0, 0, 0);
}

std::wstring Ai_GetHtmlAttributeValue(const std::wstring& tag, const std::wstring& attrName)
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
    while (end < tag.size() && ((quote && tag[end] != quote) || (!quote && tag[end] != L'>'))) {
        ++end;
    }
    return tag.substr(pos, end - pos);
}

bool Ai_TryParseStandaloneColorLine(const std::wstring& line, COLORREF& color)
{
    std::wstring text = line;
    while (!text.empty() && iswspace(text.front())) text.erase(text.begin());
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    if (text.empty()) return false;

    std::wstring lower = text;
    for (wchar_t& ch : lower) {
        ch = (wchar_t)towlower(ch);
    }

    if (lower == L"dark green" || lower == L"darkgreen") {
        color = RGB(0, 100, 0);
        return true;
    }
    if (lower == L"clear blue" || lower == L"blue") {
        color = RGB(0, 120, 215);
        return true;
    }
    if (lower == L"green" || lower == L"bright green") {
        color = RGB(38, 139, 78);
        return true;
    }
    if (lower == L"red") {
        color = RGB(220, 50, 47);
        return true;
    }
    return false;
}

COLORREF Ai_FindReplyDefaultColor(const std::wstring& text)
{
    struct Cue { const wchar_t* needle; COLORREF color; };
    static const Cue kCues[] = {
        { L"clear blue", RGB(0, 120, 215) },
        { L"dark green", RGB(0, 100, 0) },
        { L"bright green", RGB(38, 139, 78) },
        { L"green", RGB(38, 139, 78) },
        { L"red", RGB(220, 50, 47) },
        { L"blue", RGB(0, 120, 215) },
    };

    size_t bestPos = std::wstring::npos;
    COLORREF bestColor = RGB(0, 0, 0);
    std::wstring lower = text;
    for (wchar_t& ch : lower) {
        ch = (wchar_t)towlower(ch);
    }

    for (const Cue& cue : kCues) {
        size_t pos = lower.rfind(cue.needle);
        if (pos != std::wstring::npos && (bestPos == std::wstring::npos || pos >= bestPos)) {
            bestPos = pos;
            bestColor = cue.color;
        }
    }

    return bestColor;
}

std::wstring Ai_ExtractFenceLanguage(const std::wstring& info)
{
    std::wstring trimmed = info;
    while (!trimmed.empty() && iswspace(trimmed.front())) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && iswspace(trimmed.back())) trimmed.pop_back();

    size_t end = 0;
    while (end < trimmed.size() && !iswspace(trimmed[end])) ++end;
    return trimmed.substr(0, end);
}

void Ai_AppendStyledRun(HWND hLog, const std::wstring& text, COLORREF color, bool bold, bool italic, bool underline, bool strike)
{
    if (text.empty()) return;

    CHARFORMAT2W fmt = {};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
    fmt.yHeight = MulDiv(12, 20, 1);
    fmt.crTextColor = color;
    lstrcpyW(fmt.szFaceName, L"Segoe UI Emoji");
    if (bold) fmt.dwEffects |= CFE_BOLD;
    if (italic) fmt.dwEffects |= CFE_ITALIC;
    if (underline) fmt.dwEffects |= CFE_UNDERLINE;
    if (strike) fmt.dwEffects |= CFE_STRIKEOUT;
    Ai_AppendRichRun(hLog, text, &fmt, nullptr);
}

void Ai_AppendCodeRun(HWND hLog, const std::wstring& text, COLORREF color)
{
    if (!hLog || text.empty()) return;

    CHARFORMAT2W fmt = {};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    fmt.yHeight = MulDiv(12, 20, 1);
    fmt.crTextColor = color;
    lstrcpyW(fmt.szFaceName, L"Consolas");
    Ai_AppendRichRun(hLog, text, &fmt, nullptr);
}

void Ai_AppendRichRun(HWND hLog, const std::wstring& text, const CHARFORMAT2W* format, const CHARFORMAT2W* resetFormat)
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
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

void Ai_AppendMarkupLine(HWND hLog, const std::wstring& line, COLORREF baseColor)
{
    COLORREF standaloneColor = RGB(0, 0, 0);
    if (Ai_TryParseStandaloneColorLine(line, standaloneColor)) {
        Ai_AppendStyledRun(hLog, line, standaloneColor, false, false, false, false);
        return;
    }

    size_t pos = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    COLORREF color = baseColor;

    while (pos < line.size()) {
        size_t open = line.find(L'<', pos);
        if (open == std::wstring::npos) {
            Ai_AppendStyledRun(hLog, line.substr(pos), color, bold, italic, underline, strike);
            break;
        }

        if (open > pos) {
            Ai_AppendStyledRun(hLog, line.substr(pos, open - pos), color, bold, italic, underline, strike);
        }

        size_t close = line.find(L'>', open + 1);
        if (close == std::wstring::npos) {
            Ai_AppendStyledRun(hLog, line.substr(open), color, bold, italic, underline, strike);
            break;
        }

        std::wstring tag = line.substr(open + 1, close - open - 1);
        std::wstring lowerTag = tag;
        for (wchar_t& ch : lowerTag) ch = (wchar_t)towlower(ch);

        if (!tag.empty() && tag[0] == L'/') {
            if (lowerTag == L"/span" || lowerTag == L"/font" || lowerTag == L"/color") color = baseColor;
            else if (lowerTag == L"/b" || lowerTag == L"/strong") bold = false;
            else if (lowerTag == L"/i" || lowerTag == L"/em") italic = false;
            else if (lowerTag == L"/u") underline = false;
            else if (lowerTag == L"/s" || lowerTag == L"/strike" || lowerTag == L"/del") strike = false;
        } else if (lowerTag.rfind(L"span", 0) == 0) {
            std::wstring style = Ai_GetHtmlAttributeValue(tag, L"style");
            if (!style.empty()) {
                std::wstring lowerStyle = style;
                for (wchar_t& ch : lowerStyle) ch = (wchar_t)towlower(ch);
                size_t colorPos = lowerStyle.find(L"color:");
                if (colorPos != std::wstring::npos) {
                    size_t valueStart = colorPos + 6;
                    while (valueStart < style.size() && iswspace(style[valueStart])) ++valueStart;
                    size_t valueEnd = valueStart;
                    while (valueEnd < style.size() && style[valueEnd] != L';' && style[valueEnd] != L'"' && style[valueEnd] != L'\'') {
                        ++valueEnd;
                    }
                    std::wstring value = style.substr(valueStart, valueEnd - valueStart);
                    while (!value.empty() && iswspace(value.front())) value.erase(value.begin());
                    while (!value.empty() && iswspace(value.back())) value.pop_back();
                    if (!value.empty()) {
                        color = Ai_ParseHtmlColorValue(value);
                    }
                }
            }
        } else if (lowerTag.rfind(L"color=", 0) == 0) {
            std::wstring value = tag.substr(6);
            while (!value.empty() && iswspace(value.front())) value.erase(value.begin());
            while (!value.empty() && iswspace(value.back())) value.pop_back();
            if (!value.empty()) {
                color = Ai_ParseHtmlColorValue(value);
            }
        } else if (lowerTag == L"color") {
            color = baseColor;
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
        } else if (lowerTag == L"br") {
            Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
        } else {
            Ai_AppendStyledRun(hLog, line.substr(open, close - open + 1), color, bold, italic, underline, strike);
        }

        pos = close + 1;
    }
}

void Ai_AppendLiveChunkText(HWND hLog, const std::wstring& text, bool codeBlock, COLORREF codeColor)
{
    if (!hLog || text.empty()) return;

    static const CHARFORMAT2W kLiveTextFmt = []() {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
        cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(0, 0, 0);
        lstrcpyW(cf.szFaceName, L"Segoe UI Emoji");
        return cf;
    }();

    static const CHARFORMAT2W kLiveCodeFmt = []() {
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
        cf.yHeight = MulDiv(12, 20, 1);
        cf.crTextColor = RGB(34, 34, 34);
        lstrcpyW(cf.szFaceName, L"Consolas");
        return cf;
    }();

    if (codeBlock) {
        CHARFORMAT2W codeFmt = kLiveCodeFmt;
        codeFmt.crTextColor = codeColor;
        Ai_AppendRichRun(hLog, text, &codeFmt, &kLiveTextFmt);
    } else {
        Ai_AppendRichRun(hLog, text, &kLiveTextFmt, nullptr);
    }
}

void Ai_FlushLiveChunk(AiStreamChunk& chunkInfo, HWND hLog, bool force)
{
    if (!hLog) return;

    Ai_UnescapeModelText(chunkInfo.pendingText);
    COLORREF replyColor = Ai_FindReplyDefaultColor(chunkInfo.pendingText);

    size_t pos = 0;
    while (pos < chunkInfo.pendingText.size()) {
        size_t lineEnd = chunkInfo.pendingText.find(L'\n', pos);
        bool hasNewline = lineEnd != std::wstring::npos;
        std::wstring line = hasNewline ? chunkInfo.pendingText.substr(pos, lineEnd - pos) : chunkInfo.pendingText.substr(pos);
        if (Ai_IsGeneratedCopyHeaderLine(line)) {
            if (!hasNewline) {
                break;
            }
            pos = lineEnd + 1;
            continue;
        }
        std::wstring trimmed = line;
        while (!trimmed.empty() && iswspace(trimmed.front())) trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && iswspace(trimmed.back())) trimmed.pop_back();
        size_t fencePos = line.find(L"```");

        if (!chunkInfo.inCodeBlock && fencePos != std::wstring::npos) {
            std::wstring prefix = line.substr(0, fencePos);
            std::wstring suffix = line.substr(fencePos + 3);
            if (!prefix.empty()) {
                Ai_AppendMarkupLine(hLog, prefix, replyColor);
                if (!chunkInfo.inCodeBlock) {
                    Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
                }
            }
            chunkInfo.inCodeBlock = true;
            std::wstring label = L"\U0001F4CB Copy code";
            std::wstring lang = Ai_ExtractFenceLanguage(suffix);
            if (!lang.empty()) {
                label += L" (" + lang + L")";
            }
            int headerStart = GetWindowTextLengthW(hLog);
            static const CHARFORMAT2W kCopyHeaderFmt = []() {
                CHARFORMAT2W cf = {};
                cf.cbSize = sizeof(cf);
                cf.dwMask = CFM_BOLD | CFM_COLOR | CFM_UNDERLINE;
                cf.dwEffects = CFE_BOLD | CFE_UNDERLINE;
                cf.crTextColor = RGB(0, 120, 215);
                return cf;
            }();
            Ai_AppendRichRun(hLog, label, &kCopyHeaderFmt, nullptr);
            Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
            int codeStart = GetWindowTextLengthW(hLog);
            AiCopyCode_BeginBlock(hLog, headerStart, label, codeStart);
            chunkInfo.chunk.clear();
        } else if (chunkInfo.inCodeBlock && fencePos != std::wstring::npos) {
            std::wstring beforeFence = line.substr(0, fencePos);
            if (!beforeFence.empty()) {
                Ai_AppendLiveChunkText(hLog, beforeFence, true, kAiCodeTextColor);
                AiCopyCode_AppendCodeLine(hLog, beforeFence);
            }
            chunkInfo.inCodeBlock = false;
            AiCopyCode_EndBlock(hLog, GetWindowTextLengthW(hLog));
            Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
            std::wstring afterFence = line.substr(fencePos + 3);
            if (!afterFence.empty()) {
                Ai_AppendMarkupLine(hLog, afterFence, replyColor);
            }
        } else {
            if (!line.empty() || hasNewline || force) {
                if (chunkInfo.inCodeBlock) {
                    Ai_AppendLiveChunkText(hLog, line, true, kAiCodeTextColor);
                } else {
                    Ai_AppendMarkupLine(hLog, line, replyColor);
                }
                if (hasNewline) {
                    Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
                }
                if (chunkInfo.inCodeBlock) {
                    AiCopyCode_AppendCodeLine(hLog, line + (hasNewline ? L"\r\n" : L""));
                }
            }
        }

        if (!hasNewline) {
            break;
        }
        pos = lineEnd + 1;
    }

    if (pos > 0) {
        chunkInfo.pendingText.erase(0, pos);
    }
}

void Ai_RenderMarkdownReply(HWND hwnd, int baseStart, const std::wstring& reply)
{
    HWND hLog = GetDlgItem(hwnd, IDC_AI_LOG);
    if (!hLog) return;

    int logLen = GetWindowTextLengthW(hLog);
    int clearStart = std::max(0, baseStart);
    if (clearStart < logLen) {
        CHARRANGE erase = { clearStart, logLen };
        SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&erase);
        SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"");
    }

    AiCopyCode_Clear(hLog);

    std::wstring text = reply;
    Ai_ReplaceAll(text, L"\r\n", L"\n");
    Ai_ReplaceAll(text, L"\r", L"\n");
    Ai_UnescapeModelText(text);
    COLORREF replyColor = Ai_FindReplyDefaultColor(text);

    auto trimCopy = [](const std::wstring& value) {
        size_t first = 0;
        while (first < value.size() && iswspace(value[first])) {
            ++first;
        }
        size_t last = value.size();
        while (last > first && iswspace(value[last - 1])) {
            --last;
        }
        return value.substr(first, last - first);
    };

    bool inCode = false;
    bool codeHasContent = false;
    std::wstring codeLang;

    auto appendCodeHeader = [&](const std::wstring& lang) {
        std::wstring label = L"\U0001F4CB Copy code";
        std::wstring trimmedLang = Ai_ExtractFenceLanguage(lang);
        if (!trimmedLang.empty()) {
            label += L" (" + trimmedLang + L")";
        }

        int headerStart = GetWindowTextLengthW(hLog);
        static const CHARFORMAT2W kCopyHeaderFmt = []() {
            CHARFORMAT2W cf = {};
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_BOLD | CFM_COLOR | CFM_UNDERLINE;
            cf.dwEffects = CFE_BOLD | CFE_UNDERLINE;
            cf.crTextColor = RGB(0, 120, 215);
            return cf;
        }();
        Ai_AppendRichRun(hLog, label, &kCopyHeaderFmt, nullptr);
        Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
        int codeStart = GetWindowTextLengthW(hLog);
        AiCopyCode_BeginBlock(hLog, headerStart, label, codeStart);
        codeLang = trimmedLang;
    };

    auto endCodeBlock = [&]() {
        if (!inCode) return;
        AiCopyCode_EndBlock(hLog, GetWindowTextLengthW(hLog));
        if (codeHasContent) {
            Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
        }
        inCode = false;
        codeHasContent = false;
    };

    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find(L'\n', lineStart);
        std::wstring line = (lineEnd == std::wstring::npos) ? text.substr(lineStart) : text.substr(lineStart, lineEnd - lineStart);
        std::wstring trimmed = trimCopy(line);
        size_t fencePos = line.find(L"```");

        if (!inCode && fencePos != std::wstring::npos) {
            std::wstring prefix = line.substr(0, fencePos);
            std::wstring suffix = line.substr(fencePos + 3);
            if (!prefix.empty()) {
                Ai_AppendMarkupLine(hLog, prefix, replyColor);
                Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
            }
            inCode = true;
            codeHasContent = false;
            appendCodeHeader(suffix);
        } else if (inCode && fencePos != std::wstring::npos) {
            std::wstring beforeFence = line.substr(0, fencePos);
            if (!beforeFence.empty()) {
                codeHasContent = true;
                Ai_AppendScintillaColoredCode(hLog, beforeFence, codeLang, kAiCodeTextColor);
                AiCopyCode_AppendCodeLine(hLog, beforeFence);
            }
            endCodeBlock();
            codeLang.clear();
            std::wstring afterFence = line.substr(fencePos + 3);
            if (!afterFence.empty()) {
                Ai_AppendMarkupLine(hLog, afterFence, replyColor);
                Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
            }
        } else if (inCode) {
            codeHasContent = true;
            Ai_AppendScintillaColoredCode(hLog, line, codeLang, kAiCodeTextColor);
            Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
            AiCopyCode_AppendCodeLine(hLog, line + L"\r\n");
        } else {
            Ai_AppendMarkupLine(hLog, line, replyColor);
            Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
        }

        if (lineEnd == std::wstring::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    if (inCode) {
        AiCopyCode_EndBlock(hLog, GetWindowTextLengthW(hLog));
        Ai_AppendRichRun(hLog, L"\r\n", nullptr, nullptr);
    }

    CHARRANGE answerRange = { std::max(0, baseStart), GetWindowTextLengthW(hLog) };
    s_aiAnswerCopyRanges[hLog] = answerRange;

    CHARRANGE endRange = { GetWindowTextLengthW(hLog), GetWindowTextLengthW(hLog) };
    SendMessageW(hLog, EM_EXSETSEL, 0, (LPARAM)&endRange);
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}