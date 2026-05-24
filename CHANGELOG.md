# Changelog

## v2026.05.24.12 - 24.05.2026 12:00

- **French (Français) UI translation**: Full translation of all ~200 UI strings added as `locale/fr_FR.txt` (RCDATA 19, locale ID 7). Menu order: Dansk → Deutsch → English → Français → Íslenska → Norsk → Suomi → Svenska.
- **All locale files updated**: `LANG_UI_FRENCH` added to every existing locale file (en_GB → "French", no_nb → "Fransk", is_IS → "Franska", sv_SE → "Franska", da_DK → "Fransk", fi_FI → "Ranska", de_DE → "Französisch").

## v2026.05.24.11 - 24.05.2026 11:00

- **German (Deutsch) UI translation**: Full translation of all ~200 UI strings added as `locale/de_DE.txt` (RCDATA 18, locale ID 6). Menu order: Dansk → Deutsch → English → Íslenska → Norsk → Suomi → Svenska.
- **Language menu always shows native names**: The GUI Language menu now displays each language in its own native name regardless of the active UI language (`Ne_RebuildLocaleMenu` uses hardcoded native strings instead of `Ls(L"LANG_UI_*")`). A German speaker always sees "Deutsch" even when the UI is set to Norwegian.

## v2026.05.24.10 - 24.05.2026 10:00

- **Finnish (Suomi) UI translation**: Full translation of all ~200 UI strings added as `locale/fi_FI.txt` (RCDATA 17, locale ID 5). Appears in the GUI Language menu between Norsk and Svenska: Dansk → English → Íslenska → Norsk → Suomi → Svenska.
- **All locale files updated**: `LANG_UI_FINNISH` added to every existing locale file (`en_GB.txt` → "Finnish", `no_nb.txt` → "Finsk", `is_IS.txt` → "Finnska", `sv_SE.txt` → "Finska", `da_DK.txt` → "Finsk").

## v2026.05.24.09 - 24.05.2026 09:14

- **Swedish (Svenska) UI translation**: Full translation of all ~200 UI strings added as `locale/sv_SE.txt` (RCDATA 15, locale ID 3). Appears in the GUI Language menu in alphabetical order by native name: Dansk → English → Íslenska → Norsk → Svenska. All existing locale files updated with `LANG_UI_SWEDISH`.
- **Danish (Dansk) UI translation**: Full translation of all ~200 UI strings added as `locale/da_DK.txt` (RCDATA 16, locale ID 4). "Dansk" sorts before "English" alphabetically and appears at the top of the language menu. All existing locale files updated with `LANG_UI_DANISH`.
- **UK English corrections in `en_GB.txt`**: `HRP_ALIGN_C` changed from "Center" to "Centre"; `ABOUT_BTN_LICENSE` changed from "View License" to "View Licence" (British noun form).

## v2026.05.24.08 - 24.05.2026 08:55

- **Icelandic (Íslenska) UI translation**: Full translation of all ~200 UI strings added as `locale/is_IS.txt`. Appears in the GUI Language menu between English and Norwegian (alphabetical by native name). Switching requires no restart. All existing locale files updated with `LANG_UI_ICELANDIC = Íslenska`.
- **RC resource-ID collision fix**: Icelandic locale was initially assigned RCDATA 12, which was already taken by `nsb_256.png`. `windres` accepts duplicate IDs silently — the first definition wins, so `FindResourceW` returned PNG binary data to the locale parser and the language appeared not to change. Moved to RCDATA 14 (`IDR_LOCALE_IS_IS = 14`). Documented in `API_INTERNALS\INTERNALS\add_ui_language_INTERNALS.txt` with full RC ID layout table and pitfall list.
- **ZIP output moved to `zip\`**: `pack.ps1` now writes ZIPs to `zip\` (auto-created); `zip/` added to `.gitignore` — ZIPs are no longer tracked by Git.

## v2026.05.23.11 - 23.05.2026 11:18

- **FTP → Close connection**: New permanent FTP menu item "Close connection" appears directly below "Add site…", grayed when nothing is connected. Clicking opens a picker dialog listing all active connections as owner-draw buttons (same `Ne_ShowFtpSelectDialog` style as *Save to FTP*); selecting one disconnects it immediately. `Ne_ShowFtpSelectDialog` is now generalised — it accepts title and message strings from the caller so it can serve any connection-picker purpose. `Ne_ShowFtpCloseConnDialog` wraps it for this use case. The right-click context-menu machinery (WM_MENURBUTTONUP, WM_APP+2 deferred handler, `s_ftpCtxProfileId`, `IDM_FTP_CTX_EDIT/CLOSE`) has been removed entirely. Locale keys: `FTP_CLOSE_CONN_MENU`, `FTP_CLOSE_CONN_PROMPT` (en_GB + no_nb).

## v2026.05.23.10 - 23.05.2026 10:08

- **About dialog redesigned**: The About dialog has been fully rewritten. The app logo (`nsb_256.png`) is now embedded as RCDATA 12 and drawn at S(100)×S(100) DPI-scaled pixels via GDI+ from a memory stream — no external file dependency. The dialog uses Segoe UI 12 pt throughout with a red app-name title, grey subtitle, decorative separator line, and three coloured section headers: RTF editing (blue), Code editing & syntax highlighting (green), FTP & remote editing (purple). All section headers and descriptions are i18n'd — keys `ABOUT_SEC_RTF`, `ABOUT_DESC_RTF`, `ABOUT_SEC_CODE`, `ABOUT_DESC_CODE`, `ABOUT_SEC_FTP`, `ABOUT_DESC_FTP` in both `locale/en_GB.txt` and `locale/no_nb.txt`.
- **Compile-time version baking**: `makeit.bat` now includes a `[pre]` step that reads `curver.txt` via PowerShell and writes `ne_version.h` with `#define NE_PUBLISHED` and `#define NE_VERSION` as wide-string literals. `ShowNsbAboutDialog` uses these constants directly — no file I/O at runtime. `curver.txt` is no longer shipped in the distribution ZIP.
- **GNU logo embedded in License dialog**: `GnuLogo.bmp` is now embedded as RCDATA 13 and loaded via `CreateDIBitmap` from the resource — no external file required. The GNU wildebeest logo appears at the top of the License dialog as before.

