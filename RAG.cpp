#include "RAG.h"
#include <shlwapi.h>
#include <direct.h>
#include <io.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

#ifdef _MSC_VER
#pragma comment(lib, "Shlwapi.lib")   // MSVC auto-link; GCC/Clang link via -lShlwapi
#endif

RAGIndexer::RAGIndexer(const std::string& directory) : rootDir(directory) {
    if (rootDir.back() != '\\' && rootDir.back() != '/') {
        rootDir += "\\";
    }
}

RAGIndexer::~RAGIndexer() {}

bool RAGIndexer::indexDirectory() {
    files.clear();
    indexedFiles.clear();

    enumerateFiles(rootDir);
    return true;
}

bool RAGIndexer::updateIndex() {
    std::set<std::string> currentFiles;
    std::vector<FileInfo> newFiles;

    // Enumerate all files in directory
    enumerateFiles(rootDir);

    // Find files that need to be updated
    for (const auto& file : files) {
        currentFiles.insert(file.path);
        if (indexedFiles.find(file.path) == indexedFiles.end()) {
            newFiles.push_back(file);
        }
    }

    // Update indexed files set
    indexedFiles = currentFiles;

    return true;
}

void RAGIndexer::enumerateFiles(const std::string& directory) {
    WIN32_FIND_DATA findData;
    std::string searchPath = directory + "*";

    HANDLE hFind = FindFirstFile(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        // Skip directories . and ..
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0 &&
                !isExcludedDir(findData.cFileName)) {
                std::string subDir = directory + findData.cFileName + "\\";
                enumerateFiles(subDir);
            }
        } else {
            // Skip excluded files
            if (!isExcludedFile(findData.cFileName)) {
                FileInfo info;
                info.path = directory + findData.cFileName;
                info.size = findData.nFileSizeLow;
                info.lastModified = findData.ftLastWriteTime;
                info.content = readFileContent(info.path);
                files.push_back(info);
            }
        }
    } while (FindNextFile(hFind, &findData));

    FindClose(hFind);
}

bool RAGIndexer::isExcludedFile(const std::string& filename) {
    // Exclude build logs and scratch files
    if (filename.find("build") == 0 ||
        filename.find("output") == 0 ||
        filename.find(".log") != std::string::npos ||
        filename.find(".tmp") != std::string::npos ||
        filename.find(".bak") != std::string::npos) {
        return true;
    }

    // Exclude common binary and system files
    std::vector<std::string> excludedExtensions = {
        ".exe", ".dll", ".obj", ".o", ".lib", ".pdb",
        ".class", ".jar", ".so", ".dylib"
    };

    std::string lowerName = filename;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    for (const auto& ext : excludedExtensions) {
        if (lowerName.length() >= ext.length() &&
            lowerName.substr(lowerName.length() - ext.length()) == ext) {
            return true;
        }
    }

    // Skip system files
    if (filename.find("$") != std::string::npos ||
        filename.find(".DS_Store") != std::string::npos ||
        filename.find("~") == 0) {
        return true;
    }

    return false;
}

// Skip vendored / third-party / build-output directories so the index only
// covers this project's own sources.  Compared case-insensitively by folder name.
bool RAGIndexer::isExcludedDir(const std::string& dirname) {
    std::string lower = dirname;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    static const char* excludedDirs[] = {
        ".git", ".vscode", "zip",
        "scintilla", "scintilla_src", "lexilla_src",
        "curl", "sqlite3", "third_party", "rtf2html",
        "backup_current_ai"
    };

    for (const char* d : excludedDirs) {
        if (lower == d) return true;
    }
    return false;
}

std::string RAGIndexer::readFileContent(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    // Read file content
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Limit size for performance (10KB per file)
    if (content.length() > 10240) {
        content = content.substr(0, 10240);
    }

    return content;
}

