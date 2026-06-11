#include "ne_ai_client.h"

#include <winhttp.h>

#include <string>

namespace {

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

static std::wstring Ai_Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (n <= 1) return {};
    std::wstring out((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], n);
    return out;
}

}

bool NeAiClient_IsOllamaResponsive()
{
    HINTERNET hSession = WinHttpOpen(L"NSBEdit/AI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 11434, 0);
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

bool NeAiClient_AskOllama(const std::wstring& model, const std::wstring& prompt,
    std::wstring& outReply, std::wstring& outError)
{
    outReply.clear();
    outError.clear();

    HINTERNET hSession = WinHttpOpen(L"NSBEdit/AI", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        outError = L"Could not open WinHTTP session.";
        return false;
    }

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 11434, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/generate",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hRequest) {
            std::string body = std::string("{\"model\":\"") +
                Ai_EscapeJson(Ai_WideToUtf8(model.empty() ? L"qwen2.5-coder:7b" : model)) +
                "\",\"prompt\":\"" +
                Ai_EscapeJson(Ai_WideToUtf8(prompt)) +
                "\",\"stream\":false}";

            const wchar_t* headers = L"Content-Type: application/json\r\n";
            if (WinHttpSendRequest(hRequest, headers, (DWORD)-1L,
                (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
                WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                if (WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
                    std::string response;
                    if (Ai_ReadBody(hRequest, response)) {
                        if (status == 200) {
                            std::string replyUtf8;
                            if (Ai_FindJsonStringField(response, "response", replyUtf8)) {
                                outReply = Ai_Utf8ToWide(replyUtf8);
                                ok = !outReply.empty();
                            } else {
                                outError = L"Ollama replied, but the response field was missing.";
                            }
                        } else {
                            std::string errUtf8;
                            if (Ai_FindJsonStringField(response, "error", errUtf8)) {
                                outError = Ai_Utf8ToWide(errUtf8);
                            } else {
                                outError = L"Ollama returned HTTP status ";
                                outError += std::to_wstring(status);
                                outError += L".";
                            }
                        }
                    } else {
                        outError = L"Failed to read Ollama response.";
                    }
                } else {
                    outError = L"Could not query Ollama response status.";
                }
            } else {
                outError = L"Could not send prompt to Ollama.";
            }

            WinHttpCloseHandle(hRequest);
        } else {
            outError = L"Could not open Ollama request.";
        }
        WinHttpCloseHandle(hConnect);
    } else {
        outError = L"Could not connect to Ollama on localhost.";
    }

    WinHttpCloseHandle(hSession);
    return ok;
}
