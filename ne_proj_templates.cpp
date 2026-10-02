// ne_proj_templates.cpp — New-Project language catalogue (Step 6 / Phase C).
//
// Pure data: starter file sets + default build/run commands for every language
// NSBEdit can scaffold.  See NewProjectLanguageFiles.txt for the source matrix.
//
// makeit.bat generation: compiled-to-native languages (C, C++, Fortran, Assembly)
// get a verbose, colourful makeit.bat that builds a static .exe and then PACKS it
// into a sub-folder named after the project (.\<Name>\<Name>.exe) holding every
// file needed to run — mirroring how the SetupCraft workspace packages itself.
// The developer owns that script: to skip packaging they just delete its
// [PACKAGE] block.  Interpreted / toolchain-driven languages use their native
// build tool (dotnet/cargo/go/…) and are not packed.

#include "ne_proj_templates.h"

// ─────────────────────────────────────────────────────────────────────────────
//  makeit.bat builders (colourful + verbose; LF line endings to match repo style)
// ─────────────────────────────────────────────────────────────────────────────

// Shared banner + ANSI-colour setup.  {{NAME}} becomes the exe/app base name.
// A self-logging wrapper mirrors the whole run to makeit.log (overwritten each
// build) while still showing the verbose, colourful output on the console.
static std::wstring MakeitHeader()
{
    return
L"@echo off\n"
L"setlocal EnableDelayedExpansion\n"
L"\n"
L"REM NSBEdit-generated build for {{NAME}} - verbose & colourful.\n"
L"REM The whole run is mirrored to makeit.log (overwritten each build).\n"
L"REM Edit freely. To skip packaging, delete the [PACKAGE] block near the end.\n"
L"\n"
L"if \"%_NSB_LOGGED%\"==\"1\" goto :_nsb_body\n"
L"set \"_NSB_LOGGED=1\"\n"
L"call \"%~f0\" %* > \"%~dp0makeit.log\" 2>&1\n"
L"set \"RC=%errorlevel%\"\n"
L"type \"%~dp0makeit.log\"\n"
L"exit /b %RC%\n"
L":_nsb_body\n"
L"\n"
L"set \"APP={{NAME}}\"\n"
L"\n"
L"REM -- ANSI colours (Windows 10+). Set NO_COLOR=1 to turn them off. --\n"
L"for /f %%E in ('echo prompt $E ^| cmd') do set \"ESC=%%E\"\n"
L"if defined NO_COLOR set \"ESC=\"\n"
L"if defined ESC (\n"
L"  set \"C0=%ESC%[0m\"  & set \"CT=%ESC%[96m\" & set \"CS=%ESC%[95m\"\n"
L"  set \"CG=%ESC%[92m\" & set \"CY=%ESC%[93m\" & set \"CR=%ESC%[91m\"\n"
L") else (\n"
L"  set \"C0=\" & set \"CT=\" & set \"CS=\" & set \"CG=\" & set \"CY=\" & set \"CR=\"\n"
L")\n"
L"\n"
L"echo.\n"
L"echo %CT%============================================================%C0%\n"
L"echo %CT%  Building %APP%%C0%\n"
L"echo %CT%============================================================%C0%\n"
L"echo.\n";
}

// [PACKAGE] block + DONE banner for native .exe projects.
static std::wstring MakeitPackExe()
{
    return
L"\n"
L"echo.\n"
L"echo %CS%[PACKAGE] Assembling .\\%APP%\\ ...%C0%\n"
L"set \"PKG=%~dp0%APP%\"\n"
L"if exist \"%PKG%\" rmdir /s /q \"%PKG%\"\n"
L"mkdir \"%PKG%\"\n"
L"copy /y \"%~dp0%APP%.exe\" \"%PKG%\\\" >nul\n"
L"REM Copy any DLLs sitting beside the exe (none when linked -static).\n"
L"if exist \"%~dp0*.dll\" copy /y \"%~dp0*.dll\" \"%PKG%\\\" >nul\n"
L"echo %CG%  [OK] Packaged -^> %PKG%\\%APP%.exe%C0%\n"
L"\n"
L"echo.\n"
L"echo %CT%============================================================%C0%\n"
L"echo %CG%  DONE - run:  %APP%\\%APP%.exe%C0%\n"
L"echo %CT%============================================================%C0%\n"
L"exit /b 0\n";
}

// A single-command native build (C / C++ / Fortran): echo + run + error gate.
static std::wstring MakeitNativeExe(const std::wstring& compileCmd)
{
    std::wstring s = MakeitHeader();
    s += L"echo %CS%[COMPILE]%C0%\n";
    s += L"echo       " + compileCmd + L"\n";
    s += compileCmd + L"\n";
    s += L"if errorlevel 1 (\n";
    s += L"  echo %CR%  [ERROR] Compilation failed.%C0%\n";
    s += L"  exit /b 1\n";
    s += L")\n";
    s += L"echo %CG%  [OK] Built %APP%.exe%C0%\n";
    s += MakeitPackExe();
    return s;
}

// Assembly build: NASM assemble + gcc link, then pack.
static std::wstring MakeitAsm()
{
    std::wstring s = MakeitHeader();
    s += L"echo %CS%[ASSEMBLE]%C0%\n";
    s += L"echo       nasm -f win64 main.asm -o main.obj\n";
    s += L"nasm -f win64 main.asm -o main.obj\n";
    s += L"if errorlevel 1 (\n";
    s += L"  echo %CR%  [ERROR] Assembly failed.%C0%\n";
    s += L"  exit /b 1\n";
    s += L")\n";
    s += L"echo %CS%[LINK]%C0%\n";
    s += L"echo       gcc -static main.obj -o \"%APP%.exe\"\n";
    s += L"gcc -static main.obj -o \"%APP%.exe\"\n";
    s += L"if errorlevel 1 (\n";
    s += L"  echo %CR%  [ERROR] Link failed.%C0%\n";
    s += L"  exit /b 1\n";
    s += L")\n";
    s += L"echo %CG%  [OK] Built %APP%.exe%C0%\n";
    s += MakeitPackExe();
    return s;
}

