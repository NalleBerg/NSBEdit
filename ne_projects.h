#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// A project defines a root folder that the AI may read, search and work in.
// Linked folders (OS symlinks / junctions living inside the root) are followed
// naturally at read time, so no extra storage is needed for them here.
struct NeProject {
    int64_t      id = 0;
    std::wstring name;       // display name (defaults to the root folder name)
    std::wstring rootPath;   // absolute path to the project root folder
};

// Initialise the projects table.  Must be called AFTER NeProfiles_Init(),
// because it reuses the shared SQLite handle owned by ne_profiles.cpp.
bool NeProjects_Init();

// CRUD.
bool NeProjects_Add   (NeProject& p);          // sets p.id on success
bool NeProjects_Delete(int64_t id);
bool NeProjects_List  (std::vector<NeProject>& out);
bool NeProjects_GetById(int64_t id, NeProject& out);

// Full project properties, including the New Project columns (type,
// build_command, run_command).  Empty type/build_command/run_command mean
// "unset".  These helpers use the shared SQLite handle and must be called on the
// UI thread (SQLite is built with THREADSAFE=0).
struct NeProjectInfo {
    int64_t      id = 0;
    std::wstring name;
    std::wstring rootPath;
    std::wstring type;          // e.g. "cli", "gui", "db" (free-form for now)
    std::wstring buildCommand;  // e.g. "makeit.bat"
    std::wstring runCommand;    // how to run once built
};

// Read all properties for one project.  Returns false if the id is unknown.
bool NeProjects_GetInfo(int64_t id, NeProjectInfo& out);

// Update the editable properties (type, build_command, run_command) for a
// project and bump its modified timestamp.  name/root_path are not changed here.
bool NeProjects_SetInfo(const NeProjectInfo& in);

// Active project selection (0 = no project).  Persisted in the settings table.
int64_t NeProjects_GetActiveId();
void    NeProjects_SetActiveId(int64_t id);

// A text/code file discovered inside a project tree.
struct NeProjectFile {
    std::wstring relPath;    // path relative to the project root, using '/' separators
    std::wstring fullPath;   // absolute path on disk
};

// Walk rootPath collecting text/code files.  OS symlinks and junctions are
// followed (with a cycle guard), so a linked folder inside the root is treated
// as part of the workspace.  Heavy/build folders (.git, node_modules, build, …)
// and binary/oversized files are skipped.  Bounded by maxFiles.
bool NeProjects_CollectFiles(const std::wstring& rootPath,
                             std::vector<NeProjectFile>& out, int maxFiles = 500000);

// Best-effort read of a text file (UTF-16 BOM, UTF-8 BOM/plain, or ANSI fallback),
// truncated to at most maxBytes bytes of source.
bool NeProjects_ReadTextFile(const std::wstring& fullPath, std::wstring& out, size_t maxBytes);

// ── Project knowledge store (evolving) ────────────────────────────────────────
// A free-form text record that belongs to a project but lives in the database
// rather than on disk (notes, saved answers, design info, …).  The schema is
// intentionally generic (kind/title/body) so the store can grow over time; the
// AI reads these records in batch mode alongside on-disk files.
struct NeProjectDoc {
    int64_t      id = 0;
    std::wstring kind;    // free-form category, e.g. "note", "info", "answer"
    std::wstring title;   // short label
    std::wstring body;    // the text content the AI can read/search
};

// Add a knowledge record to a project.  Sets doc.id on success.
bool NeProjects_AddDoc(int64_t projectId, NeProjectDoc& doc);

// Collect all knowledge records for a project (newest first).  Bounded by maxDocs.
bool NeProjects_CollectDocs(int64_t projectId, std::vector<NeProjectDoc>& out,
                            int maxDocs = 100000);

// ── Scaffold template engine (New Project) ────────────────────────────────────
// A tiny writer that fills {{KEY}} placeholders from a variable map and writes a
// set of files into a target folder (creating any missing subfolders).  Files
// are written as UTF-8 with NO BOM to match the repository style.  By default it
// REFUSES to overwrite an existing file (safety); pass overwrite=true to force.
using NeTemplateVars = std::map<std::wstring, std::wstring>;

struct NeTemplateFile {
    std::wstring relPath;   // path relative to the target dir (placeholders allowed)
    std::wstring content;   // template text with {{KEY}} placeholders
};

// Replace every {{KEY}} in tmpl with vars[KEY].  Unknown keys are left as-is.
std::wstring NeTemplate_Expand(const std::wstring& tmpl, const NeTemplateVars& vars);

// Write one UTF-8 (no BOM) file, creating parent folders.  Returns false if the
// file exists and overwrite is false, or on any I/O error.
bool NeTemplate_WriteFile(const std::wstring& fullPath, const std::wstring& content,
                          bool overwrite = false);

// Expand and write a whole set under targetDir.  relPath and content are both
// expanded with vars.  Stops and returns false on the first failure.
bool NeTemplate_WriteSet(const std::wstring& targetDir,
                         const std::vector<NeTemplateFile>& files,
                         const NeTemplateVars& vars, bool overwrite = false);


