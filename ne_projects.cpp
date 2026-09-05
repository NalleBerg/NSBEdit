#include "ne_projects.h"
#include "ne_profiles.h"
#include "sqlite3/sqlite3.h"
#include <time.h>
#include <set>
#include <algorithm>
#include <cwctype>

// ── String helpers ────────────────────────────────────────────────────────────
static std::string Prj_W2U(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, NULL, NULL);
    return s;
}

static std::wstring Prj_U2W(const char* u)
{
    if (!u || !*u) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, NULL, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u, -1, w.data(), n);
    return w;
}

// Guarded ALTER TABLE ADD COLUMN: adds the column only when it is missing, so
// databases created by an older build gain the new columns while newer/fresh
// DBs (which already have them from CREATE TABLE) are left untouched.
static bool Prj_ColumnExists(sqlite3* db, const char* table, const char* col)
{
    sqlite3_stmt* st = nullptr;
    std::string sql = std::string("PRAGMA table_info(") + table + ");";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, NULL) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* name = (const char*)sqlite3_column_text(st, 1); // 1 = "name"
        if (name && _stricmp(name, col) == 0) { found = true; break; }
    }
    sqlite3_finalize(st);
    return found;
}

static void Prj_AddColumnIfMissing(sqlite3* db, const char* table,
                                   const char* col, const char* decl)
{
    if (Prj_ColumnExists(db, table, col)) return;
    std::string sql = std::string("ALTER TABLE ") + table + " ADD COLUMN " +
                      col + " " + decl + ";";
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), NULL, NULL, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err); // ignore (e.g. duplicate column race)
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────
bool NeProjects_Init()
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    // Fresh / portable DBs get the full schema here (incl. the New Project cols).
    const char* schema =
        "CREATE TABLE IF NOT EXISTS projects ("
        "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name          TEXT    NOT NULL,"
        "  root_path     TEXT    NOT NULL,"
        "  created       INTEGER NOT NULL DEFAULT 0,"
        "  modified      INTEGER NOT NULL DEFAULT 0,"
        "  type          TEXT    NOT NULL DEFAULT '',"
        "  build_command TEXT    NOT NULL DEFAULT '',"
        "  run_command   TEXT    NOT NULL DEFAULT ''"
        ");";
    char* err = nullptr;
    if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return false;
    }
    // Older DBs already have `projects` without the new columns; add them now.
    // CREATE TABLE IF NOT EXISTS never alters an existing table, so this is the
    // upgrade path for installed/USB DBs made by an earlier build.
    Prj_AddColumnIfMissing(db, "projects", "type",          "TEXT NOT NULL DEFAULT ''");
    Prj_AddColumnIfMissing(db, "projects", "build_command", "TEXT NOT NULL DEFAULT ''");
    Prj_AddColumnIfMissing(db, "projects", "run_command",   "TEXT NOT NULL DEFAULT ''");
    // Seed build_command = "makeit.bat" for any existing project that still has
    // an unset build_command AND whose root actually contains a makeit.bat (that
    // is the NSBEdit project / any makeit-style project).  NULL/empty stays unset
    // for everything else.
    {
        sqlite3_stmt* sel = nullptr;
        if (sqlite3_prepare_v2(db,
            "SELECT id,root_path FROM projects "
            "WHERE build_command IS NULL OR build_command='';",
            -1, &sel, NULL) == SQLITE_OK) {
            std::vector<int64_t> toSeed;
            while (sqlite3_step(sel) == SQLITE_ROW) {
                int64_t id  = sqlite3_column_int64(sel, 0);
                std::wstring root = Prj_U2W((const char*)sqlite3_column_text(sel, 1));
                if (root.empty()) continue;
                std::wstring bat = root;
                if (!bat.empty() && bat.back() != L'\\' && bat.back() != L'/') bat += L'\\';
                bat += L"makeit.bat";
                if (GetFileAttributesW(bat.c_str()) != INVALID_FILE_ATTRIBUTES)
                    toSeed.push_back(id);
            }
            sqlite3_finalize(sel);
            for (int64_t id : toSeed) {
                sqlite3_stmt* up = nullptr;
                if (sqlite3_prepare_v2(db,
                    "UPDATE projects SET build_command='makeit.bat' WHERE id=?;",
                    -1, &up, NULL) == SQLITE_OK) {
                    sqlite3_bind_int64(up, 1, id);
                    sqlite3_step(up);
                    sqlite3_finalize(up);
                }
            }
        }
    }
    // Generic, evolving project knowledge store (notes/info/answers the AI can
    // read in batch mode).  Kept separate from `projects` so it can grow freely.
    const char* docsSchema =
        "CREATE TABLE IF NOT EXISTS project_docs ("
        "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  project_id INTEGER NOT NULL,"
        "  kind       TEXT    NOT NULL DEFAULT 'note',"
        "  title      TEXT    NOT NULL DEFAULT '',"
        "  body       TEXT    NOT NULL DEFAULT '',"
        "  created    INTEGER NOT NULL DEFAULT 0,"
        "  modified   INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_project_docs_pid ON project_docs(project_id);";
    if (sqlite3_exec(db, docsSchema, NULL, NULL, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

// ── CRUD ──────────────────────────────────────────────────────────────────────
bool NeProjects_Add(NeProject& p)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO projects (name,root_path,created,modified) VALUES (?,?,?,?);",
        -1, &st, NULL) != SQLITE_OK)
        return false;
    int64_t now = (int64_t)time(NULL);
    sqlite3_bind_text (st, 1, Prj_W2U(p.name).c_str(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 2, Prj_W2U(p.rootPath).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, now);
    sqlite3_bind_int64(st, 4, now);
    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    if (ok) p.id = sqlite3_last_insert_rowid(db);
    return ok;
}