## v2026.05.23.09 - 23.05.2026 09:44

- **Changelog title updated**: The Changelog.html header now reads *NSBEdit — Standalone RTF Notepad and Programming Editor*, reflecting the dual-mode nature of the editor.
- **HTML block comment toggle**: The `[//]` toolbar button now toggles `<!-- -->` block comments when the cursor is in the HTML region of an HTML or PHP file. Three-case logic: (1) if the selection is wrapped in bare `<!--` / `-->` lines those wrapper lines are removed; (2) if the selection is inside an outer block comment the block is split so only the selected lines are excluded; (3) otherwise the selection is wrapped in a new `<!--` / `-->` block. Uses `SCI_BEGINUNDOACTION`/`SCI_ENDUNDOACTION` so the whole operation is a single undo step.
- **Smart PHP/HTML region detection**: In PHP files (which use the `hypertext` lexer for mixed PHP+HTML), the button checks the Scintilla style at the selection start via `SCI_GETSTYLEAT`. Styles < 118 are HTML regions; styles ≥ 118 (`SCE_HPHP_DEFAULT`) are PHP regions. HTML regions get `<!-- -->` block commenting; PHP regions continue to use `//` line commenting.
- **Dynamic button label**: The comment button label switches live between `<!--` and `//` as the cursor moves between HTML and PHP regions, updated via the `SCN_UPDATEUI` notification. Only redraws when the label actually changes.

## v2026.05.23.08 - 23.05.2026 08:58

- **Toolbar always shows correct row on new tab**: `Ne_New` (File → New / Ctrl+N) now calls `Ne_UpdateToolbarMode` at the end so the Rich button row is applied immediately when the new untitled tab becomes active. Previously the Prog/Txt row from the previous code-file tab stayed visible until the user switched away and back.

## v2026.05.22.12 - 22.05.2026 12:08

- **Dark-editor blink fully eliminated**: The previous fix filled the editor rect dark then `break`-ed out of `WM_ERASEBKGND`, letting `DefWindowProcW` overwrite the dark fill with the system white brush on any line-count change (Enter, Backspace on empty line, paste, etc.). Rewritten: `ExcludeClipRect` protects the editor area, `DefWindowProcW` is called explicitly so surrounding chrome paints normally, then the editor rect is filled `RGB(30,30,30)` and the handler returns 1 — preventing any further default processing. White flash fully suppressed for all editing operations.

## v2026.05.22.11 - 22.05.2026 11:57

- **FTP browser open reuses untouched tab**: Opening a file from the FTP file browser (*FTP → Browse files…*) now reuses the active tab if it is an untouched untitled RichEdit tab (path empty, not modified, no Scintilla window) — the same logic that *File → Open* already applied. Previously `NeTabs_AddUntitled` was called unconditionally, producing an empty extra tab alongside the loaded file.
- **Toolbar mode corrected after tab close**: `Ne_CloseTabAt` now calls `Ne_UpdateToolbarMode` before `Ne_SyncToolbar` when a tab is closed. Previously the toolbar kept the mode of the *closed* tab — most visibly: closing an initial RTF tab while an FTP plain-text file was open showed the full RTF button row instead of the plain-text/code row.
- **About — Edition 1**: The About dialog now shows `Edition: 1`. The "RC" suffix removed.
- **Dark-editor keystroke blink fix**: With *Dark editor background* on in Preferences (light UI, `g_darkEditor = true`, `g_darkMode = false`), pressing Enter (or any auto-indent key) in a code tab no longer causes a brief white flash. Root cause: `WM_ERASEBKGND` only painted dark for `g_darkMode`; when Scintilla's internal repaint briefly exposed the parent background the default white brush showed through. Fix: `WM_ERASEBKGND` now fills the edit area (`st->editX/editY/editW/editH`) with `RGB(30,30,30)` (Scintilla background) when `g_darkEditor` is true.

## v2026.05.22.10 - 22.05.2026 10:40

