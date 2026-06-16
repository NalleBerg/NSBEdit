#pragma once

#include <windows.h>

HWND RegexGuide_Show(HWND parent, bool postQuitOnDestroy = false,
	const wchar_t* title = L"Regex Reference Guide",
	const wchar_t* label = L"Containing",
	const wchar_t* note = L"Ctrl+F focuses the search box. Search updates as you type.",
	const wchar_t* guideText = L"",
	const wchar_t* searchCue = L"Search guide text",
	const wchar_t* clearTip = L"Clear search",
	const wchar_t* closeText = L"Close");

void RegexGuide_Close();
