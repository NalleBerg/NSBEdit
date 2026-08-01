#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

// FTP/SFTP connection profile.
struct NeProfile {
    int64_t      id              = 0;
    std::wstring friendlyName;
    std::wstring protocol;          // L"FTP" or L"SFTP"
    std::wstring host;
    int          port             = 21;
    std::wstring username;
    std::wstring password;          // decrypted in memory; empty if !rememberPassword
    bool         rememberPassword  = false;
    std::wstring initialPath;       // e.g. L"/"
    std::wstring webUrl;             // web root, e.g. L"http://mysite.com" (no trailing slash)
};

// Call NeProfiles_Init() after NeCrypto_Init().
// DB lookup order: AppData shared db first when present
//                  portable folder db next if AppData is unavailable
//                  :memory:            -> fallback when no writable DB path
bool NeProfiles_Init();
bool NeProfiles_IsMemory(); // true when running on an in-memory DB
void NeProfiles_Close();

bool NeProfiles_Add   (NeProfile& p);           // sets p.id on success
bool NeProfiles_Update(const NeProfile& p);
bool NeProfiles_Delete(int64_t id);
bool NeProfiles_List  (std::vector<NeProfile>& out);
bool NeProfiles_GetById(int64_t id, NeProfile& out);

// Generic integer settings (key/value store in DB).
bool NeProfiles_GetIntSetting(const char* key, int defaultValue, int& out);
bool NeProfiles_SetIntSetting(const char* key, int value);

// Generic string settings (key/value store in DB).
bool NeProfiles_GetStrSetting(const char* key, const std::string& defaultValue, std::string& out);
bool NeProfiles_SetStrSetting(const char* key, const std::string& value);

// Encrypted string settings (key/value store in DB).  The value is encrypted
// with NeCrypto (AES-256-CBC, DPAPI-wrapped key) before it touches the DB, so
// secrets such as API keys are never stored in plaintext.  Requires
// NeCrypto_Init() to have succeeded; falls back to leaving `out` empty.
bool NeProfiles_GetSecretSetting(const char* key, std::wstring& out);
bool NeProfiles_SetSecretSetting(const char* key, const std::wstring& value);

// ── AI answer history (conversation persistence for the AI window) ────────────
// One stored conversation turn: the user's prompt and the raw markdown reply.
struct NeAiAnswerRow { std::wstring prompt; std::wstring replyMd; };
bool NeProfiles_AiAnswersAppend(const std::wstring& prompt, const std::wstring& replyMd);
bool NeProfiles_AiAnswersLoad(std::vector<NeAiAnswerRow>& out);
bool NeProfiles_AiAnswersClear();

// True when running from the installed Program Files layout.
// False for portable mode (db next to exe) or in-memory fallback.
bool NeProfiles_IsInstalled();

// Expose the underlying sqlite3 handle.  Used only by ne_session.cpp.
// Returns NULL if the database is not open.
struct sqlite3;
sqlite3* NeProfiles_GetDb();
