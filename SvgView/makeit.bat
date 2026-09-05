
@echo off
setlocal enabledelayedexpansion

echo Building SvgView...
if not exist "SvgView.h" (
    echo [ERROR] SvgView.h not found in current directory
    exit /b 1
)

rem -----------------------------------------------------------------
rem Path to the Qt installation you are using (adjust if needed)
rem -----------------------------------------------------------------
set QT_DIR=C:\Qt\6.10.3\mingw_64

rem -----------------------------------------------------------------
rem Compile the NanoSVG fallback implementation
rem -----------------------------------------------------------------
echo Compiling nanosvg_impl.cpp...
g++ -c nanosvg_impl.cpp -o nanosvg_impl.obj ^
    -I. -I"%QT_DIR%\include" -I"%QT_DIR%\include\QtWidgets" ^
    -I"%QT_DIR%\include\QtSvg" -O2 -mwindows
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Compilation of nanosvg_impl.cpp FAILED
    exit /b 1
)

rem -----------------------------------------------------------------
rem Compile the main SvgView source
rem -----------------------------------------------------------------
echo Compiling SvgView.cpp...
g++ -c SvgView.cpp -o SvgView.obj ^
    -I. -I"%QT_DIR%\include" -I"%QT_DIR%\include\QtWidgets" ^
    -I"%QT_DIR%\include\QtSvg" -O2 -mwindows
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Compilation of SvgView.cpp FAILED
    exit /b 1
)

rem -----------------------------------------------------------------
rem Link – note the addition of Qt6Svg and removal of libresvg
rem -----------------------------------------------------------------
echo Linking...
g++ nanosvg_impl.obj SvgView.obj ^
    -o SvgView.exe ^
    -L"%QT_DIR%\lib" ^
    -lQt6Widgets -lQt6Gui -lQt6Core -lQt6Svg ^
    -lgdiplus -lcomctl32 -lshlwapi -lusp10 -lole32 -lws2_32 ^
    -mwindows -O2
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Link FAILED
    exit /b 1
)

echo Build successful: SvgView.exe
exit /b 0
