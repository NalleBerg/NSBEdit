#pragma once

#include <windows.h>
#include <string>

bool NeAiClient_IsOllamaResponsive();
bool NeAiClient_AskOllama(const std::wstring& model, const std::wstring& prompt,
	std::wstring& outReply, std::wstring& outError);