- **Shortcuts dialog fully i18n**: All 52 rows of the Keyboard Shortcuts dialog (`F1`) now use locale keys for both the function name and description columns. Keys follow the pattern `SCF_*` (function) and `SCD_*` (description) — 84 new keys added to `locale/en_GB.txt` and `locale/no_nb.txt`. Switching the UI language now immediately updates all three columns.
- **Credits dialog i18n**: The Credits dialog (About → Credits) title and all seven body sections now go through `Ls()`. Keys `CREDITS_TITLE` and `CREDITS_1`–`CREDITS_7` added to both locale files.
- **Ne_SetTip language-switch crash fix**: `Ne_SetTip` previously re-subclassed the same toolbar control on every language switch, building a chain of nested subclass procs. On the second switch the inner proc freed already-freed tip text — heap corruption and crash. Fix: check for the `NeTip` window property as a *subclassed* sentinel. If present, swap the stored tip text in-place (free old, `wcsdup` new) without touching `GWLP_WNDPROC`. The original proc is now stored as the `NePrevProc` window property (not a module-level static) so each control independently tracks its own chain. `WM_NCDESTROY` frees the tip string and removes both properties before restoring the proc. Pattern documented in `tooltip_API.txt` §19.
- **Shortcuts dialog H-scrollbar fix (no garbling)**: The horizontal scrollbar no longer appears in the Keyboard Shortcuts dialog after column auto-sizing and dialog resize. Root cause: the MSB `WM_SIZE` handler calls `origProc(WM_SIZE)` first; the ListView’s origProc momentarily re-enables the native V bar, which subtracts `SM_CXVSCROLL` (17 px) from the client width. With only `S(MSB_WIDTH_FULL)` (12 px) reserved for the scrollbar margin the columns appeared to overflow by 5 px, enabling the H bar. Fix: `lvNeededW` now uses `GetSystemMetrics(SM_CXVSCROLL)` instead of `S(MSB_WIDTH_FULL)`, so columns always fit even during that momentary V-bar re-show. The previous workaround — calling `ShowScrollBar(SB_HORZ, FALSE)` — was removed: it triggered a `WM_SIZE` cascade that corrupted the ListView’s double-buffer DC, causing all rows below the initial viewport to render as dots/dashes when first scrolled into view. Pattern documented in `my_scrollbar_vscroll_API.txt` §4e.


