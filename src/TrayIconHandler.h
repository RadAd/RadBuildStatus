#pragma once
#include <windows.h>

class TrayIconHandler
{
public:
    TrayIconHandler(LPCTSTR strTitle)
        : m_hWnd(NULL), m_strTitle(strTitle)
    {
    }

    void HandleMessage(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam);

    void Update();

private:
    HWND m_hWnd;
    BOOL OnCreate(LPCREATESTRUCT lpCreateStruct);
    void OnDestroy();

private:
    void AddTrayIcon(DWORD dwMessage);

    LPCTSTR m_strTitle;
};
