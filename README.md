# NSBEdit

A lightweight, standalone RTF notepad and programming editor for Windows. **v2026.05.29.10**

## Download

Just grab **[NSBEdit.exe](NSBEdit.exe)** — no installer, no extra files, no dependencies. Drop it anywhere and run it.

## Features

- Full RTF formatting toolbar: Bold, Italic, Underline, Strikethrough, Subscript, Superscript
- Font face, size, text colour, highlight colour
- Paragraph alignment (left / centre / right / justify)
- Bullet and numbered lists
- Image insertion (PNG / JPEG embedded in RTF)
- **Table insertion and editing** — Table Properties dialog (rows, columns, width, borders, padding, row height, alignment); insert from toolbar drop-down or dialog; right-click in table for Table Properties
- Alter current table or insert nested table in cell — mode radio in the dialog when caret is inside a table
- **Hyperlink insertion** — Insert Hyperlink dialog (URL + display text); inserts a proper RTF `\field` hyperlink at the caret
- **Ctrl+Click to follow hyperlinks** — extracts URL from the RTF field instruction and opens in the default browser
- **Hover tooltip on hyperlinks** — shows the URL and a Ctrl+Click hint as a two-line tooltip when hovering over a link
- **URL validation** — regex check on save: requires a recognised scheme, valid host, and 2–4 character TLD
- File menu: New, Open, Save, Save As, Print, Export as PDF (`Ctrl+Shift+P`)
- **Session Restore** (installed version only) — on startup, all tabs from the previous session are automatically reopened: local files, FTP/SFTP files, and unsaved (untitled) buffers, including empty placeholder tabs used as separators. Session state (including per-tab caret position, first visible line, and word-wrap state) is saved to the SQLite database every 10 seconds and at clean exit using a single atomic transaction, so the session survives crashes and reboots. Restore is completely silent: unsaved content, FTP files, and locally-edited files load from the cached BLOB with no prompts. Disabled in portable mode and when a file is passed on the command line.
- **Recent Files** — File menu *Recent Files* submenu lists the last 10 opened or saved files; persisted between sessions in the settings database
- Edit menu: Undo, Redo, Cut, Copy, Paste, Select All — greyed dynamically
- Right-click context menu on the editor with the same Edit operations
- Export as PDF via *Microsoft Print to PDF* — no third-party libraries
- **Go to Line** — Ctrl+G (or Edit → Go to Line…) opens a compact input dialog; jumps to any line number, clamped to the last line
- **Bookmarks** — F2 toggles a bookmark on the current line (blue circle in the margin); Shift+F2 goes to the previous bookmark, Ctrl+F2 to the next. *Go to Bookmark* is greyed when no bookmarks exist
- **Spell Check** — Windows built-in `ISpellChecker` COM API (Windows 8+); red squiggles under misspelled words in RTF documents; native spell-check dialog (F7) with suggestions listbox, Change / Change All / Ignore / Ignore All / Add to Dictionary; language submenu shows only installed spell checkers from the user's Windows language list
- **Insert Date/Time** — F5 (or *Edit → Insert Date/Time*) inserts the current local date and time at the caret using the system locale format; works in both RTF and code tabs
- **Find / Replace** — Ctrl+F opens the Find/Replace dialog; search across all open tabs simultaneously with the "All open tabs" checkbox; "Search backwards" checkbox; match counter (e.g. *16 / 3500*); Match case, Whole word, and Regex options; Replace and Replace All (disabled in multi-tab mode); dialog width adapts dynamically to any locale's text lengths
- **Status bar** — real-time word, character and line count; Saved/Unsaved indicator; encoding display; fully i18n (all labels localised)
- Keyboard Shortcuts dialog (`F1`) — 44 shortcuts, bold/colour-coded
- `Ctrl+W` to close tab / exit (with unsaved-change prompt)
- Tabbed editor — multiple documents open simultaneously, owner-drawn × close glyphs, [+] new-tab button, right-click tab context menu
- Custom save-changes dialog with icon buttons (Save / Don't Save / Cancel)
- On-focus external file change detection with Reload / Keep Current dialog
- **Instant focus on Alt+Tab** — returning to NSBEdit via Alt+Tab or taskbar click immediately activates the editor; text selection stays live (blue)
- Owner-draw menus at 12pt Segoe UI — white background, correct highlight/grayed states
- All UI fonts at 12pt Segoe UI, DPI-aware — readable at any screen resolution or scale
- Smart toolbar reflow — controls drop one at a time as the window narrows
- Minimum window width enforced (never narrower than 3 controls per toolbar row)
- Full i18n — all UI strings through embedded locale (en_GB)
- DPI-aware (PerMonitorV2), statically linked — no external DLLs beyond Windows system ones
- Hover tooltips on all toolbar controls
- Credits dialog (About → Credits): Scintilla, Lexilla, GDI+, MinGW-W64, SQLite3, libcurl/libssh2, rtf2html sections with links
- **Syntax highlighting** — 26 languages (Bash/Shell, PHP, Python, C/C++, JavaScript, HTML, CSS, SQL, and more); choose via Language menu. Selecting a language on a plain-text tab instantly converts it to the Scintilla code editor with full colour coding. Shell scripts with no extension are auto-detected by their shebang line (`#!/bin/bash`, `#!/usr/bin/env zsh`, etc.)
- **Typeahead autocomplete** — custom popup (yellow/green, matching tooltip style) for both keyword and phrase completion. Type part of a keyword or a phrase already in the document and pick from the list with ↑/↓/Tab/Enter or mouse click
- **Auto-close bracket and quote pairs** — typing `{`, `[`, `(`, `"`, or `«` inserts the matching closer and places the caret between them; typing a closing character when the same closer already follows the caret jumps over it. Works in both RichEdit and Scintilla editors. (Single quote `'` is intentionally excluded — it would break contractions like *it's*.)
- **Save to FTP** (File → Save to FTP…) — upload the active document to any connected FTP server; a profile-picker list lets you choose any connected server explicitly (useful to deploy a file to a different server). Browse the remote tree, enter a filename, click Save here. Connection stays open after upload
- **Export as HTML 5** (Convert → Export as HTML 5…, RTF documents only) — converts the active RTF document to a self-contained HTML5 file with all images embedded as base64 data URIs. Norwegian and other non-ASCII characters are encoded correctly as UTF-8
- **Horizontal Rule** — insert a styled divider line (single, thick, double, dotted, dashed, or hairline) via the toolbar. Colour, width %, and indent are configurable. Behaves like a character: Delete or Backspace from the adjacent line removes it; Enter above moves it down; Ctrl+Z restores it
- **RTF formatting toolbar shown on startup** — the app opens with the Rich Text toolbar active without needing to open a file first
- **File → Open reuses blank tab** — opening a file when the only open tab is a fresh untitled one loads into that tab rather than creating an extra blank alongside it
- **Paragraph Spacing dialog** — set space before/after a paragraph in points (Format → Paragraph Spacing); owner-draw blue/red buttons, white background, fully i18n
- **Line Spacing dialog** — choose Single, 1.5 lines, or Double line spacing (Format → Line Spacing); same owner-draw style, white background, radio buttons, fully i18n
- **Word wrap ↵ indicator in code tabs** — a teal-green ↵ glyph marks every wrapped visual sub-line in Scintilla (code) tabs when word wrap is on, drawn to the left of the vertical scrollbar
- **Edition 2** — About dialog now shows Edition: 2 below the version number (translated in all 15 UI languages via `ABOUT_EDITION`)
- **Portable and installed modes** — drop `nsbedit.db` (included in the ZIP) next to the exe for portable operation; installer copies it to `%APPDATA%\NSBEdit\` for installed mode. A zero-byte stub is treated as absent — the AppData database always wins. If neither file is present the app runs with an in-memory database and warns the user
- **FTP browser — Rename**: right-click any file or folder to rename it in place; input dialog pre-filled with the current name
- **FTP browser — remember last folder**: reopens in the folder you were last in (per profile); tree always roots at `/` so you can navigate anywhere
- **Auto-indent on Enter** in code (Scintilla) tabs — new lines inherit the indentation of the line above, preserving tabs and spaces exactly
- **Smart backspace unindent** in code tabs — Backspace in leading whitespace removes one full indent level
- **FTP Preview Online** — when a code/plain-text tab is FTP-linked, a violet *Preview online* button appears in the toolbar and under the FTP menu. Clicking uploads the current buffer and opens a dialog with the resolved URL; *Open in browser* launches it; closing reverts the remote file to its original. FTP profile editor gains a **Web URL root** field
- **FTP upload success auto-close** — the "File saved and uploaded successfully" notification closes itself after 2½ seconds
- **FTP browser open reuses blank tab** — opening a file from the FTP file browser reuses the active untouched untitled tab, just like *File → Open*; no extra blank tab created
- **FTP → Close connection** — new FTP menu item (grayed when nothing is connected); clicking opens a picker dialog listing all active connections so you can choose which one to close
- **Toolbar always correct after tab close** — closing a tab now immediately applies the correct button row (RTF / plain-text / code) for the newly-active tab
- **Dark mode** — full dark UI (title bar, dialogs, toolbar, menus, status bar, code editor gutter) toggled from *Edit → Preferences…*; in light mode an optional *dark editor background* applies dark colors to only the code/plain-text viewport while all other UI chrome stays light; RTF writing area is always white regardless of the dark-editor setting
- **Dark-editor keystroke blink fix** — no white flash in any editing operation (Enter, Backspace, paste…) when *Dark editor background* is enabled in Preferences while the overall UI is in light mode; `WM_ERASEBKGND` fully owns the editor area paint via `ExcludeClipRect` + explicit `DefWindowProcW` + return 1
- **Menu bar background matches system color** — the top-level menu bar items use `GetSysColor(COLOR_MENUBAR)` so the bar blends with the standard Windows toolbar/chrome; drop-down popup backgrounds remain white in light mode
- **Multiple UI languages** — Dansk (Danish), Davvisámegiella (North Sami), Deutsch (German), Ελληνικά (Greek), English, Español (Spanish), Français (French), Íslenska (Icelandic), Nederlands (Dutch), Norsk bokmål (Norwegian), Português (Portuguese), Suomi (Finnish), Svenska (Swedish), Vlaams (Flemish), and Українська (Ukrainian); switch instantly from the *GUI Language* menu (no restart required); menu always shows each language in its own native name
- **Instant language switching** — changing the UI language via the menu rebuilds the full menu bar, all tooltips, tab titles, and status bar on the spot
- **PHP syntax highlighting fixed** — PHP files now use the `hypertext` lexer so both embedded HTML and PHP tokens are coloured correctly
- **Zoom via keyboard and scroll** — `Ctrl+[+]` / `Ctrl+[-]` / `Ctrl+0` and `Ctrl+MouseScroll` all work for zooming in both RTF and code (Scintilla) tabs
- **FTP Profile Help** — a `?` button in the FTP profile editor opens a rich-text field guide explaining every setting, including *Web URL root* and how *Preview online* builds the URL
- **FTP Preview — extra options** — *Save backup locally…* and *Close without reverting* buttons let you keep the preview live or save a local copy before reverting
- **FTP keepalive** — TCP keepalive on all connections; `NeFtp_Keepalive()` sends NOOP to FTP servers during long preview sessions to prevent idle timeout
- **follow.ps1 persistent counter** — the `RUN #N` build counter survives PowerShell restarts via `makeit_count.txt`
- **HTML block comment toggle** — the `[//]` button in code tabs toggles `<!-- -->` block comments in HTML regions (HTML and PHP files). Three modes: wrap selection in a new block, remove existing wrapper lines, or split an outer block to exclude the selected lines. In PHP files the button shows `<!--` when the cursor is in HTML and `//` when in PHP code — switching live as the cursor moves
- **About dialog redesigned** — logo embedded in the executable (no external file); Segoe UI 12 pt throughout; coloured section headers for RTF editing, Code editing, and FTP; fully i18n'd in English and Norwegian
- **Compile-time version** — version and publish date are baked into the executable at build time; `curver.txt` is no longer read at runtime and is no longer shipped in the ZIP

## Building from source

Requirements: MinGW-w64 (GCC 13+) with `g++` and `windres` on PATH.

```
.\makeit.bat
```

Optionally run `NewVersion.ps1` first to stamp the build date into the About dialog.

## Changelog

See [Changelog.html](Changelog.html) for full version history.

## License

GNU General Public License v2 — see [GPLv2.md](GPLv2.md).