bool NeProjects_Delete(int64_t id)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM projects WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, id);
    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    if (ok && NeProjects_GetActiveId() == id) NeProjects_SetActiveId(0);
    return ok;
}

bool NeProjects_List(std::vector<NeProject>& out)
{
    out.clear();
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT id,name,root_path FROM projects ORDER BY name COLLATE NOCASE ASC;",
        -1, &st, NULL) != SQLITE_OK)
        return false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        NeProject p;
        p.id       = sqlite3_column_int64(st, 0);
        p.name     = Prj_U2W((const char*)sqlite3_column_text(st, 1));
        p.rootPath = Prj_U2W((const char*)sqlite3_column_text(st, 2));
        out.push_back(std::move(p));
    }
    sqlite3_finalize(st);
    return true;
}

bool NeProjects_GetById(int64_t id, NeProject& out)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT id,name,root_path FROM projects WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out.id       = sqlite3_column_int64(st, 0);
        out.name     = Prj_U2W((const char*)sqlite3_column_text(st, 1));
        out.rootPath = Prj_U2W((const char*)sqlite3_column_text(st, 2));
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}

// ── Full properties (type / build_command / run_command) ──────────────────────
bool NeProjects_GetInfo(int64_t id, NeProjectInfo& out)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT id,name,root_path,type,build_command,run_command "
        "FROM projects WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, id);
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out.id           = sqlite3_column_int64(st, 0);
        out.name         = Prj_U2W((const char*)sqlite3_column_text(st, 1));
        out.rootPath     = Prj_U2W((const char*)sqlite3_column_text(st, 2));
        out.type         = Prj_U2W((const char*)sqlite3_column_text(st, 3));
        out.buildCommand = Prj_U2W((const char*)sqlite3_column_text(st, 4));
        out.runCommand   = Prj_U2W((const char*)sqlite3_column_text(st, 5));
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}

