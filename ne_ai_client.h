#pragma once

#include <windows.h>
#include <string>
#include <vector>

bool NeAiClient_IsOllamaResponsive();

// Queries the local Ollama daemon for the real cloud sign-in state.  Ollama
// tracks sign-in via `ollama signin` / `ollama signout`; the daemon answers a
// POST to /api/me with HTTP 200 when signed in and HTTP 401 when signed out.
// Returns:
//   1  = signed in       (daemon confirmed an active account)
//   0  = signed out       (daemon reachable but no account)
//  -1  = unknown          (daemon unreachable / request failed — caller should
//                          keep whatever state it already had)
// When the state is 0 (signed out) and outSigninUrl is non-null, it receives
// the ollama.com/connect URL the daemon returned so the caller can complete
// authorization of this machine's key.
int NeAiClient_QueryOllamaSignIn(std::wstring* outSigninUrl = nullptr);

// Signs the local Ollama daemon out of ollama.com (POST /api/signout).
// Returns true when the daemon confirms the sign-out (HTTP 200).
bool NeAiClient_OllamaSignout();

// --- Ollama cloud via API key (browser-free sign-in) ---------------------
// The key is entered by the user in the app and stored ONLY in their local
// profile (never in the repo or the distributed package).  When a key is set,
// "-cloud" model requests are sent directly to https://ollama.com/api with an
// Authorization: Bearer header instead of the local daemon, so no `ollama
// signin` / browser authorization step is needed.
void NeAiClient_SetCloudApiKey(const std::wstring& key);

// Validates an API key against the cloud (GET https://ollama.com/api/tags with
// the bearer token).  Returns true only on HTTP 200.
bool NeAiClient_ValidateCloudApiKey(const std::wstring& key);

// Lists the models currently available via Ollama Cloud (GET
// https://ollama.com/api/tags with the stored API key). The returned list is
// authoritative: retired models are absent and newly-released ones are present.
// Returns false when no API key is set or the request fails.
bool NeAiClient_ListCloudModels(std::vector<std::wstring>& outModels);

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