- **Single-quote removed from auto-pair**: Typing `'` now inserts a plain apostrophe in both RichEdit and Scintilla (code) tabs. The single-quote was removed from `Ne_SciAutoPair` (jump-over and auto-close branches) and from the `WM_CHAR` handler in `Ne_EditCaretProc` — it caused unwanted doubling in contractions like *it's* and *that's*. Remaining auto-pair openers: `{`, `[`, `(`, `"`, `«`.
- **Preview dialog tooltip fix**: Tooltips on the *Open in Browser* and *Revert & Close* buttons in the Preview Online dialog now hide correctly when the mouse leaves. Root cause: `WM_MOUSELEAVE` is never delivered to toolbar controls after `EnableWindow(parent, FALSE)`, *and* is never delivered to dialog controls when the dialog is destroyed — both leave `s_neTipTracking`/`s_neTipHwnd` stale. Fix: `HideTooltip()` + reset of both tracking variables called **twice** in `Ne_ShowPreviewOnFtp` — once before `EnableWindow(hwnd, FALSE)` (so the dialog's buttons can register their own `TrackMouseEvent`) and once before `EnableWindow(hwnd, TRUE)` (so a tooltip visible in the dialog does not linger over the editor). Pattern documented in `tooltip_API.txt` §18.

## v2026.05.21.20 - 21.05.2026 20:25

- **Menu bar background matches system color**: The owner-drawn top-level menu bar items (File, Edit, Convert, etc.) now fill their background with `GetSysColor(COLOR_MENUBAR)` instead of hardcoded white, so the bar blends with the standard Windows toolbar/chrome background. Popup (drop-down) item backgrounds remain white in light mode.
- **Instant UI language switching**: Selecting a language from the GUI Language menu now switches the interface immediately — no restart required. New functions `Ne_BuildMainMenu(HWND)`, `Ne_RefreshTooltips(HWND)`, and `Ne_RefreshLocale(HWND)` rebuild the full menu bar, refresh all toolbar tooltip strings, update tab titles, and update the status bar text in one call. WM_CREATE no longer builds the menu or sets tooltips inline; both call the shared helpers.
- **Norwegian Bokmål UI translation**: Full `no_nb` locale added (`locale/no_nb.txt`, RCDATA 11, `IDR_LOCALE_NO_NB`). All ~200 UI strings translated. Selectable from GUI Language → Norsk (bokmål). `LANG_UI_ENGLISH` / `LANG_UI_NORWEGIAN` locale keys added to `en_GB.txt` and `no_nb.txt`.
- **RTF viewport always white when dark editor is on**: `g_darkEditor` now only affects the Scintilla (plain-text / code) viewport. The RichEdit control background and default text colour are set from `g_darkMode` only, so switching on *Dark editor background* in Preferences no longer turns the RTF writing area black.
- **Convert to RTF restores white background**: The *Convert → Convert to RTF* handler now resets the RichEdit background and text colour (white / auto-colour) immediately after conversion, matching the behaviour of a freshly opened RTF tab.

## v2026.05.20.11 - 20.05.2026 11:26

- **Dark mode**: Full dark UI via `dark_mode` DB setting, toggled live by `Ne_RethemeAll(HWND)`. Title bar darkened with `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` (links `-ldwmapi`). Main window, toolbar area, and non-client menu-bar gap fill `RGB(25,26,27)` via `WM_ERASEBKGND` / `WM_NCPAINT`. Dialogs use `Ne_DlgBgBrush()` (cached `HBRUSH`) and `Ne_DlgCtlColor()` for `WM_CTLCOLOR*` messages. Owner-draw toolbar buttons: dark fill (`RGB(50,50,54)` normal, `RGB(45,75,120)` pressed/checked) with flat 1 px border pens. Menu popup items: `WM_DRAWITEM ODT_MENU` dark bg / light text. Status bar: `NeStatusBar_SetDarkMode(hBar, dark)`. Scintilla gutter: `RGB(25,26,27)` bg with matching line-number, fold-margin, caret-line colors. Dark palette: frames/menus/dialogs `RGB(25,26,27)`; toolbar buttons `RGB(50,50,54)`; text `RGB(210,210,215)`.
- **Preferences dialog**: New `Ne_PrefsDlgProc` / `Ne_ShowPrefsDialog` dialog (Edit → Preferences…). *Appearance* section with *Dark mode* (`IDC_PREFS_DARK` 263) / *Light mode* (`IDC_PREFS_LIGHT` 267) radio buttons. Save applies theme live via `Ne_RethemeAll` and persists `dark_mode` to DB. Locale keys: `MENU_PREFS`, `PREF_TITLE`, `PREF_SEC_APPEARANCE`, `PREF_DARK_MODE`, `PREF_LIGHT_MODE`.
- **Dark editor in light mode**: When *Light mode* is selected, an *Editor background* sub-section with *Light background* / *Dark background* radio buttons appears for the Scintilla code editor. `g_darkEditor` (persisted as `dark_editor`) is OR'd with `g_darkMode` in `Ne_SetupScintillaStyle` (`bool darkEd = g_darkMode || g_darkEditor`). Sub-section is hidden in dark mode; toggling the mode radio shows/hides it and resizes the dialog via `SetWindowPos` + `MapWindowPoints`. Locale keys: `PREF_SEC_EDITOR`, `PREF_EDITOR_LIGHT_BG`, `PREF_EDITOR_DARK_BG`.
- **Preferences crash fix**: `PostQuitMessage(0)` removed from `Ne_PrefsDlgProc WM_DESTROY` — it was posting `WM_QUIT` to the main message loop and terminating the app on every Save/Cancel in Preferences. The inner dialog loop exits correctly via `IsWindow(dlg)` after `DestroyWindow`.

## v2026.05.20.09 - 20.05.2026 09:32

- **Ctrl+scroll zoom fix**: `Ctrl+MouseScroll` now zooms in both RTF and Scintilla (code) tabs. Previously the `WM_MOUSEWHEEL` intercept in the WinMain message loop only triggered for RTF tabs (`hEdit != NULL`); the condition is now `hSci || hEdit` so both tab types call `Ne_StepZoom` and the message is consumed before it reaches the native editor handler. `Ctrl+[+]`, `Ctrl+[-]`, `Ctrl+0` (numpad and regular keys) continue to work in all tabs.
- **Zero compiler warnings**: All `-Wall` warnings eliminated across `NSBEdit.cpp`, `ne_statusbar.cpp`, `tooltip/tooltip.cpp`, and `checkbox.cpp` — removed unused functions (`Ne_ApplyZoomToDoc`, `Ne_ApplyDlgFont`, `Ne_ShowTableDialog` marked `[[maybe_unused]]`), removed unused variables (`gapTB`, `padB`, `bG`, `sG`, `LBLH`, `hLarge`, `startX`, and 8 unnamed spin-control HWNDs in the Table Properties dialog), removed `#pragma comment` directives not supported by MinGW, and fixed misleading indentation on zoom-clamping lines. Build now produces zero warnings.
- **zoom_INTERNALS.txt**: New INTERNALS document covering the dual zoom model (RTF percentage preset table vs Scintilla signed-point offset), both trigger paths, DB persistence, startup clamping, and the Ctrl+scroll bug history.

## v2026.05.19.14 - 19.05.2026 14:42

- **PHP syntax highlighting fixed**: PHP files now use the `hypertext` lexer (replacing `phpscript`) so both embedded HTML tokens and PHP tokens are coloured correctly. PHP keywords are passed to keyword slot 4 (not slot 0). Line-comment toggling (`//`) updated to check the language name instead of the old lexer name.
- **FTP Profile Help dialog**: A `?` owner-draw button in the FTP profile editor opens a rich-text *FTP/SFTP Profile — Field Guide* window explaining every field, including *Web URL root* and how *Preview online* builds the preview URL. New locale key: `FTP_HELP_TITLE`.
- **FTP Preview dialog — extra buttons**: *Save backup locally…* (`BTN_SAVE_BACKUP`) and *Close without reverting* (`BTN_CLOSE_ANYWAY`) added to the Preview online dialog for cases where the user wants to keep the preview file live or save a local copy before reverting the remote file.
- **FTP keepalive**: TCP keepalive options set on the curl handle (`CURLOPT_TCP_KEEPALIVE`, 60 s idle, 30 s interval) to prevent server-side disconnects during long sessions. New `NeFtp_Keepalive()` (in `ne_ftp.h/cpp`): sends a NOOP to FTP servers to prevent idle-timeout on the control connection while the Preview dialog is open; no-op for SFTP (SSH transport handles its own keepalives).
- **follow.ps1 — persistent run counter**: The `RUN #N` counter is now saved to `makeit_count.txt` and persists across PowerShell session restarts. To reset, set the file to `0` or delete it.

## v2026.05.19.11 - 19.05.2026 11:27

- **FTP Preview Online**: A violet *Preview online* toolbar button (`IDC_NE_PREVIEW` 265) and `FTP > Preview online.` menu item (`IDM_PREVIEW_FTP` 134) appear when a Scintilla/plain-text tab is FTP-linked. Clicking uploads the current buffer to the remote server and opens a preview dialog with the resolved URL pre-filled. *Open in browser* launches the URL; closing (or *Revert & close*) re-uploads the backup to restore the original. The FTP profile editor gains a **Web URL root** field (`FTP_WEB_URL` locale key, stored in profile) that maps the server root to its public URL. New: `NePreviewDlgData`, `Ne_PreviewDlgProc`. New locale keys: `TIP_PREVIEW`, `FTP_WEB_URL`, `MENU_PREVIEW_FTP`, `FTP_PREVIEW_LOCKED`, `FTP_PREVIEW_URL`, `BTN_OPEN_BROWSER`, `BTN_REVERT`, `FTP_PREVIEW_UP_FAIL`, `FTP_PREVIEW_REV_FAIL`.
- **FTP upload success auto-close**: The "File saved and uploaded successfully" dialog closes itself after 2½ seconds. `Ne_ShowChoiceDialog` gains optional `int autoCloseMs = 0`; `Ne_DialogWndProc` handles `WM_TIMER` id 1 → posts `WM_CLOSE`. `NeDialogData` gains `int autoCloseMs = 0`.
- **About — Edition 1 RC**: About dialog now shows `Edition: 1 RC`.

## v2026.05.19.09 - 19.05.2026 09:20

- **FTP browser — Rename**: Right-clicking any file or folder (index > 0) now shows a Rename… item in the context menu. An input dialog pre-filled with the current name appears; confirming sends `RNFR`/`RNTO` commands (FTP) or a `rename` command (SFTP) via `NeFtp_Rename(oldPath, newPath)` (new function in `ne_ftp.h/cpp`). The parent folder reloads on success. `FTP_CTX_RENAME` and `FTP_INPUT_RENAME` added to `locale/en_GB.txt`. `Ne_ShowInputDialog` gains an optional `initialValue` parameter for pre-filling the edit control.
- **FTP browser — remember last visited folder**: The last folder the user expanded is saved per profile to the DB on dialog close (`ftp_lastdir_<id>` key via new `NeProfiles_SetStrSetting`). On reopen the tree always roots at `/` (server root) and auto-expands down to the saved folder; if no saved folder exists it auto-expands to the profile's initial path. New helper `Ne_FtpTreeExpandToPath` walks the tree, expanding each component in turn (each `TreeView_Expand` fires `TVN_ITEMEXPANDINGW` synchronously, loading children on demand before the message loop starts).
- **`ne_profiles` — string settings**: `NeProfiles_GetStrSetting` / `NeProfiles_SetStrSetting` added to `ne_profiles.h/cpp`. The existing `settings` table already stores values as TEXT so no schema change is needed.
- **Auto-indent on Enter** (Scintilla tabs): `Ne_SciAutoIndent` (called from `SCN_CHARADDED` alongside `Ne_SciAutoPair`) copies the leading whitespace — tabs or spaces — of the previous line to the new line after pressing Enter.
- **Smart backspace unindent** (Scintilla tabs): `SCI_SETBACKSPACEUNINDENTS TRUE` set in `Ne_SetupScintillaStyle` — pressing Backspace when the caret is inside the leading whitespace of a line removes one full indent level.

## v2026.05.18.12 - 18.05.2026 12:19

- **Scintilla word wrap ↵ indicator**: A teal-green ↵ glyph now appears at the right edge of every wrapped visual sub-line in Scintilla (code) tabs when word wrap is on. Implemented via `SCN_PAINTED` — Scintilla's documented post-paint notification — rather than a `WM_PAINT` subclass (Scintilla's own caret/selection repaints were overwriting the subclass overlay). The glyph is drawn to the left of the custom MSB vertical scrollbar; `WS_CLIPSIBLINGS` on the Scintilla window was silently clipping the previous attempt into invisibility. New helper: `Ne_DrawSciWrapIndicators(hSci)` called from `WM_NOTIFY → SCN_PAINTED`.
- **[+] new-tab button always visible**: `NeTabs_TabProc` `WM_PAINT` now calls `RedrawWindow(hBtnNew, RDW_INVALIDATE | RDW_UPDATENOW)` after painting the tab strip, so the [+] button is never left erased when Windows theming overdraw covers the sibling button area.
- **Edition 1 in About dialog**: The About dialog now shows `Edition: 1` below the version line. Locale key `ABOUT_EDITION` added to `locale/en_GB.txt`.
- **Portable/installed/memory DB modes** (`ne_profiles.cpp`): `Np_GetDbPath()` now checks `%APPDATA%\NSBEdit\nsbedit.db` first (installed — file must pre-exist; directory is never auto-created), then `.\nsbedit.db` next to the exe (portable stub from ZIP), then falls back to `:memory:` with a `MessageBoxW` warning so the user knows FTP profiles and settings will be lost on exit. `NeProfiles_IsMemory()` added. Portable stub `nsbedit.db` (0-byte seed file recognised by SQLite3 as a fresh empty database) included in the workspace and in the distributable ZIP via `pack.ps1`.

