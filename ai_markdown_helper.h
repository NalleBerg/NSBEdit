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
	std::wstring chunk;
};

extern std::map<HWND, CHARRANGE> s_aiAnswerCopyRanges;

void Ai_ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to);
void Ai_UnescapeModelText(std::wstring& text);
std::string Ai_WideToUtf8(const std::wstring& w);
std::wstring Ai_Utf8ToWide(const std::string& s);
void Ai_AppendRichRun(HWND hLog, const std::wstring& text, const CHARFORMAT2W* format, const CHARFORMAT2W* resetFormat = nullptr);
