@echo off
setlocal enabledelayedexpansion

taskkill /F /IM NSBEdit.exe 2>nul

:: Re-invoke ourselves with output mirrored to makeit.log
if "%1" neq "__logged__" (
    call "%~f0" __logged__ 2>&1 | tee makeit.log
    exit /b !ERRORLEVEL!
)

:: ── Timer start ──────────────────────────────────────────────────────────────
for /f %%T in ('powershell -NoProfile -Command "(Get-Date).Ticks"') do set BUILD_TICKS=%%T

echo ============================================================
echo   NSBEdit Build  --  %DATE%  %TIME%
echo ============================================================
echo.

:: ─────────────────────────────────────────────────────────────────────────────
echo [STEP 1/6] Prepare  --  version header  +  kill running instance
:: Clean stale build logs so they don't pile up or mislead the AI's project scan
:: (makeit.log is preserved -- it is the current run's log).
del /q build*.txt build*.log output.txt makeit_fresh.log 2>nul
powershell -NoProfile -Command "& { $l = gc 'curver.txt'; $p = ($l -match '^Published: ')[0] -replace '^Published: ',''; $v = ($l -match '^Version: ')[0] -replace '^Version: ',''; $q = [char]34; [IO.File]::WriteAllText([IO.Path]::GetFullPath('ne_version.h'), '#pragma once' + [char]10 + '#define NE_PUBLISHED L' + $q + $p + $q + [char]10 + '#define NE_VERSION   L' + $q + $v + $q, [Text.Encoding]::UTF8) }"
@if !ERRORLEVEL! neq 0 ( echo [ERROR] ne_version.h generation FAILED & exit /b 1 )
echo   ne_version.h  OK
taskkill /F /IM NSBEdit.exe 2>nul
if !ERRORLEVEL! equ 0 (
    echo   NSBEdit.exe killed  --  waiting 1 s for handles to release
    timeout /T 1 /NOBREAK >nul
) else (
    echo   NSBEdit.exe not running
)
echo [STEP_DONE 1/6]
echo.

:: ─────────────────────────────────────────────────────────────────────────────
echo [STEP 2/6] Resources  --  windres  NSBEdit.rc  -^>  NSBEdit.res
windres NSBEdit.rc -o NSBEdit.res --output-format=coff
@if !ERRORLEVEL! neq 0 ( echo [ERROR] windres FAILED & exit /b 1 )
echo [STEP_DONE 2/6]
echo.

:: ─────────────────────────────────────────────────────────────────────────────
echo [STEP 3/6] SQLite3  --  sqlite3.c  -^>  sqlite3.o
gcc -v -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_DEFAULT_MEMSTATUS=0 -c sqlite3\sqlite3.c -o sqlite3\sqlite3.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] sqlite3 compile FAILED & exit /b 1 )
echo [STEP_DONE 3/6]
echo.

:: ─────────────────────────────────────────────────────────────────────────────
echo [STEP 4/6] QUIC stubs  --  quic_stubs.c  -^>  quic_stubs.o
gcc -v -O2 -c curl\lib\quic_stubs.c -o curl\lib\quic_stubs.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] quic_stubs compile FAILED & exit /b 1 )
echo [STEP_DONE 4/6]
echo.

:: ─────────────────────────────────────────────────────────────────────────────
echo [STEP 5/6] cmark-gfm  --  vendored C sources  -^>  .o
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\arena.c -o third_party\cmark-gfm\src\arena.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm arena compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\blocks.c -o third_party\cmark-gfm\src\blocks.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm blocks compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\buffer.c -o third_party\cmark-gfm\src\buffer.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm buffer compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\cmark.c -o third_party\cmark-gfm\src\cmark.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm cmark compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\cmark_ctype.c -o third_party\cmark-gfm\src\cmark_ctype.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm cmark_ctype compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\commonmark.c -o third_party\cmark-gfm\src\commonmark.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm commonmark compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\footnotes.c -o third_party\cmark-gfm\src\footnotes.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm footnotes compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\html.c -o third_party\cmark-gfm\src\html.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm html compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\houdini_href_e.c -o third_party\cmark-gfm\src\houdini_href_e.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm houdini_href_e compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\houdini_html_e.c -o third_party\cmark-gfm\src\houdini_html_e.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm houdini_html_e compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\houdini_html_u.c -o third_party\cmark-gfm\src\houdini_html_u.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm houdini_html_u compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\inlines.c -o third_party\cmark-gfm\src\inlines.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm inlines compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\iterator.c -o third_party\cmark-gfm\src\iterator.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm iterator compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\latex.c -o third_party\cmark-gfm\src\latex.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm latex compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\linked_list.c -o third_party\cmark-gfm\src\linked_list.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm linked_list compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\man.c -o third_party\cmark-gfm\src\man.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm man compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\map.c -o third_party\cmark-gfm\src\map.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm map compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\node.c -o third_party\cmark-gfm\src\node.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm node compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\plaintext.c -o third_party\cmark-gfm\src\plaintext.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm plaintext compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\plugin.c -o third_party\cmark-gfm\src\plugin.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm plugin compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\references.c -o third_party\cmark-gfm\src\references.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm references compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\registry.c -o third_party\cmark-gfm\src\registry.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm registry compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\render.c -o third_party\cmark-gfm\src\render.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm render compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\scanners.c -o third_party\cmark-gfm\src\scanners.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm scanners compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\syntax_extension.c -o third_party\cmark-gfm\src\syntax_extension.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm syntax_extension compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\utf8.c -o third_party\cmark-gfm\src\utf8.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm utf8 compile FAILED & exit /b 1 )
gcc -v -O2 -std=c99 -Ithird_party\cmark-gfm\src -c third_party\cmark-gfm\src\xml.c -o third_party\cmark-gfm\src\xml.o
@if !ERRORLEVEL! neq 0 ( echo [ERROR] cmark-gfm xml compile FAILED & exit /b 1 )
echo [STEP_DONE 5/6]
echo.

