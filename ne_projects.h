#pragma once
#include <windows.h>
#include <string>
#include <vector>
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