## v2026.05.18.09 - 18.05.2026 09:08

- **Paragraph Spacing dialog restyled and fixed**: The Paragraph Spacing dialog (Format → Paragraph Spacing) now uses the app-standard owner-draw button system (`NeBtnTone` / `NeDialogButtonSpec` / `Ne_DrawDialogButton` / `Ne_BtnHoverProc`) with a white background, blue Save and red Cancel buttons, and hover-highlight. Root cause of Save/Cancel doing nothing fixed: the dialog class previously used `DefWindowProcW` as its `WndProc`, which silently discarded the `WM_COMMAND` sent synchronously by button clicks — the `GetMessageW` loop never saw it. The dialog now has a proper `Ne_ParSpaceDlgProc` that reads the spin-box values on IDOK, stores them in module-level statics, and calls `DestroyWindow`; the message loop exits when `IsWindow(dlg)` returns false, and the values are applied afterwards. All strings go through `Ls()` (i18n-correct: `DLG_PARSPACE`, `DLG_PARSPACE_BEF`, `DLG_PARSPACE_AFT`, `BTN_SAVE`, `BTN_CANCEL`).
- **Line Spacing dialog restyled and fixed**: Same root-cause fix as Paragraph Spacing. The Line Spacing dialog (Format → Line Spacing) now has `Ne_LineSpaceDlgProc` as its WndProc; it reads the selected radio button on IDOK, stores the rule in `s_lineSpRule`, and calls `DestroyWindow`. Owner-draw blue Save / red Cancel buttons with `Ne_BtnHoverProc` hover tracking; white background with `WM_CTLCOLORBTN` so radio-button backgrounds match. All strings i18n via `Ls()` (`DLG_LINESPACE`, `RDO_LINESPACE_S`, `RDO_LINESPACE_15`, `RDO_LINESPACE_D`, `BTN_SAVE`, `BTN_CANCEL`).