bool NeProjects_SetInfo(const NeProjectInfo& in)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
        "UPDATE projects SET type=?,build_command=?,run_command=?,modified=? "
        "WHERE id=?;", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_text (st, 1, Prj_W2U(in.type).c_str(),         -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 2, Prj_W2U(in.buildCommand).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 3, Prj_W2U(in.runCommand).c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, (int64_t)time(NULL));
    sqlite3_bind_int64(st, 5, in.id);
    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

// ── Active selection ──────────────────────────────────────────────────────────
int64_t NeProjects_GetActiveId()
{
    int v = 0;
    NeProfiles_GetIntSetting("active_project", 0, v);
    if (v <= 0) return 0;

    // Self-heal a stale selection: if the active project's root was moved or
    // deleted, forget it so callers don't try to walk a path that's gone.
    NeProject proj;
    if (NeProjects_GetById((int64_t)v, proj)) {
        DWORD attr = proj.rootPath.empty() ? INVALID_FILE_ATTRIBUTES
                                           : GetFileAttributesW(proj.rootPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            NeProfiles_SetIntSetting("active_project", 0);
            return 0;
        }
    }
    return (int64_t)v;
}

void NeProjects_SetActiveId(int64_t id)
{
    NeProfiles_SetIntSetting("active_project", (int)id);
}

// ── Project knowledge store (evolving) ────────────────────────────────────────
bool NeProjects_AddDoc(int64_t projectId, NeProjectDoc& doc)
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db || projectId <= 0) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO project_docs (project_id,kind,title,body,created,modified) "
        "VALUES (?,?,?,?,?,?);", -1, &st, NULL) != SQLITE_OK)
        return false;
    int64_t now = (int64_t)time(NULL);
    std::wstring kind = doc.kind.empty() ? L"note" : doc.kind;
    sqlite3_bind_int64(st, 1, projectId);
    sqlite3_bind_text (st, 2, Prj_W2U(kind).c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 3, Prj_W2U(doc.title).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 4, Prj_W2U(doc.body).c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, now);
    sqlite3_bind_int64(st, 6, now);
    bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    if (ok) doc.id = sqlite3_last_insert_rowid(db);
    return ok;
}

bool NeProjects_CollectDocs(int64_t projectId, std::vector<NeProjectDoc>& out, int maxDocs)
{
    out.clear();
    sqlite3* db = NeProfiles_GetDb();
    if (!db || projectId <= 0) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT id,kind,title,body FROM project_docs WHERE project_id=? "
        "ORDER BY modified DESC, id DESC LIMIT ?;", -1, &st, NULL) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(st, 1, projectId);
    sqlite3_bind_int  (st, 2, maxDocs > 0 ? maxDocs : 100000);
    while (sqlite3_step(st) == SQLITE_ROW) {
        NeProjectDoc d;
        d.id    = sqlite3_column_int64(st, 0);
        d.kind  = Prj_U2W((const char*)sqlite3_column_text(st, 1));
        d.title = Prj_U2W((const char*)sqlite3_column_text(st, 2));
        d.body  = Prj_U2W((const char*)sqlite3_column_text(st, 3));
        out.push_back(std::move(d));
    }
    sqlite3_finalize(st);
    return true;
}


