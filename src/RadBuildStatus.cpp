#include "Rad/Window.h"
#include "Rad/Windowxx.h"
//#include <tchar.h>
#include <shellapi.h>
#include <crtdbg.h>
#include <string>
#include <memory>
#include <map>
#include <set>

#include "Rad/Log.h"
#include "Rad/ListViewPlus.h"
#include "Rad/MemoryPlus.h"
#include "Rad/WindowsPlus.h"
#include "Rad/TrayIconHandler.h"
#include "Rad/AboutDlg.h"
#include "Job.h"

#include "..\resource.h"

// TODO
// Use listview callback for getting data
// Need a log window
// Compare job list to last job list, report on changes
// Do refresh on another thread

extern HINSTANCE g_hInstance;

#define TIMER_REFRESH 1

#define ID_LIST (101)

class RootWindow : public Window
{
    friend WindowManager<RootWindow>;
public:
    static ATOM Register() { return WindowManager<RootWindow>::Register(); }
    static RootWindow* Create() { return WindowManager<RootWindow>::Create(); }

protected:
    static void GetCreateWindow(CREATESTRUCT& cs);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    BOOL OnCreate(LPCREATESTRUCT lpCreateStruct);
    void OnDestroy();
    void OnClose();
    void OnSize(UINT state, int cx, int cy);
    void OnSetFocus(HWND hwndOldFocus);
    LRESULT OnNotify(DWORD dwID, LPNMHDR lParam);
    void OnTimer(UINT id);
    void OnContextMenu(HWND hWndContext, UINT xPos, UINT yPos);
    void OnCommand(int id, HWND hWndCtl, UINT codeNotify);
    void OnInitMenuPopup(HMENU hMenu, UINT item, BOOL fSystemMenu);

    void Refresh();

    static LPCTSTR ClassName() { return TEXT("RadBuildStatus"); }

    HWND m_hWndChild = NULL;
    std::map<DWORD, int> m_IconMap;
    std::vector<Service> m_services;
    TrayIconHandler m_trayIcon = TEXT("Rad Build Status");

    struct JobId
    {
        std::tstring name;
        int iGroupId;

        bool operator<(const JobId& other) const
        {
            if (name != other.name)
                return name < other.name;
            return iGroupId < other.iGroupId;
        }
    };

    static JobId GetId(const Job& j)
    {
            return { j.name, j.iGroupId };
    }

    JobId GetId(int i) const
    {
        TCHAR name[200] = TEXT("");
        LVITEM item;
        ZeroMemory(&item, sizeof(LVITEM));
        item.mask = LVIF_TEXT | LVIF_GROUPID;
        item.iItem = i;
        item.pszText = name;
        item.cchTextMax = ARRAYSIZE(name);
        ListView_GetItem(m_hWndChild, &item);
        return { name, item.iGroupId };
    }

    std::set<JobId> m_ignored;
    bool IsIgnored(const JobId& id) const
    {
        return m_ignored.find(id) != m_ignored.end();
    }

    bool IsIgnored(int i) const
    {
        return IsIgnored(GetId(i));
    }

    void SetIgnore(int i, bool ignore)
    {
        const JobId id = GetId(i);
        if (ignore)
            m_ignored.insert(id);
        else
            m_ignored.erase(id);
    }
};

void RootWindow::GetCreateWindow(CREATESTRUCT& cs)
{
    Window::GetCreateWindow(cs);
    cs.lpszName = TEXT("Rad Build Status");
    cs.style = WS_OVERLAPPEDWINDOW;
    cs.dwExStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
}

