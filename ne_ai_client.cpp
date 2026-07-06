#include "ne_ai_client.h"

#include <winhttp.h>
#include "curl/include/curl/curl.h"

#include <regex>
#include <string>
#include <vector>

namespace {

static constexpr const wchar_t* kOllamaHost = L"localhost";

struct OllamaStreamState {
    void* context = nullptr;
    NeAiOllamaChunkFn onChunk = nullptr;
    std::string pending;
    std::wstring outReply;
    std::wstring outError;
    bool ok = false;
};

static std::string Ai_WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 1) return {};
    std::string out((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &out[0], n, NULL, NULL);
    return out;
}

static std::string Ai_EscapeJson(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (unsigned char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                char buf[7] = {};
                sprintf_s(buf, "\\u%04x", (unsigned int)ch);
                out += buf;
            } else {
                out += (char)ch;
            }
            break;
        }
    }
    return out;
}

static bool Ai_ReadBody(HINTERNET hRequest, std::string& outBody)
{
    outBody.clear();
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
        std::string chunk;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), available, &read) || read == 0) {
            return false;
        }
        chunk.resize(read);
        outBody += chunk;
    }
    return true;
}

static std::string Ai_TrimJsonLine(const std::string& line);
static bool Ai_FindJsonStringField(const std::string& json, const char* field, std::string& outValue);
static bool Ai_FindJsonNumberField(const std::string& json, const char* field, unsigned long long& outValue);
static bool Ai_FindJsonBoolField(const std::string& json, const char* field, bool& outValue);
static std::wstring Ai_Utf8ToWide(const std::string& s);

static size_t Ai_OllamaWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* state = (OllamaStreamState*)userdata;
    if (!state || !ptr) return 0;

    size_t total = size * nmemb;
    if (total == 0) return 0;

    state->pending.append(ptr, total);

    size_t linePos = 0;
    while ((linePos = state->pending.find('\n')) != std::string::npos) {
        std::string rawLine = state->pending.substr(0, linePos);
        state->pending.erase(0, linePos + 1);
        std::string line = Ai_TrimJsonLine(rawLine);
        if (line.empty()) continue;

        std::string replyUtf8;
        if (Ai_FindJsonStringField(line, "response", replyUtf8)) {
            std::wstring wideChunk = Ai_Utf8ToWide(replyUtf8);
            state->outReply += wideChunk;
            if (state->onChunk && !wideChunk.empty()) {
                state->onChunk(state->context, wideChunk);
            }
        }

        std::string thinkingUtf8;
        if (Ai_FindJsonStringField(line, "thinking", thinkingUtf8)) {
            std::wstring wideChunk = Ai_Utf8ToWide(thinkingUtf8);
            state->outReply += wideChunk;
            if (state->onChunk && !wideChunk.empty()) {
                state->onChunk(state->context, wideChunk);
            }
        }

        bool done = false;
        if (Ai_FindJsonBoolField(line, "done", done) && done) {
            state->ok = true;
            break;
        }

        std::string errUtf8;
        if (Ai_FindJsonStringField(line, "error", errUtf8)) {
            state->outError = Ai_Utf8ToWide(errUtf8);
            break;
        }
    }

    return total;
}

static bool Ai_FindJsonStringField(const std::string& json, const char* field, std::string& outValue)
{
    outValue.clear();
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    ++pos;
    bool escape = false;
    for (; pos < json.size(); ++pos) {
        char ch = json[pos];
        if (escape) {
            switch (ch) {
            case '"': outValue += '"'; break;
            case '\\': outValue += '\\'; break;
            case '/': outValue += '/'; break;
            case 'b': outValue += '\b'; break;
            case 'f': outValue += '\f'; break;
            case 'n': outValue += '\n'; break;
            case 'r': outValue += '\r'; break;
            case 't': outValue += '\t'; break;
            default: outValue += ch; break;
            }
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (ch == '"') return true;
        outValue += ch;
    }
    outValue.clear();
    return false;
}

static bool Ai_FindJsonNumberField(const std::string& json, const char* field, unsigned long long& outValue)
{
    outValue = 0;
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && isspace((unsigned char)json[pos])) ++pos;
    size_t start = pos;
    while (pos < json.size() && isdigit((unsigned char)json[pos])) ++pos;
    if (pos <= start) return false;
    outValue = _strtoui64(json.substr(start, pos - start).c_str(), NULL, 10);
    return true;
}

static bool Ai_FindJsonBoolField(const std::string& json, const char* field, bool& outValue)
{
    outValue = false;
    std::string needle = std::string("\"") + field + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && isspace((unsigned char)json[pos])) ++pos;
    if (json.compare(pos, 4, "true") == 0) {
        outValue = true;
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        outValue = false;
        return true;
    }
    return false;
}

static std::string Ai_TrimJsonLine(const std::string& line)
{
    size_t start = 0;
    while (start < line.size() && (line[start] == '\r' || line[start] == '\n' || isspace((unsigned char)line[start]))) ++start;
    size_t end = line.size();
    while (end > start && (line[end - 1] == '\r' || line[end - 1] == '\n' || isspace((unsigned char)line[end - 1]))) --end;
    return line.substr(start, end - start);
}

