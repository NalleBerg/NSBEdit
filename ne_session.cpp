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

    // Rolling backup: before overwriting the live session, snapshot the CURRENT
    // rows into session_tabs_prev (previous-good slot). Only when the live table
    // is non-empty, so an accidental empty write can never wipe the backup.
    // CREATE ... AS SELECT auto-matches the live schema (survives column adds).
    {
        int liveRows = 0;
        sqlite3_stmt* cst = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM session_tabs", -1, &cst, nullptr) == SQLITE_OK) {
            if (sqlite3_step(cst) == SQLITE_ROW) liveRows = sqlite3_column_int(cst, 0);
            sqlite3_finalize(cst);
        }
        if (liveRows > 0) {
            sqlite3_exec(db, "DROP TABLE IF EXISTS session_tabs_prev", NULL, NULL, NULL);
            sqlite3_exec(db, "CREATE TABLE session_tabs_prev AS SELECT * FROM session_tabs", NULL, NULL, NULL);
        }
    }

    sqlite3_exec(db, "DELETE FROM session_tabs", NULL, NULL, NULL);

    const char* sql =
        "INSERT INTO session_tabs"
        " (sort_order, local_path, is_ftp, ftp_profile_id,"
        "  ftp_remote_path, ftp_friendly,"
        "  content, content_is_rtf, is_active,"
        "  disk_time_lo, disk_time_hi, disk_size, was_modified,"
        "  word_wrap, is_sci_tab, caret_pos, scroll_line, spell_lang)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";

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
        sqlite3_bind_int   (stmt, 14, t.wordWrap   ? 1 : 0);
        sqlite3_bind_int   (stmt, 15, t.isSciTab   ? 1 : 0);
        sqlite3_bind_int   (stmt, 16, t.caretPos);
        sqlite3_bind_int   (stmt, 17, t.scrollLine);
        auto sl = Nse_W2U(t.spellLang);
        sqlite3_bind_text  (stmt, 18, sl.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    return true;
}

// ── NeSession_Load ────────────────────────────────────────────────────────────
// Read all rows from the named session table into out. Returns rows appended.
static bool Nse_LoadTable(sqlite3* db, const char* table, std::vector<NeSessionTab>& out)
{
    std::string sql =
        "SELECT sort_order, local_path, is_ftp, ftp_profile_id,"
        "       ftp_remote_path, ftp_friendly,"
        "       content, content_is_rtf, is_active,"
        "       disk_time_lo, disk_time_hi, disk_size, was_modified,"
        "       word_wrap, is_sci_tab, caret_pos, scroll_line, spell_lang"
        " FROM ";
    sql += table;
    sql += " ORDER BY sort_order";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return false;   // table may not exist (e.g. first run) — treat as empty

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
        t.wordWrap     = sqlite3_column_int   (stmt, 13) != 0;
        t.isSciTab     = sqlite3_column_int   (stmt, 14) != 0;
        t.caretPos     = sqlite3_column_int   (stmt, 15);
        t.scrollLine   = sqlite3_column_int   (stmt, 16);
        const char* sl = (const char*)sqlite3_column_text(stmt, 17);
        t.spellLang    = sl ? Nse_U2W(sl) : L"";
        out.push_back(std::move(t));
    }
    sqlite3_finalize(stmt);
    return true;
}

bool NeSession_Load(std::vector<NeSessionTab>& out)
{
    out.clear();
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;

    Nse_LoadTable(db, "session_tabs", out);
    // Rolling-backup fallback: if the live slot is empty (e.g. a bad/empty write),
    // recover the previous-good slot so a session is never silently lost.
    if (out.empty())
        Nse_LoadTable(db, "session_tabs_prev", out);
    return true;
}

// ── NeSession_HasData ─────────────────────────────────────────────────────────
bool NeSession_HasData()
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;

    // Data in EITHER the live slot or the previous-good backup counts.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT (SELECT COUNT(*) FROM session_tabs) + "
            "(SELECT COUNT(*) FROM session_tabs_prev WHERE 1)",
            -1, &stmt, nullptr) != SQLITE_OK) {
        // session_tabs_prev may not exist yet — fall back to the live table only.
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM session_tabs",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return false;
    }

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

// ── Recently closed tabs ──────────────────────────────────────────────────────
static void Nse_EnsureClosedTable(sqlite3* db)
{
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS closed_tabs ("
        "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sort_order      INTEGER NOT NULL DEFAULT 0,"
        "  local_path      TEXT    NOT NULL DEFAULT '',"
        "  is_ftp          INTEGER NOT NULL DEFAULT 0,"
        "  ftp_profile_id  INTEGER NOT NULL DEFAULT -1,"
        "  ftp_remote_path TEXT    NOT NULL DEFAULT '',"
        "  ftp_friendly    TEXT    NOT NULL DEFAULT '',"
        "  content         BLOB,"
        "  content_is_rtf  INTEGER NOT NULL DEFAULT 0,"
        "  is_active       INTEGER NOT NULL DEFAULT 0,"
        "  disk_time_lo    INTEGER NOT NULL DEFAULT 0,"
        "  disk_time_hi    INTEGER NOT NULL DEFAULT 0,"
        "  disk_size       INTEGER NOT NULL DEFAULT 0,"
        "  was_modified    INTEGER NOT NULL DEFAULT 0,"
        "  word_wrap       INTEGER NOT NULL DEFAULT 1,"
        "  is_sci_tab      INTEGER NOT NULL DEFAULT 0,"
        "  caret_pos       INTEGER NOT NULL DEFAULT 0,"
        "  scroll_line     INTEGER NOT NULL DEFAULT 0,"
        "  spell_lang      TEXT    NOT NULL DEFAULT ''"
        ")", NULL, NULL, NULL);
}