// Java build (javac): compile to out\, no packing (needs a JVM to run).
static std::wstring MakeitJava()
{
    std::wstring s = MakeitHeader();
    s +=
L"echo %CS%[COMPILE]%C0%\n"
L"echo       javac -d out src\\Main.java\n"
L"if not exist out mkdir out\n"
L"javac -d out src\\Main.java\n"
L"if errorlevel 1 (\n"
L"  echo %CR%  [ERROR] Compilation failed.%C0%\n"
L"  exit /b 1\n"
L")\n"
L"echo %CG%  [OK] Compiled classes -^> out\\%C0%\n"
L"\n"
L"echo.\n"
L"echo %CT%============================================================%C0%\n"
L"echo %CG%  DONE - run:  java -cp out Main%C0%\n"
L"echo %CT%============================================================%C0%\n"
L"exit /b 0\n";
    return s;
}

// SQL build: materialise app.db from schema.sql + seed.sql via sqlite3.
static std::wstring MakeitSql()
{
    std::wstring s = MakeitHeader();
    s +=
L"echo %CS%[BUILD] Creating app.db from schema.sql + seed.sql%C0%\n"
L"if exist app.db del /q app.db\n"
L"sqlite3 app.db \".read schema.sql\"\n"
L"if errorlevel 1 (\n"
L"  echo %CR%  [ERROR] schema.sql failed - is sqlite3 on PATH?%C0%\n"
L"  exit /b 1\n"
L")\n"
L"sqlite3 app.db \".read seed.sql\"\n"
L"echo %CG%  [OK] Built app.db%C0%\n"
L"\n"
L"echo.\n"
L"echo %CT%============================================================%C0%\n"
L"echo %CG%  DONE - open:  sqlite3 app.db%C0%\n"
L"echo %CT%============================================================%C0%\n"
L"exit /b 0\n";
    return s;
}

// Win32 GUI build: compiles the .rc + source into a windowed exe with whatever
// compiler is on PATH (MinGW preferred; MSVC cl fallback), then packs it.
// srcFile = L"main.cpp"/L"main.c"; mingwCc = L"g++"/L"gcc".
static std::wstring MakeitWin32(const std::wstring& srcFile, const std::wstring& mingwCc)
{
    std::wstring s = MakeitHeader();
    s +=
L"echo %CS%[COMPILE]%C0%\n"
L"REM Force a compiler with:  set NSB_CC=mingw   or   set NSB_CC=msvc\n"
L"set \"MODE=\"\n"
L"if defined NSB_CC set \"MODE=%NSB_CC%\"\n"
L"if not defined MODE where " + mingwCc + L" >nul 2>&1 && set \"MODE=mingw\"\n"
L"if not defined MODE where cl >nul 2>&1 && set \"MODE=msvc\"\n"
L"if \"%MODE%\"==\"mingw\" goto :_mingw\n"
L"if \"%MODE%\"==\"msvc\" goto :_msvc\n"
L"echo %CR%  [ERROR] No compiler on PATH (need MinGW " + mingwCc + L" or MSVC cl).%C0%\n"
L"echo %CY%  Install one and add it to PATH, then run makeit.bat again.%C0%\n"
L"exit /b 1\n"
L"\n"
L":_mingw\n"
L"echo       windres app.rc -O coff -o app_res.o\n"
L"windres app.rc -O coff -o app_res.o\n"
L"if errorlevel 1 (\n"
L"  echo %CR%  [ERROR] windres failed.%C0%\n"
L"  exit /b 1\n"
L")\n"
L"echo       " + mingwCc + L" -municode -mwindows -static -O2 " + srcFile + L" app_res.o -o \"%APP%.exe\"\n"
+ mingwCc + L" -municode -mwindows -static -O2 " + srcFile + L" app_res.o -o \"%APP%.exe\"\n"
L"if errorlevel 1 (\n"
L"  echo %CR%  [ERROR] compile failed.%C0%\n"
L"  exit /b 1\n"
L")\n"
L"goto :_built\n"
L"\n"
L":_msvc\n"
L"echo       rc /nologo /fo app.res app.rc\n"
L"rc /nologo /fo app.res app.rc\n"
L"if errorlevel 1 (\n"
L"  echo %CR%  [ERROR] rc failed.%C0%\n"
L"  exit /b 1\n"
L")\n"
L"echo       cl /nologo /O2 " + srcFile + L" app.res user32.lib gdi32.lib /Fe:\"%APP%.exe\" /link /SUBSYSTEM:WINDOWS\n"
L"cl /nologo /O2 " + srcFile + L" app.res user32.lib gdi32.lib /Fe:\"%APP%.exe\" /link /SUBSYSTEM:WINDOWS\n"
L"if errorlevel 1 (\n"
L"  echo %CR%  [ERROR] cl failed.%C0%\n"
L"  exit /b 1\n"
L")\n"
L"goto :_built\n"
L"\n"
L":_built\n"
L"echo %CG%  [OK] Built %APP%.exe (%MODE%)%C0%\n";
    s += MakeitPackExe();
    return s;
}

