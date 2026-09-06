#include "ne_licenses.h"
#include "ne_profiles.h"
#include "sqlite3/sqlite3.h"

// ── String helpers ────────────────────────────────────────────────────────────
static std::string Lic_W2U(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, NULL, NULL);
    return s;
}

static std::wstring Lic_U2W(const char* u, int bytes)
{
    if (!u || bytes <= 0) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u, bytes, NULL, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u, bytes, w.data(), n);
    return w;
}

// Read an embedded RCDATA text resource as UTF-8 (strips a BOM if present).
static std::wstring Lic_LoadResource(int resId)
{
    HMODULE hMod = GetModuleHandleW(NULL);
    HRSRC hRes = FindResourceW(hMod, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return {};
    HGLOBAL hData = LoadResource(hMod, hRes);
    if (!hData) return {};
    DWORD sz = SizeofResource(hMod, hRes);
    const char* p = (const char*)LockResource(hData);
    if (!p || sz == 0) return {};
    size_t off = 0;
    if (sz >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF) off = 3;
    return Lic_U2W(p + off, (int)(sz - off));
}

// Seed catalogue: id (0..18), RCDATA resource id, display name.
struct LicSeed { int id; int resId; const wchar_t* name; };
static const LicSeed kLicenses[] = {
    {  0, 200, L"The Unlicense (Public Domain)" },
    {  1, 201, L"MIT License" },
    {  2, 202, L"Apache License 2.0" },
    {  3, 203, L"GNU General Public License v2.0" },
    {  4, 204, L"GNU General Public License v3.0" },
    {  5, 205, L"GNU Lesser General Public License v2.1" },
    {  6, 206, L"GNU Lesser General Public License v3.0" },
    {  7, 207, L"GNU Affero General Public License v3.0" },
    {  8, 208, L"BSD 2-Clause \"Simplified\" License" },
    {  9, 209, L"BSD 3-Clause \"New\" License" },
    { 10, 210, L"ISC License" },
    { 11, 211, L"Mozilla Public License 2.0" },
    { 12, 212, L"Boost Software License 1.0" },
    { 13, 213, L"European Union Public Licence 1.2" },
    { 14, 214, L"Creative Commons Zero v1.0 Universal" },
    { 15, 215, L"Creative Commons Attribution 4.0" },
    { 16, 216, L"Creative Commons Attribution-ShareAlike 4.0" },
    { 17, 217, L"Artistic License 2.0" },
    { 18, 218, L"Do What The F*ck You Want To Public License" },
};
static const int kLicenseCount = (int)(sizeof(kLicenses) / sizeof(kLicenses[0]));

// Bump when the embedded texts / names change so an existing DB reseeds.
static const int kLicenseSeedVersion = 1;

bool NeLicenses_Init()
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    const char* schema =
        "CREATE TABLE IF NOT EXISTS licenses ("
        "  id   INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL DEFAULT '',"
        "  body TEXT NOT NULL DEFAULT ''"
        ");";
    if (sqlite3_exec(db, schema, NULL, NULL, NULL) != SQLITE_OK) return false;

    int ver = 0;
    NeProfiles_GetIntSetting("licenses_seed_ver", 0, ver);
    if (ver == kLicenseSeedVersion) return true;   // already seeded at this version

    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    for (int i = 0; i < kLicenseCount; ++i) {
        std::wstring body = Lic_LoadResource(kLicenses[i].resId);
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO licenses (id,name,body) VALUES (?,?,?);",
            -1, &st, NULL) != SQLITE_OK) continue;
        sqlite3_bind_int (st, 1, kLicenses[i].id);
        sqlite3_bind_text(st, 2, Lic_W2U(kLicenses[i].name).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, Lic_W2U(body).c_str(),              -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    NeProfiles_SetIntSetting("licenses_seed_ver", kLicenseSeedVersion);
    return true;
}

bool NeLicenses_List(std::vector<NeLicenseInfo>& out)
{
    out.clear();
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id,name FROM licenses ORDER BY id ASC;",
                           -1, &st, NULL) != SQLITE_OK)
        return false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        NeLicenseInfo li;
        li.id   = sqlite3_column_int(st, 0);
        const char* nm = (const char*)sqlite3_column_text(st, 1);
        li.name = Lic_U2W(nm, nm ? (int)strlen(nm) : 0);
        out.push_back(std::move(li));
    }
    sqlite3_finalize(st);
    return true;
}

bool NeLicenses_GetBody(int id, std::wstring& out)
{
    out.clear();
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT body FROM licenses WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char* b = (const char*)sqlite3_column_text(st, 0);
        out = Lic_U2W(b, b ? (int)strlen(b) : 0);
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}
