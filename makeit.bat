@echo off
setlocal enabledelayedexpansion

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
echo [STEP 5/6] Compile ^& link  --  NSBEdit.cpp + all sources  -^>  NSBEdit.exe
g++ -v -std=c++17 -O2 -Wall -mwindows -municode ^
    -I. -Isqlite3 -Icurl\include ^
    -DCURL_STATICLIB ^
    -Iscintilla_src\scintilla\include -Ilexilla_src\lexilla\include ^
    NSBEdit.cpp ne_tabs.cpp ne_statusbar.cpp dpi.cpp tooltip\tooltip.cpp scroll\my_scrollbar_vscroll.cpp ^
    highlight\highlight.cpp checkbox.cpp ^
    ne_crypto.cpp ne_profiles.cpp ne_session.cpp ne_ftp.cpp ne_autocomplete\ne_autocomplete.cpp ^
    rtf2html\ne_rtf2html_lib.cpp ^
    sqlite3\sqlite3.o curl\lib\quic_stubs.o NSBEdit.res ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -luser32 -lgdi32 -lgdiplus -lwinspool -lmsimg32 -ldwmapi -luxtheme ^
    -Lscintilla_src\scintilla\bin -Llexilla_src\lexilla\bin -lscintilla -llexilla ^
    -limm32 -loleaut32 -ladvapi32 -lole32 -luuid ^
    -Lcurl\lib -lcurl -lssh2 -lssl -lcrypto -lz -lnghttp2 -lbrotlidec -lbrotlicommon -lpsl -lzstd ^
    -lws2_32 -lcrypt32 -lbcrypt -lwinhttp -lwldap32 -lsecur32 -liphlpapi -lntdll ^
    -static -static-libgcc -static-libstdc++ ^
    -o NSBEdit.exe
@if !ERRORLEVEL! neq 0 ( echo. & echo [ERROR] Compile/link FAILED & exit /b 1 )
echo [STEP_DONE 5/6]
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


