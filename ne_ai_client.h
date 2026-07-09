#pragma once

#include <windows.h>
#include <string>
#include <vector>

bool NeAiClient_IsOllamaResponsive();
bool NeAiClient_ListOllamaModels(std::vector<std::wstring>& outModels);
typedef void (*NeAiPullProgressFn)(void* context, const std::wstring& status,
	unsigned long long completed, unsigned long long total);
typedef void (*NeAiOllamaChunkFn)(void* context, const std::wstring& chunk);
bool NeAiClient_PullOllamaModel(const std::wstring& model, void* context,
	NeAiPullProgressFn onProgress, std::wstring& outError);
bool NeAiClient_AskOllamaStream(const std::wstring& model, const std::wstring& prompt,
	void* context, NeAiOllamaChunkFn onChunk, std::wstring& outReply, std::wstring& outError,
	int numCtx = 0);
bool NeAiClient_AskOllama(const std::wstring& model, const std::wstring& prompt,
	std::wstring& outReply, std::wstring& outError, int numCtx = 0);

// Cooperative cancellation for an in-progress Ollama request.  RequestCancel()
// makes the streaming write callback abort the current curl transfer; the worker
// thread should also check IsCancelRequested() before starting any retry.
// ResetCancel() must be called on the UI thread right before a new send begins.
void NeAiClient_RequestCancel();
void NeAiClient_ResetCancel();
bool NeAiClient_IsCancelRequested();
