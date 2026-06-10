#include "ne_ai_client.h"

#include <winhttp.h>

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
