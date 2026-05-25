#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

// ── Session restore — one persisted tab entry ─────────────────────────────────
struct NeSessionTab {
    int            sortOrder     = 0;

    // Local filesystem path.  Empty for untitled (unsaved) tabs.
    std::wstring   localPath;

    // FTP / SFTP remote-editing fields.
    bool           isFtp         = false;
    int64_t        ftpProfileId  = -1;
    std::wstring   ftpRemotePath;
    std::wstring   ftpFriendly;     // friendly server name for display

    // Raw file content for modified / unsaved tabs.
    // Empty means "load from disk/FTP at restore time".
    std::vector<uint8_t> content;
    bool           contentIsRtf  = false; // true = RTF bytes; false = UTF-8 text

    bool           isActive      = false; // was the active tab when session was saved
    bool           wasModified   = false; // had unsaved edits when session was saved

    // Disk stamp at last load (used to detect remote FTP changes on restore).
    DWORD          diskTimeLo    = 0;
    DWORD          diskTimeHi    = 0;
    int64_t        diskSize      = 0;
};

// ── API ───────────────────────────────────────────────────────────────────────

// Atomically overwrite the saved session with the supplied tab list.
// Returns false if the DB is unavailable.
bool NeSession_Save(const std::vector<NeSessionTab>& tabs);

// Load the previously saved session into |out|.
// Returns false if the DB is unavailable.
bool NeSession_Load(std::vector<NeSessionTab>& out);

// True if the session_tabs table contains at least one row.
bool NeSession_HasData();

// Delete all rows from session_tabs.
bool NeSession_Clear();
