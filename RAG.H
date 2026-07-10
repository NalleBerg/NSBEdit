#ifndef RAG_H
#define RAG_H

#include <windows.h>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <iostream>

// Structure to hold file information
struct FileInfo {
    std::string path;
    std::string content;
    size_t size;
    FILETIME lastModified;
};

// Main RAG class
class RAGIndexer {
private:
    std::string rootDir;
    std::vector<FileInfo> files;
    std::set<std::string> indexedFiles;

public:
    RAGIndexer(const std::string& directory);
    ~RAGIndexer();

    bool indexDirectory();
    bool updateIndex();
    std::string buildReport();
    void displayResults();
    void outputToStdout();
    bool saveIndex(const std::string& filename);
    bool loadIndex(const std::string& filename);

private:
    void enumerateFiles(const std::string& directory);
    bool isExcludedFile(const std::string& filename);
    bool isExcludedDir(const std::string& dirname);
    std::string readFileContent(const std::string& filepath);
    std::string getRelativePath(const std::string& fullpath);
};

#endif // RAG_H
