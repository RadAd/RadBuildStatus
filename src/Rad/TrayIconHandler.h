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
    void SetTitle(LPCTSTR strTitle) { m_strTitle = strTitle; }

    void Update();

private:
    HWND m_hWnd;
    bool m_bHandled;
    BOOL OnCreate(LPCREATESTRUCT lpCreateStruct);
    void OnDestroy();

    void SetHandled(bool bHandled) { m_bHandled = bHandled; }

private:
    void AddTrayIcon(DWORD dwMessage);

    LPCTSTR m_strTitle;
};
