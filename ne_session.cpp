#include "ne_session.h"
#include "ne_profiles.h"
#include "sqlite3/sqlite3.h"
#include <string>
#include <vector>

// ── String helpers ────────────────────────────────────────────────────────────
static std::string Nse_W2U(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, NULL, NULL);
    return s;
}

static std::wstring Nse_U2W(const char* u)
{
    if (!u || !*u) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, NULL, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u, -1, w.data(), n);
    return w;
}

// ── NeSession_Save ────────────────────────────────────────────────────────────
bool NeSession_Save(const std::vector<NeSessionTab>& tabs)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;

    char* err = nullptr;
    if (sqlite3_exec(db, "BEGIN", NULL, NULL, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return false;
    }

    sqlite3_exec(db, "DELETE FROM session_tabs", NULL, NULL, NULL);

    const char* sql =
        "INSERT INTO session_tabs"
        " (sort_order, local_path, is_ftp, ftp_profile_id,"
        "  ftp_remote_path, ftp_friendly,"
        "  content, content_is_rtf, is_active,"
        "  disk_time_lo, disk_time_hi, disk_size, was_modified)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return false;
    }

    for (const auto& t : tabs) {
        sqlite3_reset(stmt);
        sqlite3_bind_int   (stmt,  1, t.sortOrder);
        auto lp = Nse_W2U(t.localPath);
        sqlite3_bind_text  (stmt,  2, lp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int   (stmt,  3, t.isFtp ? 1 : 0);
        sqlite3_bind_int64 (stmt,  4, t.ftpProfileId);
        auto rp = Nse_W2U(t.ftpRemotePath);
        sqlite3_bind_text  (stmt,  5, rp.c_str(), -1, SQLITE_TRANSIENT);
        auto fn = Nse_W2U(t.ftpFriendly);
        sqlite3_bind_text  (stmt,  6, fn.c_str(), -1, SQLITE_TRANSIENT);
        if (!t.content.empty())
            sqlite3_bind_blob(stmt, 7, t.content.data(), (int)t.content.size(), SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 7);
        sqlite3_bind_int   (stmt,  8, t.contentIsRtf ? 1 : 0);
        sqlite3_bind_int   (stmt,  9, t.isActive ? 1 : 0);
        sqlite3_bind_int64 (stmt, 10, (int64_t)t.diskTimeLo);
        sqlite3_bind_int64 (stmt, 11, (int64_t)t.diskTimeHi);
        sqlite3_bind_int64 (stmt, 12, t.diskSize);
        sqlite3_bind_int   (stmt, 13, t.wasModified ? 1 : 0);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    return true;
}

// ── NeSession_Load ────────────────────────────────────────────────────────────
bool NeSession_Load(std::vector<NeSessionTab>& out)
{
    out.clear();
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;

    const char* sql =
        "SELECT sort_order, local_path, is_ftp, ftp_profile_id,"
        "       ftp_remote_path, ftp_friendly,"
        "       content, content_is_rtf, is_active,"
        "       disk_time_lo, disk_time_hi, disk_size, was_modified"
        " FROM session_tabs ORDER BY sort_order";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NeSessionTab t;
        t.sortOrder    = sqlite3_column_int   (stmt, 0);
        const char* lp = (const char*)sqlite3_column_text(stmt, 1);
        t.localPath    = lp ? Nse_U2W(lp) : L"";
        t.isFtp        = sqlite3_column_int   (stmt, 2) != 0;
        t.ftpProfileId = sqlite3_column_int64 (stmt, 3);
        const char* rp = (const char*)sqlite3_column_text(stmt, 4);
        t.ftpRemotePath = rp ? Nse_U2W(rp) : L"";
        const char* fn  = (const char*)sqlite3_column_text(stmt, 5);
        t.ftpFriendly   = fn ? Nse_U2W(fn) : L"";
        const void* blob = sqlite3_column_blob (stmt, 6);
        int blen         = sqlite3_column_bytes(stmt, 6);
        if (blob && blen > 0)
            t.content.assign((const uint8_t*)blob, (const uint8_t*)blob + blen);
        t.contentIsRtf = sqlite3_column_int   (stmt,  7) != 0;
        t.isActive     = sqlite3_column_int   (stmt,  8) != 0;
        t.diskTimeLo   = (DWORD)sqlite3_column_int64(stmt, 9);
        t.diskTimeHi   = (DWORD)sqlite3_column_int64(stmt, 10);
        t.diskSize     = sqlite3_column_int64 (stmt, 11);
        t.wasModified  = sqlite3_column_int   (stmt, 12) != 0;
        out.push_back(std::move(t));
    }

    sqlite3_finalize(stmt);
    return true;
}

// ── NeSession_HasData ─────────────────────────────────────────────────────────
bool NeSession_HasData()
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM session_tabs",
                           -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    bool has = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        has = sqlite3_column_int(stmt, 0) > 0;

    sqlite3_finalize(stmt);
    return has;
}

// ── NeSession_Clear ───────────────────────────────────────────────────────────
bool NeSession_Clear()
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    return sqlite3_exec(db, "DELETE FROM session_tabs",
                        NULL, NULL, NULL) == SQLITE_OK;
}
