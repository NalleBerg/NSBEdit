#pragma once

#include <windows.h>
#include <string>

struct NeAiBootstrapConfig {
    bool         enabled   = false;
    std::wstring provider;
    std::wstring model;
    std::wstring fallback;
    std::wstring note;
    std::wstring installedAt;
    std::wstring version;
};

bool NeAiBootstrap_Load(NeAiBootstrapConfig& out);
const NeAiBootstrapConfig& NeAiBootstrap_Get();
