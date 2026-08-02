# Changelog
## v2026.08.02.09 (Session autosave made dead-solid: no clobber on file-launch, background code/FTP tabs saved correctly, FTP edits survive restart; Swedish completed) - 02.08.2026 09:27

- **Critical fix: opening a file no longer destroys your other tabs**: launching NSBEdit **with a file** (double-clicking a document in Explorer, a file association, or any command-line path) deliberately skips session restore and shows only that one file — but the 10-second autosave and the on-close save then rewrote the session table (`DELETE` + re-insert) with just that single tab, **permanently discarding every other tab from your saved session, including unsaved ones**. NSBEdit now tracks whether the running process **owns** the saved session: it only owns (and may overwrite) the session when it actually **restored** it at startup, or when there was **no** saved session to begin with. A file-launch that happens while a saved session exists no longer touches the session at all, so your full set of tabs — unsaved untitled documents, local files and FTP/SFTP tabs — comes back intact on the next normal launch. (Confirmed against the on-disk database, whose session table had been reduced to a single row by the old behaviour.)
- **Critical fix: background (inactive) code/FTP tabs were autosaved empty**: the session autosave decided a tab's type (Scintilla code vs RichEdit) and read its content using `IsWindowVisible` — but only the **active** tab's editor is visible, so every **inactive** code or plain-text tab was misclassified as RichEdit and its content read from the wrong, empty control. Inactive code tabs (including FTP-opened ones) were therefore stored as an empty RTF stub and would come back blank. Tab type is now determined by the tab's actual editor handle (`doc->hSci`), independent of which tab is on top, so background tabs save their real content. Verified in the database: an inactive FTP `index.php` tab that used to store a 168-byte empty RTF now stores its full 9 KB of source.
- **Fix: unsaved edits to FTP files survive a restart**: on session restore an FTP tab always re-downloaded the file and reloaded that fresh copy, discarding any **unsaved local edits** whenever the server copy happened to be unchanged. A modified FTP tab now restores your **cached edited content** (and stays marked unsaved); only clean FTP tabs reload from the server.
- **i18n: Swedish (sv_SE) completed**: the Swedish locale was a near-stub falling back to English; it is now fully translated — menus, dialogs, tooltips, spell-check, tables, horizontal-rule and paragraph/line-spacing dialogs, the FTP browser and profile editor, keyboard-shortcut names and descriptions, the About/Credits text, and session-restore messages — plus the new `BTN_FIND` and the interface-language menu (**GUI-språk**, distinct from the code-language **Språk** menu). Technical tokens (encodings, file filters, `%d × %d`, `r/w/x`, brand names) stay identical, and the Regex Guide falls back to English until translated. This completes the Scandinavian set: **Danish, Norwegian and Swedish**.

## v2026.08.02.08 (Robustness: DB autosave + empty-overwrite save guard; AI Clear confirmation; search on Find Next only; Northern Sami removed; da/de/el/nl/no locales completed) - 02.08.2026 08:46

- **Robustness: a file can no longer be lost to an empty session restore**: three layers now protect your work. (1) The session database is a full **autosave/backup** — the content of *every* open tab is stored (not just modified/FTP ones) on the 10-second timer and on close. (2) On restore, if a clean file loads from disk as **empty** while the database still holds a non-empty copy (a truncated/glitched read), the last-known content is **recovered from the database** and the tab is marked modified so it can never be written back empty. (3) A final backstop: **Save now refuses to overwrite a non-empty file on disk with an empty buffer** without an explicit confirmation. This closes the exact scenario where an emptied tab, saved on exit, wiped the real file.
- **New: confirm before clearing the AI conversation**: the AI **Clear** button (and *Clear log* menu) now asks a **Yes/No** question first, so the whole conversation is never wiped by an accidental click. Uses a new shared owner-draw confirmation dialog.
- **Change: search runs only on Find Next / Enter**: the Find box no longer searches and highlights *while you type*. Nothing happens until you press **Find Next** or **Enter**; typing or toggling any option (Match case, Whole word, Regex, All tabs, Backwards) simply clears the previous highlights. The button reads **Find** until the first match is highlighted, then **Find Next** afterwards. This also fixes Whole word / options not taking effect until the text was retyped.
- **Change: Northern Sami removed**: the `se_NO` locale was only ~10% translated (the rest fell back to English), so it has been removed from the **GUI Language** menu and the build. A previously saved Sami selection now falls back to English.
- **i18n: Danish, German, Greek, Dutch and Norwegian completed**: the remaining English-fallback strings in `da_DK`, `de_DE`, `el_GR`, `nl_NL` and `no_nb` have been translated (session-restore messages, AI window strings, reload-file entries, the new `BTN_FIND`, empty-overwrite and Clear-confirmation strings). The Norwegian **About** page now uses the same icon + bullet-list section layout as the English one, and the interface-language menu reads **GUI Språk** so it is distinct from the code-language **Språk** menu. Technical tokens (encodings, file filters, `%d × %d`, `r/w/x`, brand names) intentionally stay identical, and any untranslated key still falls back to English.

## v2026.08.01.10 (AI: paste images into the query; accumulating answers + live cloud model list; green active tab; new-file .txt default) - 01.08.2026 10:59