## v2026.05.17.15 - 17.05.2026 15:07

- **Horizontal Rule (HR) in RTF documents**: HR paragraphs render as a custom-drawn line across the editor in one of six styles — single, thick, double, dotted, dashed, or hairline. Colour (solid or gradient), width %, and left/right indent are configurable via a properties dialog. Core functions: `Ne_InsertHRule`, `Ne_PaintHRules`, `Ne_RebuildHRList` (`g_hrMap` HWND→entry cache), `Ne_DeleteHRule`, `Ne_ShowHRulePropsDialog`. Drawing is overlaid via `GetDC` after `CallWindowProc` in `WM_PAINT` (avoids nested-`BeginPaint` clip conflicts). HR behaves like a character that occupies its whole line: Delete at the end of the line above or Backspace at the start of the line below deletes it; pressing Enter on any line above an HR moves it one line down; Ctrl+Z correctly restores a deleted HR. Three bugs fixed during development: (a) typing on a line above the HR used to draw the HR through the text — `EN_CHANGE → Ne_RebuildHRList` now keeps `charIdx` current; (b) pressing Enter above the HR used to erase it — `NE_WM_HR_CLEANUP` (posted via `PostMessageW`) now receives `enterPos` in `wParam` and strips only the paragraph at that position if it inherited HR format, keeping the undo chain intact and avoiding re-entrant `EM_EXSETSEL` calls during mid-split RichEdit processing; (c) `Ctrl+Z` after deleting an HR now correctly redraws it — the `EN_CHANGE` guard that skipped `Ne_RebuildHRList` when `g_hrMap` was empty has been removed.
- **Status bar shows "Rich text"** on RTF/RichEdit tabs alongside the word and character count.

## v2026.05.16.14 - 16.05.2026 14:32

- **Export as HTML 5…** (Convert menu, RTF only): converts active RTF to self-contained HTML5 with base64-embedded images. Uses `rtf2html/ne_rtf2html_lib.cpp` wrapper. Fixes: `\*\picprop` groups inside `\pict` are now skipped (their property names contain hex-like letters that were corrupting image data); `char_by_code()` in `rtf_tools.h` now emits proper UTF-8 (CP1252 → Unicode → UTF-8) instead of raw bytes — Norwegian/non-ASCII characters now render correctly. Menu item greyed for non-RTF tabs.
- **RTF toolbar on startup**: `Ne_DocIsRtf()` returns `true` for untitled RichEdit tabs; `Ne_UpdateToolbarMode` uses `Ne_DocIsRtf` — Rich Text toolbar shows immediately on launch without opening a file first.
- **File → Open reuses untitled tab**: if the active tab is an untouched untitled RichEdit tab, the opened file loads into it directly instead of creating a new blank tab.

## v2026.05.16.11 - 16.05.2026 11:41

- MSB custom scrollbars now on Scintilla code tabs in addition to RichEdit: `s_sciSbV`/`s_sciSbH` maps, `Ne_AttachSciScrollbars`, `Ne_DetachSciScrollbars`. `SCN_UPDATEUI` → `msb_sync` keeps the thumb in sync during keyboard scrolling. Bug fix: `Ne_AttachScrollbars` was missing from the RTF branch of `Ne_LoadPathIntoEditor` — added.
- Auto-close bracket/quote pairs (both RichEdit and Scintilla): typing `{`, `[`, `(`, `"`, `'`, `«` inserts the matching closer and leaves the caret between them. Typing a closing char when the same char already follows the caret jumps over it instead of inserting a duplicate. `Ne_SciAutoPair` (called from `SCN_CHARADDED`) handles Scintilla; a `WM_CHAR` handler in `Ne_EditCaretProc` handles RichEdit. The RichEdit handler also wraps selected text when an opener is typed with a non-empty selection.
- Save to FTP — profile list picker (`Ne_ShowFtpSelectDialog` / `Ne_FtpSelectDlgProc`): connected profiles shown as a vertically-stacked list of full-width blue owner-draw buttons, no server cap (old code was limited to 3). Even a single connected profile requires explicit selection. Not-connected dialog message corrected: was showing `FTP_STATUS` = "Connected:", now shows `FTP_NOT_CONNECTED`.
- Locale: `FTP_NOT_CONNECTED`, `FTP_PICK_PROFILE` added to `locale/en_GB.txt`.

