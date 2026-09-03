#pragma once

#include <windows.h>
#include <string>

void Ne_ShowAiWindow(HWND parent);

// Zoom the AI window's query + answer panes together when it is the active
// window. dir: +1 in, -1 out, 0 reset. Returns true if the AI window handled it.
bool NsbAi_StepZoomIfActive(HWND fromHwnd, int dir);


std::string SanitizeAIInput(const std::string& raw);