// Qt6 GUI build: CMake configure+build against the developer's Qt folder, then
// pack the exe and run windeployqt to drop the needed Qt DLLs/plugins beside it.
static std::wstring MakeitQt6(const std::wstring& qtPath)
{
    std::wstring qp = qtPath.empty() ? L"<set your Qt6 folder here, e.g. C:\\Qt\\6.7.2\\mingw_64>"
                                     : qtPath;
    std::wstring s = MakeitHeader();
    s += L"set \"QTDIR=" + qp + L"\"\n";
    s +=
L"REM Pick a CMake generator matching the Qt build (mingw_* -> MinGW; else default/MSVC).\n"
L"set \"GENMODE=msvc\"\n"
L"echo %QTDIR% | find /i \"mingw\" >nul && set \"GENMODE=mingw\"\n"
L"if not \"%GENMODE%\"==\"mingw\" goto :_cfg\n"
L"REM Use the MinGW that ships with Qt (same ABI as the Qt libraries).\n"
L"set \"QTMINGW=\"\n"
L"for /d %%D in (\"%QTDIR%\\..\\..\\Tools\\mingw*_64\") do set \"QTMINGW=%%~fD\"\n"
L"if defined QTMINGW set \"PATH=%QTMINGW%\\bin;%PATH%\"\n"
L"if not defined QTMINGW echo %CY%  [WARN] Qt's bundled MinGW not found under %QTDIR%\\..\\..\\Tools - using PATH g++, ABI may mismatch.%C0%\n"
L":_cfg\n"
L"echo %CS%[CONFIGURE] cmake (%GENMODE%) -DCMAKE_PREFIX_PATH=\"%QTDIR%\"%C0%\n"
L"if \"%GENMODE%\"==\"mingw\" goto :_cfg_mingw\n"
L"cmake -S . -B build -DCMAKE_PREFIX_PATH=\"%QTDIR%\"\n"
L"goto :_cfg_done\n"
L":_cfg_mingw\n"
L"cmake -S . -B build -G \"MinGW Makefiles\" -DCMAKE_PREFIX_PATH=\"%QTDIR%\" -DCMAKE_BUILD_TYPE=Release\n"
L":_cfg_done\n"
L"if errorlevel 1 (\n"
L"  echo %CR%  [ERROR] CMake configure failed - check Qt6 at %%QTDIR%% and that cmake is on PATH.%C0%\n"
L"  exit /b 1\n"
L")\n"
L"echo %CS%[BUILD] cmake --build build --config Release%C0%\n"
L"cmake --build build --config Release\n"
L"if errorlevel 1 (\n"
L"  echo %CR%  [ERROR] Build failed.%C0%\n"
L"  exit /b 1\n"
L")\n"
L"echo %CG%  [OK] Built %APP%.exe%C0%\n"
L"\n"
L"echo %CS%[PACKAGE] Assembling .\\%APP%\\ ...%C0%\n"
L"set \"EXE=\"\n"
L"for /f \"delims=\" %%I in ('dir /b /s \"build\\%APP%.exe\" 2^>nul') do set \"EXE=%%~fI\"\n"
L"if not defined EXE (\n"
L"  echo %CR%  [ERROR] %APP%.exe not found under build\\%C0%\n"
L"  exit /b 1\n"
L")\n"
L"set \"PKG=%~dp0%APP%\"\n"
L"if exist \"%PKG%\" rmdir /s /q \"%PKG%\"\n"
L"mkdir \"%PKG%\"\n"
L"copy /y \"%EXE%\" \"%PKG%\\\" >nul\n"
L"if exist \"%QTDIR%\\bin\\windeployqt.exe\" (\n"
L"  echo       windeployqt --release \"%PKG%\\%APP%.exe\"\n"
L"  \"%QTDIR%\\bin\\windeployqt.exe\" --release \"%PKG%\\%APP%.exe\"\n"
L") else (\n"
L"  echo %CY%  [WARN] windeployqt not found in %%QTDIR%%\\bin - copy the Qt DLLs yourself.%C0%\n"
L")\n"
L"echo %CG%  [OK] Packaged -^> %PKG%\\%APP%.exe%C0%\n"
L"\n"
L"echo.\n"
L"echo %CT%============================================================%C0%\n"
L"echo %CG%  DONE - run:  %APP%\\%APP%.exe%C0%\n"
L"echo %CT%============================================================%C0%\n"
L"exit /b 0\n";
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
//  GUI source bodies (one window at a set geometry, a File -> Exit menu, no more)
// ─────────────────────────────────────────────────────────────────────────────

// Win32 source — valid as both C and C++ (pure Win32 API).
static std::wstring Win32MainSrc()
{
    return
L"#ifndef UNICODE\n"
L"#define UNICODE\n"
L"#endif\n"
L"#include <windows.h>\n\n"
L"#define IDR_MAINMENU  101\n"
L"#define IDM_FILE_EXIT 1001\n\n"
L"static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {\n"
L"    switch (msg) {\n"
L"    case WM_COMMAND:\n"
L"        if (LOWORD(wParam) == IDM_FILE_EXIT) { DestroyWindow(hWnd); return 0; }\n"
L"        break;\n"
L"    case WM_DESTROY:\n"
L"        PostQuitMessage(0);\n"
L"        return 0;\n"
L"    }\n"
L"    return DefWindowProcW(hWnd, msg, wParam, lParam);\n"
L"}\n\n"
L"int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR cmdLine, int nCmdShow) {\n"
L"    (void)hPrev; (void)cmdLine;\n"
L"    const wchar_t* cls = L\"{{NAME}}Window\";\n"
L"    WNDCLASSW wc;\n"
L"    ZeroMemory(&wc, sizeof(wc));\n"
L"    wc.lpfnWndProc   = WndProc;\n"
L"    wc.hInstance     = hInstance;\n"
L"    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);\n"
L"    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);\n"
L"    wc.lpszClassName = cls;\n"
L"    wc.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAINMENU);\n"
L"    RegisterClassW(&wc);\n\n"
L"    HWND hWnd = CreateWindowExW(0, cls, L\"{{NAME}}\",\n"
L"        WS_OVERLAPPEDWINDOW,\n"
L"        120, 80, 800, 600,\n"
L"        NULL, NULL, hInstance, NULL);\n"
L"    if (!hWnd) return 0;\n\n"
L"    ShowWindow(hWnd, nCmdShow);\n"
L"    UpdateWindow(hWnd);\n\n"
L"    MSG msg;\n"
L"    while (GetMessageW(&msg, NULL, 0, 0)) {\n"
L"        TranslateMessage(&msg);\n"
L"        DispatchMessageW(&msg);\n"
L"    }\n"
L"    return (int)msg.wParam;\n"
L"}\n";
}

static std::wstring Win32Rc()
{
    return
L"#include <windows.h>\n\n"
L"101 MENU\n"
L"BEGIN\n"
L"    POPUP \"&File\"\n"
L"    BEGIN\n"
L"        MENUITEM \"E&xit\", 1001\n"
L"    END\n"
L"END\n";
}

static std::wstring Qt6MainSrc()
{
    return
L"#include <QApplication>\n"
L"#include <QMainWindow>\n"
L"#include <QMenuBar>\n"
L"#include <QMenu>\n"
L"#include <QAction>\n\n"
L"int main(int argc, char *argv[]) {\n"
L"    QApplication app(argc, argv);\n\n"
L"    QMainWindow window;\n"
L"    window.setWindowTitle(\"{{NAME}}\");\n"
L"    window.setGeometry(120, 80, 800, 600);\n\n"
L"    QMenu *fileMenu = window.menuBar()->addMenu(\"&File\");\n"
L"    QAction *exitAction = fileMenu->addAction(\"E&xit\");\n"
L"    QObject::connect(exitAction, &QAction::triggered, &app, &QApplication::quit);\n\n"
L"    window.show();\n"
L"    return app.exec();\n"
L"}\n";
}

static std::wstring Qt6CMake()
{
    return
L"cmake_minimum_required(VERSION 3.16)\n"
L"project({{NAME}} LANGUAGES CXX)\n\n"
L"set(CMAKE_CXX_STANDARD 17)\n"
L"set(CMAKE_AUTOMOC ON)\n\n"
L"find_package(Qt6 REQUIRED COMPONENTS Widgets)\n\n"
L"add_executable({{NAME}} WIN32 main.cpp)\n"
L"target_link_libraries({{NAME}} PRIVATE Qt6::Widgets)\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Common small file helpers
// ─────────────────────────────────────────────────────────────────────────────

static NeTemplateFile GitIgnore(const std::wstring& extra)
{
    std::wstring body =
L"# Editor / OS cruft\n"
L".vs/\n.vscode/\n*.user\nThumbs.db\n.DS_Store\n";
    if (!extra.empty()) { body += L"\n# Language / build output\n"; body += extra; }
    body += L"\n# NSBEdit pack output\n/{{NAME}}/\n";
    return { L".gitignore", body };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Catalogue
// ─────────────────────────────────────────────────────────────────────────────

const std::vector<NeLangInfo>& NeLang_Catalog()
{
    static const std::vector<NeLangInfo> cat = {
        // id            display            kinds                         compiled
        { L"cpp",        L"C++",            { L"cli", L"gui", L"db" },     true  },
        { L"c",          L"C",              { L"cli", L"gui" },            true  },
        { L"csharp",     L"C#",             { L"cli", L"gui" },            true  },
        { L"go",         L"Go",             { L"cli" },                    true  },
        { L"rust",       L"Rust",           { L"cli" },                    true  },
        { L"swift",      L"Swift",          { L"cli" },                    true  },
        { L"objc",       L"Objective-C",    { L"cli" },                    true  },
        { L"asm",        L"Assembly (x64)", { L"cli" },                    true  },
        { L"fortran",    L"Fortran",        { L"cli" },                    true  },
        { L"java",       L"Java",           { L"cli", L"gui" },            true  },
        { L"kotlin",     L"Kotlin",         { L"cli", L"gui" },            true  },
        { L"python",     L"Python",         { L"cli", L"gui" },            false },
        { L"javascript", L"JavaScript",     { L"cli" },                    false },
        { L"typescript", L"TypeScript",     { L"cli" },                    false },
        { L"ruby",       L"Ruby",           { L"cli" },                    false },
        { L"perl",       L"Perl",           { L"cli" },                    false },
        { L"lua",        L"Lua",            { L"cli" },                    false },
        { L"dart",       L"Dart",           { L"cli" },                    false },
        { L"r",          L"R",              { L"cli" },                    false },
        { L"bash",       L"Shell (Bash)",   { L"cli" },                    false },
        { L"powershell", L"PowerShell",     { L"cli" },                    false },
        { L"batch",      L"Batch",          { L"cli" },                    false },
        { L"html",       L"HTML / CSS / JS",{ L"website" },                false },
        { L"php",        L"PHP",            { L"website" },                false },
        { L"sql",        L"SQL (SQLite)",   { L"db" },                     true  },
    };
    return cat;
}

const NeLangInfo* NeLang_Find(const std::wstring& langId)
{
    for (const auto& l : NeLang_Catalog())
        if (l.id == langId) return &l;
    return nullptr;
}

bool NeLang_GetKindFiles(const std::wstring& langId, const std::wstring& kindId,
                         NeLangKindFiles& out,
                         const std::wstring& toolkit,
                         const std::wstring& qtPath)
{
    const NeLangInfo* li = NeLang_Find(langId);
    if (!li) return false;
    bool kindOk = false;
    for (const auto& k : li->kindIds) if (k == kindId) { kindOk = true; break; }
    if (!kindOk) return false;

    out = NeLangKindFiles{};

    // ── C ────────────────────────────────────────────────────────────────────
    if (langId == L"c") {
        if (kindId == L"gui") {
            out.buildCommand = L"makeit.bat";
            out.runCommand   = L"{{NAME}}.exe";
            out.files = {
                { L"main.c",      Win32MainSrc() },
                { L"app.rc",      Win32Rc() },
                { L"makeit.bat",  MakeitWin32(L"main.c", L"gcc") },
                GitIgnore(L"*.o\n*.obj\n*.res\n*.exe\n*.pdb\n*.ilk\n"),
            };
            return true;
        }
        out.buildCommand = L"makeit.bat";
        out.runCommand   = L"{{NAME}}.exe";
        out.files = {
            { L"main.c",
L"#include <stdio.h>\n\n"
L"int main(void) {\n"
L"    printf(\"Hello from {{NAME}}!\\n\");\n"
L"    return 0;\n"
L"}\n" },
            { L"makeit.bat", MakeitNativeExe(L"gcc -std=c11 -static -O2 main.c -o \"%APP%.exe\"") },
            { L"CMakeLists.txt",
L"cmake_minimum_required(VERSION 3.15)\n"
L"project({{NAME}} LANGUAGES C)\n"
L"set(CMAKE_C_STANDARD 11)\n"
L"add_executable({{NAME}} main.c)\n" },
            GitIgnore(L"*.o\n*.obj\n*.exe\n*.pdb\n*.ilk\nbuild/\n"),
        };
        return true;
    }

    // ── C++ ──────────────────────────────────────────────────────────────────
    if (langId == L"cpp") {
        if (kindId == L"cli") {
            out.buildCommand = L"makeit.bat";
            out.runCommand   = L"{{NAME}}.exe";
            out.files = {
                { L"main.cpp",
L"#include <iostream>\n\n"
L"int main() {\n"
L"    std::cout << \"Hello from {{NAME}}!\\n\";\n"
L"    return 0;\n"
L"}\n" },
                { L"makeit.bat", MakeitNativeExe(L"g++ -std=c++17 -static -O2 main.cpp -o \"%APP%.exe\"") },
                { L"CMakeLists.txt",
L"cmake_minimum_required(VERSION 3.15)\n"
L"project({{NAME}} LANGUAGES CXX)\n"
L"set(CMAKE_CXX_STANDARD 17)\n"
L"add_executable({{NAME}} main.cpp)\n" },
                GitIgnore(L"*.o\n*.obj\n*.exe\n*.pdb\n*.ilk\nbuild/\n"),
            };
            return true;
        }
        if (kindId == L"gui") {
            out.buildCommand = L"makeit.bat";
            out.runCommand   = L"{{NAME}}.exe";
            if (toolkit == L"qt6") {
                out.files = {
                    { L"main.cpp",       Qt6MainSrc() },
                    { L"CMakeLists.txt", Qt6CMake() },
                    { L"makeit.bat",     MakeitQt6(qtPath) },
                    GitIgnore(L"build/\n*.exe\n"),
                };
            } else {
                out.files = {
                    { L"main.cpp",   Win32MainSrc() },
                    { L"app.rc",     Win32Rc() },
                    { L"makeit.bat", MakeitWin32(L"main.cpp", L"g++") },
                    GitIgnore(L"*.o\n*.obj\n*.res\n*.exe\n*.pdb\n*.ilk\nbuild/\n"),
                };
            }
            return true;
        }
        // db kind: own step (8) — only the common scaffold for now.
        out.buildCommand = L"makeit.bat";
        out.runCommand   = L"{{NAME}}.exe";
        out.files = { GitIgnore(L"*.o\n*.obj\n*.exe\n*.pdb\n*.ilk\nbuild/\n") };
        return true;
    }

    // ── C# (.NET) ─────────────────────────────────────────────────────────────
    if (langId == L"csharp") {
        if (kindId == L"gui") {
            out.buildCommand = L"dotnet build";
            out.runCommand   = L"dotnet run";
            out.files = {
                { L"Program.cs",
L"using System;\n"
L"using System.Windows.Forms;\n\n"
L"static class Program {\n"
L"    [STAThread]\n"
L"    static void Main() {\n"
L"        ApplicationConfiguration.Initialize();\n"
L"        var form = new Form { Text = \"{{NAME}}\", Width = 800, Height = 600 };\n"
L"        var menu = new MenuStrip();\n"
L"        var fileItem = new ToolStripMenuItem(\"&File\");\n"
L"        var exitItem = new ToolStripMenuItem(\"E&xit\", null, (s, e) => Application.Exit());\n"
L"        fileItem.DropDownItems.Add(exitItem);\n"
L"        menu.Items.Add(fileItem);\n"
L"        form.MainMenuStrip = menu;\n"
L"        form.Controls.Add(menu);\n"
L"        Application.Run(form);\n"
L"    }\n"
L"}\n" },
                { L"{{NAME}}.csproj",
L"<Project Sdk=\"Microsoft.NET.Sdk\">\n"
L"  <PropertyGroup>\n"
L"    <OutputType>WinExe</OutputType>\n"
L"    <TargetFramework>net8.0-windows</TargetFramework>\n"
L"    <UseWindowsForms>true</UseWindowsForms>\n"
L"    <ImplicitUsings>enable</ImplicitUsings>\n"
L"    <Nullable>enable</Nullable>\n"
L"  </PropertyGroup>\n"
L"</Project>\n" },
                GitIgnore(L"bin/\nobj/\n"),
            };
            return true;
        }
        out.buildCommand = L"dotnet build";
        out.runCommand   = L"dotnet run";
        out.files = {
            { L"Program.cs",
L"using System;\n\n"
L"class Program {\n"
L"    static void Main() {\n"
L"        Console.WriteLine(\"Hello from {{NAME}}!\");\n"
L"    }\n"
L"}\n" },
            { L"{{NAME}}.csproj",
L"<Project Sdk=\"Microsoft.NET.Sdk\">\n"
L"  <PropertyGroup>\n"
L"    <OutputType>Exe</OutputType>\n"
L"    <TargetFramework>net8.0</TargetFramework>\n"
L"    <ImplicitUsings>enable</ImplicitUsings>\n"
L"    <Nullable>enable</Nullable>\n"
L"  </PropertyGroup>\n"
L"</Project>\n" },
            GitIgnore(L"bin/\nobj/\n"),
        };
        return true;
    }

    // ── Go ────────────────────────────────────────────────────────────────────
    if (langId == L"go") {
        out.buildCommand = L"go build";
        out.runCommand   = L"go run main.go";
        out.files = {
            { L"main.go",
L"package main\n\n"
L"import \"fmt\"\n\n"
L"func main() {\n"
L"    fmt.Println(\"Hello from {{NAME}}!\")\n"
L"}\n" },
            { L"go.mod",
L"module {{NAME}}\n\n"
L"go 1.22\n" },
            GitIgnore(L"/bin\n*.exe\n"),
        };
        return true;
    }

    // ── Rust ──────────────────────────────────────────────────────────────────
    if (langId == L"rust") {
        out.buildCommand = L"cargo build";
        out.runCommand   = L"cargo run";
        out.files = {
            { L"src/main.rs",
L"fn main() {\n"
L"    println!(\"Hello from {{NAME}}!\");\n"
L"}\n" },
            { L"Cargo.toml",
L"[package]\n"
L"name = \"{{NAME}}\"\n"
L"version = \"0.1.0\"\n"
L"edition = \"2021\"\n\n"
L"[dependencies]\n" },
            GitIgnore(L"/target\n"),
        };
        return true;
    }

    // ── Swift ─────────────────────────────────────────────────────────────────
    if (langId == L"swift") {
        out.buildCommand = L"swift build";
        out.runCommand   = L"swift run";
        out.files = {
            { L"Sources/{{NAME}}/main.swift",
L"print(\"Hello from {{NAME}}!\")\n" },
            { L"Package.swift",
L"// swift-tools-version:5.9\n"
L"import PackageDescription\n\n"
L"let package = Package(\n"
L"    name: \"{{NAME}}\",\n"
L"    targets: [\n"
L"        .executableTarget(name: \"{{NAME}}\")\n"
L"    ]\n"
L")\n" },
            GitIgnore(L".build/\n"),
        };
        return true;
    }

    // ── Objective-C ───────────────────────────────────────────────────────────
    if (langId == L"objc") {
        out.buildCommand = L"make";
        out.runCommand   = L"app.exe";
        out.files = {
            { L"main.m",
L"#import <Foundation/Foundation.h>\n\n"
L"int main(int argc, const char* argv[]) {\n"
L"    @autoreleasepool {\n"
L"        NSLog(@\"Hello from {{NAME}}!\");\n"
L"    }\n"
L"    return 0;\n"
L"}\n" },
            { L"Makefile",
L"app:\n"
L"\tclang -framework Foundation main.m -o app\n" },
            GitIgnore(L"*.o\napp\napp.exe\n"),
        };
        return true;
    }

    // ── Assembly (x64, NASM) ──────────────────────────────────────────────────
    if (langId == L"asm") {
        out.buildCommand = L"makeit.bat";
        out.runCommand   = L"{{NAME}}.exe";
        out.files = {
            { L"main.asm",
L"; NASM x64 (Windows). Links against the C runtime via gcc.\n"
L"    global  main\n"
L"    extern  printf\n\n"
L"    section .data\n"
L"msg:    db  \"Hello from {{NAME}}!\", 10, 0\n\n"
L"    section .text\n"
L"main:\n"
L"    push    rbp\n"
L"    mov     rbp, rsp\n"
L"    sub     rsp, 32\n"
L"    lea     rcx, [rel msg]\n"
L"    call    printf\n"
L"    xor     eax, eax\n"
L"    add     rsp, 32\n"
L"    pop     rbp\n"
L"    ret\n" },
            { L"makeit.bat", MakeitAsm() },
            GitIgnore(L"*.obj\n*.exe\n"),
        };
        return true;
    }

    // ── Fortran ───────────────────────────────────────────────────────────────
    if (langId == L"fortran") {
        out.buildCommand = L"makeit.bat";
        out.runCommand   = L"{{NAME}}.exe";
        out.files = {
            { L"main.f90",
L"program main\n"
L"    print *, \"Hello from {{NAME}}!\"\n"
L"end program main\n" },
            { L"makeit.bat", MakeitNativeExe(L"gfortran -static -O2 main.f90 -o \"%APP%.exe\"") },
            { L"CMakeLists.txt",
L"cmake_minimum_required(VERSION 3.15)\n"
L"project({{NAME}} LANGUAGES Fortran)\n"
L"add_executable({{NAME}} main.f90)\n" },
            GitIgnore(L"*.o\n*.mod\n*.exe\nbuild/\n"),
        };
        return true;
    }

    // ── Java ──────────────────────────────────────────────────────────────────
    if (langId == L"java") {
        out.buildCommand = L"makeit.bat";
        out.runCommand   = L"java -cp out Main";
        if (kindId == L"gui") {
            out.files = {
                { L"src/Main.java",
L"import javax.swing.*;\n\n"
L"public class Main {\n"
L"    public static void main(String[] args) {\n"
L"        SwingUtilities.invokeLater(() -> {\n"
L"            JFrame frame = new JFrame(\"{{NAME}}\");\n"
L"            frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);\n"
L"            frame.setBounds(120, 80, 800, 600);\n\n"
L"            JMenuBar menuBar = new JMenuBar();\n"
L"            JMenu fileMenu = new JMenu(\"File\");\n"
L"            JMenuItem exitItem = new JMenuItem(\"Exit\");\n"
L"            exitItem.addActionListener(e -> System.exit(0));\n"
L"            fileMenu.add(exitItem);\n"
L"            menuBar.add(fileMenu);\n"
L"            frame.setJMenuBar(menuBar);\n\n"
L"            frame.setVisible(true);\n"
L"        });\n"
L"    }\n"
L"}\n" },
                { L"makeit.bat", MakeitJava() },
                GitIgnore(L"*.class\nout/\ntarget/\nbuild/\n.gradle/\n"),
            };
            return true;
        }
        out.files = {
            { L"src/Main.java",
L"public class Main {\n"
L"    public static void main(String[] args) {\n"
L"        System.out.println(\"Hello from {{NAME}}!\");\n"
L"    }\n"
L"}\n" },
            { L"makeit.bat", MakeitJava() },
            GitIgnore(L"*.class\nout/\ntarget/\nbuild/\n.gradle/\n"),
        };
        return true;
    }

    // ── Kotlin ────────────────────────────────────────────────────────────────
    if (langId == L"kotlin") {
        out.buildCommand = L"gradle build";
        out.runCommand   = L"gradle run";
        std::wstring kt = (kindId == L"gui")
?
L"import javax.swing.*\n\n"
L"fun main() {\n"
L"    SwingUtilities.invokeLater {\n"
L"        val frame = JFrame(\"{{NAME}}\")\n"
L"        frame.defaultCloseOperation = JFrame.EXIT_ON_CLOSE\n"
L"        frame.setBounds(120, 80, 800, 600)\n\n"
L"        val menuBar = JMenuBar()\n"
L"        val fileMenu = JMenu(\"File\")\n"
L"        val exitItem = JMenuItem(\"Exit\")\n"
L"        exitItem.addActionListener { System.exit(0) }\n"
L"        fileMenu.add(exitItem)\n"
L"        menuBar.add(fileMenu)\n"
L"        frame.jMenuBar = menuBar\n\n"
L"        frame.isVisible = true\n"
L"    }\n"
L"}\n"
:
L"fun main() {\n"
L"    println(\"Hello from {{NAME}}!\")\n"
L"}\n";
        out.files = {
            { L"src/main/kotlin/Main.kt", kt },
            { L"build.gradle.kts",
L"plugins {\n"
L"    kotlin(\"jvm\") version \"1.9.22\"\n"
L"    application\n"
L"}\n\n"
L"repositories { mavenCentral() }\n\n"
L"application {\n"
L"    mainClass.set(\"MainKt\")\n"
L"}\n" },
            GitIgnore(L"build/\n.gradle/\n"),
        };
        return true;
    }

    // ── Python ────────────────────────────────────────────────────────────────
    if (langId == L"python") {
        out.buildCommand = L"";
        out.runCommand   = L"python main.py";
        if (kindId == L"gui") {
            out.files = {
                { L"main.py",
L"import tkinter as tk\n\n\n"
L"def main():\n"
L"    root = tk.Tk()\n"
L"    root.title(\"{{NAME}}\")\n"
L"    root.geometry(\"800x600+120+80\")\n\n"
L"    menubar = tk.Menu(root)\n"
L"    file_menu = tk.Menu(menubar, tearoff=0)\n"
L"    file_menu.add_command(label=\"Exit\", command=root.destroy)\n"
L"    menubar.add_cascade(label=\"File\", menu=file_menu)\n"
L"    root.config(menu=menubar)\n\n"
L"    root.mainloop()\n\n\n"
L"if __name__ == \"__main__\":\n"
L"    main()\n" },
                GitIgnore(L"__pycache__/\n*.pyc\n.venv/\nvenv/\n"),
            };
            return true;
        }
        out.files = {
            { L"main.py",
L"def main():\n"
L"    print(\"Hello from {{NAME}}!\")\n\n\n"
L"if __name__ == \"__main__\":\n"
L"    main()\n" },
            { L"requirements.txt",
L"# Add your dependencies here, one per line.\n" },
            GitIgnore(L"__pycache__/\n*.pyc\n.venv/\nvenv/\n"),
        };
        return true;
    }

    // ── JavaScript (Node.js) ──────────────────────────────────────────────────
    if (langId == L"javascript") {
        out.buildCommand = L"";
        out.runCommand   = L"node index.js";
        out.files = {
            { L"index.js",
L"console.log(\"Hello from {{NAME}}!\");\n" },
            { L"package.json",
L"{\n"
L"  \"name\": \"{{NAME}}\",\n"
L"  \"version\": \"0.1.0\",\n"
L"  \"main\": \"index.js\",\n"
L"  \"scripts\": {\n"
L"    \"start\": \"node index.js\"\n"
L"  }\n"
L"}\n" },
            GitIgnore(L"node_modules/\n"),
        };
        return true;
    }

    // ── TypeScript ────────────────────────────────────────────────────────────
    if (langId == L"typescript") {
        out.buildCommand = L"tsc";
        out.runCommand   = L"node dist/index.js";
        out.files = {
            { L"src/index.ts",
L"const appName: string = \"{{NAME}}\";\n"
L"console.log(`Hello from ${appName}!`);\n" },
            { L"package.json",
L"{\n"
L"  \"name\": \"{{NAME}}\",\n"
L"  \"version\": \"0.1.0\",\n"
L"  \"main\": \"dist/index.js\",\n"
L"  \"scripts\": {\n"
L"    \"build\": \"tsc\",\n"
L"    \"start\": \"node dist/index.js\"\n"
L"  }\n"
L"}\n" },
            { L"tsconfig.json",
L"{\n"
L"  \"compilerOptions\": {\n"
L"    \"target\": \"ES2020\",\n"
L"    \"module\": \"CommonJS\",\n"
L"    \"outDir\": \"dist\",\n"
L"    \"rootDir\": \"src\",\n"
L"    \"strict\": true\n"
L"  }\n"
L"}\n" },
            GitIgnore(L"node_modules/\ndist/\n"),
        };
        return true;
    }

    // ── Ruby ──────────────────────────────────────────────────────────────────
    if (langId == L"ruby") {
        out.buildCommand = L"";
        out.runCommand   = L"ruby main.rb";
        out.files = {
            { L"main.rb",
L"puts \"Hello from {{NAME}}!\"\n" },
            { L"Gemfile",
L"source \"https://rubygems.org\"\n" },
            GitIgnore(L"vendor/bundle/\n.bundle/\n"),
        };
        return true;
    }

    // ── Perl ──────────────────────────────────────────────────────────────────
    if (langId == L"perl") {
        out.buildCommand = L"";
        out.runCommand   = L"perl main.pl";
        out.files = {
            { L"main.pl",
L"#!/usr/bin/env perl\n"
L"use strict;\n"
L"use warnings;\n\n"
L"print \"Hello from {{NAME}}!\\n\";\n" },
            GitIgnore(L""),
        };
        return true;
    }

    // ── Lua ───────────────────────────────────────────────────────────────────
    if (langId == L"lua") {
        out.buildCommand = L"";
        out.runCommand   = L"lua main.lua";
        out.files = {
            { L"main.lua",
L"print(\"Hello from {{NAME}}!\")\n" },
            GitIgnore(L""),
        };
        return true;
    }

    // ── Dart ──────────────────────────────────────────────────────────────────
    if (langId == L"dart") {
        out.buildCommand = L"dart pub get";
        out.runCommand   = L"dart run";
        out.files = {
            { L"bin/main.dart",
L"void main() {\n"
L"  print('Hello from {{NAME}}!');\n"
L"}\n" },
            { L"pubspec.yaml",
L"name: {{NAME}}\n"
L"description: A new project created with NSBEdit.\n"
L"version: 0.1.0\n"
L"environment:\n"
L"  sdk: '>=3.0.0 <4.0.0'\n" },
            GitIgnore(L".dart_tool/\nbuild/\n"),
        };
        return true;
    }

    // ── R ─────────────────────────────────────────────────────────────────────
    if (langId == L"r") {
        out.buildCommand = L"";
        out.runCommand   = L"Rscript main.R";
        out.files = {
            { L"main.R",
L"cat(\"Hello from {{NAME}}!\\n\")\n" },
            GitIgnore(L".Rhistory\n.RData\n"),
        };
        return true;
    }

    // ── Shell (Bash) ──────────────────────────────────────────────────────────
    if (langId == L"bash") {
        out.buildCommand = L"";
        out.runCommand   = L"bash main.sh";
        out.files = {
            { L"main.sh",
L"#!/usr/bin/env bash\n"
L"echo \"Hello from {{NAME}}!\"\n" },
            GitIgnore(L""),
        };
        return true;
    }

    // ── PowerShell ────────────────────────────────────────────────────────────
    if (langId == L"powershell") {
        out.buildCommand = L"";
        out.runCommand   = L"pwsh ./main.ps1";
        out.files = {
            { L"main.ps1",
L"Write-Host \"Hello from {{NAME}}!\"\n" },
            GitIgnore(L""),
        };
        return true;
    }

    // ── Batch (Windows) ───────────────────────────────────────────────────────
    if (langId == L"batch") {
        out.buildCommand = L"";
        out.runCommand   = L"main.bat";
        out.files = {
            { L"main.bat",
L"@echo off\n"
L"echo Hello from {{NAME}}!\n" },
            GitIgnore(L""),
        };
        return true;
    }

    // ── HTML / CSS / JS (static site) ─────────────────────────────────────────
    if (langId == L"html") {
        out.buildCommand = L"";
        out.runCommand   = L"index.html";
        out.files = {
            { L"index.html",
L"<!DOCTYPE html>\n"
L"<html lang=\"en\">\n"
L"<head>\n"
L"  <meta charset=\"utf-8\">\n"
L"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
L"  <title>{{NAME}}</title>\n"
L"  <link rel=\"stylesheet\" href=\"css/style.css\">\n"
L"</head>\n"
L"<body>\n"
L"  <h1>{{NAME}}</h1>\n"
L"  <p>A new static site created with NSBEdit.</p>\n"
L"  <script src=\"js/script.js\"></script>\n"
L"</body>\n"
L"</html>\n" },
            { L"css/style.css",
L"body {\n"
L"  font-family: system-ui, sans-serif;\n"
L"  margin: 2rem;\n"
L"  color: #222;\n"
L"}\n" },
            { L"js/script.js",
L"console.log('Hello from {{NAME}}!');\n" },
            GitIgnore(L""),
        };
        return true;
    }

    // ── PHP (server site) ─────────────────────────────────────────────────────
    if (langId == L"php") {
        out.buildCommand = L"";
        out.runCommand   = L"php -S localhost:8000";
        out.files = {
            { L"index.php",
L"<?php require __DIR__ . '/includes/header.php'; ?>\n"
L"  <h1><?= htmlspecialchars('{{NAME}}') ?></h1>\n"
L"  <p>A new PHP site created with NSBEdit.</p>\n"
L"</body>\n"
L"</html>\n" },
            { L"includes/header.php",
L"<?php\n"
L"// Shared header for {{NAME}}.\n"
L"?>\n"
L"<!DOCTYPE html>\n"
L"<html lang=\"en\">\n"
L"<head><meta charset=\"utf-8\"><title>{{NAME}}</title></head>\n"
L"<body>\n" },
            { L".htaccess",
L"DirectoryIndex index.php\n" },
            GitIgnore(L"vendor/\n"),
        };
        return true;
    }

    // ── SQL (SQLite / DB project) ─────────────────────────────────────────────
    if (langId == L"sql") {
        out.buildCommand = L"makeit.bat";
        out.runCommand   = L"sqlite3 app.db";
        out.files = {
            { L"schema.sql",
L"CREATE TABLE IF NOT EXISTS items (\n"
L"    id    INTEGER PRIMARY KEY,\n"
L"    name  TEXT NOT NULL\n"
L");\n" },
            { L"seed.sql",
L"INSERT INTO items (name) VALUES ('first'), ('second');\n" },
            { L"makeit.bat", MakeitSql() },
            GitIgnore(L"*.db\n*.sqlite\n"),
        };
        return true;
    }

    // Known language but no file set wired yet: common scaffold only.
    return true;
}