- **Feature: paste images into the AI query (multimodal)**: you can now paste an image straight into the AI query box. It appears **inline in the text flow** (sized big enough to read, capped at ~420 px wide) and can be mixed freely with typed text. The image is **sent to Ollama** alongside your prompt (in the request's `images` array), and it stays visible in the **query echo, the answer, and every previous answer** — the whole conversation, images included, is **saved to the database** and restored the next time you open NSBEdit. Text-only models (the usual coding models such as `qwen3-coder`) simply ignore the image; select a **vision model** in the **Model** menu — e.g. `llava`, `llama3.2-vision`, `qwen2.5vl` — to have the picture actually understood.
- **Feature: the AI window keeps every answer**: answers are no longer wiped on each new question. Each prompt + reply is appended below the previous one with a divider between them and the newest scrolled into view, so you can scroll up through the whole conversation. New **Prev / Next** buttons (between **Copy** and **Clear**) jump from answer to answer, and **Clear** wipes them all. The conversation is **saved to the database**, so your answers are restored the next time you open NSBEdit — until you clear them. Streaming still shows the live text as it arrives and renders it (with copyable, syntax-highlighted code cells) once the answer completes.
- **Feature: the cloud model list is live**: opening the AI window now fetches the current cloud catalogue from `ollama.com/api/tags` (using your **Cloud → Add API key**) in the background and rebuilds the **Model** menu, so **retired models disappear** (e.g. `qwen3-coder:480b`) and **newly-released ones show up** automatically. Without an API key it falls back to the built-in curated list.
- **Change: Check local models moved to the Model menu**: it now sits between the **Local** and **Cloud** model groups (with a divider above and below) instead of in the Cloud menu, where it belongs with the local models.
- **Feature: the active tab's name is green**: the current tab's file name is drawn in green — `rgb(59, 125, 59)` in light mode and a lighter green in dark mode — bold, with a standout background and blue accent, so the active page is easy to spot. Tabs are now custom-painted in both light and dark modes.
- **Fix: new files are plain text, not RTF**: a brand-new (Ctrl+N) file no longer asks to "strip formatting" on save, and **Save As now defaults to `.txt`** (with the Text filter preselected) for a plain document. Only a document actually converted to RTF via *Convert → Add formatting* (or an opened `.rtf`) defaults to `.rtf`. The uniform editor-theme text colour is no longer mistaken for user formatting.
- **Fix: restored blank tab keeps the dark editor background**: an empty untitled tab reopened from the saved session came back with a white background instead of the dark text area; it now matches a fresh Ctrl+N tab.

## v2026.08.01.08 (Fixes: Find/Replace on code tabs; AI query-box paste; autocomplete Esc; DB-only autosave) - 01.08.2026 08:34

- **Fix: search highlight missed the phrase on code tabs**: on a code (Scintilla) tab, Find highlighted/selected the wrong spot — and the further down the file, the bigger the miss. Matches were computed in UTF-16 character positions but Scintilla addresses text in UTF-8 byte offsets, so every multi-byte comment glyph (box-drawing `─ │`, bullets `•`, dashes `—`, arrows, accented letters) shifted the selection. Match ranges are now converted to byte offsets before the editor jumps to them, in both single-tab and all-open-tabs search. (RTF tabs were unaffected.)
- **Fix: Replace / Replace All did nothing on code tabs**: replace only ever ran on RTF (RichEdit) documents; on a code tab the RichEdit messages were no-ops, so nothing changed. Replace now has a proper Scintilla path — Replace All rewrites from the last match to the first inside a single undo step, and single Replace swaps the current match and finds the next.
- **Fix: could not type or paste a multi-word Find phrase**: while typing, an intermediate prefix that did not exist popped a modal *“text not found”* box that stole focus, so the next characters (including the space) never reached the box. The not-found message now appears only on an explicit **Find Next** / Enter, so multi-word phrases type and paste normally.
- **Fix: AI query box — pasted text was nearly invisible or could crash the app**: pasting into the AI query box with **Ctrl+V** went through RichEdit’s built-in paste, which bypassed the box’s safe paste handler. Text copied from a dark editor tab kept its light colour (invisible on the white box), and text copied from a terminal (CMD/PowerShell) could corrupt the control and crash the app a moment later (typically on the next **Send**). Ctrl+V and Shift+Insert now route through the safe handler that inserts sanitized, solid-black plain text, and stray control characters are stripped.
- **Fix: multi-line AI prompt truncated on Send**: a pasted multi-line prompt was cut short when sent because the buffer was sized for single-CR line breaks while the control returns CRLF. The whole prompt is now read intact.- **Fix: pressing Esc to close the code autocomplete inserted a literal “ESC” glyph**: on a code tab, dismissing the autocomplete list with **Esc** left an inverted `ESC` control character (0x1B) in the document. The key-down was swallowed but the follow-up `WM_CHAR` reached Scintilla after the popup’s subclass had already been removed. Esc now defers the dismissal until that character is swallowed (the same way Tab/Enter already do), so the list closes cleanly and nothing is typed.
- **Change: autosave conserves to the database only — it no longer “saves” the file**: the 10-second autosave used to write the real file on disk and flip the status line to **Saved**, even though you had not saved. It now only conserves the live tab content to the database for crash / restart recovery; the document stays **Unsaved** and the file on disk is untouched until you explicitly save (**Ctrl+S / Save / Save As**). After a crash or restart the unsaved content is restored from the database, still marked as unsaved.## v2026.07.30.09 (All translations complete) - 30.07.2026 09:09

- **All translations to all languages are complete** (30.07.2026, 09:09):
  - Locale files for every supported language have been reviewed, corrected and marked as complete.

## v2026.07.10.15 (Fix: show the real Ollama cloud error instead of blaming the API key) - 10.07.2026 15:34

- **Fix: honest cloud error messages**: when a cloud request failed, the AI window always showed *"Ollama cloud rejected the API key — check Cloud → Add API key"*, even when the key was perfectly valid. It now reads the daemon's actual error from the response and shows it verbatim — for example, a premium-only model such as `deepseek-v3.1:671b-cloud` returns *"this model requires a subscription, upgrade for access: https://ollama.com/upgrade"*. Only a genuine **HTTP 401** (key actually rejected) still points you to re-enter the key via **Cloud → Add API key**. The other cloud models (`qwen3-coder:480b-cloud`, `gpt-oss:120b-cloud`, `gpt-oss:20b-cloud`) work on the free tier.

## v2026.07.10.14 (Feature: browser-free Ollama cloud sign-in with an encrypted API key; fix intermittent tiny AI menu font) - 10.07.2026 14:09

- **Feature: browser-free cloud sign-in with an API key**: a new **Cloud → Add API key…** entry opens a dialog where you paste your ollama.com API key. With a key you never need the browser `ollama.com/connect` authorization or `ollama signin` — the AI window shows **Account: signed in** as soon as the key validates. The menu item has a hover tooltip explaining what it is for.
- **Feature: cloud requests go straight to Ollama cloud**: when a key is set, `-cloud` models are sent directly to `https://ollama.com/api/generate` with an `Authorization: Bearer` header (over WinHTTP/schannel, so TLS uses the Windows certificate store), instead of the local daemon. Streaming, the **Stop** button and the **Please Wait** spinner behave exactly like local mode.
- **Security: the API key is stored encrypted**: new encrypted profile settings (`NeProfiles_SetSecretSetting`/`GetSecretSetting`) run the value through the same **AES-256-CBC** `NeCrypto` used for FTP passwords, with the master key **DPAPI-wrapped** per Windows user. The key is never written to the DB in plaintext, never appears in the repo or the distributed zip, and `.gitignore` additionally blocks any `*api*key*` / `*.db` file from being committed. The key lives only in `%APPDATA%\NSBEdit\nsbedit.db` and survives upgrades/reinstalls (the installer preserves an existing AppData DB and the uninstaller leaves AppData untouched).
- **Change: sign-in state follows the key**: with a key configured, the account is **Signed in** while the key validates (`GET /api/tags`); **Sign out** forgets the key locally, signs the daemon out, and reverts to your last-used local model.
- **Fix: AI menu sometimes rendered in a tiny font**: the owner-drawn menu font (a global `HFONT`) was freed when the AI window closed but the handle was not cleared, so the next time the window opened it reused the dangling handle, `SelectObject` failed, and Windows fell back to the default (tiny) system font. The handle is now cleared on destroy, so the menu always uses the 12 pt Segoe UI owner-draw font.
- **Fix: AI menu tooltip no longer steals focus**: the hover tooltip for **Add API key** used the shared multilingual tooltip window, which is owned by whichever top-level window creates it first (normally the main editor). Showing that editor-owned window from the separate AI window pulled the editor forward and sent the AI window behind. The AI window now uses its **own** tooltip window (owned by the AI window, identical yellow look, `WS_EX_NOACTIVATE`), so it never steals activation.
- **i18n**: translated the new API-key strings (`AI_MENU_ADD_API_KEY`, `AI_APIKEY_TITLE`, `AI_APIKEY_PROMPT`, `AI_TIP_ADD_API_KEY`, `AI_LOG_API_KEY_OK`/`_BAD`/`_CLEARED`) into all 14 non-English embedded locales (Northern Sami `se_NO` left in English pending a Sami translation).

## v2026.07.10.12 (Feature: working Ollama cloud mode with cloud-model picker; honest sign-in detection; Edition 3 final) - 10.07.2026 12:46

- **Fix: honest sign-in state**: the AI window used to blindly trust a cached `ai.signed_in` flag, so signing out with `ollama signout` while NSBEdit was closed still showed **Account: signed in** with the Cloud models enabled. The window now asks the local Ollama daemon for the real state (`POST /api/me` &mdash; signed in returns 200, signed out returns 401) every time it opens or is reactivated, and adopts the truth. If the daemon is unreachable the last known state is kept.
- **Fix: auto-fall back to a local model when signed out**: when the daemon reports signed-out, cloud mode is switched off automatically and the model reverts to your **last-used local model** (or, if that model is no longer available, the **first model in the Local list**), so you are never stranded on a greyed-out cloud model.
- **Feature: Ollama cloud mode now actually works**: cloud mode was previously a stub that only logged a placeholder. It now streams real answers through the same path as local mode, so the **Stop** button and the **Please Wait** spinner work in cloud mode exactly like local. Cloud models are served through the signed-in local Ollama daemon (`ollama signin`), so no separate API key handling is needed.
- **Feature: choose a cloud model**: the **Model** menu now lists real Ollama cloud models — `qwen3-coder:480b-cloud`, `gpt-oss:120b-cloud`, `gpt-oss:20b-cloud`, `deepseek-v3.1:671b-cloud` — plus any `-cloud` models your local Ollama already reports. Cloud entries are greyed until you sign in (Cloud → Sign in to Ollama). The selection is persisted (`ai.cloud_model`) and shown in the title, header and status bar.
- **Feature: Model menu section labels**: the menu now shows greyed **"Local:"** and **"Cloud"** headers above the two model groups.
- **Fix: doubled cloud answers**: cloud reasoning models (qwen3-coder, gpt-oss, deepseek) stream a `thinking` field alongside `response`, and the client appended both — duplicating every token and corrupting code fences. The client now uses `response` for the answer (with `thinking` only as a fallback when a line has no `response`), so the answer is clean and the chain-of-thought no longer leaks into it.
- **Fix: code block split across the cell boundary**: on fast cloud responses the completion could render a *partially* pumped reply, cutting a code block mid-line and appending the tail as raw text below the code cell (so it was not copyable). Completion now adopts the worker's complete reply, clears the pending live-typing queue and stops the typing timer before the final render, so code always renders in a single clean, fully-copyable cell.
- **Change: no debug logging in the release**: the raw-reply debug file (`ai.txt`) is no longer written.
- **Change: Edition 3 is final**: the About dialog now shows **Edition: 3** (the "RC" suffix has been removed) in all 15 UI languages.
- **i18n**: added a translated **AI cloud** section (`AI_LOG_CLOUD_MODEL_SELECTED`, `AI_LOG_CLOUD_SENDING`) and the Model-menu headers (`AI_MENU_LOCAL_HEADER`, `AI_MENU_CLOUD_HEADER`) to all 15 locale files; also added the missing `ABOUT_EDITION_VALUE` key to the French locale.

## v2026.07.09.17 (Feature: right-click spelling suggestions; fix squiggles on loaded/restored RTF tabs) - 09.07.2026 17:23

- **Feature: right-click spelling suggestions**: right-clicking a misspelled (squiggled) word in an RTF document now shows replacement suggestions at the top of the context menu, plus **Ignore** and **Add to Dictionary**. Clicking a suggestion replaces the word in place — no need to run the full `F7` spell-check dialog just to fix one word. Suggestions come from the live `ISpellChecker`; the change flows through the normal edit path so it marks the document modified and refreshes the squiggles.
- **Fix: squiggles missing on loaded and session-restored RTF tabs**: the code that *paints* the red squiggles lives in the RichEdit caret/paint subclass, which was only installed on brand-new tabs (`Ne_New`). Opened and session-restored tabs never received that subclass, so they showed no squiggles at all even though the words were misspelled. The subclass — together with the `EN_CHANGE` notification mask — is now installed on **every** RichEdit at creation, so new, opened, and restored tabs all behave identically.
- **Fix: typing in a loaded/restored tab did nothing**: the custom scrollbars (`msb_attach`) reset the RichEdit event mask during load, and only `Ne_New` re-asserted it. Opened/restored tabs were left with `ENM_NONE`, so `EN_CHANGE` never fired — the document stayed "Saved" after typing and the spell scan never ran. The mask is now re-asserted after every load.
- **Fix: opened RTF files now scan immediately**: the file-open path never triggered a spell scan, so squiggles only appeared after the first keystroke. Opening an RTF file now scans it right away when *Mark Misspelled Words* is on.
- **Fix: squiggle drawn through the middle of the word on single-line documents**: with no adjacent line to measure from, the underline fell back to the control's default-font height and landed mid-word for larger text. It now builds the caret's real font at the current zoom (DPI-aware) and measures its height, so the squiggle sits ~2 px below the text and recalculates on **Ctrl+ / Ctrl−** zoom.
- **Fix: restored tab with an uninstalled per-document language**: if a session-restored tab carried a spell language whose dictionary is not installed, it now falls back to the app locale default instead of showing no squiggles.

## v2026.07.09.13 (Feature: AI Stop control, full-project file access, and window geometry persistence) - 09.07.2026 13:44

- **Feature: Stop the AI mid-request**: a red **Stop** button (and the **Esc** key) in the AI window interrupts an in-progress request so you can add more detail and resend. The streaming transfer is cancelled cooperatively, the busy spinner is dismissed, and your prompt is restored for editing. A matching **Stop** button also appears below the animation in the "Please Wait" spinner.
- **Feature: AI reads the whole project**: file references now expand **directories**, **glob wildcards** (`locale/*`, `*.txt`, `**/*.cpp`), and explicit **regex** (`regex:…`, `/…/`). A **regex content search** greps every file line-by-line and returns matching lines with context. The full project file list is sent (no small cap) and sorted shallow-first, so your own files — such as all `locale/*.txt` — are always visible instead of being crowded out by vendored libraries.
- **Feature: batch reading is fast and parallel**: anything belonging to the project (files and a new DB knowledge store) can be read in batch mode. Each normal file is one whole request (no needless splitting); only genuinely huge files are chunked. The batch requests run in parallel across CPU cores with a larger context window, and a simple question is answered in one fast request instead of grinding through every file.
- **Feature: the AI window is its own app**: the AI window now has its own taskbar button and app icon and can be **Alt+Tab**'d to independently of the editor.
- **Feature: windows reopen as you left them**: the editor and the AI window remember their size, position, and maximized state, saved continuously to the database so the geometry survives a crash. A window closed while minimized reopens at its default size.
- **Fix: session restore no longer self-destructs**: a single pristine (never-touched) untitled tab can no longer overwrite a saved multi-file session, so a one-off restore hiccup can't wipe your open files — the session self-heals on the next open.
- **Fix: dark editor on a fresh tab**: a new untitled tab in a light UI with the dark-editor option no longer flashes up with a white background.
- **Fix: pasted AI code keeps real newlines**: copying from an answer (a code block, `Ctrl+C`, or right-click Copy) now normalises line breaks to CRLF, so code no longer pastes with stray `VT` characters where the newlines should be.
- **Fix: right-click Copy anywhere in an answer**: the answer area's context menu now offers **Copy** for any text selection, not only inside a code block.
- **Fix: the wait spinner behaves**: the "Please Wait" spinner is now owned by the AI window instead of floating on top of every application, and it no longer steals focus — so you can keep editing in the main window (or any other app) while Ollama is working in the background.
- **i18n: Stop button and log strings added to all 15 locales**, and the AI button labels (Send/Copy/Clear/Stop) are now properly translated per language instead of always English.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.07.09.13`.

## v2026.07.06.11 (Feature: AI project control — suggestion-mode workspace, code search, and big-file batch reading) - 06.07.2026 11:30

- **Feature: Project menu and workspace**: a new **Project** top-level menu (built like the FTP menu) lets you point the AI at a project root folder. The active project is marked with a check-tick (like the GUI-language menu), stored in the shared SQLite database, and the selection survives closing and re-opening the app. Add a project via a standard folder picker; remove the current one from the menu.
- **Feature: AI suggestion mode reads your project**: when a project is active, the AI receives the project structure and relevant files and is told to answer directly and point you at the exact file and location for any suggested code. Linked folders (OS symlinks / junctions such as `MyStyle`) inside the root are followed as if they were part of the workspace.
- **Feature: filename typo auto-correction against disk**: a file the AI is asked to read is matched against what is actually on disk, segment by segment, so a mistyped path like `.\;yStyle\API_list.txt` is understood as `MyStyle/API_list.txt`, resolving straight through junctions.
- **Feature: code search**: the AI greps the project for a named symbol or function (anything you put in `` `backticks` `` plus identifier-like words) and receives just the matching snippet with surrounding context, so it can analyse or rewrite a function without ingesting the whole file.
- **Feature: large files read in batches**: a big referenced file that has no matching symbol is split into batches; each batch is analysed and a final answer is synthesised (map-reduce), so files with thousands of lines can be handled without exceeding the model context.
- **Feature: spinner progress**: the wait spinner now shows a rising best-guess percentage while the model is queried, and real `Reading batch i/N (nn%)` → `Synthesising answer` progress during batch reading, so it never looks frozen.
- **Fix: responsiveness and context window**: the Ollama request now sets `num_ctx` and the project context is bounded, so answers start quickly instead of stalling for minutes when a large file is involved.
- **Fix: answer rendering**: bold/italic no longer leak through the rest of the answer, inline `` `code` `` renders in a monospace box, and headings, block quotes, and bullet lists render correctly — with a single renderer used for every answer.
- **Change: Agent mode removed from the Model menu**: this is a suggest-mode-only build for the public release; the Model menu lists only the Suggest entries.
- **Change: stale build logs cleaned up**: `makeit.bat` now deletes stale `build*` / `output.txt` logs on each build, and the AI project scan skips build logs and scratch files so outdated errors can no longer mislead the model.
- **i18n: Project-menu and progress strings added to all 15 locales.**
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.07.06.11`.

## v2026.07.05.22 (Feature: syntax-highlighted AI code blocks, code-cell copy UX, and custom answer scrollbar) - 05.07.2026 22:16

- **Feature: AI code blocks are now syntax highlighted**: fenced code in the rendered answer is coloured with the same Scintilla/Lexilla lexers the editor uses, mapped from the fence language, so keywords, strings, and comments show in colour inside the bordered code box.
- **Feature: double-click selects only the code in a cell**: double-clicking inside a rendered code box now selects just that block's code text, no longer spilling into the padding lines or the prose around the box.
- **Feature: Ctrl+A and Ctrl+C are scoped to the code cell**: pressing Ctrl+A while inside a code box selects only that cell's code, and Ctrl+C copies the selection without the empty buffer lines above and below, so the snippet pastes straight into existing code.
- **Feature: right-click Copy on code boxes**: a right-click inside a rendered code cell now shows an owner-drawn Copy menu that copies the current selection, or the whole cell's code when nothing is selected.
- **Fix: the answer pane now uses the custom scrollbar**: the non-working native scrollbar was removed from the answer host and the rendered answer now fills it, so the custom MSB scrollbar on the right is the only vertical bar.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.07.05.22`.

## v2026.06.28.11 (Status: single RichEdit AI render now shows query, prose, and the first code block) - 28.06.2026 11:04

- **Status: the AI answer now keeps the first render blocks together**: the final RichEdit page shows the query, the text before the first code block, and the first code block in one continuous view.
- **Status: the visible code box and copy label stay intact**: the first code block still renders with its bordered box and Copy code header while the prose around it stays in the same answer surface.
- **Status: the remaining work is scroll polish**: streaming should not show a dormant scrollbar, and the rendered RichEdit scrollbar still needs one more pass to track the full content height cleanly.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.28.11`.

## v2026.06.26.09 (Status: query-only final render is working; next step is adding blocks one by one) - 26.06.2026 09:36

- **Status: the final AI render now shows only the query block**: the rendered RichEdit page is now anchored on the first part again, which is the correct stepping stone before adding the rest of the answer.
- **Status: the renderer is now being built in small ordered pieces**: the current process is to make one block perfect, then add the next block, then keep going until the whole page renders cleanly.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.26.09`.

## v2026.06.22.10 (Fix: curl-shipped AI transport and close-focus handoff) - 22.06.2026 10:40

- **Fix: the AI reply pane now starts writing as soon as chunks arrive**: the streamed Ollama response is fed straight into the live typing path, so the answer begins appearing immediately instead of waiting for the end of the request.
- **Fix: curl is now shipped with the app folder**: the package and installer both copy the curl static libraries into the app tree, so the AI transport is no longer a build-only dependency.
- **Fix: closing the AI window now restores the main editor to the front**: the owner window gets the brief topmost nudge on close, then the pin is removed again so it stays usable without remaining always-on-top.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.22.10`.

## v2026.06.20.16 (Fix: live Ollama replies again, with code blocks copyable) - 20.06.2026 16:30

- **Fix: the AI answer pane now updates while the reply streams**: incoming Ollama chunks repaint the live answer text immediately again, so the output appears progressively instead of waiting for the final completion message.
- **Fix: fenced code blocks have a working Copy code header again**: the reply pane now restores a visible copy affordance for code blocks while keeping the rest of the answer plain text.
- **Fix: streamed code and colour hints now appear incrementally too**: the AI pane keeps applying the lightweight live formatter while chunks arrive, so code blocks and simple colour styling show up as the reply is still streaming instead of only after the final render.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.20.16`.

## v2026.06.20.11 (Status: AI reply rendering still needs faithful Ollama output) - 20.06.2026 11:21

- **Fix: the installer no longer waits for an extra Enter at the end**: after Ollama is detected and the models are pulled, the installer exits instead of stopping on a final prompt.
- **Status: the standalone AI pane still needs faithful Ollama rendering**: Ollama answers, but NSBEdit is still not preserving the live code colouring, links, and copy-code affordances that the Ollama app shows while the reply arrives.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.20.11`.

## v2026.06.18.10 (Fix: qwen3-coder is now the default AI model) - 18.06.2026 10:41

- **Fix: the standalone AI window now opens on qwen3-coder by default**: the model loader, install defaults, and saved-preference migration now all prefer `qwen3-coder`, so the dialog no longer comes back on `qwen2.5-coder:7b` after a restart.
- **Fix: installer and local AI docs now match the new default**: the reinstall path seeds `qwen3-coder`, while the internal release notes and AI window notes now describe the new model selection and fallback handling.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.18.10`.

## v2026.06.17.11 (Fix: AI marker contract uses descriptive tags) - 17.06.2026 11:03

- **Fix: the AI shell now uses descriptive code markers instead of markdown fences**: code replies are now driven by the literal `[[CODE_BLOCK_START]]` and `[[CODE_BLOCK_END]]` tags, with the AI prompt explicitly rejecting ```cpp / ``` fences in that path.
- **Fix: the visible Copy code header remains separate from the copied block**: the reply renderer keeps the header and marker labels visible in the answer pane while the copied span is tracked independently through the copy helper.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.17.11`.

## v2026.06.16.12 (Feature: regex guide window; FTP menu cleanup) - 16.06.2026 12:26

- **Feature: regex guide is now a floating help window with its own taskbar entry**: the new regex page opens as a separate window from Help, tracks search-as-you-type, highlights all matches, and keeps the clear button and tooltip behavior in the guide itself.
- **Feature: regex guide explanations are localized**: the guide content now comes from locale text instead of a hardcoded English body, so the same help page can be translated alongside the rest of the UI.
- **Fix: the FTP menu now uses only "Lukk tilkobling" for closing an active connection**: the separate direct-disconnect entry was removed from the FTP popup, so the menu keeps a single close-connection action instead of two overlapping commands.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.16.12`.

## v2026.06.15.15 (Fix: shortcut label now shows Shift+F5) - 15.06.2026 15:58

- **Fix: the Insert Date/Time menu entry now shows Shift+F5**: the shortcut label in the locale menu strings and the API list now matches the actual remapped key binding.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.15.15`.

## v2026.06.15.11 (Fix: AI code boundaries and end-marker handling) - 15.06.2026 11:46

- **Fix: the AI shell now keeps code examples inside explicit boundaries**: the reply renderer now treats the visible `Copy code` label as the start of the block and closes the rendered code again when it reaches a real end marker, a standalone ellipsis, a closing fence, or the next markdown heading.
- **Fix: the AI reply parser now prefers explicit marker lines when they are present**: the model prompt now asks for clear begin/end sentinels around code samples, and the parser uses those boundaries first instead of guessing from the prose around them.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.15.11`.

## v2026.06.14.11 (Fix: About dialog edition line wraps correctly) - 14.06.2026 11:39

- **Fix: the About dialog now forces the edition value onto its own line**: the locale strings for the edition line now end with a newline marker, so the text after `ß` drops below the label instead of staying on the same baseline.
- **Fix: version metadata refreshed for the new release**: `curver.txt`, the build-time version header, and the docs now point at `2026.06.14.11`.

## v2026.06.13.09 (Fix: stable AI window restored and release metadata bumped) - 13.06.2026 09:26

- **Fix: the AI window is back on the stable reply-rendering baseline**: the temporary copy-code experiment was rolled back so the standalone AI pane now tracks the known-good local renderer again.
- **Fix: release metadata and version stamp were updated together**: `curver.txt`, the build-time version header, and the docs now all point at `2026.06.13.09`.

## v2026.06.12.10 (Fix: AI reply renderer, copy answer, and code-link hit testing) - 12.06.2026 10:59

- **Fix: AI replies now unescape model-generated HTML tokens before rendering**: the reply pane now normalises bare `u003c` / `u003e` tokens and common HTML entities before the RichEdit pass, so code examples and inline HTML-like spans are no longer shown as literal escape text.
- **Fix: simple formatting hints are now translated into real RichEdit styles**: the reply renderer now understands basic bold, italic, underline, and strikethrough markup, and it maps simple `<span style="color:...">` hints to actual text colours instead of flattening them to plain text.
- **Fix: Copy code remains visible and clickable**: fenced code blocks keep a visible Copy code label and the click target is measured from the rendered text position and width, not a guessed character span, so the link no longer drifts away from the label.
- **Fix: Copy answer is now available from the answer pane**: right-clicking the rendered answer opens a small Copy popup, and the top Log menu's Copy action targets the rendered answer range instead of the whole log buffer.

# Changelog

## v2026.06.11.15 (Fix: AI window wraps text instead of using sideways scroll) - 11.06.2026 15:10

- **Fix: the AI log and prompt boxes now wrap to the control width**: the standalone AI window now uses RichEdit wrap-to-window mode instead of horizontal scrolling, so long replies and prompts stay readable without a sideways scrollbar.
- **Fix: the AI input still sends on plain Enter**: the prompt box remains multiline, Enter sends the prompt immediately, and Shift+Enter still inserts a newline.
- **Fix: the AI window keeps the centered action row**: Send, Copy, Clear, and Close stay centered under the prompt field, and the output pane still fills the remaining space.

## v2026.06.11.14 (Fix: AI button crash after launch) - 11.06.2026 14:52

- **Fix: the AI button no longer crashes the app**: the AI window now allocates space for all four owner-draw buttons instead of writing past the end of its button table when the window opens.
- **Fix: Enter still sends and clears the prompt immediately**: the AI input box is multiline, but a plain Enter keypress now sends the prompt and empties the field right away, while Shift+Enter still inserts a newline.
- **Fix: the AI window keeps the 1/3 prompt / 2/3 answer split**: the prompt editor takes the top third of the body, the answer pane fills the rest, and both panes are multiline with scrollbars so long conversations and code blocks stay readable.
- **Fix: action buttons stay centered under the prompt field**: Send, Copy, Clear, and Close sit in a centered row below the query box, and Copy uses a clipboard-style icon while Send keeps the Ollama icon.
- **Fix: answers are nudged into formatted markdown**: prompts are prefixed with a short instruction so code responses are more likely to come back fenced and indentation-safe.

## v2026.06.11.11 (Fix: shared AppData DB for portable and installed launches) - 11.06.2026 11:00

- **Fix: portable launches now prefer the shared AppData database**: `NeProfiles_Init()` now resolves `%APPDATA%\NSBEdit\nsbedit.db` first for both installed and portable runs, and if the portable `nsbedit.db` exists but AppData does not yet, the file is migrated upward so FTP profiles and saved settings stay together across upgrades.
- **Fix: portable fallback is only used when AppData is unavailable**: the app no longer silently diverges onto a separate local db when a shared AppData db already exists; the portable file is only kept as a last-resort fallback when the shared path cannot be used.

## v2026.06.11.10 (Fix: installed AI self-heal and packaged Ollama icon) - 11.06.2026 10:07

- **Fix: installed AI prefs now normalise old model tags on load**: the standalone AI window now converts any saved `qwen2.5-coder:7b-instruct` / `qwen2.5-coder:3b-instruct` values back to the valid current `qwen2.5-coder:7b` / `qwen2.5-coder:3b` tags when it starts, so older AppData settings stop forcing the stale `instruct` names.
- **Fix: Ollama icon is now shipped with the release and installer**: `ollama.png` is included in the ZIP and copied by the installer, and the UI resolves it from the executable directory instead of relying on the current working directory.

## v2026.06.11.09 (Fix: Ollama model tags and install-time model pulls) - 11.06.2026 09:44

- **Fix: Ollama requests now default to valid local model tags**: the AI shell now sends `qwen2.5-coder:7b` by default and falls back to `qwen2.5-coder:3b`, with old saved `*-instruct` values still normalised on load so existing installs keep working.
- **Fix: installer now pulls both AI models when Ollama is available**: a fresh install provisions the default and fallback local models up front, so the AI window can answer immediately after setup.

## v2026.06.10.12 (Fix: no shipped FTP db; fresh db on first run) - 10.06.2026 12:36

- **Fix: release ZIP no longer ships `nsbedit.db`**: The packaging step now excludes the personal database file entirely, so FTP credentials and other user-specific records cannot be released by accident.
- **Fix: empty database is created on first run**: Installed builds create `%APPDATA%\NSBEdit\nsbedit.db` on first launch, and portable builds create `nsbedit.db` next to the executable on first launch. If neither path is writable, the app falls back to in-memory mode and warns the user.

## v2026.06.10.11 (Release: Ollama icon + localized AI tooltip + GDI+ startup fix) - 10.06.2026 11:48

- **Feature: AI button now uses the Ollama icon**: The toolbar AI button now loads `ollama.png` and renders the image in the owner-draw handler, with localized text as a fallback if the icon cannot be loaded.
- **Fix: GDI+ now starts for the full app lifetime**: GDI+ is initialized in `wWinMain` so the toolbar icon path is safe in the main window, instead of relying on the About dialog to start it.
- **Fix: AI tooltip is fully localized**: `TIP_AI` is translated in all bundled locale files, so the button tooltip now says the native “Ask Ollama …” wording in each language.

## v2026.06.09.16 (Release: Ollama credits + local AI model pin) - 09.06.2026 16:33

- **Feature: Ollama added to About → Credits**: The credits dialog now includes an Ollama section with a link to the project site, and all locale files carry a matching `CREDITS_OLLAMA` entry.
- **Feature: local AI default model pinned for the editor plan**: `AIAdd.txt` now records `qwen2.5-coder:7b-instruct` as the default Ollama coding model, with `qwen2.5-coder:3b-instruct` as fallback for lower-memory machines.

## v2026.06.09.14 (Fix: spell squiggle redraw; zoom/font-size-aware underline) - 09.06.2026 14:23

- **Fix: spell squiggle redraw no longer blinks the RTF page**: The misspelling underline path now stays read-only during paint, so it no longer changes the RichEdit selection/caret state while drawing. That removes the fast blinking and blue square that could appear on the page.
- **Fix: spell squiggle geometry now follows zoom and font size**: The underline position is recalculated from the currently rendered line geometry on every paint, so Ctrl+mouse-wheel zoom and character font-size changes keep the squiggle below the word instead of drifting through the letters or disappearing.

## v2026.06.09.12 (Fix: Find dialog follows highlighted text; live hit count) - 09.06.2026 12:10

- **Fix: Find dialog now uses highlighted text immediately**: If text is already highlighted in a Scintilla tab, Ctrl+F now pre-fills the Find box from that highlighted text, refreshes the hit count right away, and treats the highlighted occurrence as the active match. The dialog still opens idle when nothing is highlighted, so typing can begin from an empty Find box as before.
- **Fix: live Find count / first hit**: The Find dialog now refreshes its result count while you type and selects the first match at or after the highlighted occurrence or caret, instead of waiting for Find Next. Backwards search now anchors to the highlighted occurrence and moves to the previous match.

## v2026.05.29.11 (Fix: Scintilla change-history stripes; fold arrows restored) - 29.05.2026 11:07

- **Fix: Scintilla change-history stripes in code tab gutter**: Red, green, and yellow vertical stripes were permanently visible in the left gutter of all code tabs — including on reopen — and were displacing the fold arrows so they appeared broken or invisible. These are Scintilla's built-in change-history markers (red = reverted, green = saved, yellow = unsaved), which this version of Scintilla enables by default. The margin layout has been reorganised: margin 2 is now a dedicated 4 px history-marker strip (mask `0x01E00000`, markers 21–24), and the fold arrows have been moved to margin 3 (14 px, click-sensitive). The history colours are still visible as a thin strip to the left of the fold arrows, while the fold arrows are fully unobstructed and clickable again. Compare Tabs produces only the read-only unified diff result tab — no colored stripes are painted on the original source files.

## v2026.05.29.10 (Fix: RTF formatting lost on session restore; remove Scintilla change-history markers) - 29.05.2026 10:09

- **Fix: RTF formatting (bold, italic, underline, etc.) lost on session restore**: After a restart, unsaved RTF tabs had all character formatting stripped — bold text became plain, italic became plain, and so on. Root cause: the session-restore colour fixup used `CFM_EFFECTS` as the `dwMask` in `EM_SETCHARFORMAT`. Since `CFM_EFFECTS` covers all effect bits (bold, italic, underline, strikethrough, …), and `dwEffects` only had `CFE_AUTOCOLOR` set, RichEdit zeroed out every other effect on every character. Fixed by changing the mask to `CFE_AUTOCOLOR` only, so just the autocolor flag is written and all per-character bold/italic/underline/colour runs from the streamed-in RTF are preserved intact.
- **Fix: Remove Scintilla change-history markers**: Red, green, and yellow stripes were appearing in the left gutter of code tabs and displacing the fold arrows. These are Scintilla's built-in change-history indicators (red = reverted, green = saved, yellow = unsaved). Disabled with `SCI_SETCHANGEHISTORY, 0` (`ChangeHistoryOption::Disabled`) in `Ne_SetupScintillaStyle`. The fold margin and bookmark margin are unaffected and display correctly again.

## v2026.05.29.09 (Feature: Insert Date/Time; column selection docs; RTF session restore fix; 10 s autosave + WAL) - 29.05.2026 09:57

- **Feature: Insert Date/Time (F5 / Edit menu)**: New *Edit → Insert Date/Time* command (also F5). Inserts the current local date and time at the caret using Windows `GetLocalTime` / `GetDateFormatW` / `GetTimeFormatW` with the user's locale format. Works in both RTF (RichEdit) and code (Scintilla) tabs. F5 is intercepted globally in the message pump so it fires even when toolbar buttons have focus. All 15 locale files updated with `MENU_INSERT_DATETIME`, `SCF_INSERT_DATETIME`, and `SCD_INSERT_DATETIME`.
- **Feature: Column (rectangular) selection documented**: Alt+Drag and Alt+Shift+Arrows for column/block selection are now listed in Help → Shortcuts. Scintilla handles column selection natively — no code changes required.
- **Fix: RTF tab black background on session restore**: When using a light UI with "dark editor background" enabled (*g_darkEditor*), session-restored RTF tabs opened with a solid black background. Root cause: the `loadContent` lambda and the *Convert to Plain Text* path both used `bool darkEd = g_darkMode || g_darkEditor` to set the RichEdit background colour — but `g_darkEditor` applies only to the Scintilla code viewport and must never affect RichEdit. Fixed by using `bool darkEd = g_darkMode` for all RichEdit background colour decisions.
- **Fix: Session autosave interval reduced to 10 seconds (from 60)**: `NE_TIMER_SESSION` interval changed from 60 000 ms to 10 000 ms. SQLite WAL journal mode (`PRAGMA journal_mode=WAL`) is now enabled at database open in `NeProfiles_Init()`. In WAL mode each `COMMIT` appends to the WAL file rather than fsyncing the main database file, making every 10-second save imperceptible to the user.

## v2026.05.29.08 (Feature: Find/Replace — All open tabs; Search backwards; dynamic sizing; fixes) - 29.05.2026 08:37

- **Feature: Find in all open tabs**: New "All open tabs" checkbox in the Find/Replace dialog. When checked, Find Next searches across every open tab (both RTF and code files), wrapping through all tabs in order and jumping directly to the matching tab. The match counter shows the current position across all tabs combined (e.g. "16 / 3500"). Replace and Replace All are greyed out while this mode is active.
- **Feature: Search backwards**: New "Search backwards" checkbox in the Find/Replace dialog. When checked, Find Next navigates towards the beginning of the document (or backwards through all tabs when "All open tabs" is also checked). Wrap-around is preserved in both directions.
- **Feature: Dynamic Find/Replace dialog sizing**: All checkbox labels and the dialog width are now measured at runtime via `Ne_MeasureCheckWidth()` (same pattern as `Ne_MeasureButtonWidth`). The dialog automatically widens to fit any locale's translated checkbox text — no hardcoded pixel offsets.
- **Fix: "All open tabs" only searched current tab**: The multi-tab scan used `IsWindowVisible(doc->hSci)` to detect code files, which returned false for inactive (hidden) tabs and fell through to the empty RichEdit. Changed to `doc->hSci != NULL` so all code tabs are read regardless of which one is currently visible.
- **Fix: Find/Replace search broken for code (Scintilla) tabs in single-tab mode**: The single-tab path always called `Ne_GetEditText(hEdit)` — which returns empty text for code files because their content lives in the Scintilla control, not the hidden RichEdit. Now detects `doc->hSci` and uses `Ne_SciGetText` + `SCI_GETCURRENTPOS` / `SCI_SETSEL` for code tabs.
- **Fix: Find/Replace button colours**: Find Next is now green; Replace and Replace All are blue. Disabled buttons (Replace/Replace All when "All open tabs" is checked) paint with a gray background and gray text via `ODS_DISABLED` in the owner-draw handler — previously they retained full colour.
- **Fix: locale key fusion in 11 locale files**: `MENU_SELECTALL` and `MENU_FIND` were concatenated on a single line (missing newline after `\tCtrl+A`). The Edit menu showed the raw key name `MENU_FIND` instead of the localised text. Fixed in all 11 affected files (de_DE, el_GR, en_GB, es_ES, fi_FI, fr_FR, nl_BE, nl_NL, pt_PT, se_NO, sv_SE).

## v2026.05.28.16 (Fix: spell squiggle position; persist Mark Misspelled Words; status bar truncation; per-doc spell language) - 28.05.2026 16:39

- **Fix: spell squiggle X position**: Misspelled-word underlines were drawn shifted right of the actual word. Root cause: `GetWindowTextW` returns `\r\n` (two chars) per paragraph break, but `EM_POSFROMCHAR` counts each break as one char. Fixed by switching to `EM_GETTEXTEX` with `GT_RAWTEXT`, which returns the same single-`\r` representation RichEdit uses internally.
- **Fix: spell squiggle Y zoom-tracking**: The squiggle stayed at a fixed pixel row when zooming in or out. Fixed by deriving actual rendered line height from `EM_POSFROMCHAR` on the next line (already zoom-adjusted) instead of scaling `GetTextMetrics` height manually.
- **Fix: phantom full-width red line**: A word near a line-wrap boundary caused a squiggle to extend to the right edge of the control. Fixed by skipping errors whose start and end fall on different visual lines.
- **Fix: "Mark Misspelled Words" state not persisted**: The toggle was reset to off on every restart. State is now saved to the settings DB (`spell_mark` key) on every toggle and restored on startup. All session-restored RTF tabs are immediately scanned so squiggles appear without needing to type anything.
- **Fix: status bar left section truncated on large files**: The Words/Chars/Lines segment was clipped to one-third of the window width (`rc.right / 3`), which is not enough when any count reaches five or more digits. The section now measures its own text with `GetTextExtentPoint32W` and uses the actual pixel width as the clip boundary, so the full string is always visible.
- **Feature: per-document spell-check language persisted across sessions**: The language chosen in *Spelling → Language* is stored in `doc->spellLang` (BCP-47) and saved to the `session_tabs.spell_lang` DB column on every periodic snapshot and at clean exit. On restore the language is applied back to the document, so each document keeps its own language across restarts and crashes. Works for saved files, FTP files, and unsaved buffers.

## v2026.05.28.14 (Feature: native spell-check dialog; status bar line count; language menu) - 28.05.2026 14:24

- **Feature: native spell-check dialog**: The plain `MessageBoxW`-based spell-check prompt has been replaced with a fully custom NSBEdit-style dialog. The dialog shows the misspelled word in bold, lists up to 10 suggestions in a listbox (double-click to apply), and provides six owner-draw buttons — **Ignore**, **Ignore All** (blue), **Add to Dictionary**, **Change**, **Change All** (green), and **Close** (red). Button widths are measured from the active locale strings so every language fits without clipping. All errors are collected upfront; a cumulative offset delta keeps replacement positions correct after earlier words are changed. Dark-mode aware throughout.
- **Feature: word highlighting and viewport scroll**: As the dialog advances to each misspelled word it selects that word in the editor (`EM_EXSETSEL`) and scrolls it into view (`EM_SCROLLCARET`), so the context of each error is always visible behind the dialog.
- **Feature: status bar line count**: The status bar now shows **Lines: N** alongside Words and Chars, updated on every text change via `EM_GETLINECOUNT`. New locale key `SB_LINES` added to all 15 locale files.
- **Fix: spell language menu shows only installed checkers**: The Language sub-menu is now built from the user's own Windows language list (`HKCU\Control Panel\International\User Profile\Languages`), filtered by `ISpellCheckerFactory::IsSupported`. Previously it enumerated every language Windows could theoretically support, showing dozens of English regional variants even when only one dictionary was installed.
- **Fix: spell language menu labels capitalised, no BCP-47 tag**: Language names now show the native display name with a capital first letter (e.g. *Norsk bokmål*, *English*, *Dansk*). The BCP-47 tag is stored in a parallel `s_spellLangTags` vector used internally; it no longer appears in the menu label.

## v2026.05.28.12 (Feature: Go to Line; Bookmarks) - 28.05.2026 12:15

- **Feature: Go to Line (Ctrl+G)**: New *Edit → Go to Line…* command (also Ctrl+G anywhere). A compact input dialog asks for a line number; the editor scrolls to that line and places the caret there. The number is clamped to the last line so any out-of-range value goes to the end of the document. Works in all Scintilla (code) tabs. Ctrl+G is intercepted globally in the message pump — it fires even when the editor does not have keyboard focus.
- **Feature: Bookmarks (F2 / Shift+F2 / Ctrl+F2)**: Bookmarks can be toggled on any line with F2 (or *Edit → Toggle Bookmark*). A blue filled-circle glyph (SC_MARK_CIRCLE) is drawn in margin 1 (10 px, left of the fold margin). Shift+F2 cycles to the previous bookmark; Ctrl+F2 (or *Edit → Go to Bookmark*) cycles to the next. All three keys are listed in the Help → Shortcuts dialog. The *Go to Bookmark* menu item is greyed automatically when no bookmarks exist in the active document.

## v2026.05.28.10 (Feature: brace-pair & HTML tag-pair & Bash keyword-pair highlighting) - 28.05.2026 10:50

- **Feature: brace-pair highlighting `(){}[]`**: When the caret is on or adjacent to a bracket character, Scintilla's built-in `SCI_BRACEHIGHLIGHT` / `SCI_BRACEBADLIGHT` API is used to highlight the matching pair. Theme-aware colours: dark editor — golden `RGB(255,215,100)` on a dark-blue background; light editor — deep-blue `RGB(0,90,180)` on a pale-blue background. Unmatched brackets are highlighted in red (`STYLE_BRACEBAD`). Applied on every `SCN_UPDATEUI`.
- **Feature: HTML tag-pair highlighting**: When the caret is inside an HTML tag in a `hypertext`-lexer file (HTML / PHP), both the opening and closing tags are outlined with a box indicator (`NE_IND_TAGMATCH`, `INDIC_BOX`, indicator index 9) — blue tint on dark, deeper blue on light. Nesting depth is tracked correctly so `<div>` highlights its own `</div>` even when inner divs are present. Scans up to 50 000 characters in each direction. Self-closing (`<br/>`) and comment/doctype tags are excluded. Does not fire inside PHP regions (Scintilla style ≥ 118).
- **Feature: Bash keyword-pair highlighting**: When the caret rests on a paired shell keyword, the same box indicator highlights the partner keyword up to 50 000 characters away. Pairs covered: `if` ↔ `fi`, `case` ↔ `esac`, `for` / `while` / `until` → `done`, `do` → `done`, `done` ← `do`. Full nesting depth tracked in both forward and backward searches. Only fires when the word is a Scintilla `SCE_SH_WORD` (style 4) token.
- **File-open filter extended for Shell/Bash**: `FILTER_OPEN` in all 15 locale files now includes `.sh`, `.bash`, `.zsh`, `.ksh`, `.csh` in the "All supported files" group and adds a dedicated "Shell / Bash (*.sh)" filter entry.

## v2026.05.28.09 (Fix: FTP save-as tab; strip-RTF dialog style; status bar i18n) - 28.05.2026 09:45

- **Fix: FTP save-as tab name and language not updating**: After saving a new or renamed file to FTP with a different extension (e.g. saving an untitled plain-text buffer as `index.php`), the tab now immediately shows the remote filename and re-applies the correct syntax highlighting for the new extension. Previously the tab kept the old name and language until the file was closed and reopened from FTP.
- **Fix: Strip Formatting dialog upgraded to custom style**: The system `MessageBoxW` prompt for “Strip RTF formatting?” has been replaced with `Ne_ShowChoiceDialog` — a fully custom styled dialog with a green **Continue** button and a blue **Cancel** button, matching every other confirmation dialog in NSBEdit. New locale keys `DLG_CONV_TO_PLAIN` and `BTN_CONTINUE` added to all 15 locale files.
- **Fix: Status bar i18n — “Plain Text”, “Ln” and “Col” were hardcoded English**: “Plain Text” in the centre info segment now uses the `LANG_PLAIN_TEXT` locale key (Norwegian: “Ren tekst”). New locale keys `SB_LN` and `SB_COL` added to all 15 locale files and applied via `NeStatusBar_SetLineColLabels` at startup (Norwegian: “Ln” / “Kol”, Swedish: “Rad” / “Kol”, German: “Zl” / “Sp”, etc.).

## v2026.05.27.14 (Feature: indent guides) - 27.05.2026 14:25

- **Feature: indent guides (dotted vertical lines)**: Scintilla's built-in indentation guide support (`SC_IV_LOOKBOTH`) is now enabled in all file types. Dotted vertical lines mark each indentation level so it is easy to follow nested code blocks visually. The guide colour is theme-aware: `RGB(115,115,115)` against the dark editor background and `RGB(130,130,130)` against the white background — clearly visible in both modes without being distracting.

## v2026.05.27.13 (Fix: dialog focus restore; PHP/JS/Python typeahead) - 27.05.2026 13:21

- **Fix: NSBEdit loses focus after FTP save dialog closes**: After the "File saved" auto-close dialog dismissed, `EnableWindow(parent, TRUE)` occasionally handed focus to another window (e.g. Explorer) instead of giving it back to NSBEdit. Fixed with the TOPMOST trick: when the dialog closes, `SetWindowPos(parent, HWND_TOPMOST, …)` is called immediately followed by `SetWindowPos(parent, HWND_NOTOPMOST, …)` — this atomically brings the parent to the front and removes the always-on-top pin. The trick is only applied when `restoreOnClose` is set, which is recorded at dialog-creation time (true when NSBEdit was the foreground window as the dialog opened, meaning the user hadn't already switched away).
- **Fix: PHP typeahead regression — no completions inside `<?PHP` blocks**: After the HTML/PHP autocomplete feature landed in v11, PHP keyword completions stopped appearing. Root cause: the style dispatch included `else if (style == 0) kws = ""` which suppressed completions for any Scintilla sub-style not explicitly listed — including contexts where `<?PHP` (uppercase) caused the lexer to fall back to style 0. Fixed by removing the catch-all suppression: unmatched styles now fall through to the file's default keyword list so PHP files always offer PHP keywords as a safe fallback.
- **Feature: context-aware completions in all sub-languages of HTML/PHP files**: Scintilla styles are now dispatched across all four embedded languages inside the `hypertext` lexer. Typing inside a `<script>` block (styles 40–89) now offers JavaScript keywords; embedded Python (styles 90–117) offers Python keywords; PHP blocks (styles 118+) offer PHP keywords; HTML tag names and attributes work as before. Pure HTML files and PHP files each see the right list wherever the caret sits.

## v2026.05.27.11 (Fix: Opera focus steal — FTP dialog + debounce) - 27.05.2026 11:35

- **Fix: FTP "file saved" dialog steals focus from Opera/browser**: When saving a file via FTP, an auto-close choice dialog ("File saved — X seconds ago") was shown. Its close path called `SetForegroundWindow(parent)` unconditionally, yanking focus back to NSBEdit even if the user had already switched to a browser. Fixed two ways: (1) the auto-close timeout is reduced from 2500 ms to 1000 ms so the window disappears before a typical Alt+Tab completes; (2) `Ne_ShowChoiceDialog` now only calls `SetForegroundWindow(parent)` if NSBEdit or the dialog itself still owns the foreground at close time — if the user has moved to another app, focus stays there.
- **Fix: Alt+Tab to Opera then Ctrl+R (or just Ctrl) jumps focus back to NSBEdit**: Chromium-based browsers briefly fire `WM_ACTIVATEAPP(TRUE)` back to the previous foreground window as part of their own activation settling, which was resetting the `s_appIsActive` guard and letting the "file changed on disk" dialog fire during that transient re-activation. The dialog's close code then called `SetForegroundWindow(NSBEdit)`, locking NSBEdit to the front. Fixed by adding a 500 ms debounce: `s_deactivatedAt` is recorded when `WM_ACTIVATEAPP(FALSE)` fires, and `Ne_CheckExternalFileChangeOnFocus` suppresses the check for 500 ms after the last deactivation — long enough to outlast Opera's spurious `WM_ACTIVATEAPP(TRUE)` bounce.
- **Build tooling**: `makeit.bat` rewritten with 6 explicit build steps, integrated zip packaging (prune to 3 zips), and a total-time banner. `follow.ps1` rewritten with coloured progress bars, step counters, error/warning highlighting, and a build-time summary.

## v2026.05.27.10 (Tab drag-to-reorder; fix Opera focus steal) - 27.05.2026 10:40

- **Feature: tabs can be reordered by dragging**: click and hold any tab, drag left or right — a blue vertical insertion line shows the drop position, release to move the tab there. The active tab tracks through the reorder. The close (×) button is excluded from drag initiation. If mouse capture is lost (Alt+Tab, modal dialog) the drag is cancelled cleanly.
- **Fix: Alt+Tab to Opera then Ctrl+R jumps focus back to NSBEdit**: The "file changed on disk" dialog could fire while NSBEdit was in the background if Opera briefly yielded activation during its first page refresh. A new `WM_ACTIVATEAPP` handler tracks whether NSBEdit is the foreground application; `Ne_CheckExternalFileChangeOnFocus` now returns immediately when the flag is false, so the dialog is never shown while another app owns the foreground.

## v2026.05.27.09 (RTF-safe FTP open; suppress disk-check on open) - 27.05.2026 09:55

- **Fix: files with code extensions never misdetected as RTF**: `Ne_LoadPathIntoEditor` now checks `Ne_LangFromExt(path)` after the `{\rtf` header scan. If the file extension is a known code type (`.php`, `.js`, `.py`, `.cpp`, etc.) the RTF flag is overridden and the file always loads in Scintilla, even if the server copy was accidentally overwritten with RTF content.
- **Fix: spurious "file changed on disk" dialog on File > Open and FTP open**: Opening a file via *File › Open* or the FTP browser no longer triggers the external-change check. A `s_suppressDiskCheck` flag is raised for the entire open sequence (including the `EN_SETFOCUS` fired when the dialog closes) and cleared only after `Ne_LoadPathIntoEditor` has written the correct fresh disk stamp.

## v2026.05.26.12 (Deep session restore + Bash syntax) - 26.05.2026 12:57

- **Session restore — per-tab state saved and restored**: Every tab now records its word-wrap on/off state, editor type (Scintilla code vs RichEdit RTF), caret character position, and first visible line to `session_tabs`. On restore all four are reapplied — same line, same scroll position, same wrap mode.
- **Empty untitled tabs restored**: Blank placeholder tabs are now fully preserved. The editor type is stored in the new `is_sci_tab` column; on restore the correct editor window is created and word-wrap applied even with no content.
- **Silent restore — no "open cached?" dialogs**: The session restore loop is now completely silent for unsaved, locally-changed, disk-changed, and FTP-unreachable files. Unsaved content and FTP files load silently from the cache BLOB; locally-changed files silently keep the cached version. Only a genuine unrecoverable error (no cache and no disk file) shows a message.
- **Word-wrap button synced after restore**: The toolbar wrap-toggle button reflects the restored active tab's state immediately on startup.
- **Bash / Shell syntax highlighting**: New "Bash / Shell" language in the Language menu. Extension detection: `.sh`, `.bash`, `.zsh`, `.ksh`, `.bashrc`, `.bash_profile`, `.bash_aliases`, `.bash_login`, `.bash_logout`, `.zshrc`, `.zshenv`, `.zprofile`. Extensionless scripts auto-detected by shebang: `#!/bin/bash`, `#!/usr/bin/env bash`, `#!/bin/sh`, `#!/usr/bin/env zsh`, `#!/bin/dash`, `#!/bin/fish`, etc. Colours: keywords bold-blue, strings, comments, numbers, `$scalar`/`$param` variables (purple), heredoc delimiters and bodies, backticks.

## v2026.05.26.11 (Session restore colour fix) - 26.05.2026 11:18

- **Fix: wrong foreground colour on session-restored plain-text-in-RichEdit files**: When a file was converted from RTF to plain text (*Convert → To Plain Text*) the RichEdit retained the dark-editor character colour (`RGB(220,220,220)` light-grey). The session serialiser, seeing no Scintilla window, streamed the content out as RTF — embedding those colour runs in the BLOB. On restore the RTF was loaded back into a fresh RichEdit (default white background), producing light-grey text on white. Fixed in `Ne_SessionRestore`'s `loadContent` helper: after `Ne_StreamIn`, if the path does not end in `.rtf`, the correct editor colours are re-applied via `EM_SETBKGNDCOLOR` and `EM_SETCHARFORMAT`, respecting `g_darkMode` / `g_darkEditor`. Genuine `.rtf` files are unaffected.

## v2026.05.26.10 (Scrollbar tab-switch fix) - 26.05.2026 10:53

- **Fix: custom scrollbars vanish after switching tabs**: Switching away from a tab and back caused the custom scrollbar windows to disappear permanently. Root cause: `Ne_SyncScrollbarVisibility` called `ShowWindow(hBar, SW_HIDE)` directly on the bar HWND, bypassing the scrollbar library's internal `fadeState`. On re-activation, `msb_reposition` → `Msb_UpdateVisibility` only re-shows the bar when `fadeState == FADE_INVISIBLE`; since that was never set, the bar stayed hidden forever. Fixed by adding `msb_hide(HMSB)` to the public API: resets `fadeState` to `FADE_INVISIBLE`, kills any fade timer, then hides the window. `Ne_SyncScrollbarVisibility` now calls `msb_hide()` instead of `ShowWindow` directly. Affects all four bar handles (RichEdit V/H and Scintilla V/H).

## v2026.05.25.14 (Session Restore) - 25.05.2026 14:47

- **Session persistence**: The installed version saves the full tab list to the `session_tabs` SQLite table every 60 seconds and at clean exit. Each row stores the file path, FTP details, a content BLOB for unsaved/FTP files, and a disk timestamp. The save is a single `BEGIN … COMMIT` transaction — a crash mid-write cannot corrupt the previous session.
- **Session restore on startup**: When the installed version is launched with no command-line file argument it automatically reopens every tab from the last session — local files, FTP/SFTP files, and unsaved (untitled) buffers.
- **Unsaved-buffer recovery**: Tabs with unsaved changes have their full RTF or UTF-8 content stored in the DB BLOB; it is streamed back into a new tab on restore.
- **FTP/SFTP tab reconnect**: Remote tabs reconnect on restore. If the server is unreachable and a cached copy exists, *Open Cached / Skip* dialog appears. If no cache exists, the tab is skipped with a warning.
- **Remote-file conflict detection**: If the FTP file's last-write timestamp changed since the session was saved, a *Reload from Server / Keep Local* dialog is shown.
- **Missing local file handling**: If a local file is gone, a dialog offers *Open Cached* (when content was stored) or warns and skips (when no content exists).
- **Portable and command-line modes unaffected**: Session restore only activates for the installed version. Portable mode or a command-line file argument bypasses the feature.
- **All dialogs fully i18n'd**: New locale keys `DLG_SESSION_RESTORE`, `MSG_SESSION_FILE_MISSING`, `BTN_OPEN_CACHED`, `BTN_SKIP`, `MSG_SESSION_FILE_GONE`, `MSG_SESSION_FTP_FAIL`, `MSG_SESSION_FTP_FAIL_NC`, `MSG_SESSION_REMOTE_CHANGED`, `BTN_RELOAD_REMOTE`, `BTN_KEEP_LOCAL` added to all 15 locale files.
- **New module `ne_session.h / ne_session.cpp`**: SQLite CRUD wrapper (`NeSession_Save` / `NeSession_Load` / `NeSession_HasData` / `NeSession_Clear`) + `NeSessionTab` struct. `ne_profiles.cpp` extended with `NeProfiles_IsInstalled()` and `NeProfiles_GetDb()`.
- **Fix false-positive "remote file changed" dialog on unmodified FTP files**: The remote-change and local disk-change dialogs during session restore now only fire when the file had unsaved edits in the previous session (`wasModified` flag). Clean FTP/local tabs load silently — no more false positives from CRLF vs LF differences between Scintilla text and raw server bytes. `was_modified` column added to `session_tabs` with an automatic `ALTER TABLE` migration.
- **Fix `follow.ps1` run counter always showing #1**: Replaced `$PSScriptRoot` (empty when dot-sourced or invoked without `-File`) with a robust `$scriptDir` from `$MyInvocation.MyCommand.Path`. Removed the false-positive trigger `($run -eq 0 -and $size -gt 0)` that fired on startup when the log already had content; new runs now only trigger on genuine file truncation (`$size -lt $pos`).

## v2026.05.25.12 (Recent Files; Focus fix) - 25.05.2026 12:46

- **Recent Files submenu**: The File menu now has a *Recent Files* submenu listing the last 10 opened or saved files. Selecting an entry opens the file; dead paths are silently removed. Persisted in the SQLite settings database (`recent_0`–`recent_9` keys). Translated in all 15 UI languages via the new `MENU_RECENT` and `MENU_RECENT_EMPTY` locale keys.
- **Alt+Tab focus fix**: When returning to NSBEdit via Alt+Tab or a taskbar click, the active editor now immediately receives keyboard focus — text selection stays live (blue) instead of turning grey. Fixed by handling `WM_SETFOCUS` on the top-level frame and redirecting focus to the correct child editor (`hSci` for Scintilla code tabs, `hEdit` for RichEdit RTF tabs).

## v2026.05.24.13 (Dutch/Flemish locale fix) - 24.05.2026 13:33

- **Dutch & Flemish — "GUI-taal" label**: `MENU_GUI_LANG` in `nl_NL.txt` and `nl_BE.txt` showed "Taal", identical to `MENU_LANGUAGE` ("&Taal"), making the GUI Language and Code Language menu items indistinguishable. `MENU_GUI_LANG` is now "GUI-taal" in both files, matching the English "GUI Lang" pattern.

## v2026.05.24.12 (North Sami + Fixes) - 24.05.2026 12:20

- **About dialog — Edition 2**: The About dialog now renders the `ABOUT_EDITION` locale key (already translated in all 15 UI languages) as an "Edition: 2" line immediately after the version number and above the separator rule.
- **FTP profile delete confirmation**: Clicking "Delete Profile" in the Add/Edit FTP/SFTP Site dialog now shows a "Delete profile '%s'? — This cannot be undone." confirmation with "No" as the default button. Previously the profile was deleted immediately with no warning. Localised in all 15 UI languages via the new `MSG_PROFILE_DELETE_CONFIRM` key.
- **Database safety — portable stub ignored**: `ne_profiles.cpp` now skips a zero-byte `nsbedit.db` stub next to the executable. The AppData database (`%APPDATA%\NSBEdit\nsbedit.db`) is always preferred when it exists.
- **Unicode escape fix in locale files**: About-dialog symbols (✏ • ⌨ ⚡) and special characters in `nl_NL.txt`, `nl_BE.txt`, and `se_NO.txt` were stored as `\uXXXX` sequences which the locale parser does not handle. All replaced with literal UTF-8 characters.
- **About dialog button row auto-resizes**: Pre-measures the three buttons before window creation; widens dialog if total width exceeds `S(480)`. Fixes clipping of long translated labels (e.g. North Sami "Lisensa geahčadit").
- **North Sami (Davvisámegiella) UI translation**: Full North Sami translation of all ~200 UI strings added as `locale/se_NO.txt` (RCDATA 26, locale ID 14). "Davvisámegiella" sorts between "Dansk" and "Deutsch", giving the menu order: Dansk → Davvisámegiella → Deutsch → Ελληνικά → English → Español → Français → Íslenska → Nederlands → Norsk → Português → Suomi → Svenska → Vlaams → Українська.
- **All locale files updated**: `LANG_UI_SAMI` added to every existing locale file (en_GB → "Northern Sami", no_nb → "Nordsamisk", is_IS → "Norðursamíska", sv_SE → "Nordsamiska", da_DK → "Nordsamisk", fi_FI → "Pohjoissaame", de_DE → "Nordsamisch", fr_FR → "Same du Nord", es_ES → "Sami del Norte", uk_UA → "Північносаамська", el_GR → "Βόρεια Σαμικά", pt_PT → "Sami do Norte", nl_NL → "Noordsamisch", nl_BE → "Noordsamisch").

## v2026.05.24.11 (Dutch + Flemish) - 24.05.2026 11:33

- **Dutch (Nederlands) UI translation**: Full Dutch (Netherlands) translation of all ~200 UI strings added as `locale/nl_NL.txt` (RCDATA 24, locale ID 12). "Nederlands" sorts between "Íslenska" and "Norsk", giving the menu order: Dansk → Deutsch → Ελληνικά → English → Español → Français → Íslenska → Nederlands → Norsk → Português → Suomi → Svenska → Vlaams → Українська.
- **Flemish (Vlaams) UI translation**: Complete Belgian Dutch (Flemish) translation added as `locale/nl_BE.txt` (RCDATA 25, locale ID 13). Uses "bewaren" (to save/keep) where Dutch uses "opslaan", and a slightly more formal register throughout. "Vlaams" sorts after "Svenska" in the language menu.
- **All locale files updated**: `LANG_UI_DUTCH` and `LANG_UI_FLEMISH` added to every existing locale file (en_GB → "Dutch"/"Flemish", no_nb → "Nederlandsk"/"Flamsk", is_IS → "Hollenska"/"Flæmska", sv_SE → "Nederländska"/"Flamländska", da_DK → "Nederlandsk"/"Flamsk", fi_FI → "Hollanti"/"Flaami", de_DE → "Niederländisch"/"Flämisch", fr_FR → "Néerlandais"/"Flamand", es_ES → "Neerlandés"/"Flamenco", uk_UA → "Нідерландська"/"Фламандська", el_GR → "Ολλανδικά"/"Φλαμανδικά", pt_PT → "Neerlandês"/"Flamengo").

## v2026.05.24.12 (Portuguese) - 24.05.2026 11:09

- **Portuguese (Português) UI translation**: Full European Portuguese translation of all ~200 UI strings added as `locale/pt_PT.txt` (RCDATA 23, locale ID 11). "Português" sorts between "Norsk" and "Suomi", giving the menu order: Dansk → Deutsch → Ελληνικά → English → Español → Français → Íslenska → Norsk → Português → Suomi → Svenska → Українська.
- **All locale files updated**: `LANG_UI_PORTUGUESE` added to every existing locale file (en_GB → "Portuguese", no_nb → "Portugisisk", is_IS → "Portúgalska", sv_SE → "Portugisiska", da_DK → "Portugisisk", fi_FI → "Portugali", de_DE → "Portugiesisch", fr_FR → "Portugais", es_ES → "Portugués", uk_UA → "Португальська", el_GR → "Πορτογαλικά").

## v2026.05.24.11 (Greek) - 24.05.2026 11:01

- **Greek (Ελληνικά) UI translation**: Full translation of all ~200 UI strings added as `locale/el_GR.txt` (RCDATA 22, locale ID 10). "Ελληνικά" sorts as "El…" — between "Deutsch" and "English" — giving the menu order: Dansk → Deutsch → Ελληνικά → English → Español → Français → Íslenska → Norsk → Suomi → Svenska → Українська.
- **All locale files updated**: `LANG_UI_GREEK` added to every existing locale file (en_GB → "Greek", no_nb → "Gresk", is_IS → "Gríska", sv_SE → "Grekiska", da_DK → "Græsk", fi_FI → "Kreikka", de_DE → "Griechisch", fr_FR → "Grec", es_ES → "Griego", uk_UA → "Грецька").
- **FTP site dialog auto-resizes width**: Pre-measures button labels before window creation; width = `max(S(420), totalButtonWidth + 2×padding)`. Fixes clipping of long translated button labels (e.g. "Delete Profile" in Ukrainian). Button gap widened S(8) → S(12).

## v2026.05.24.10 (Ukrainian) - 24.05.2026 10:20

- **Ukrainian (Українська) UI translation**: Full translation of all ~200 UI strings added as `locale/uk_UA.txt` (RCDATA 21, locale ID 9). Menu order: Dansk → Deutsch → English → Español → Français → Íslenska → Norsk → Suomi → Svenska → Українська.
- **All locale files updated**: `LANG_UI_UKRAINIAN` added to every existing locale file (en_GB → "Ukrainian", no_nb → "Ukrainsk", is_IS → "Úkraínska", sv_SE → "Ukrainska", da_DK → "Ukrainsk", fi_FI → "Ukraina", de_DE → "Ukrainisch", fr_FR → "Ukrainien", es_ES → "Ucraniano").

## v2026.05.24.10 - 24.05.2026 10:07

- **Spanish (Español) UI translation**: Full translation of all ~200 UI strings added as `locale/es_ES.txt` (RCDATA 20, locale ID 8). Menu order: Dansk → Deutsch → English → Español → Français → Íslenska → Norsk → Suomi → Svenska.
- **All locale files updated**: `LANG_UI_SPANISH` added to every existing locale file (en_GB → "Spanish", no_nb → "Spansk", is_IS → "Spænska", sv_SE → "Spanska", da_DK → "Spansk", fi_FI → "Espanja", de_DE → "Spanisch", fr_FR → "Espagnol").

## v2026.05.24.10 (French) - 24.05.2026 09:54

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
