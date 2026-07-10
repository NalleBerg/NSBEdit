#pragma once

#include <windows.h>
#include <richedit.h>
#include <string>

void AiCodeCopy_Clear(HWND hwndLog);
void AiCodeCopy_BeginBlock(HWND hwndLog, int headerStartChar, int headerEndChar, const std::wstring& headerText);
void AiCodeCopy_AppendCodeLine(HWND hwndLog, const std::wstring& line);
void AiCodeCopy_EndBlock(HWND hwndLog);
bool AiCodeCopy_IsOverLink(HWND hwndLog, POINT pt);
bool AiCodeCopy_HandleLink(HWND hwndLog, const ENLINK* link);
bool AiCodeCopy_HandleClick(HWND hwndLog, POINT pt);
void AiCodeCopy_Remove(HWND hwndLog);