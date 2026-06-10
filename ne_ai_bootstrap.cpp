#include "ne_ai_bootstrap.h"
#include "ne_profiles.h"

#include <shlobj.h>
#include <cstdio>
#include <regex>
#include <vector>

static NeAiBootstrapConfig s_aiBootstrap;

static std::wstring Na_Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, NULL, 0);
    if (n <= 1) {
        n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, NULL, 0);
        if (n <= 1) return {};
        std::wstring w(n - 1, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), n);
        return w;
    }
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static bool Na_ReadFileUtf8(const std::wstring& path, std::string& out)
{
    out.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return false; }
    rewind(f);

    out.resize((size_t)sz);
    if (sz > 0 && fread(out.data(), 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        out.clear();
        return false;
    }
    fclose(f);

    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

static bool Na_SplitJsonBool(const std::string& text, const char* key, bool& out)
{
    std::regex re(std::string("\\\"") + key + "\\\"\\s*:\\s*(true|false)", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(text, m, re) || m.size() < 2) return false;
    out = (m[1].str() == "true" || m[1].str() == "TRUE");
    return true;
}

static bool Na_SplitJsonString(const std::string& text, const char* key, std::wstring& out)
{
    std::regex re(std::string("\\\"") + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    if (!std::regex_search(text, m, re) || m.size() < 2) return false;
    out = Na_Utf8ToWide(m[1].str());
    return true;
}

static std::wstring Na_GetAiConfigPath()
{
    wchar_t appdata[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) return {};
    return std::wstring(appdata) + L"\\NSBEdit\\ai-settings.json";
}

bool NeAiBootstrap_Load(NeAiBootstrapConfig& out)
{
    out = {};
    s_aiBootstrap = {};

    if (!NeProfiles_IsInstalled())
        return false;

    std::wstring path = Na_GetAiConfigPath();
    if (path.empty())
        return false;

    std::string text;
    if (!Na_ReadFileUtf8(path, text))
        return false;

    Na_SplitJsonBool(text, "enabled", out.enabled);
    Na_SplitJsonString(text, "provider", out.provider);
    Na_SplitJsonString(text, "model", out.model);
    Na_SplitJsonString(text, "fallback", out.fallback);
    Na_SplitJsonString(text, "note", out.note);
    Na_SplitJsonString(text, "installedAt", out.installedAt);
    Na_SplitJsonString(text, "version", out.version);

    s_aiBootstrap = out;
    return true;
}

const NeAiBootstrapConfig& NeAiBootstrap_Get()
{
    return s_aiBootstrap;
}
