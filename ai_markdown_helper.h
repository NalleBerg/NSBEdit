#pragma once

#include <windows.h>
#include <richedit.h>

#include <map>
#include <string>

#define IDC_AI_LOG 1952

struct AiStreamChunk {
	HWND hwnd = NULL;
	int answerStart = 0;
	bool streamed = false;
	std::wstring pendingText;
	bool inCodeBlock = false;
	std::wstring chunk;
};

extern std::map<HWND, CHARRANGE> s_aiAnswerCopyRanges;

void Ai_ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to);
void Ai_UnescapeModelText(std::wstring& text);
COLORREF Ai_ParseHtmlColorValue(const std::wstring& value);
std::wstring Ai_GetHtmlAttributeValue(const std::wstring& tag, const std::wstring& attrName);
bool Ai_TryParseStandaloneColorLine(const std::wstring& line, COLORREF& color);
COLORREF Ai_FindReplyDefaultColor(const std::wstring& text);
std::string Ai_WideToUtf8(const std::wstring& w);
std::wstring Ai_Utf8ToWide(const std::string& s);
std::wstring Ai_ExtractFenceLanguage(const std::wstring& info);
void Ai_AppendRichRun(HWND hLog, const std::wstring& text, const CHARFORMAT2W* format, const CHARFORMAT2W* resetFormat = nullptr);
void Ai_AppendStyledRun(HWND hLog, const std::wstring& text, COLORREF color, bool bold, bool italic, bool underline, bool strike);
void Ai_AppendCodeRun(HWND hLog, const std::wstring& text, COLORREF color);
void Ai_AppendMarkupLine(HWND hLog, const std::wstring& line, COLORREF baseColor);
void Ai_AppendLiveChunkText(HWND hLog, const std::wstring& text, bool codeBlock, COLORREF codeColor = RGB(0, 0, 0));
void Ai_FlushLiveChunk(struct AiStreamChunk& chunkInfo, HWND hLog, bool force = false);
void Ai_RenderMarkdownReply(HWND hwnd, int baseStart, const std::wstring& reply);
bool Ai_IsCodeRelatedPrompt(const std::wstring& prompt);