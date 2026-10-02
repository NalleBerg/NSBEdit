#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "ne_projects.h"   // NeTemplateFile / NeTemplateVars

// ─────────────────────────────────────────────────────────────────────────────
//  New-Project LANGUAGE CATALOGUE (Phase C / Step 6 — CLI templates for every
//  language NSBEdit supports; see NewProjectLanguageFiles.txt).
//
//  The catalogue is pure data: one entry per language, each listing the project
//  KINDS it offers (cli / gui / db / website) and, per kind, the starter file
//  set + default build_command + run_command.  Adding a language is one entry —
//  there is no hard-coded switch anywhere else.
//
//  File contents carry {{KEY}} placeholders expanded by the existing template
//  engine (NeTemplate_Expand / NeTemplate_WriteSet).  Common placeholders:
//      {{NAME}}  project / exe base name     {{YEAR}}  current year
//      {{DATE}}  dd.mm.yyyy                   {{TYPE}}  kind id (cli/gui/db/…)
// ─────────────────────────────────────────────────────────────────────────────

// One language in the catalogue.
struct NeLangInfo {
    std::wstring              id;        // stable id, e.g. L"cpp", L"python"
    std::wstring              display;   // proper-noun label, e.g. L"C++" (not localised)
    std::vector<std::wstring> kindIds;   // offered kinds; [0] is the default
    bool                      compiled;  // true => produces a native .exe / makeit.bat + pack
};

// The scaffold for one (language, kind) pairing.
struct NeLangKindFiles {
    std::wstring                 buildCommand;  // default build_command (may be empty)
    std::wstring                 runCommand;    // default run_command   (may be empty)
    std::vector<NeTemplateFile>  files;         // starter files (relPath + content, placeholders OK)
};

// The whole catalogue (stable order; first entry is the default = C++).
const std::vector<NeLangInfo>& NeLang_Catalog();

// Look up one language by id.  Returns nullptr if unknown.
const NeLangInfo* NeLang_Find(const std::wstring& langId);

// Fill `out` with the starter file set + default commands for one (language,
// kind).  Returns false if the pairing is unknown (out left unchanged).  A
// known pairing with no type-specific files (e.g. the not-yet-built C++ DB
// kind) returns true with an empty `files` list.
//
// toolkit / qtPath only apply to the C / C++ "gui" kind: toolkit is L"win32"
// (default) or L"qt6"; for L"qt6", qtPath is the developer's Qt install folder
// (baked into the generated makeit.bat / used by windeployqt).  Both are ignored
// for every other language/kind.
bool NeLang_GetKindFiles(const std::wstring& langId, const std::wstring& kindId,
                         NeLangKindFiles& out,
                         const std::wstring& toolkit = L"win32",
                         const std::wstring& qtPath  = L"");