// ── File collection (AI workspace scan) ───────────────────────────────────────
static std::wstring Prj_Lower(std::wstring s)
{
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

// Returns false only for files that are almost certainly BINARY (by extension).
// Everything else is treated as candidate text, so the AI can search whatever is
// in the workspace — config files, dotfiles, .manifest, extensionless files, etc.
// A final NUL-byte content check in NeProjects_ReadTextFile rejects any binary
// that slips through with an unknown or missing extension.
static bool Prj_IsTextFile(const wchar_t* name)
{
    std::wstring low = Prj_Lower(name);

    // Known bare build files (kept explicit; they pass the block-list anyway).
    static const wchar_t* bare[] = {
        L"makefile", L"dockerfile", L"cmakelists.txt", L"rakefile", L"gemfile"
    };
    for (auto b : bare) if (low == b) return true;

    size_t dot = low.find_last_of(L'.');
    if (dot == std::wstring::npos) return true;   // no extension → treat as text
    std::wstring ext = low.substr(dot + 1);
    static const wchar_t* binExt[] = {
        // images
        L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"ico", L"cur", L"ani",
        L"tif", L"tiff", L"webp", L"psd", L"xcf", L"pbm", L"pgm", L"ppm",
        L"pnm", L"tga", L"heic", L"heif", L"jxl", L"avif", L"dds", L"svgz",
        // audio
        L"mp3", L"wav", L"ogg", L"oga", L"flac", L"aac", L"m4a", L"wma",
        L"opus", L"mid", L"midi",
        // video
        L"mp4", L"m4v", L"avi", L"mov", L"mkv", L"webm", L"wmv", L"flv",
        L"3gp", L"mpg", L"mpeg",
        // archives
        L"zip", L"gz", L"tgz", L"tar", L"7z", L"rar", L"bz2", L"xz", L"zst",
        L"lz", L"lzma", L"cab", L"arj",
        // executables / objects / libraries
        L"exe", L"dll", L"so", L"dylib", L"o", L"obj", L"a", L"lib", L"res",
        L"pdb", L"ilk", L"exp", L"bin", L"class", L"pyc", L"pyo", L"pyd",
        L"wasm", L"elf", L"ko", L"ncb", L"sdf", L"suo",
        // installers / disk images
        L"msi", L"msix", L"appx", L"deb", L"rpm", L"dmg", L"pkg",
        L"iso", L"img", L"vhd", L"vhdx",
        // fonts
        L"ttf", L"otf", L"ttc", L"woff", L"woff2", L"eot",
        // binary documents
        L"pdf", L"doc", L"docx", L"xls", L"xlsx", L"ppt", L"pptx",
        L"odt", L"ods", L"odp",
        // databases
        L"db", L"sqlite", L"sqlite3", L"mdb", L"accdb",
        // binary certificates / keystores
        L"der", L"p12", L"pfx", L"jks", L"keystore",
        // git / vcs binary
        L"pack", L"idx"
    };
    for (auto e : binExt) if (ext == e) return false;
    return true;
}

static bool Prj_SkipDir(const wchar_t* name)
{
    static const wchar_t* skip[] = {
        L".git", L".svn", L".hg", L".vs", L".vscode", L".idea", L".cache",
        L"node_modules", L"__pycache__", L"bin", L"obj", L"build", L"builds",
        L"dist", L"out", L"target", L"vendor", L"packages", L".gradle", L".next",
        // Dependency-manager caches: huge (vcpkg alone is ~100k files) and never
        // part of the developer's own source — walking them froze the AI context
        // build, which runs on the UI thread on every send.
        L"vcpkg", L"buildtrees", L"installed", L".conan", L".cargo"
    };
    std::wstring low = Prj_Lower(name);
    for (auto s : skip) if (low == s) return true;
    return false;
}

// Skip build logs, AI scratch files and similar noise so they never pollute the
// AI's project context (stale build errors otherwise mislead the model).
static bool Prj_SkipFile(const wchar_t* name)
{
    std::wstring low = Prj_Lower(name);
    static const wchar_t* exact[] = {
        L"output.txt", L"ai.txt", L"ai_attach.txt", L"here.txt", L"start.txt"
    };
    for (auto e : exact) if (low == e) return true;
    // build*.txt / build*.log / makeit*.log and any *.log
    auto ends = [&](const wchar_t* suf) {
        size_t n = wcslen(suf);
        return low.size() >= n && low.compare(low.size() - n, n, suf) == 0;
    };
    auto starts = [&](const wchar_t* pre) {
        size_t n = wcslen(pre);
        return low.size() >= n && low.compare(0, n, pre) == 0;
    };
    if (ends(L".log")) return true;
    if (starts(L"build") && ends(L".txt")) return true;
    if (starts(L"makeit") && (ends(L".txt") || ends(L".log"))) return true;
    return false;
}

// Canonical (resolved) path for a directory, lower-cased, for cycle detection
// when following symlinks / junctions.  Empty on failure.
static std::wstring Prj_Canonical(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return {};
    wchar_t buf[1024];
    DWORD n = GetFinalPathNameByHandleW(h, buf, 1024, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (n == 0 || n >= 1024) return {};
    return Prj_Lower(std::wstring(buf, n));
}

static void Prj_Walk(const std::wstring& dir, const std::wstring& rel,
                     std::vector<NeProjectFile>& out, int depth,
                     std::set<std::wstring>& visited, int maxFiles)
{
    if ((int)out.size() >= maxFiles || depth > 32) return;
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const wchar_t* nm = fd.cFileName;
        if (wcscmp(nm, L".") == 0 || wcscmp(nm, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + nm;
        std::wstring childRel = rel.empty() ? std::wstring(nm) : (rel + L"/" + nm);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (Prj_SkipDir(nm)) continue;
            // Follow symlinks / junctions, guarding against cycles.
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                std::wstring canon = Prj_Canonical(full);
                if (canon.empty() || visited.count(canon)) continue;
                visited.insert(canon);
            }
            Prj_Walk(full, childRel, out, depth + 1, visited, maxFiles);
            if ((int)out.size() >= maxFiles) break;
        } else if (Prj_IsTextFile(nm)) {
            if (Prj_SkipFile(nm)) continue;
            ULONGLONG sz = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (sz <= 64ull * 1024ull * 1024ull) {   // skip files larger than 64 MB
                NeProjectFile f;
                f.relPath  = childRel;
                f.fullPath = full;
                out.push_back(std::move(f));
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool NeProjects_CollectFiles(const std::wstring& rootPath,
                             std::vector<NeProjectFile>& out, int maxFiles)
{
    out.clear();
    if (rootPath.empty()) return false;
    DWORD attr = GetFileAttributesW(rootPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    std::set<std::wstring> visited;
    std::wstring canon = Prj_Canonical(rootPath);
    if (!canon.empty()) visited.insert(canon);
    Prj_Walk(rootPath, L"", out, 0, visited, maxFiles);
    return true;
}

bool NeProjects_ReadTextFile(const std::wstring& fullPath, std::wstring& out, size_t maxBytes)
{
    out.clear();
    HANDLE h = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li = {};
    GetFileSizeEx(h, &li);
    size_t toRead = (size_t)li.QuadPart;
    if (toRead > maxBytes) toRead = maxBytes;
    std::string buf;
    buf.resize(toRead);
    DWORD got = 0;
    BOOL ok = toRead ? ReadFile(h, buf.data(), (DWORD)toRead, &got, NULL) : TRUE;
    CloseHandle(h);
    if (!ok) return false;
    buf.resize(got);
    if (got == 0) return true;

    // UTF-16 LE BOM
    if (got >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE) {
        const wchar_t* w = (const wchar_t*)(buf.data() + 2);
        out.assign(w, (got - 2) / 2);
        return true;
    }
    // Binary guard: a NUL byte in a non-UTF-16 stream means this is not text.
    // Catches binaries that reached here with an unknown or missing extension.
    if (buf.find('\0') != std::string::npos) return false;
    // UTF-8 (strip BOM if present)
    size_t off = 0;
    if (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) off = 3;
    int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 buf.data() + off, (int)(got - off), NULL, 0);
    if (wn <= 0) {   // not valid UTF-8 → treat as ANSI
        wn = MultiByteToWideChar(CP_ACP, 0, buf.data() + off, (int)(got - off), NULL, 0);
        if (wn <= 0) return false;
        out.resize(wn);
        MultiByteToWideChar(CP_ACP, 0, buf.data() + off, (int)(got - off), out.data(), wn);
        return true;
    }
    out.resize(wn);
    MultiByteToWideChar(CP_UTF8, 0, buf.data() + off, (int)(got - off), out.data(), wn);
    return true;
}