BOOL RootWindow::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    //const HINSTANCE hInstance = GetWindowInstance(*this);
    const HINSTANCE hInstance = g_hInstance;

    auto hKey = MakeUniqueHandle<HKEY>(NULL, RegCloseKey);
    RegOpenKey(HKEY_CURRENT_USER, TEXT("SOFTWARE\\RadSoft\\RadBuildStatus"), OutPtr(hKey));
    if (hKey)
    {
        RECT rc;
        GetWindowRect(*this, &rc);
        rc.left = RegGetDWORD(hKey.get(), TEXT("x"), rc.left);
        rc.top = RegGetDWORD(hKey.get(), TEXT("y"), rc.top);
        rc.right = rc.left + RegGetDWORD(hKey.get(), TEXT("Width"), rc.right - rc.left);
        rc.bottom = rc.top + RegGetDWORD(hKey.get(), TEXT("Height"), rc.bottom - rc.top);
        SetWindowPos(*this, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOOWNERZORDER);

        auto hKeyServices = MakeUniqueHandle<HKEY>(NULL, RegCloseKey);
        RegOpenKey(hKey.get(), TEXT("Services"), OutPtr(hKeyServices));
        if (hKeyServices)
        {
            TCHAR keyname[1024];
            int i = 0;
            DWORD len;
            while (len = ARRAYSIZE(keyname), RegEnumKeyEx(hKeyServices.get(), i++, keyname, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
            {
                auto hKeyService = MakeUniqueHandle<HKEY>(NULL, RegCloseKey);
                RegOpenKey(hKeyServices.get(), keyname, OutPtr(hKeyService));
                if (hKeyService)
                {
                    Service s = {};
                    s.name = RegGetString(hKeyService.get(), TEXT("name"), keyname);
                    const std::tstring type = RegGetString(hKeyService.get(), TEXT("type"), TEXT(""));
                    if (type == TEXT("jenkins"))
                        s.type = ServiceType::JENKINS;
                    else if (type == TEXT("gitlab"))
                        s.type = ServiceType::GITLAB;
                    else if (type == TEXT("appveyor"))
                        s.type = ServiceType::APPVEYOR;
                    s.url = RegGetString(hKeyService.get(), TEXT("url"), TEXT(""));
                    s.authorization = RegGetString(hKeyService.get(), TEXT("authorization"), TEXT(""));
                    s.token = RegGetString(hKeyService.get(), TEXT("token"), TEXT(""));
                    m_services.push_back(s);
                }
            }
        }
    }

    std::vector<HICON> hIcons;
    for (const DWORD id : { IDI_ICON_INFO, IDI_ICON_ERROR, IDI_ICON_OK, IDI_ICON_RUN })
    {
        const HICON hIcon = LoadIconImage(hInstance, MAKEINTRESOURCE(id), ICON_SMALL);
        if (hIcon)
        {
            m_IconMap[id] = static_cast<int>(hIcons.size());
            hIcons.push_back(hIcon);
        }
    }

    const HIMAGELIST hImageList = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), ILC_MASK | ILC_COLOR32, static_cast<int>(hIcons.size()), 0);
    for (const HICON hIcon : hIcons)
        ImageList_AddIcon(hImageList, hIcon);
    _ASSERT(ImageList_GetImageCount(hImageList) == hIcons.size());

    m_hWndChild = ListView_Create(*this, RECT(), WS_CHILD | WS_VISIBLE | LVS_ALIGNTOP | LVS_REPORT, LVS_EX_FLATSB | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_COLUMNOVERFLOW | LVS_EX_LABELTIP, 101);
    _ASSERT(m_hWndChild);
    ListView_EnableGroupView(m_hWndChild, TRUE);
    //ListView_SetImageList(m_hWndChild, hImageList, LVSIL_NORMAL);
    ListView_SetImageList(m_hWndChild, hImageList, LVSIL_SMALL);

    ListView_AddColumn(m_hWndChild, TEXT("Name"), LVCFMT_LEFT, 200);
    ListView_AddColumn(m_hWndChild, TEXT("Status"), LVCFMT_LEFT, 100);
    ListView_AddColumn(m_hWndChild, TEXT("Timestamp"), LVCFMT_RIGHT, 200);

    {
        LVGROUP lvg = { sizeof(LVGROUP) };
        lvg.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
        lvg.state = LVGS_COLLAPSIBLE;

        for (Service& s : m_services)
        {
            lvg.pszHeader = const_cast<LPWSTR>(s.name.c_str());
            ++lvg.iGroupId;
            ListView_InsertGroup(m_hWndChild, -1, &lvg);
            s.iGroupId = lvg.iGroupId;
        }
    }

    SendMessage(*this, WM_SETICON, ICON_BIG, (LPARAM) LoadIconImage(g_hInstance, MAKEINTRESOURCE(IDI_MAIN), ICON_BIG));
    Refresh();

#if 0
    // TODO Calculate size
    DWORD sz = ListView_ApproximateViewRect(m_hWndChild, -1, -1, 20);
    int rcx = LOWORD(sz);
    int rcy = HIWORD(sz);
#endif

    SetTimer(*this, TIMER_REFRESH, 5 * 60 * 1000, nullptr);

    return TRUE;
}

void RootWindow::OnDestroy()
{
    PostQuitMessage(0);

    auto hKey = MakeUniqueHandle<HKEY>(NULL, RegCloseKey);
    RegCreateKey(HKEY_CURRENT_USER, TEXT("SOFTWARE\\RadSoft\\RadBuildStatus"), OutPtr(hKey));

    if (hKey)
    {
        RECT rc;
        GetWindowRect(*this, &rc);
        RegSetDWORD(hKey.get(), TEXT("x"), rc.left);
        RegSetDWORD(hKey.get(), TEXT("y"), rc.top);
        RegSetDWORD(hKey.get(), TEXT("Width"), rc.right - rc.left);
        RegSetDWORD(hKey.get(), TEXT("Height"), rc.bottom - rc.top);
    }
}

