@echo off
setlocal

:: Kill running instance so exe can be replaced
taskkill /F /IM NSBEdit.exe >nul 2>&1

:: Compile resource
windres NSBEdit.rc -o NSBEdit.res --output-format=coff
if %ERRORLEVEL% neq 0 ( echo [ERROR] windres failed & pause & exit /b 1 )

:: Compile and link
g++ -std=c++17 -O2 -mwindows -municode ^
    NSBEdit.cpp NSBEdit.res ^
    ../dpi.cpp ../tooltip.cpp ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -luser32 -lgdi32 -lgdiplus ^
    -static -static-libgcc -static-libstdc++ ^
    -o NSBEdit.exe

if %ERRORLEVEL% neq 0 ( echo. & echo BUILD FAILED & pause & exit /b 1 )

::Keep runtime assets in sync
copy /Y ..\GPLv2.md GPLv2.md >nul
copy /Y ..\GnuLogo.bmp GnuLogo.bmp >nul
copy /Y NSB.png NSB.png >nul
copy /Y curver.txt curver.txt >nul

echo.
echo Done - NSBEdit.exe updated
