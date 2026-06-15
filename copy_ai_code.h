#pragma once

#include <windows.h>
#include <string>

void Ai_CopyTextToClipboard(HWND hwnd, const std::wstring& text);

void AiCopyCode_Clear(HWND hwndLog);
void AiCopyCode_BeginBlock(HWND hwndLog, int headerStartChar, const std::wstring& headerText, int codeStartChar);
void AiCopyCode_AppendCodeLine(HWND hwndLog, const std::wstring& line);
void AiCopyCode_EndBlock(HWND hwndLog);
bool AiCopyCode_IsHot(HWND hwndLog, POINT ptClient);
bool AiCopyCode_HandleClick(HWND hwndLog, POINT ptClient);
void AiCopyCode_HandleTimer(HWND hwndLog, UINT_PTR timerId);
void AiCopyCode_HandleDestroy(HWND hwndLog);