void RootWindow::OnClose()
{
    SetHandled(true);
    ShowWindow(*this, SW_HIDE);
}

void RootWindow::OnSize(const UINT state, const int cx, const int cy)
{
    if (m_hWndChild)
    {
        SetWindowPos(m_hWndChild, NULL, 0, 0,
            cx, cy,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void RootWindow::OnSetFocus(const HWND hwndOldFocus)
{
    if (m_hWndChild)
        SetFocus(m_hWndChild);
}

LRESULT RootWindow::OnNotify(const DWORD dwID, const LPNMHDR pNmHdr)
{
    switch (dwID)
    {
        case ID_LIST:
        {
            switch (pNmHdr->code)
            {
                case LVN_KEYDOWN:
                {
                    LPNMLVKEYDOWN pnkd = (LPNMLVKEYDOWN) pNmHdr;
                    if (pnkd->wVKey == VK_F5)
                        Refresh();
                    break;
                }

                case LVN_DELETEITEM:
                {
                    LPNMLISTVIEW pnlv = (LPNMLISTVIEW) pNmHdr;
                    free((LPTSTR) pnlv->lParam);
                    break;
                }

                case NM_DBLCLK:
                {
                    LPNMITEMACTIVATE pnia = (LPNMITEMACTIVATE) pNmHdr;
                    LPCTSTR strUrl = (LPCTSTR) ListView_GetItemParam(pnia->hdr.hwndFrom, pnia->iItem);
                    if (strUrl != nullptr)
                        ShellExecute(NULL, TEXT("open"), strUrl, NULL, NULL, SW_SHOWNORMAL);
                    break;
                }
            }
            break;
        }
    }
    return 0;
}

void RootWindow::OnTimer(UINT id)
{
    if (id == TIMER_REFRESH)
        Refresh();
}

void RootWindow::OnContextMenu(HWND hWndContext, UINT xPos, UINT yPos)
{
    const DWORD menuId = hWndContext == NULL ? IDR_MENU1 : IDR_MENU2;
    const auto hMenu = MakeUniqueHandle(LoadPopupMenu(g_hInstance, menuId), DestroyMenu);
    TrackPopupMenu(hMenu.get(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, xPos, yPos, 0, *this, nullptr);
}

void RootWindow::OnCommand(int id, HWND hwndCtl, UINT codeNotify)
{
    switch (id)
    {
        case ID_FILE_OPEN:
            ShowWindow(*this, SW_NORMAL);
            break;
        case ID_FILE_ABOUT:
            AboutDlg::DoModal(*this);
            break;
        case ID_FILE_EXIT:
            DestroyWindow(*this);
            break;
        case ID_ITEM_GOTO:
        {
            int i = -1;
            while ((i = ListView_GetNextItem(m_hWndChild, i, LVNI_SELECTED)) != -1)
            {
                LPCTSTR strUrl = (LPCTSTR)ListView_GetItemParam(m_hWndChild, i);
                if (strUrl != nullptr)
                    ShellExecute(NULL, TEXT("open"), strUrl, NULL, NULL, SW_SHOWNORMAL);
            }
            break;
        }
        case ID_ITEM_IGNORE:
        {
            bool ignored = false;
            int i = -1;
            while ((i = ListView_GetNextItem(m_hWndChild, i, LVNI_SELECTED)) != -1)
                ignored |= IsIgnored(i);
            i = -1;
            while ((i = ListView_GetNextItem(m_hWndChild, i, LVNI_SELECTED)) != -1)
                SetIgnore(i, !ignored);
            Refresh();
            break;
        }
        case ID_ITEM_REFRESH:
            Refresh();
            break;
    }
}

void RootWindow::OnInitMenuPopup(HMENU hMenu, UINT item, BOOL fSystemMenu)
{
    if (!fSystemMenu && item == 0)
    {
        switch (GetMenuItemID(hMenu, 0))
        {
        case ID_ITEM_GOTO:
        {
            bool ignored = false;
            int i = -1;
            while ((i = ListView_GetNextItem(m_hWndChild, i, LVNI_SELECTED)) != -1)
                ignored |= IsIgnored(i);
            CheckMenuItem(hMenu, ID_ITEM_IGNORE, MF_BYCOMMAND | (ignored ? MF_CHECKED : MF_UNCHECKED));
            break;
        }
        case ID_FILE_OPEN:
        {
            SetMenuDefaultItem(hMenu, ID_FILE_OPEN, MF_BYCOMMAND);
            break;
        }
        }
    }
}

LRESULT RootWindow::HandleMessage(const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    m_trayIcon.HandleMessage(*this, uMsg, wParam, lParam);

    LRESULT ret = 0;

    switch (uMsg)
    {
        HANDLE_MSG(WM_CREATE, OnCreate);
        HANDLE_MSG(WM_DESTROY, OnDestroy);
        HANDLE_MSG(WM_CLOSE, OnClose);
        HANDLE_MSG(WM_SIZE, OnSize);
        HANDLE_MSG(WM_SETFOCUS, OnSetFocus);
        HANDLE_MSG(WM_NOTIFY, OnNotify);
        HANDLE_MSG(WM_TIMER, OnTimer);
        HANDLE_MSG(WM_CONTEXTMENU, OnContextMenu);
        HANDLE_MSG(WM_COMMAND, OnCommand);
        HANDLE_MSG(WM_INITMENUPOPUP, OnInitMenuPopup);
    }

    if (!IsHandled())
        ret = Window::HandleMessage(uMsg, wParam, lParam);

    return ret;
}

void RootWindow::Refresh()
{
    static bool bInRefresh = false;
    if (bInRefresh)
        return;

    bInRefresh = true;

    const HCURSOR hOldCursor = SetCursor(LoadCursor(0, IDC_WAIT));

    std::vector<Job> jobs;
    for (const Service& s : m_services)
    {
        const std::vector<Job> service_jobs = GetJobs(s);
        jobs.insert(jobs.end(), service_jobs.begin(), service_jobs.end());
    }

    // TODO Find and replace instead of clearing
    ListView_DeleteAllItems(m_hWndChild);

    DWORD iMaxIcon = IDI_ICON_OK;

    for (const Job& j : jobs)
    {
        const bool bIgnored = IsIgnored(GetId(j));

        //ListView_InsertItemText(m_hWndChild, iItem, name.c_str());
        LVITEM item;
        ZeroMemory(&item, sizeof(LVITEM));
        item.iItem = INT_MAX;
        item.mask = LVIF_TEXT | LVIF_GROUPID | LVIF_IMAGE | LVIF_PARAM;
        item.pszText = const_cast<LPTSTR>(j.name.c_str());
        item.iGroupId = j.iGroupId;
        item.iImage = m_IconMap[j.iIcon];
        item.lParam = (LPARAM) (j.url.empty() ? nullptr : _wcsdup(j.url.c_str()));
        if (bIgnored)
        {
            item.mask |= LVIF_STATE;
            item.stateMask |= LVIS_CUT;
            item.state |= LVIS_CUT;
        }
        const int iItem = ListView_InsertItem(m_hWndChild, &item);
        ListView_SetItemText(m_hWndChild, iItem, 1, const_cast<LPTSTR>(j.status.c_str()));

        if (j.timestamp.wYear != 0)
        {
            TCHAR bufTimestamp[200];
            int len = GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &j.timestamp, NULL, bufTimestamp, ARRAYSIZE(bufTimestamp));
            bufTimestamp[len - 1] = TEXT(' ');
            GetTimeFormat(LOCALE_USER_DEFAULT, 0, &j.timestamp, NULL, bufTimestamp + len, ARRAYSIZE(bufTimestamp) - len);
            ListView_SetItemText(m_hWndChild, iItem, 2, bufTimestamp);
        }

        // TODO
        // GetDurationFormatEx

        if (!bIgnored && j.iIcon > iMaxIcon)
            iMaxIcon = j.iIcon;
    }

    SendMessage(*this, WM_SETICON, ICON_BIG, (LPARAM) LoadIconImage(g_hInstance, MAKEINTRESOURCE(iMaxIcon), ICON_BIG));
    m_trayIcon.Update();

    SetCursor(hOldCursor);

    bInRefresh = false;
}

bool Run(_In_ const LPCTSTR lpCmdLine, _In_ const int nShowCmd)
{
    RadLogInitWnd(NULL, "Rad Build Status", TEXT("Rad Build Status"));

    CHECK_LE_RET(RootWindow::Register(), false);

    RootWindow* prw = RootWindow::Create();
    CHECK_LE_RET(prw != nullptr, false);

    RadLogInitWnd(*prw, "Rad Build Status", TEXT("Rad Build Status"));
    //ShowWindow(*prw, nShowCmd);
    return true;
}
