#pragma once

#include <windows.h>
#include <string>
#include <vector>

bool NeAiClient_IsOllamaResponsive();
bool NeAiClient_ListOllamaModels(std::vector<std::wstring>& outModels);
bool NeAiClient_AskOllama(const std::wstring& model, const std::wstring& prompt,
	std::wstring& outReply, std::wstring& outError);