## v2026.05.16.10 - 16.05.2026 10:47

- Custom autocomplete popup component (`ne_autocomplete/`): `NsbAutoComplete` window class, `CS_DROPSHADOW`, `WS_EX_NOACTIVATE | WS_EX_TOPMOST`. Appearance matches the tooltip style — system tooltip yellow background, dark amber border `RGB(120,100,20)`, muted sage green selection `RGB(80,160,110)` with white text. DPI-aware 12pt Segoe UI font via `GetDpiForWindow` + `MulDiv`.
- Replaces `SCI_AUTOCSHOW` for both keyword and phrase-completion modes. Scintilla's built-in popup cancelled itself when the entered text contained non-word characters (spaces, `=`, `$`); the custom popup has no such restriction.
- Phrase completion: when the line-prefix (trimmed) contains a space, all matching whole lines from the document are collected as candidates (case-insensitive prefix match, deduplicated with `unordered_set`, up to 30 results, sorted).
- Keyword completion: same custom popup as phrase mode — consistent yellow/green look, same keyboard and mouse behaviour.
- Popup shows up to 9 items; scrollable with ▲/▼ arrows and mouse wheel when list is longer.
- Keyboard: ↑/↓ navigate; Tab/Enter accept; Escape dismiss; Backspace/Delete dismiss and pass through to Scintilla. Tab/Enter acceptance: `WM_KEYDOWN` consumed via `pendingAccept` flag; next `WM_CHAR` swallowed so no stray character is inserted into the document.
- Mouse click acceptance: `WM_MOUSEACTIVATE → MA_NOACTIVATE` + `WM_KILLFOCUS` guard (skip dismiss if focus went to popup window) ensures item clicks always work.
- Scintilla HWND subclassed while popup is visible; restored on every dismiss path. `g_acInserting` flag prevents autocomplete re-triggering during `SCI_REPLACESEL` insertion.
- Popup positioned below caret line; flips above if it would extend past the monitor bottom edge.
- `makeit.bat`: taskkill output now shown (was suppressed); 1-second `timeout` added after kill so the linker never fails with "Permission denied" on `NSBEdit.exe` when the app was still running.
- New files: `ne_autocomplete/ne_autocomplete.h`, `ne_autocomplete/ne_autocomplete.cpp`, `API_INTERNALS/API/ne_autocomplete_API.txt`.

## v2026.05.16.09 - 16.05.2026 09:58

- RichEdit line-number gutter (`NsbLineGutter`): custom child window class that renders line numbers alongside RichEdit tabs. Always present as a thin strip (S(20)) even when numbers are off; expands to full width (S(44)) when on.
- `Ne_EnsureLineGutter()`: creates the `NsbLineGutter` window for a RichEdit doc and attaches a tooltip ("Show / Hide line numbers").
- `Ne_SyncRichGutters()`: repositions all gutters and trims the editor rect after `NeTabs_SetRects`. Scintilla tabs get the thin strip only (Scintilla draws its own margin); RichEdit tabs get full or thin width depending on `s_lineNumsOn`.
- `Ne_SyncLineNumBtn()` rewritten: iterates all tabs, sets `SCI_SETMARGINWIDTHN` on Scintilla windows and invalidates / updates tooltip on RichEdit gutters. Calls `Ne_SyncRichGutters` at the end.
- `s_lineNumsOn` global persists across tab switches; `Ne_SetupScintillaStyle` reads it to set the initial margin width.
- `NeTabDoc::hLineGutter` field added to store the companion gutter HWND.
- Autocomplete: `SCI_AUTOCSETIGNORECASE TRUE` set in `Ne_SetupScintillaStyle`.

## v2026.05.15.16 - 15.05.2026 16:56

