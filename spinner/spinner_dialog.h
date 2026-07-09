#pragma once

#include <windows.h>
#include <string>
#include <functional>

// SpinnerDialog - Reusable animated loading spinner dialog
// Displays a blue spinning circle with customizable text message
// Modal dialog that runs in a separate thread with independent message loop
//
// Usage:
//   SpinnerDialog* spinner = new SpinnerDialog(parentHwnd);
//   spinner->Show(L"Processing, please wait...");
//   // ... do work ...
//   spinner->Hide();
//   delete spinner;

class SpinnerDialog {
public:
    // Constructor
    // hParent: Parent window handle (optional, for centering)
    SpinnerDialog(HWND hParent = NULL);
    
    // Destructor - automatically hides and cleans up
    ~SpinnerDialog();
    
    // Show the spinner dialog with custom message
    // text: Message to display below the icon (e.g., "Querying winget, please wait...")
    void Show(const std::wstring& text);
    
    // Hide the spinner dialog
    void Hide();
    
    // Update the displayed text without hiding/showing
    // text: New message to display
    void SetText(const std::wstring& text);

    // Set the title-bar caption (defaults to "Please Wait"). Call before Show()
    // to localise the caption.
    void SetTitle(const std::wstring& title);

    // Enable an optional owner-drawn Stop button below the spinner. When the
    // user clicks it (or presses Esc) onStop is invoked on the UI thread. Call
    // before Show(), or while visible to add it on the fly. Only spinners that
    // set this get a button; all other callers keep the plain spinner.
    void SetStopButton(const std::wstring& label, std::function<void()> onStop);

    // Check if dialog is currently visible
    bool IsVisible() const;
    
    // Get the dialog window handle (for advanced use)
    HWND GetHandle() const { return m_hDialog; }

private:
    HWND m_hParent;
    HWND m_hDialog;
    HWND m_hSpinnerCtrl;
    HWND m_hTextCtrl;
    HWND m_hStopBtn = NULL;
    HFONT m_hStopFont = NULL;
    std::wstring m_stopLabel;
    std::wstring m_title = L"Please Wait";
    std::function<void()> m_onStop;
    int m_spinnerFrame;
    bool m_visible;
    
    // Window procedure
    static LRESULT CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    // Create the optional Stop button when a handler is set and the dialog exists.
    void EnsureStopButton();

    // Helper to create and center dialog
    void CreateDialogWindow();
    void LayoutForText(const std::wstring& text);
    
    // Not copyable
    SpinnerDialog(const SpinnerDialog&) = delete;
    SpinnerDialog& operator=(const SpinnerDialog&) = delete;
};
