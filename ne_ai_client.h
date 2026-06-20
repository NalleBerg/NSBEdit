#pragma once

#include <windows.h>
#include <string>
#include <vector>

bool NeAiClient_IsOllamaResponsive();
bool NeAiClient_ListOllamaModels(std::vector<std::wstring>& outModels);
typedef void (*NeAiPullProgressFn)(void* context, const std::wstring& status,
	unsigned long long completed, unsigned long long total);
bool NeAiClient_PullOllamaModel(const std::wstring& model, void* context,
	NeAiPullProgressFn onProgress, std::wstring& outError);
bool NeAiClient_AskOllama(const std::wstring& model, const std::wstring& prompt,
	std::wstring& outReply, std::wstring& outError);