static std::wstring Ai_Utf8ToWide(const std::string& s);

static bool Ai_FindJsonArrayStringFields(const std::string& json, const char* field, std::vector<std::wstring>& outValues)
{
    outValues.clear();
    std::regex re(std::string("\\\"") + field + "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    auto begin = std::sregex_iterator(json.begin(), json.end(), re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        if (it->size() >= 2) {
            outValues.push_back(Ai_Utf8ToWide((*it)[1].str()));
        }
    }
    return !outValues.empty();
}

static std::wstring Ai_Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (n <= 1) return {};
    std::wstring out((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    return out;
}

static std::wstring Ai_FormatWinHttpErrorAt(const wchar_t* prefix, const wchar_t* stage)
{
    DWORD error = GetLastError();
    std::wstring message = prefix ? prefix : L"WinHTTP error";
    if (stage && *stage) {
        message += L" (";
        message += stage;
        message += L")";
    }
    message += L" (";
    message += std::to_wstring(error);
    message += L")";
    return message;
}

}

bool NeAiClient_IsOllamaResponsive()
{
    HINTERNET hSession = WinHttpOpen(L"NSBEdit/AI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, kOllamaHost, 11434, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/api/tags",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest,
                WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                if (WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) &&
                    status == 200) {
                    ok = true;
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }

    WinHttpCloseHandle(hSession);
    return ok;
}

bool NeAiClient_ListOllamaModels(std::vector<std::wstring>& outModels)
{
    outModels.clear();

    HINTERNET hSession = WinHttpOpen(L"NSBEdit/AI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return false;
    }

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, kOllamaHost, 11434, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/api/tags",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest,
                WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                if (WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) &&
                    status == 200) {
                    std::string response;
                    if (Ai_ReadBody(hRequest, response)) {
                        std::vector<std::wstring> models;
                        if (Ai_FindJsonArrayStringFields(response, "name", models)) {
                            outModels = std::move(models);
                        }
                        ok = true;
                    }
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }

    WinHttpCloseHandle(hSession);
    return ok;
}