std::string RAGIndexer::getRelativePath(const std::string& fullpath) {
    size_t pos = fullpath.find(rootDir);
    if (pos != std::string::npos) {
        return fullpath.substr(pos + rootDir.length());
    }
    return fullpath;
}

// Build the human-readable index report as a UTF-8 string so it can be sent to
// a console, a GUI window, or a log file without duplicating the formatting.
std::string RAGIndexer::buildReport() {
    std::ostringstream out;
    out << "RAG Indexer Results\n";
    out << "===================\n";
    out << "Root: " << rootDir << "\n";
    out << "Indexed " << files.size() << " files\n\n";

    for (const auto& file : files) {
        out << "File: " << getRelativePath(file.path) << "\n";
        out << "Size: " << file.size << " bytes\n";
        out << "Content preview:\n";

        // Show first 100 chars of content
        size_t contentLength = std::min(file.content.length(), size_t(100));
        for (size_t i = 0; i < contentLength; ++i) {
            unsigned char c = static_cast<unsigned char>(file.content[i]);
            if (c >= 32 && c <= 126) {
                out << file.content[i];
            } else {
                out << "?";
            }
        }
        if (file.content.length() > 100) {
            out << " ... ";
        }
        out << "\n\n";
    }

    return out.str();
}

void RAGIndexer::displayResults() {
    HWND hwnd = GetConsoleWindow();
    if (hwnd == NULL) {
        // Try to create a console window
        AllocConsole();
        hwnd = GetConsoleWindow();
    }

    if (hwnd != NULL) {
        // Emit UTF-8 so paths/content with non-ASCII bytes render correctly.
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleTitleA("RAG Index Results");
        std::string report = buildReport();
        fwrite(report.data(), 1, report.size(), stdout);
        fflush(stdout);
    }
}

void RAGIndexer::outputToStdout() {
    SetConsoleOutputCP(CP_UTF8);
    std::string report = buildReport();
    fwrite(report.data(), 1, report.size(), stdout);
    fflush(stdout);
}

bool RAGIndexer::saveIndex(const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Save number of files
    size_t numFiles = files.size();
    file.write(reinterpret_cast<const char*>(&numFiles), sizeof(numFiles));

    for (const auto& fileinfo : files) {
        // Save path length and path
        size_t pathLength = fileinfo.path.length();
        file.write(reinterpret_cast<const char*>(&pathLength), sizeof(pathLength));
        file.write(fileinfo.path.c_str(), pathLength);

        // Save content length and content
        size_t contentLength = fileinfo.content.length();
        file.write(reinterpret_cast<const char*>(&contentLength), sizeof(contentLength));
        file.write(fileinfo.content.c_str(), contentLength);

        // Save file size
        file.write(reinterpret_cast<const char*>(&fileinfo.size), sizeof(fileinfo.size));

        // Save last modified time
        file.write(reinterpret_cast<const char*>(&fileinfo.lastModified), sizeof(fileinfo.lastModified));
    }

    return true;
}

bool RAGIndexer::loadIndex(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    files.clear();

    // Read number of files
    size_t numFiles = 0;
    file.read(reinterpret_cast<char*>(&numFiles), sizeof(numFiles));

    for (size_t i = 0; i < numFiles; ++i) {
        FileInfo info;

        // Read path
        size_t pathLength = 0;
        file.read(reinterpret_cast<char*>(&pathLength), sizeof(pathLength));
        info.path.resize(pathLength);
        file.read(&info.path[0], pathLength);

        // Read content
        size_t contentLength = 0;
        file.read(reinterpret_cast<char*>(&contentLength), sizeof(contentLength));
        info.content.resize(contentLength);
        file.read(&info.content[0], contentLength);

        // Read file size
        file.read(reinterpret_cast<char*>(&info.size), sizeof(info.size));

        // Read last modified time
        file.read(reinterpret_cast<char*>(&info.lastModified), sizeof(info.lastModified));

        files.push_back(info);
    }

    return true;
}
