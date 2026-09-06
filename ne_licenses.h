#pragma once
#include <windows.h>
#include <string>
#include <vector>

// License catalogue for the New Project dialog.  The 19 license texts are
// embedded as RCDATA resources and seeded into the shared SQLite database (the
// `licenses` table) at startup, so the portable exe always carries them.

struct NeLicenseInfo {
    int          id = 0;     // 0..18, matches the seed / dropdown order
    std::wstring name;       // display name for the dropdown
};

// Create the `licenses` table and seed it from the embedded resources.
// Idempotent; must be called AFTER NeProfiles_Init() (uses the shared DB handle).
bool NeLicenses_Init();

// All licenses in display order (id + name).  "Choose License" and "Other" are
// UI-only entries handled by the dialog, not stored here.
bool NeLicenses_List(std::vector<NeLicenseInfo>& out);

// Full license body for an id, with {{YEAR}}/{{NAME}} placeholders intact
// (the short licenses carry them; the rest are verbatim).  False if id unknown.
bool NeLicenses_GetBody(int id, std::wstring& out);
