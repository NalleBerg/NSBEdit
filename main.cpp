#include "RAG.h"

#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>

// ── UTF-8 → UTF-16 helper ───────────────────────────────────────────────────
static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// ── Report window ───────────────────────────────────────────────────────────
static const wchar_t* kReportClass = L"RagReportWindow";
static std::wstring    g_reportText;

static LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        HWND hEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)1, GetModuleHandleW(nullptr), nullptr);

        // Fixed-pitch font so the preview lines up like a console.
        HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        SetWindowTextW(hEdit, g_reportText.c_str());
        return 0;
    }
    case WM_SIZE: {
        HWND hEdit = GetDlgItem(hwnd, 1);
        if (hEdit) {
            MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowReportWindow(const std::string& report)
{
    g_reportText = Utf8ToWide(report);

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = ReportWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kReportClass;
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, kReportClass, L"RAG Indexer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 560,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

// ── Write the report to a log file (appended) ───────────────────────────────
// Uses the Win32 API with full share mode so it can append even while another
// process (e.g. makeit.bat's "tee makeit.log") holds the file open.
static bool WriteReportToLog(const std::string& report, const std::string& logPath)
{
    std::wstring wpath = Utf8ToWide(logPath);
    HANDLE h = CreateFileW(wpath.c_str(), FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    SetFilePointer(h, 0, nullptr, FILE_END);

    std::string block;
    block  = "\n============================================================\n";
    block += report;
    block += "============================================================\n";

    DWORD written = 0;
    BOOL ok = WriteFile(h, block.data(), (DWORD)block.size(), &written, nullptr);
    CloseHandle(h);
    return ok && written == block.size();
}

int main(int argc, char* argv[])
{
    // Treat all narrow strings as UTF-8 for console output.
    SetConsoleOutputCP(CP_UTF8);

    std::string directory;          // defaults to current directory below
    bool toStdout = false;
    bool toLog    = false;
    std::string logFile;
    std::string saveFile;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stdout") {
            toStdout = true;
        } else if (arg == "--log") {
            // --log requires a filename, e.g. --log .\makeit.log
            if (i + 1 < argc) {
                toLog = true;
                logFile = argv[++i];
            } else {
                std::cerr << "Error: --log requires a file name.\n";
                return 1;
            }
        } else if (arg == "--save") {
            if (i + 1 < argc) {
                saveFile = argv[++i];
            } else {
                std::cerr << "Error: --save requires a file name.\n";
                return 1;
            }
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "Error: unknown argument '" << arg << "'.\n";
            return 1;
        } else if (directory.empty()) {
            directory = arg;
        }
    }

    // No directory given (e.g. double-clicked): index the current directory.
    if (directory.empty()) directory = ".";

    RAGIndexer indexer(directory);
    if (!indexer.indexDirectory()) {
        std::string err = "RAG Indexer: failed to index directory '" + directory + "'.";
        if (toLog)          WriteReportToLog(err + "\n", logFile);
        else if (toStdout)  std::cerr << err << "\n";
        else                MessageBoxW(nullptr, Utf8ToWide(err).c_str(),
                                        L"RAG Indexer", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::string report = indexer.buildReport();

    if (toStdout) {
        indexer.outputToStdout();
    } else if (toLog) {
        if (!WriteReportToLog(report, logFile)) {
            std::cerr << "Error: failed to write log '" << logFile << "'.\n";
            return 1;
        }
    } else {
        // Launched by mouse: show the report in a window.
        ShowReportWindow(report);
    }

    if (!saveFile.empty()) {
        if (!indexer.saveIndex(saveFile)) {
            std::cerr << "Error: failed to save index to '" << saveFile << "'.\n";
            return 1;
        }
    }

    return 0;
}
