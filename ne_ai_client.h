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

// Asks the local Ollama daemon whether a model can accept images, by reading the
// "capabilities" list returned from POST /api/show and testing it for "vision".
// Returns:
//    1 = the model supports images (vision)
//    0 = text-only (reachable, but no vision capability)
//   -1 = unknown (daemon unreachable / model not found / request failed)
// This performs ONE network call and does no caching; the caller is expected to
// cache the result (e.g. in the profile DB).
int NeAiClient_QueryModelVision(const std::wstring& model);

// Like NeAiClient_QueryModelVision but for Ollama CLOUD models, asking
// https://ollama.com/api/show (public; no key required). Accepts the app's
// "-cloud" routing name and retries without the suffix if needed.
//   1 = vision, 0 = text only, -1 = unknown (offline / not found).
int NeAiClient_QueryCloudModelVision(const std::wstring& model);

// Checks whether a model:tag can be pulled, by asking the Ollama registry for its
// manifest (HEAD https://registry.ollama.ai/v2/<ns>/<name>/manifests/<tag>):
//   1 = downloadable (HTTP 200)
//   0 = not found     (HTTP 404)
//  -1 = unknown       (network/TLS failure — caller should not block the pull)
// Accepts "name", "name:tag", or "namespace/name:tag"; defaults ns=library,
// tag=latest. Cloud ("-cloud") models are not in this registry.
int NeAiClient_IsModelDownloadable(const std::wstring& model);

typedef void (*NeAiPullProgressFn)(void* context, const std::wstring& status,
	unsigned long long completed, unsigned long long total);

// --- Web search (DuckDuckGo) -------------------------------------------------
// One result from a web search: page title, URL, and a short snippet.
struct NeAiWebResult {
	std::wstring title;
	std::wstring url;
	std::wstring snippet;
};
// Searches the web via DuckDuckGo's HTML endpoint and fills `out` with up to
// maxResults hits. Returns true when at least one result was parsed; on failure
// outError holds a short reason. Network call (HTTPS) — run off the UI thread or
// expect a brief block.
bool NeAiClient_WebSearch(const std::wstring& query, int maxResults,
	std::vector<NeAiWebResult>& out, std::wstring& outError);

typedef void (*NeAiOllamaChunkFn)(void* context, const std::wstring& chunk);
bool NeAiClient_PullOllamaModel(const std::wstring& model, void* context,
	NeAiPullProgressFn onProgress, std::wstring& outError);
// Deletes a locally-installed Ollama model (DELETE /api/delete). Returns true on
// HTTP 200; on failure outError holds a short reason (e.g. model not found).
bool NeAiClient_DeleteOllamaModel(const std::wstring& model, std::wstring& outError);
bool NeAiClient_AskOllamaStream(const std::wstring& model, const std::wstring& prompt,
	void* context, NeAiOllamaChunkFn onChunk, std::wstring& outReply, std::wstring& outError,
	int numCtx = 0, const std::vector<std::string>& images = std::vector<std::string>());
bool NeAiClient_AskOllama(const std::wstring& model, const std::wstring& prompt,
	std::wstring& outReply, std::wstring& outError, int numCtx = 0);

// Cooperative cancellation for an in-progress Ollama request.  RequestCancel()
// makes the streaming write callback abort the current curl transfer; the worker
// thread should also check IsCancelRequested() before starting any retry.
// ResetCancel() must be called on the UI thread right before a new send begins.
void NeAiClient_RequestCancel();
void NeAiClient_ResetCancel();
bool NeAiClient_IsCancelRequested();