:: ─────────────────────────────────────────────────────────────────────────────
echo [STEP 6/6] Compile ^& link  --  NSBEdit.cpp + all sources  -^>  NSBEdit.exe
g++ -v -std=c++17 -O2 -Wall -mwindows -municode ^
    -I. -Isqlite3 -Icurl\include -Ithird_party\cmark-gfm\src ^
    -DCURL_STATICLIB ^
    -Iscintilla_src\scintilla\include -Ilexilla_src\lexilla\include ^
    NSBEdit.cpp ne_tabs.cpp ne_statusbar.cpp dpi.cpp tooltip\tooltip.cpp scroll\my_scrollbar_vscroll.cpp ^
    highlight\highlight.cpp checkbox.cpp regex_guide\regex_guide.cpp ^
    ne_crypto.cpp ne_profiles.cpp ne_ai_bootstrap.cpp ne_ai_client.cpp ne_session.cpp ne_ftp.cpp ne_autocomplete\ne_autocomplete.cpp ^
    ne_projects.cpp ^
    spinner\spinner_dialog.cpp ^
    ai_markdown_helper.cpp ^
    copy_ai_code.cpp ^
    ollama_ai.cpp ^
    rtf2html\ne_rtf2html_lib.cpp ^
    third_party\cmark-gfm\src\arena.o third_party\cmark-gfm\src\blocks.o third_party\cmark-gfm\src\buffer.o ^
    third_party\cmark-gfm\src\cmark.o third_party\cmark-gfm\src\cmark_ctype.o third_party\cmark-gfm\src\commonmark.o ^
    third_party\cmark-gfm\src\footnotes.o third_party\cmark-gfm\src\html.o third_party\cmark-gfm\src\houdini_href_e.o ^
    third_party\cmark-gfm\src\houdini_html_e.o third_party\cmark-gfm\src\houdini_html_u.o third_party\cmark-gfm\src\inlines.o ^
    third_party\cmark-gfm\src\iterator.o third_party\cmark-gfm\src\latex.o third_party\cmark-gfm\src\linked_list.o ^
    third_party\cmark-gfm\src\man.o third_party\cmark-gfm\src\map.o third_party\cmark-gfm\src\node.o ^
    third_party\cmark-gfm\src\plaintext.o third_party\cmark-gfm\src\plugin.o ^
    third_party\cmark-gfm\src\references.o third_party\cmark-gfm\src\registry.o third_party\cmark-gfm\src\render.o ^
    third_party\cmark-gfm\src\scanners.o third_party\cmark-gfm\src\syntax_extension.o third_party\cmark-gfm\src\utf8.o ^
    third_party\cmark-gfm\src\xml.o ^
    sqlite3\sqlite3.o curl\lib\quic_stubs.o NSBEdit.res ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -luser32 -lgdi32 -lgdiplus -lwinspool -lmsimg32 -ldwmapi -luxtheme ^
    -Lscintilla_src\scintilla\bin -Llexilla_src\lexilla\bin -lscintilla -llexilla ^
    -limm32 -loleaut32 -ladvapi32 -lole32 -luuid ^
    -Lcurl\lib -lcurl -lssh2 -lssl -lcrypto -lz -lnghttp2 -lbrotlidec -lbrotlicommon -lpsl -lzstd ^
    -lws2_32 -lcrypt32 -lbcrypt -lwinhttp -lwldap32 -lsecur32 -liphlpapi -lntdll ^
    -static -static-libgcc -static-libstdc++ ^
    -o NSBEdit.exe
@if !ERRORLEVEL! neq 0 ( echo. & echo [ERROR] Compile/link FAILED & exit /b 1 )
echo [STEP_DONE 6/6]
echo.

:: ─────────────────────────────────────────────────────────────────────────────
echo [STEP 6/6] Package  --  zip release  +  prune to 3 zips
powershell -NoProfile -File ".\pack.ps1"
@if !ERRORLEVEL! neq 0 ( echo [ERROR] pack.ps1 FAILED & exit /b 1 )
powershell -NoProfile -Command "Get-ChildItem zip\*.zip | Sort-Object LastWriteTime -Descending | Select-Object -Skip 3 | Remove-Item -Force; Get-ChildItem zip\*.zip | Sort-Object LastWriteTime -Descending | ForEach-Object { Write-Output \"  kept: $($_.Name)\" }"
echo [STEP_DONE 6/6]
echo.

:: ── Timer end + final banner ─────────────────────────────────────────────────
powershell -NoProfile -Command "$e = [math]::Round(([long](Get-Date).Ticks - [long]$env:BUILD_TICKS) / 1e7, 1); if ($e -ge 60) { $m = [math]::Floor($e / 60); $s = [math]::Round($e - $m * 60, 1); Write-Output \"[BUILD_TIME: $m m, $s s]\" } else { Write-Output \"[BUILD_TIME: $e s]\" }"
echo ============================================================
echo   BUILD SUCCEEDED  --  NSBEdit.exe + zip updated
echo ============================================================


