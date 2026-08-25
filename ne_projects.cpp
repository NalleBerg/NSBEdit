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

// ── Init ──────────────────────────────────────────────────────────────────────
bool NeProjects_Init()
{
    sqlite3* db = NeProfiles_GetDb();
    if (!db) return false;
    const char* schema =
        "CREATE TABLE IF NOT EXISTS projects ("
        "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name      TEXT    NOT NULL,"
        "  root_path TEXT    NOT NULL,"
        "  created   INTEGER NOT NULL DEFAULT 0,"
        "  modified  INTEGER NOT NULL DEFAULT 0"
        ");";
    char* err = nullptr;
    if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return false;
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

static bool Prj_IsTextFile(const wchar_t* name)
{
    // Bare build files with no extension.
    static const wchar_t* bare[] = {
        L"makefile", L"dockerfile", L"cmakelists.txt", L"rakefile", L"gemfile"
    };
    std::wstring low = Prj_Lower(name);
    for (auto b : bare) if (low == b) return true;

    size_t dot = low.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = low.substr(dot + 1);
    static const wchar_t* exts[] = {
        L"c", L"cc", L"cpp", L"cxx", L"h", L"hh", L"hpp", L"hxx", L"inl",
        L"cs", L"java", L"kt", L"kts", L"go", L"rs", L"swift", L"m", L"mm",
        L"js", L"jsx", L"ts", L"tsx", L"mjs", L"cjs", L"vue", L"svelte",
        L"py", L"rb", L"php", L"pl", L"pm", L"lua", L"sh", L"bash", L"zsh",
        L"bat", L"cmd", L"ps1", L"psm1",
        L"html", L"htm", L"css", L"scss", L"sass", L"less",
        L"xml", L"json", L"jsonc", L"yaml", L"yml", L"toml", L"ini", L"cfg",
        L"conf", L"properties", L"env",
        L"md", L"markdown", L"txt", L"rst", L"tex",
        L"sql", L"cmake", L"mk", L"gradle", L"vb", L"fs", L"fsx",
        L"r", L"jl", L"dart", L"scala", L"groovy", L"clj", L"ex", L"exs",
        L"gd", L"nim", L"zig", L"asm", L"s", L"rc"
    };
    for (auto e : exts) if (ext == e) return true;
    return false;
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