bool NeSession_PushClosedTab(const NeSessionTab& t)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    Nse_EnsureClosedTable(db);

    const char* sql =
        "INSERT INTO closed_tabs"
        " (sort_order, local_path, is_ftp, ftp_profile_id,"
        "  ftp_remote_path, ftp_friendly,"
        "  content, content_is_rtf, is_active,"
        "  disk_time_lo, disk_time_hi, disk_size, was_modified,"
        "  word_wrap, is_sci_tab, caret_pos, scroll_line, spell_lang)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
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
    sqlite3_bind_int   (stmt, 14, t.wordWrap   ? 1 : 0);
    sqlite3_bind_int   (stmt, 15, t.isSciTab   ? 1 : 0);
    sqlite3_bind_int   (stmt, 16, t.caretPos);
    sqlite3_bind_int   (stmt, 17, t.scrollLine);
    auto sl = Nse_W2U(t.spellLang);
    sqlite3_bind_text  (stmt, 18, sl.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    // Keep only the newest 5.
    sqlite3_exec(db,
        "DELETE FROM closed_tabs WHERE id NOT IN "
        "(SELECT id FROM closed_tabs ORDER BY id DESC LIMIT 5)",
        NULL, NULL, NULL);
    return ok;
}

bool NeSession_PopClosedTab(NeSessionTab& out)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT id, sort_order, local_path, is_ftp, ftp_profile_id,"
        "       ftp_remote_path, ftp_friendly,"
        "       content, content_is_rtf, is_active,"
        "       disk_time_lo, disk_time_hi, disk_size, was_modified,"
        "       word_wrap, is_sci_tab, caret_pos, scroll_line, spell_lang"
        " FROM closed_tabs ORDER BY id DESC LIMIT 1", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    bool found = false;
    int64_t rowId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        rowId          = sqlite3_column_int64 (stmt, 0);
        out.sortOrder  = sqlite3_column_int   (stmt, 1);
        const char* lp = (const char*)sqlite3_column_text(stmt, 2);
        out.localPath  = lp ? Nse_U2W(lp) : L"";
        out.isFtp      = sqlite3_column_int   (stmt, 3) != 0;
        out.ftpProfileId = sqlite3_column_int64(stmt, 4);
        const char* rp = (const char*)sqlite3_column_text(stmt, 5);
        out.ftpRemotePath = rp ? Nse_U2W(rp) : L"";
        const char* fn = (const char*)sqlite3_column_text(stmt, 6);
        out.ftpFriendly = fn ? Nse_U2W(fn) : L"";
        const void* blob = sqlite3_column_blob (stmt, 7);
        int blen         = sqlite3_column_bytes(stmt, 7);
        out.content.clear();
        if (blob && blen > 0)
            out.content.assign((const uint8_t*)blob, (const uint8_t*)blob + blen);
        out.contentIsRtf = sqlite3_column_int (stmt, 8) != 0;
        out.isActive     = sqlite3_column_int (stmt, 9) != 0;
        out.diskTimeLo   = (DWORD)sqlite3_column_int64(stmt, 10);
        out.diskTimeHi   = (DWORD)sqlite3_column_int64(stmt, 11);
        out.diskSize     = sqlite3_column_int64(stmt, 12);
        out.wasModified  = sqlite3_column_int (stmt, 13) != 0;
        out.wordWrap     = sqlite3_column_int (stmt, 14) != 0;
        out.isSciTab     = sqlite3_column_int (stmt, 15) != 0;
        out.caretPos     = sqlite3_column_int (stmt, 16);
        out.scrollLine   = sqlite3_column_int (stmt, 17);
        const char* sl   = (const char*)sqlite3_column_text(stmt, 18);
        out.spellLang    = sl ? Nse_U2W(sl) : L"";
        found = true;
    }
    sqlite3_finalize(stmt);
    if (found) {
        sqlite3_stmt* del = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM closed_tabs WHERE id=?", -1, &del, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(del, 1, rowId);
            sqlite3_step(del);
            sqlite3_finalize(del);
        }
    }
    return found;
}

bool NeSession_HasClosedTab()
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM closed_tabs", -1, &stmt, nullptr) != SQLITE_OK)
        return false;   // table not created yet → none
    bool has = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) has = sqlite3_column_int(stmt, 0) > 0;
    sqlite3_finalize(stmt);
    return has;
}