bool NeAiClient_PullOllamaModel(const std::wstring& model, void* context,
    NeAiPullProgressFn onProgress, std::wstring& outError)
{
    outError.clear();

    HINTERNET hSession = WinHttpOpen(L"NSBEdit/AI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        outError = L"Could not open WinHTTP session.";
        return false;
    }

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, kOllamaHost, 11434, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/pull",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            std::string body = std::string("{\"model\":\"") +
                Ai_EscapeJson(Ai_WideToUtf8(model)) +
                "\",\"stream\":true}";

            const wchar_t* headers = L"Content-Type: application/json\r\n";
            if (!WinHttpSendRequest(hRequest, headers, (DWORD)-1L,
                (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0)) {
                outError = Ai_FormatWinHttpErrorAt(L"Could not send prompt to Ollama", L"send request");
            } else if (!WinHttpReceiveResponse(hRequest, NULL)) {
                outError = Ai_FormatWinHttpErrorAt(L"Could not send prompt to Ollama", L"receive response");
            } else {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                if (WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
                    if (status == 200) {
                        DWORD available = 0;
                        std::string pending;
                        while (true) {
                            if (!WinHttpQueryDataAvailable(hRequest, &available)) {
                                outError = Ai_FormatWinHttpErrorAt(L"Could not read Ollama response", L"query available");
                                break;
                            }
                            if (available == 0) {
                                break;
                            }
                            std::string chunk;
                            chunk.resize(available);
                            DWORD read = 0;
                            if (!WinHttpReadData(hRequest, chunk.data(), available, &read) || read == 0) {
                                outError = Ai_FormatWinHttpErrorAt(L"Failed to read Ollama response", L"read data");
                                break;
                            }
                            chunk.resize(read);
                            pending += chunk;

                            size_t linePos = 0;
                            while ((linePos = pending.find('\n')) != std::string::npos) {
                                std::string rawLine = pending.substr(0, linePos);
                                pending.erase(0, linePos + 1);
                                std::string line = Ai_TrimJsonLine(rawLine);
                                if (line.empty()) continue;

                                std::string statusUtf8;
                                unsigned long long completed = 0;
                                unsigned long long total = 0;
                                if (Ai_FindJsonStringField(line, "status", statusUtf8)) {
                                    Ai_FindJsonNumberField(line, "completed", completed);
                                    Ai_FindJsonNumberField(line, "total", total);
                                    if (onProgress) {
                                        onProgress(context, Ai_Utf8ToWide(statusUtf8), completed, total);
                                    }
                                    if (statusUtf8 == "success") {
                                        ok = true;
                                    } else if (statusUtf8 == "error") {
                                        std::string errUtf8;
                                        if (Ai_FindJsonStringField(line, "error", errUtf8)) {
                                            outError = Ai_Utf8ToWide(errUtf8);
                                        } else {
                                            outError = L"Ollama returned an error while pulling the model.";
                                        }
                                        break;
                                    }
                                }
                            }
                            if (!outError.empty()) break;
                        }
                        if (outError.empty() && !pending.empty()) {
                            std::string line = Ai_TrimJsonLine(pending);
                            if (!line.empty()) {
                                std::string statusUtf8;
                                unsigned long long completed = 0;
                                unsigned long long total = 0;
                                if (Ai_FindJsonStringField(line, "status", statusUtf8)) {
                                    Ai_FindJsonNumberField(line, "completed", completed);
                                    Ai_FindJsonNumberField(line, "total", total);
                                    if (onProgress) {
                                        onProgress(context, Ai_Utf8ToWide(statusUtf8), completed, total);
                                    }
                                    if (statusUtf8 == "success") {
                                        ok = true;
                                    } else if (statusUtf8 == "error") {
                                        std::string errUtf8;
                                        if (Ai_FindJsonStringField(line, "error", errUtf8)) {
                                            outError = Ai_Utf8ToWide(errUtf8);
                                        } else {
                                            outError = L"Ollama returned an error while pulling the model.";
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        std::string response;
                        if (Ai_ReadBody(hRequest, response)) {
                            std::string errUtf8;
                            if (Ai_FindJsonStringField(response, "error", errUtf8)) {
                                outError = Ai_Utf8ToWide(errUtf8);
                            } else {
                                outError = L"Ollama returned HTTP status ";
                                outError += std::to_wstring(status);
                                outError += L".";
                            }
                        } else {
                            outError = L"Could not read Ollama error response.";
                        }
                    }
                } else {
                    outError = L"Could not query Ollama response status.";
                }
            }

            WinHttpCloseHandle(hRequest);
        } else {
            outError = L"Could not open Ollama pull request.";
        }
        WinHttpCloseHandle(hConnect);
    } else {
        outError = L"Could not connect to Ollama on localhost.";
    }

    WinHttpCloseHandle(hSession);
    return ok && outError.empty();
}

bool NeAiClient_AskOllamaStream(const std::wstring& model, const std::wstring& prompt,
    void* context, NeAiOllamaChunkFn onChunk, std::wstring& outReply, std::wstring& outError)
{
    outReply.clear();
    outError.clear();

    bool ok = false;
    CURL* curl = curl_easy_init();
    if (!curl) {
        outError = L"Could not initialize curl.";
        return false;
    }

    OllamaStreamState state = {};
    state.context = context;
    state.onChunk = onChunk;

    std::string body = std::string("{\"model\":\"") +
        Ai_EscapeJson(Ai_WideToUtf8(model.empty() ? L"qwen3-coder" : model)) +
        "\",\"prompt\":\"" +
        Ai_EscapeJson(Ai_WideToUtf8(prompt)) +
        "\",\"stream\":true,\"think\":false,\"keep_alive\":\"30m\""
        ",\"options\":{\"num_ctx\":12288}}";

    std::string responseBody;
    char errbuf[CURL_ERROR_SIZE] = {};

    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:11434/api/generate");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, (struct curl_slist*)NULL);
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Ai_OllamaWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (status == 200) {
            if (!state.ok && state.outError.empty() && !state.pending.empty()) {
                std::string line = Ai_TrimJsonLine(state.pending);
                if (!line.empty()) {
                    std::string replyUtf8;
                    if (Ai_FindJsonStringField(line, "response", replyUtf8)) {
                        std::wstring wideChunk = Ai_Utf8ToWide(replyUtf8);
                        state.outReply += wideChunk;
                        if (state.onChunk && !wideChunk.empty()) {
                            state.onChunk(state.context, wideChunk);
                        }
                    }

                    std::string thinkingUtf8;
                    if (Ai_FindJsonStringField(line, "thinking", thinkingUtf8)) {
                        std::wstring wideChunk = Ai_Utf8ToWide(thinkingUtf8);
                        state.outReply += wideChunk;
                        if (state.onChunk && !wideChunk.empty()) {
                            state.onChunk(state.context, wideChunk);
                        }
                    }

                    bool done = false;
                    if (Ai_FindJsonBoolField(line, "done", done) && done) {
                        state.ok = true;
                    } else {
                        std::string errUtf8;
                        if (Ai_FindJsonStringField(line, "error", errUtf8)) {
                            state.outError = Ai_Utf8ToWide(errUtf8);
                        }
                    }
                }
            }
            ok = state.ok;
            outReply = std::move(state.outReply);
            outError = std::move(state.outError);
        } else {
            outError = L"Ollama returned HTTP status ";
            outError += std::to_wstring(status);
            outError += L".";
        }
    } else {
        outError = errbuf[0] ? Ai_Utf8ToWide(errbuf) : Ai_FormatWinHttpErrorAt(L"Could not read Ollama response", L"curl perform");
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok && outError.empty();
}

bool NeAiClient_AskOllama(const std::wstring& model, const std::wstring& prompt,
    std::wstring& outReply, std::wstring& outError)
{
    return NeAiClient_AskOllamaStream(model, prompt, NULL, NULL, outReply, outError);
}
