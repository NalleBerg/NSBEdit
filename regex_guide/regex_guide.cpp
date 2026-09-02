#include "regex_guide.h"

#include <algorithm>
#include <cwctype>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <string>
#include <vector>

#include "dpi.h"
#include "highlight/highlight.h"
#include "tooltip/tooltip.h"

namespace {

constexpr int IDC_GUIDE_TITLE   = 2001;
constexpr int IDC_GUIDE_LABEL    = 2002;
constexpr int IDC_GUIDE_SEARCH   = 2003;
constexpr int IDC_GUIDE_BUTTON   = 2004;
constexpr int IDC_GUIDE_NOTE     = 2005;
constexpr int IDC_GUIDE_VIEW     = 2006;

static HWND s_hGuideWindow = NULL;

struct GuideCreateParams {
    HWND parent = nullptr;
    bool postQuitOnDestroy = false;
    const wchar_t* title = L"Regex Reference Guide";
    const wchar_t* label = L"Containing";
    const wchar_t* note = L"Ctrl+F focuses the search box. Search updates as you type.";
    const wchar_t* guideText = L"";
    const wchar_t* searchCue = L"Search guide text";
    const wchar_t* clearTip = L"Clear search";
    const wchar_t* closeText = L"Close";
};

enum class GuideBtnTone {
    Red,
};

struct GuideState {
    HWND hWindow = nullptr;
    HWND hTitle = nullptr;
    HWND hLabel = nullptr;
    HWND hSearch = nullptr;
    HWND hButton = nullptr;
    HWND hNote = nullptr;
    HWND hView = nullptr;
    HFONT hTitleFont = nullptr;
    HFONT hBodyFont = nullptr;
    HFONT hCodeFont = nullptr;
    bool postQuitOnDestroy = false;
    std::wstring titleText;
    std::wstring labelText;
    std::wstring noteText;
    std::wstring guideText;
    std::wstring searchCueText;
    std::wstring clearTipText;
    std::wstring closeText;
    int closeButtonWidth = 0;
    std::wstring plainText;
    NeHighlightState searchHighlight;
};

static void Guide_ClearSearch(GuideState* st);

enum class LineKind {
    Title,
    Subtitle,
    Heading,
    Body,
    Bullet,
    Code,
    Blank,
};

struct GuideLine {
    LineKind kind;
    const wchar_t* text;
};

struct TextStyle {
    const wchar_t* face = L"Segoe UI";
    int pointSize = 12;
    bool bold = false;
    COLORREF color = RGB(30, 30, 30);
};

static constexpr COLORREF kGuideAccentBlue = RGB(0, 56, 184);

static HFONT Guide_CreateFont(HWND hwnd, const wchar_t* face, int pointSize, bool bold);

static int Guide_MeasureTextWidth(HWND hwnd, const wchar_t* text, bool bold = false)
{
    if (!text) {
        return 0;
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        return 0;
    }
    HFONT hf = Guide_CreateFont(hwnd, L"Segoe UI", 12, bold);
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    if (old) {
        SelectObject(hdc, old);
    }
    if (hf) {
        DeleteObject(hf);
    }
    ReleaseDC(hwnd, hdc);
    return sz.cx;
}

static int Guide_MeasureTextHeight(HWND hwnd, const wchar_t* text, int width, bool bold = false)
{
    if (!text || width <= 0) {
        return S(24);
    }
    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        return S(24);
    }
    HFONT hf = Guide_CreateFont(hwnd, L"Segoe UI", 12, bold);
    HFONT old = hf ? (HFONT)SelectObject(hdc, hf) : NULL;
    RECT rc = { 0, 0, width, 0 };
    DrawTextW(hdc, text, -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    if (old) {
        SelectObject(hdc, old);
    }
    if (hf) {
        DeleteObject(hf);
    }
    ReleaseDC(hwnd, hdc);
    return std::max((int)(rc.bottom - rc.top), S(24));
}

static LRESULT CALLBACK Guide_ClearBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR data)
{
    auto* st = reinterpret_cast<GuideState*>(data);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (st) {
            if (!GetPropW(hwnd, L"GuideHover")) {
                SetPropW(hwnd, L"GuideHover", (HANDLE)1);
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (!GetPropW(hwnd, L"GuideTipShown") && !st->clearTipText.empty()) {
                RECT rc = {};
                GetWindowRect(hwnd, &rc);
                std::vector<TooltipEntry> entries = { { L"", st->clearTipText } };
                ShowMultilingualTooltip(entries, rc.left, rc.bottom + S(4), GetParent(hwnd), rc.top);
                SetPropW(hwnd, L"GuideTipShown", (HANDLE)1);
            }
        }
        break;
    case WM_MOUSELEAVE:
        HideTooltip();
        RemovePropW(hwnd, L"GuideTipShown");
        RemovePropW(hwnd, L"GuideHover");
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_NCDESTROY:
        HideTooltip();
        RemovePropW(hwnd, L"GuideTipShown");
        RemovePropW(hwnd, L"GuideHover");
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static int Guide_GetDpi(HWND hwnd)
{
    int dpi = hwnd ? (int)GetDpiForWindow(hwnd) : (int)GetDpiForSystem();
    return dpi > 0 ? dpi : 96;
}

static HFONT Guide_CreateFont(HWND hwnd, const wchar_t* face, int pointSize, bool bold = false)
{
    int height = -MulDiv(pointSize, Guide_GetDpi(hwnd), 72);
    return CreateFontW(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
}

static std::wstring Guide_GetWindowText(HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len + 1, L'\0');
    if (len > 0) {
        GetWindowTextW(hwnd, text.data(), len + 1);
        text.resize(len);
    }
    return text;
}

static std::vector<GuideLine> Guide_GetLocalizedLines(const std::wstring& guideText)
{
    std::vector<GuideLine> lines;
    if (guideText.empty() || guideText == L"REGEX_GUIDE_TEXT") {
        return lines;
    }

    static thread_local std::vector<std::wstring> storage;
    storage.clear();

    size_t start = 0;
    while (start <= guideText.size()) {
        size_t end = guideText.find(L'\n', start);
        std::wstring line = guideText.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        storage.push_back(std::move(line));
        const std::wstring& item = storage.back();

        LineKind kind = LineKind::Body;
        const wchar_t* text = item.c_str();
        if (item.empty()) {
            kind = LineKind::Blank;
            text = L"";
        } else if (item.size() > 2 && item[1] == L'|') {
            text = item.c_str() + 2;
            switch (item[0]) {
            case L'T': kind = LineKind::Title; break;
            case L'S': kind = LineKind::Subtitle; break;
            case L'H': kind = LineKind::Heading; break;
            case L'B': kind = LineKind::Body; break;
            case L'C': kind = LineKind::Code; break;
            case L'L': kind = LineKind::Bullet; break;
            case L'X': kind = LineKind::Blank; break;
            default: kind = LineKind::Body; break;
            }
        }

        lines.push_back({ kind, text });
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

static std::vector<NeHighlightRange> Guide_FindMatches(HWND hView, const std::wstring& needle)
{
    std::vector<NeHighlightRange> matches;
    if (!hView || needle.empty()) {
        return matches;
    }

    FINDTEXTEXW ft = {};
    ft.chrg.cpMin = 0;
    ft.chrg.cpMax = -1;
    ft.lpstrText = const_cast<LPWSTR>(needle.c_str());

    for (;;) {
        LONG found = (LONG)SendMessageW(hView, EM_FINDTEXTEXW, FR_DOWN, (LPARAM)&ft);
        if (found < 0) {
            break;
        }
        matches.push_back({ (int)ft.chrgText.cpMin, (int)ft.chrgText.cpMax });
        if (ft.chrgText.cpMax <= ft.chrg.cpMin) {
            ++ft.chrg.cpMin;
        } else {
            ft.chrg.cpMin = ft.chrgText.cpMax;
        }
        ft.chrg.cpMax = -1;
    }
    return matches;
}

// Scroll the read-only view so the character at charPos is visible near the top.
// EM_SCROLLCARET is unreliable while another control (the search box) holds focus,
// so scroll explicitly by line delta instead.
static void Guide_ScrollToChar(HWND hView, int charPos)
{
    if (!hView) {
        return;
    }
    LONG targetLine  = (LONG)SendMessageW(hView, EM_EXLINEFROMCHAR, 0, (LPARAM)charPos);
    LONG firstLine   = (LONG)SendMessageW(hView, EM_GETFIRSTVISIBLELINE, 0, 0);
    LONG desiredTop  = targetLine - 2;   // keep a little context above the match
    if (desiredTop < 0) {
        desiredTop = 0;
    }
    LONG delta = desiredTop - firstLine;
    if (delta != 0) {
        SendMessageW(hView, EM_LINESCROLL, 0, delta);
    }
}

static void Guide_UpdateSearch(GuideState* st)
{
    if (!st || !st->hSearch || !st->hView) {
        return;
    }

    std::wstring needle = Guide_GetWindowText(st->hSearch);
    if (needle.empty()) {
        NeHighlight_Clear(st->hView, st->searchHighlight);
        CHARRANGE sel = { 0, 0 };
        SendMessageW(st->hView, EM_EXSETSEL, 0, (LPARAM)&sel);
        Guide_ScrollToChar(st->hView, 0);
        return;
    }

    std::vector<NeHighlightRange> matches = Guide_FindMatches(st->hView, needle);
    if (matches.empty()) {
        NeHighlight_Clear(st->hView, st->searchHighlight);
        CHARRANGE sel = { 0, 0 };
        SendMessageW(st->hView, EM_EXSETSEL, 0, (LPARAM)&sel);
        return;
    }

    NeHighlight_SetAll(st->hView, matches, 0,
        RGB(30, 30, 30), NE_HL_BG_INACTIVE, NE_HL_BG_INACTIVE, st->searchHighlight);

    // Jump to the first match. As letters arrive this can move either forward
    // or backward, so the view follows the top-most match at every keystroke.
    // Collapse the caret to the match start so the highlight colour stays visible.
    CHARRANGE sel = { matches[0].start, matches[0].start };
    SendMessageW(st->hView, EM_EXSETSEL, 0, (LPARAM)&sel);
    Guide_ScrollToChar(st->hView, matches[0].start);
}
static void Guide_AppendStyledLine(HWND hView, LONG& pos, std::wstring& plainText,
    const wchar_t* text, const TextStyle& style, bool addBlankLine = false)
{
    std::wstring line = text ? text : L"";
    line += L"\r\n";
    if (addBlankLine) {
        line += L"\r\n";
    }

    CHARRANGE range = { pos, pos };
    SendMessageW(hView, EM_EXSETSEL, 0, (LPARAM)&range);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD;
    if (style.bold) {
        cf.dwEffects |= CFE_BOLD;
    }
    cf.yHeight = style.pointSize * 20;
    cf.crTextColor = style.color;
    lstrcpynW(cf.szFaceName, style.face, LF_FACESIZE);
    SendMessageW(hView, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hView, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());

    LONG end = pos + (LONG)line.size();
    pos = end;
    plainText += line;
}

static void Guide_AppendBlankLine(HWND hView, LONG& pos, std::wstring& plainText)
{
    Guide_AppendStyledLine(hView, pos, plainText, L"", TextStyle{}, false);
}

static std::vector<GuideLine> Guide_GetLines()
{
    return {
        { LineKind::Title,    L"Regex Reference Guide" },
        { LineKind::Subtitle, L"A small learning book for NSBEdit's ECMAScript regex search" },
        { LineKind::Body,     L"Search in this guide is containing-only. Type in the box above or press Ctrl+F to jump to a topic." },
        { LineKind::Body,     L"Each example is written to be short enough to remember and detailed enough to use." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"1. What a regex does" },
        { LineKind::Body,     L"A regular expression is a compact description of text you want to find." },
        { LineKind::Body,     L"Instead of matching one exact string, a regex can match many related strings." },
        { LineKind::Body,     L"NSBEdit uses std::wregex with ECMAScript syntax, so the patterns below follow that style." },
        { LineKind::Body,     L"Start simple: a regex can be as small as a single word." },
        { LineKind::Code,     L"error" },
        { LineKind::Body,     L"That matches the word error anywhere it appears." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"2. Literals and escaping" },
        { LineKind::Body,     L"Most letters and numbers mean themselves." },
        { LineKind::Body,     L"Some punctuation is special, so it must be escaped with a backslash." },
        { LineKind::Code,     L"\\.    a literal dot" },
        { LineKind::Code,     L"\\?    a literal question mark" },
        { LineKind::Code,     L"\\(    a literal opening parenthesis" },
        { LineKind::Code,     L"\\\\   a literal backslash" },
        { LineKind::Body,     L"If you want to match the exact text C:\\Temp, remember to escape each backslash in the regex." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"3. Character classes" },
        { LineKind::Body,     L"Character classes describe one character out of a set." },
        { LineKind::Code,     L"[abc]     one of a, b, or c" },
        { LineKind::Code,     L"[^abc]    anything except a, b, or c" },
        { LineKind::Code,     L"[a-z]     a range" },
        { LineKind::Code,     L"[0-9A-F]  hex digits" },
        { LineKind::Code,     L"[._-]     a few punctuation characters" },
        { LineKind::Body,     L"Inside a class, the dash usually means a range, so put it first or last if you want a literal dash." },
        { LineKind::Code,     L"[-_.]     literal dash, underscore, or dot" },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"4. Shorthand classes" },
        { LineKind::Body,     L"Shorthand classes are the fastest way to describe common text shapes." },
        { LineKind::Code,     L"\\d    digit" },
        { LineKind::Code,     L"\\D    anything that is not a digit" },
        { LineKind::Code,     L"\\w    letter, digit, or underscore" },
        { LineKind::Code,     L"\\W    anything that is not a word character" },
        { LineKind::Code,     L"\\s    whitespace" },
        { LineKind::Code,     L"\\S    anything that is not whitespace" },
        { LineKind::Code,     L".      any character except a line break" },
        { LineKind::Body,     L"A dot is broad; a shorthand class is usually more specific and safer." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"5. Anchors and boundaries" },
        { LineKind::Body,     L"Anchors do not match characters. They match positions." },
        { LineKind::Code,     L"^      start of the searched text" },
        { LineKind::Code,     L"$      end of the searched text" },
        { LineKind::Code,     L"\\b    word boundary" },
        { LineKind::Code,     L"\\B    not a word boundary" },
        { LineKind::Body,     L"Use anchors when you want a full line, a word at the start, or a word at the end." },
        { LineKind::Code,     L"^TODO" },
        { LineKind::Body,     L"That finds lines or strings that start with TODO." },
        { LineKind::Code,     L"\\bfix\\b" },
        { LineKind::Body,     L"That finds the word fix, but not words like prefix or fixture." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"6. Repetition" },
        { LineKind::Body,     L"Quantifiers say how many times the previous token may repeat." },
        { LineKind::Code,     L"*      zero or more" },
        { LineKind::Code,     L"+      one or more" },
        { LineKind::Code,     L"?      optional / zero or one" },
        { LineKind::Code,     L"{n}     exactly n times" },
        { LineKind::Code,     L"{n,}    n or more times" },
        { LineKind::Code,     L"{n,m}   between n and m times" },
        { LineKind::Body,     L"Quantifiers are greedy by default, so they try to consume as much as they can before backtracking." },
        { LineKind::Code,     L"\\d+" },
        { LineKind::Body,     L"That finds one or more digits." },
        { LineKind::Code,     L"\\s+" },
        { LineKind::Body,     L"That finds one or more whitespace characters." },
        { LineKind::Code,     L"[A-Za-z_][A-Za-z0-9_]*" },
        { LineKind::Body,     L"That is a common identifier pattern: one starter character, then zero or more continuation characters." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"7. Grouping and alternation" },
        { LineKind::Body,     L"Parentheses group parts of the pattern so you can repeat them or remember them." },
        { LineKind::Code,     L"(abc)       capture a group" },
        { LineKind::Code,     L"(?:abc)     group without capture" },
        { LineKind::Code,     L"a|b|c       match left or right" },
        { LineKind::Body,     L"Capturing groups become numbered left to right." },
        { LineKind::Code,     L"(first)-(second)" },
        { LineKind::Body,     L"The first group becomes $1, the second becomes $2 in replacements." },
        { LineKind::Code,     L"(cat|dog|fox)" },
        { LineKind::Body,     L"Alternation is useful when one field can hold a few known words." },
        { LineKind::Code,     L"(?:https?|ftp)://" },
        { LineKind::Body,     L"A non-capturing group keeps the structure but skips a capture slot." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"8. Practical patterns" },
        { LineKind::Body,     L"These examples are small enough to adapt and large enough to be useful." },
        { LineKind::Code,     L"TODO|FIXME" },
        { LineKind::Body,     L"Find either tag in notes, comments, or source files." },
        { LineKind::Code,     L"\\b\\w+@\\w+\\.\\w+\\b" },
        { LineKind::Body,     L"A simple email-shaped pattern. It is not a full RFC validator, but it is useful for rough searches." },
        { LineKind::Code,     L"\"[^\"]*\"" },
        { LineKind::Body,     L"Find quoted text on a single line." },
        { LineKind::Code,     L"\\b\\d{4}-\\d{2}-\\d{2}\\b" },
        { LineKind::Body,     L"Find an ISO-style date such as 2026-06-15." },
        { LineKind::Code,     L"\\b[A-Z][A-Za-z0-9_]*\\b" },
        { LineKind::Body,     L"Find simple identifier-like names that start with a capital letter." },
        { LineKind::Code,     L"^\\s*$" },
        { LineKind::Body,     L"Find blank lines or lines that contain only spaces." },
        { LineKind::Code,     L"\\b0x[0-9A-Fa-f]+\\b" },
        { LineKind::Body,     L"Find hex-style numbers such as 0x1A2B." },
        { LineKind::Code,     L"\\b[+-]?\\d+(?:\\.\\d+)?\\b" },
        { LineKind::Body,     L"Find a simple integer or decimal number." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"9. Replacement ideas" },
        { LineKind::Body,     L"When you replace, capture groups let you reuse the parts you matched." },
        { LineKind::Code,     L"Find:    ([A-Za-z]+) ([A-Za-z]+)" },
        { LineKind::Code,     L"Replace: $2, $1" },
        { LineKind::Body,     L"That swaps two words, so John Smith becomes Smith, John." },
        { LineKind::Code,     L"Find:    (\\d{4})-(\\d{2})-(\\d{2})" },
        { LineKind::Code,     L"Replace: $3/$2/$1" },
        { LineKind::Body,     L"That turns 2026-06-15 into 15/06/2026." },
        { LineKind::Code,     L"Find:    ^(.*)$" },
        { LineKind::Code,     L"Replace: [$1]" },
        { LineKind::Body,     L"That wraps each full line in square brackets." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"10. Common mistakes" },
        { LineKind::Body,     L"Most mistakes are small: a missing escape, a greedy quantifier, or an anchor in the wrong place." },
        { LineKind::Code,     L".txt" },
        { LineKind::Body,     L"This is not a literal dot and t x t; the dot means any character. Use \\.txt if you want a literal extension." },
        { LineKind::Code,     L"(abc" },
        { LineKind::Body,     L"Unclosed groups are a syntax error." },
        { LineKind::Code,     L"foo\\bar" },
        { LineKind::Body,     L"A single backslash usually starts an escape, so double it when you need a literal one." },
        { LineKind::Code,     L"^word$" },
        { LineKind::Body,     L"That matches only a full line containing exactly word." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"11. How to think while searching" },
        { LineKind::Body,     L"Ask yourself three questions: what must be present, what may vary, and where should the match stop?" },
        { LineKind::Body,     L"If the answer is ""any character"", use a class or a dot. If the answer is ""the start or end"", use anchors." },
        { LineKind::Body,     L"If the answer is ""repeat this part"", put it in a group before adding a quantifier." },
        { LineKind::Body,     L"If you are unsure, search with a smaller pattern first and grow it one piece at a time." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"12. Notes for NSBEdit" },
        { LineKind::Body,     L"The editor's regex search uses the standard library engine, so prefer portable ECMAScript syntax." },
        { LineKind::Body,     L"Containing search in this guide is literal, not regex-based, so you can jump by plain words or fragments." },
        { LineKind::Body,     L"If you want a subject quickly, press Ctrl+F and type a word from the topic you want." },
        { LineKind::Body,     L"The search line is intentionally plain: no dialog, no regex mode, just the fastest possible topic finder." },
        { LineKind::Blank,    L"" },
        { LineKind::Heading,  L"13. Try these searches" },
        { LineKind::Code,     L"digit" },
        { LineKind::Code,     L"capture" },
        { LineKind::Code,     L"boundary" },
        { LineKind::Code,     L"replace" },
        { LineKind::Code,     L"greedy" },
        { LineKind::Code,     L"anchor" },
        { LineKind::Code,     L"literal" },
        { LineKind::Code,     L"escape" },
        { LineKind::Code,     L"pattern" },
    };
}

static void Guide_RebuildContent(GuideState* st)
{
    if (!st || !st->hView) {
        return;
    }

    SendMessageW(st->hView, EM_SETREADONLY, FALSE, 0);
    SendMessageW(st->hView, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(st->hView, L"");
    st->plainText.clear();

    LONG pos = 0;
    const auto localizedLines = Guide_GetLocalizedLines(st->guideText);
    const auto& lines = localizedLines.empty() ? Guide_GetLines() : localizedLines;
    for (const auto& line : lines) {
        switch (line.kind) {
        case LineKind::Title:
            Guide_AppendStyledLine(st->hView, pos, st->plainText, line.text,
                { L"Segoe UI", 16, true, kGuideAccentBlue });
            break;
        case LineKind::Subtitle:
            Guide_AppendStyledLine(st->hView, pos, st->plainText, line.text,
                { L"Segoe UI", 11, false, RGB(92, 92, 92) });
            break;
        case LineKind::Heading:
            Guide_AppendStyledLine(st->hView, pos, st->plainText, line.text,
                { L"Segoe UI", 13, true, kGuideAccentBlue });
            break;
        case LineKind::Body:
            Guide_AppendStyledLine(st->hView, pos, st->plainText, line.text,
                { L"Segoe UI", 12, false, RGB(30, 30, 30) });
            break;
        case LineKind::Bullet:
            Guide_AppendStyledLine(st->hView, pos, st->plainText, line.text,
                { L"Segoe UI", 12, false, RGB(30, 30, 30) });
            break;
        case LineKind::Code:
            Guide_AppendStyledLine(st->hView, pos, st->plainText, line.text,
                { L"Consolas", 11, false, RGB(40, 40, 40) });
            break;
        case LineKind::Blank:
            Guide_AppendBlankLine(st->hView, pos, st->plainText);
            break;
        }
    }

    CHARRANGE sel = { 0, 0 };
    SendMessageW(st->hView, EM_EXSETSEL, 0, (LPARAM)&sel);
    SendMessageW(st->hView, EM_SCROLLCARET, 0, 0);
    SendMessageW(st->hView, WM_SETREDRAW, TRUE, 0);
    SendMessageW(st->hView, EM_SETREADONLY, TRUE, 0);
    InvalidateRect(st->hView, NULL, TRUE);
}

static void Guide_ClearSearch(GuideState* st)
{
    if (!st || !st->hSearch) {
        return;
    }
    SetWindowTextW(st->hSearch, L"");
    Guide_UpdateSearch(st);
    SetFocus(st->hSearch);
}

static void Guide_FocusSearch(GuideState* st)
{
    if (!st || !st->hSearch) {
        return;
    }
    SetFocus(st->hSearch);
    SendMessageW(st->hSearch, EM_SETSEL, 0, -1);
}

static void Guide_Layout(GuideState* st)
{
    if (!st || !st->hWindow) {
        return;
    }

    RECT rc = {};
    GetClientRect(st->hWindow, &rc);

    const int pad = S(18);
    const int gap = S(8);
    const int top = S(14);
    const int titleH = std::max(S(40), Guide_MeasureTextHeight(st->hWindow, st->titleText.c_str(), rc.right - 2 * pad, true) + S(4));
    const int searchH = S(32);
    const int noteH = std::max(S(24), Guide_MeasureTextHeight(st->hWindow, st->noteText.c_str(), rc.right - 2 * pad, false) + S(4));
    const int buttonH = S(30);
    const int buttonW = buttonH;
    const int labelW = std::max(S(100), Guide_MeasureTextWidth(st->hWindow, st->labelText.c_str(), false) + S(10));

    int searchY = top + titleH + gap;
    int noteY = searchY + searchH + gap;
    int buttonY = rc.bottom - pad - buttonH;
    int contentTop = noteY + noteH + gap;
    int contentH = std::max(S(120), buttonY - gap - contentTop);

    int right = rc.right - pad;
    int searchEditW = std::max(S(220), right - pad - labelW - gap - buttonW - gap);
    if (searchEditW < S(160)) {
        searchEditW = S(160);
    }

    if (st->hTitle) {
        SetWindowPos(st->hTitle, NULL, pad, top, rc.right - 2 * pad, titleH,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (st->hLabel) {
        SetWindowPos(st->hLabel, NULL, pad, searchY + (searchH - S(22)) / 2, labelW, S(22),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (st->hSearch) {
        SetWindowPos(st->hSearch, NULL, pad + labelW + gap, searchY,
            searchEditW, searchH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (st->hNote) {
        SetWindowPos(st->hNote, NULL, pad, noteY, rc.right - 2 * pad, noteH,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (st->hView) {
        SetWindowPos(st->hView, NULL, pad, contentTop,
            rc.right - 2 * pad, contentH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (st->hButton) {
        int buttonX = right - buttonW;
        SetWindowPos(st->hButton, NULL, buttonX, searchY,
            buttonW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static void Guide_DrawClearButton(HWND hwnd, HDC hdc, RECT rc, bool pressed)
{
    bool hover = (GetPropW(hwnd, L"GuideHover") != NULL);
    COLORREF back = pressed ? RGB(185, 36, 36) : hover ? RGB(220, 58, 58) : RGB(208, 48, 48);
    COLORREF border = pressed ? RGB(120, 20, 20) : hover ? RGB(152, 26, 26) : RGB(132, 22, 22);

    HBRUSH hBack = CreateSolidBrush(back);
    FillRect(hdc, &rc, hBack);
    DeleteObject(hBack);

    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hNull);

    int pad = S(7);
    HPEN xPen = CreatePen(PS_SOLID, S(2), RGB(255, 248, 248));
    HPEN oldXPen = (HPEN)SelectObject(hdc, xPen);
    MoveToEx(hdc, rc.left + pad, rc.top + pad, NULL);
    LineTo(hdc, rc.right - pad, rc.bottom - pad);
    MoveToEx(hdc, rc.right - pad, rc.top + pad, NULL);
    LineTo(hdc, rc.left + pad, rc.bottom - pad);
    SelectObject(hdc, oldXPen);
    DeleteObject(xPen);

    if (GetFocus() == hwnd) {
        RECT focus = rc;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(hdc, &focus);
    }

    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

static LRESULT CALLBACK Guide_SearchSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR data)
{
    auto* st = reinterpret_cast<GuideState*>(data);

    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            Guide_UpdateSearch(st);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) < 0) && (wParam == 'F')) {
            Guide_FocusSearch(st);
            return 0;
        }
        if (wParam == VK_ESCAPE && st && st->hWindow) {
            PostMessageW(st->hWindow, WM_CLOSE, 0, 0);
            return 0;
        }
    }

    // Dead keys (e.g. ^ on Nordic layouts) only produce a character after the
    // next keystroke composes it. Let the edit process the message first, then
    // re-scan the box so whatever now sits left of the caret registers and the
    // view jumps. WM_KEYUP covers the composing keystroke that finalises it.
    if (msg == WM_CHAR || msg == WM_KEYUP) {
        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        Guide_UpdateSearch(st);
        return r;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void Guide_UpdateChrome(GuideState* st)
{
    if (!st) {
        return;
    }
    if (st->hWindow) {
        SetWindowTextW(st->hWindow, st->titleText.c_str());
    }
    if (st->hTitle) {
        SetWindowTextW(st->hTitle, st->titleText.c_str());
    }
    if (st->hLabel) {
        SetWindowTextW(st->hLabel, st->labelText.c_str());
    }
    if (st->hNote) {
        SetWindowTextW(st->hNote, st->noteText.c_str());
    }
    if (st->hSearch) {
        SendMessageW(st->hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)st->searchCueText.c_str());
    }
    if (st->hButton) {
        SetWindowTextW(st->hButton, L"");
    }
    st->closeButtonWidth = S(30);
    Guide_Layout(st);
}

static LRESULT CALLBACK Guide_ViewSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR data)
{
    auto* st = reinterpret_cast<GuideState*>(data);

    if (msg == WM_KEYDOWN) {
        if ((GetKeyState(VK_CONTROL) < 0) && (wParam == 'F')) {
            Guide_FocusSearch(st);
            return 0;
        }
        if (wParam == VK_ESCAPE && st && st->hWindow) {
            PostMessageW(st->hWindow, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK Guide_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* st = reinterpret_cast<GuideState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* cfg = reinterpret_cast<GuideCreateParams*>(cs->lpCreateParams);
        st = new GuideState();
        st->hWindow = hwnd;
        if (cfg) {
            st->postQuitOnDestroy = cfg->postQuitOnDestroy;
            st->titleText = cfg->title ? cfg->title : L"Regex Reference Guide";
            st->labelText = cfg->label ? cfg->label : L"Containing";
            st->noteText = cfg->note ? cfg->note : L"Ctrl+F focuses the search box. Search updates as you type.";
            st->guideText = cfg->guideText ? cfg->guideText : L"";
            st->searchCueText = cfg->searchCue ? cfg->searchCue : L"Search guide text";
            st->clearTipText = cfg->clearTip ? cfg->clearTip : L"Clear search";
            st->closeText = cfg->closeText ? cfg->closeText : L"Close";
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        return TRUE;
    }
    case WM_CREATE: {
        LoadLibraryW(L"Msftedit.dll");

        HFONT hBody = Guide_CreateFont(hwnd, L"Segoe UI", 12, false);
        HFONT hTitle = Guide_CreateFont(hwnd, L"Segoe UI", 16, true);
        HFONT hCode = Guide_CreateFont(hwnd, L"Consolas", 11, false);
        st->hBodyFont = hBody;
        st->hTitleFont = hTitle;
        st->hCodeFont = hCode;
        st->closeButtonWidth = S(30);

        st->hTitle = CreateWindowExW(0, L"STATIC", st->titleText.c_str(),
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_GUIDE_TITLE, GetModuleHandleW(NULL), NULL);
        st->hLabel = CreateWindowExW(0, L"STATIC", st->labelText.c_str(),
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_GUIDE_LABEL, GetModuleHandleW(NULL), NULL);
        st->hSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_GUIDE_SEARCH, GetModuleHandleW(NULL), NULL);
        st->hButton = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, st->closeButtonWidth, 0, hwnd, (HMENU)(UINT_PTR)IDC_GUIDE_BUTTON, GetModuleHandleW(NULL), NULL);
        st->hNote = CreateWindowExW(0, L"STATIC", st->noteText.c_str(),
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_GUIDE_NOTE, GetModuleHandleW(NULL), NULL);
        st->hView = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_GUIDE_VIEW, GetModuleHandleW(NULL), NULL);

        if (st->hTitle) {
            SendMessageW(st->hTitle, WM_SETFONT, (WPARAM)hTitle, TRUE);
        }
        if (st->hLabel) {
            SendMessageW(st->hLabel, WM_SETFONT, (WPARAM)hBody, TRUE);
        }
        if (st->hSearch) {
            SendMessageW(st->hSearch, WM_SETFONT, (WPARAM)hBody, TRUE);
            SetWindowSubclass(st->hSearch, Guide_SearchSubclass, 1, (DWORD_PTR)st);
        }
        if (st->hButton) {
            SendMessageW(st->hButton, WM_SETFONT, (WPARAM)hBody, TRUE);
            SetWindowSubclass(st->hButton, Guide_ClearBtnProc, 1, (DWORD_PTR)st);
        }
        if (st->hNote) {
            SendMessageW(st->hNote, WM_SETFONT, (WPARAM)hBody, TRUE);
        }
        if (st->hView) {
            SendMessageW(st->hView, WM_SETFONT, (WPARAM)hBody, TRUE);
            SendMessageW(st->hView, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(255, 255, 255));
            SetWindowSubclass(st->hView, Guide_ViewSubclass, 1, (DWORD_PTR)st);
        }

        Guide_RebuildContent(st);
        Guide_UpdateChrome(st);
        if (st->hSearch) {
            SetFocus(st->hSearch);
        }
        return 0;
    }
    case WM_SIZE:
        Guide_Layout(st);
        return 0;
    case WM_DRAWITEM:
        if (reinterpret_cast<LPDRAWITEMSTRUCT>(lParam)->CtlID == IDC_GUIDE_BUTTON) {
            auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            Guide_DrawClearButton(dis->hwndItem, dis->hDC, dis->rcItem,
                (dis->itemState & ODS_SELECTED) != 0);
            return TRUE;
        }
        break;
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == IDC_GUIDE_SEARCH && code == EN_CHANGE) {
            Guide_UpdateSearch(st);
            return 0;
        }
        if (id == IDC_GUIDE_BUTTON && code == BN_CLICKED) {
            Guide_ClearSearch(st);
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        if (HWND(lParam) == st->hNote) {
            SetTextColor((HDC)wParam, RGB(100, 100, 100));
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_SETFOCUS:
        if (st && st->hSearch) {
            SetFocus(st->hSearch);
            SendMessageW(st->hSearch, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (st) {
            if (st->hSearch) RemoveWindowSubclass(st->hSearch, Guide_SearchSubclass, 1);
            if (st->hButton) RemoveWindowSubclass(st->hButton, Guide_ClearBtnProc, 1);
            if (st->hView) RemoveWindowSubclass(st->hView, Guide_ViewSubclass, 1);
            if (st->hView) NeHighlight_Clear(st->hView, st->searchHighlight);
            if (st->hTitleFont) DeleteObject(st->hTitleFont);
            if (st->hBodyFont) DeleteObject(st->hBodyFont);
            if (st->hCodeFont) DeleteObject(st->hCodeFont);
            bool shouldQuit = st->postQuitOnDestroy;
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (s_hGuideWindow == hwnd) {
                s_hGuideWindow = NULL;
            }
            if (shouldQuit) {
                PostQuitMessage(0);
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

HWND RegexGuide_Show(HWND parent, bool postQuitOnDestroy,
    const wchar_t* title, const wchar_t* label, const wchar_t* note,
    const wchar_t* guideText, const wchar_t* searchCue, const wchar_t* clearTip, const wchar_t* closeText)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    if (s_hGuideWindow && IsWindow(s_hGuideWindow)) {
        ShowWindow(s_hGuideWindow, SW_SHOWNORMAL);
        SetForegroundWindow(s_hGuideWindow);
        return s_hGuideWindow;
    }
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = Guide_WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"NsbRegexGuideClass";
        RegisterClassW(&wc);
        classRegistered = true;
    }

    RECT pr = {};
    if (parent && IsWindow(parent)) {
        GetWindowRect(parent, &pr);
    } else {
        pr.left = GetSystemMetrics(SM_CXSCREEN) / 2 - S(480);
        pr.top = GetSystemMetrics(SM_CYSCREEN) / 2 - S(320);
        pr.right = pr.left + S(960);
        pr.bottom = pr.top + S(640);
    }

    GuideCreateParams params = {};
    params.parent = parent;
    params.postQuitOnDestroy = postQuitOnDestroy;
    params.title = title;
    params.label = label;
    params.note = note;
    params.guideText = guideText;
    params.searchCue = searchCue;
    params.clearTip = clearTip;
    params.closeText = closeText;

    int winW = std::max(S(640), (int)(pr.right - pr.left));
    int winH = std::max(S(480), (int)(pr.bottom - pr.top));

    int x = pr.left + ((pr.right - pr.left) - winW) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - winH) / 2;
    if (x < 20) x = 20;
    if (y < 20) y = 20;

    // Own the guide by its parent so a topmost tooltip (owned by the main window)
    // can never push the guide behind the app and make it look "closed".
    HWND owner = (parent && IsWindow(parent)) ? parent : NULL;
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE, L"NsbRegexGuideClass",
        title ? title : L"Regex Reference Guide",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, winW, winH, owner, NULL, hi, &params);
    if (!hwnd) {
        return NULL;
    }
    s_hGuideWindow = hwnd;
    return hwnd;
}

void RegexGuide_Close()
{
    if (s_hGuideWindow && IsWindow(s_hGuideWindow)) {
        DestroyWindow(s_hGuideWindow);
    }
}
