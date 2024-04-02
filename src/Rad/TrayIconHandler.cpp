#define STRICT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "TrayIconHandler.h"

#include "Windowxx.h"

#include <shellapi.h>

#define WM_TRAY (WM_USER+101)

void TrayIconHandler::Update()
{
    AddTrayIcon(NIM_MODIFY);
}

void TrayIconHandler::HandleMessage(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    m_hWnd = hWnd;
    m_bHandled = false;

    static const UINT s_uTaskbarRestart = RegisterWindowMessage(TEXT("TaskbarCreated"));
    if (uMsg == s_uTaskbarRestart)
        AddTrayIcon(NIM_ADD);

    LRESULT ret = 0;
    switch (uMsg)
    {
        HANDLE_MSG(WM_CREATE, OnCreate);
        HANDLE_MSG(WM_DESTROY, OnDestroy);

    case WM_TRAY:
        switch (lParam)
        {
        case WM_LBUTTONDBLCLK:
        {
            SetForegroundWindow(m_hWnd);
            ShowWindow(m_hWnd, SW_NORMAL);
            break;
        }
        case WM_RBUTTONDOWN:
        {
            SetForegroundWindow(m_hWnd);
            POINT pt;
            GetCursorPos(&pt);
            SendMessage(m_hWnd, WM_CONTEXTMENU, 0, MAKELONG(pt.x, pt.y));
            break;
        }
        }
        break;
    }
}

BOOL TrayIconHandler::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    AddTrayIcon(NIM_ADD);
    return true;
}

void TrayIconHandler::OnDestroy()
{
    AddTrayIcon(NIM_DELETE);
}

void TrayIconHandler::AddTrayIcon(DWORD dwMessage)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    nid.hWnd = m_hWnd;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAY;
    wcscpy_s(nid.szTip, m_strTitle);
    nid.hIcon = (HICON) SendMessage(m_hWnd, WM_GETICON, ICON_SMALL, 0);
    if (nid.hIcon == NULL)
        nid.hIcon = (HICON) SendMessage(m_hWnd, WM_GETICON, ICON_BIG, 0);
    if (nid.hIcon == NULL)
        nid.hIcon = (HICON) GetClassLongPtr(m_hWnd, GCLP_HICONSM);
    if (nid.hIcon == NULL)
        nid.hIcon = (HICON) GetClassLongPtr(m_hWnd, GCLP_HICON);
    if (nid.hIcon == NULL)
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    Shell_NotifyIcon(dwMessage, &nid);
}