- Ne_ApplyLang(hSci, langIdx) added: sends keyword list via SCI_SETKEYWORDS and applies per-lexer style overrides. Previously no keywords reached the lexer so all text stayed black.
- PHP switched from hypertext to phpscript lexer; SCE_HPHP_* style IDs (118-127) mapped: strings red, keywords blue/bold, $variables purple, comments green italic, numbers green.
- Language menu on RichEdit tab now converts to Scintilla on demand: extracts plain text, creates Scintilla at same position, loads text, hides RichEdit, applies chosen lexer.
- AltGr fix: Ctrl-shortcut intercept in message loop now skips when VK_RMENU (Right Alt / AltGr) is held — AltGr+0 (}), AltGr+7 ({) etc. now reach the editor correctly.
- File > Save to FTP... (IDM_SAVE_TO_FTP): uploads active document to any connected FTP server. FTP browser in save mode — filename edit + Save here (green) / Cancel (red) buttons. Keeps connection open. Marks tab as FTP-linked on success.
- Ne_ShowFtpBrowserSave: save-mode FTP browser dialog — filename label + edit above tree, Save here / Cancel at bottom, Refresh top-right.
- Locale: MENU_SAVE_TO_FTP, FTP_SAVE_BROWSER, FTP_FILENAME_PROMPT, FTP_SAVE_HERE, FTP_SAVE_TO, FTP_SAVED_OK added to locale/en_GB.txt.

## v2026.05.15.12 - 15.05.2026 12:15

- Credits dialog added: accessible via About → Credits. Sections for Scintilla, Lexilla, GDI+, and MinGW-W64, each with description and link. Rendered in a RichEdit pane with colour-coded headers.
- About, License, and Credits dialogs converted to owner-draw button system (NeBtnTone / NeDialogButtonSpec / Ne_DrawDialogButton / Ne_BtnHoverProc) — DPI-aware measured widths, hover highlight, icon + text layout.
- Locale additions: ABOUT_BTN_CREDITS, ABOUT_BTN_CLOSE added to locale/en_GB.txt.

## v2026.05.14.10 - 14.05.2026 10:51

- Added Insert Hyperlink dialog (URL + display text, Tab navigation, Save/Cancel owner-draw buttons).
- URL field validated with std::wregex: requires scheme (https/http/ftp(s)/mailto/file/www.), host with dot-separated labels, and 2–4 alpha TLD.
- Invalid URL shows custom NSBEdit warning dialog (IDI_WARNING icon + OK button) instead of MessageBoxW.
- Ctrl+Click follows hyperlinks: Ne_EditCaretProc intercepts WM_LBUTTONDOWN while cursor is IDC_HAND, extracts URL from RTF field instruction via Ne_ExtractLinkUrlAt, opens with ShellExecuteW.
- Hover tooltip on hyperlinks: two-line ShowMultilingualTooltip (URL on line 1, Ctrl+Click hint on line 2) triggered by IDC_HAND cursor detection in WM_MOUSEMOVE subclass.
- Ne_ShowChoiceDialog extended with optional hMsgIcon parameter; dialog font upgraded from DEFAULT_GUI_FONT to 12pt Segoe UI (Ne_CreateDialogFont), stored in NeDialogData and freed on WM_NCDESTROY.
- ENM_LINK added to all EM_SETEVENTMASK calls; EN_LINK WM_LBUTTONDOWN fallback handler in WM_NOTIFY.
- Link dialog: static bool registered guard, COLOR_WINDOW+1 background, WM_CTLCOLORSTATIC, AdjustWindowRectEx sizing, button height S(34).
- API/INTERNALS rebrand: all SetupCraft references in API_INTERNALS/**/*.txt replaced with NSBEdit.
- Locale: LINK_TIP_CTRL, MSG_LINK_BAD_URL, BTN_OK added to locale/en_GB.txt.

## v2026.05.11.13 - 11.05.2026 13:11

- Table properties dialog: Apply and Cancel owner-draw buttons now work correctly.
- Table values (rows, cols, column width, borders, padding, row height, alignment) are read in the window procedure before DestroyWindow, stored in a module-level NeTableProps struct, so Apply always inserts/alters the table with the user's chosen values.
- Apply button inserts a new table when caret is outside a table.
- When caret is inside a table, a mode radio group appears at the top of the dialog: "Alter current table" (pre-selected) or "Table in current cell". Alter mode replaces the table by scanning \intbl paragraphs around the caret; nested mode inserts a fresh table at the caret position.
- Buttons centred horizontally in the dialog.
- All new strings fully localised: TBLP_MODE_ALTER, TBLP_MODE_NESTED added to locale/en_GB.txt.

## v2026.05.11.09 - 11.05.2026 09:59

- All UI fonts unified to 12pt Segoe UI (DPI-aware via GetDpiForWindow/GetDpiForSystem) — toolbar, dialogs, status bar, tooltips.
- Owner-draw menus with 12pt Segoe UI: white background, correct highlight/grayed colours, right-aligned accelerator text.
- Menu bar items (File/Edit/Help) also owner-drawn at 12pt via Ne_AppendMenuOD(isBar=true).
- Status bar: correct shell32.dll icons — index 294 (green checkmark = Saved), index 131 (red X = Unsaved).
- Added ne_statusbar.h/cpp: custom owner-drawn status bar with word/char count and Saved/Unsaved status with shell32 icons.
- Added ne_tabs.h/cpp: tabbed editor with owner-drawn × close glyphs, hover highlight, [+] new-tab button, right-click context menu.
- Tab context menu: New Tab / Close Tab (localized).
- Ctrl+W with one tab closes the app; with multiple tabs closes the active tab.
- Softer owner-draw dialog button colours with hover state tracking.

## v2026.05.11.08 - 11.05.2026 08:38

- Replaced the system save-changes MessageBox with a custom NSBEdit modal dialog.
- Added owner-draw icon buttons for Save, Don't Save, and Cancel.
- Added focus-based external file-change detection using file stamp comparison.
- Added a custom Reload/Keep Current dialog when a file changed on disk while unfocused.
- Added reusable Ne_ShowChoiceDialog and unified Ne_LoadPathIntoEditor load path.
- Added localized keys for new dialog titles, prompts, and button labels.

## v2026.05.10.16 - 10.05.2026 16:25

- Added Edit menu: Undo, Redo, Cut, Copy, Paste, Select All.
- Added right-click context menu on RichEdit with dynamic enabled/disabled states.
- Added Export as PDF via Microsoft Print to PDF (Ctrl+Shift+P).
- Added Keyboard Shortcuts dialog (F1) with bold shortcut column and royal-blue descriptions.
- Added Ctrl+W shortcut for Exit and updated menu accelerator hints.
