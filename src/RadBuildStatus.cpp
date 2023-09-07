#include "Window.h"
#include "Windowxx.h"
//#include <tchar.h>
#include <shellapi.h>
#include <wininet.h>
#include <crtdbg.h>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>

#include "ListViewPlus.h"
#include "TrayIconHandler.h"
#include "Utils.h"

#include "..\resource.h"

// TODO
// Use listview callback for getting data
// Need a log window
// Compare job list to last job list, report on changes
// Do refresh on another thread

extern HINSTANCE g_hInstance;

#define TIMER_REFRESH 1

#define ID_LIST (101)

using json = nlohmann::json;

/* LRESULT Cls::OnNotify(DWORD dwID, LPNMHDR pNmHdr) */
#define HANDLEX_WM_NOTIFY(wParam, lParam, fn) \
    MAKELRESULT((LRESULT)(fn)((DWORD) wParam, (LPNMHDR) lParam), 0L)

// sz : ICON_BIG or ICON_SMALL
HICON LoadIcon(HINSTANCE hInstance, LPCTSTR resource, DWORD sz)
{
    const int CX_ICON = sz == ICON_SMALL ? GetSystemMetrics(SM_CXSMICON) : GetSystemMetrics(SM_CXICON);
    const int CY_ICON = sz == ICON_SMALL ? GetSystemMetrics(SM_CYSMICON) : GetSystemMetrics(SM_CYICON);
    return (HICON) LoadImage(hInstance, resource, IMAGE_ICON, CX_ICON, CY_ICON, LR_SHARED);
}

HMENU LoadPopupMenu(HINSTANCE hInstance, DWORD id)
{
    const HMENU hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(id));
    const HMENU hPopupMenu = GetSubMenu(hMenu, 0);
    RemoveMenu(hMenu, 0, MF_BYPOSITION);
    DestroyMenu(hMenu);
    return hPopupMenu;
}

std::string get_or(const json& a, const std::string& def)
{
    return a.is_null() ? def : a.get<std::string>();
}

std::string LoadUrl(LPCTSTR strUrl, const std::map<std::tstring, std::tstring>& headers = {})
{
    std::tstring strHeader;
    for (const auto& h : headers)
        strHeader += h.first + TEXT(": ") + h.second + TEXT("\n");

    const auto hInternetSession = MakeHandleDeleter(InternetOpen(TEXT("RadBuildStatus"), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, ID_LIST), InternetCloseHandle);
    const auto hURL = MakeHandleDeleter(InternetOpenUrl(hInternetSession.get(), strUrl, strHeader.c_str(), DWORD(strHeader.size()), INTERNET_FLAG_RELOAD, 0), InternetCloseHandle);

    std::string ret;
    if (hURL)
    {
        DWORD dwBytesRead = 1;
        for (; dwBytesRead > 0;)
        {
            char buf[1024];
            InternetReadFile(hURL.get(), buf, (DWORD) sizeof(buf), &dwBytesRead);
            ret.append(buf, dwBytesRead);
        }
    }
    else
    {
        DWORD dwError = GetLastError();
#if 0
        DWORD error = 0;
        TCHAR buf[1024];
        DWORD len = ARRAYSIZE(buf);
        if (!InternetGetLastResponseInfo(&error, buf, &len))
            dwError = GetLastError();
        _RPTFWN(_CRT_WARN, TEXT("%d:%s"), error, buf);
#endif
    }

    return ret;
}

SYSTEMTIME ConvertFromISO8601(const std::string& strTimestamp)
{
    SYSTEMTIME timestamp_utc = {};
    timestamp_utc.wYear = (WORD) strtol(strTimestamp.substr(0, 4).c_str(), nullptr, 10);
    timestamp_utc.wMonth = (WORD) strtol(strTimestamp.substr(5, 2).c_str(), nullptr, 10);
    timestamp_utc.wDay = (WORD) strtol(strTimestamp.substr(8, 2).c_str(), nullptr, 10);
    timestamp_utc.wHour = (WORD) strtol(strTimestamp.substr(11, 2).c_str(), nullptr, 10);
    timestamp_utc.wMinute = (WORD) strtol(strTimestamp.substr(14, 2).c_str(), nullptr, 10);
    timestamp_utc.wSecond = (WORD) strtol(strTimestamp.substr(17, 2).c_str(), nullptr, 10);
    return timestamp_utc;
}

SYSTEMTIME ConvertFromUnixTime(int64_t time)
{
    ULARGE_INTEGER    li;
    li.QuadPart = time;
    li.QuadPart *= 10000;
    li.QuadPart += 116444736000000000;
    FILETIME ft = { li.LowPart, li.HighPart };
    SYSTEMTIME timestamp_utc = {};
    FileTimeToSystemTime(&ft, &timestamp_utc);
    return timestamp_utc;
}

enum class ServiceType
{
    NONE,
    JENKINS,
    APPVEYOR,
};

struct Job
{
    std::tstring name;
    DWORD iIcon;
    int iGroupId;
    std::tstring url;
    std::tstring status;
    SYSTEMTIME timestamp;
};

struct Service
{
    std::tstring name;
    ServiceType type;
    std::tstring url;
    std::tstring authorization;

    int iGroupId;
};

std::vector<Job> GetJobs(const Service& s)
{
    std::vector<Job> jobs;
    switch (s.type)
    {
        case ServiceType::JENKINS:
        {
            const std::string str = LoadUrl((s.url + TEXT("?tree=jobs[name,buildable,url,lastBuild[number,duration,timestamp,result,changeSet[items[msg,author[fullName]]]]]")).c_str());
            const json data = json::parse(str);

            for (const auto& job : data["jobs"])
            {
                Job j = {};
                j.iGroupId = s.iGroupId;
                try
                {
                    j.name = convert(job["name"].get<std::string>());
                }
                catch (const json::type_error& e)
                {
                    _RPTF0(_CRT_WARN, e.what());
                }
                try
                {
                    j.url = convert(job["url"].get<std::string>());
                }
                catch (const json::type_error& e)
                {
                    _RPTF0(_CRT_WARN, e.what());
                }
                try
                {
                    j.status = convert(get_or(job["lastBuild"]["result"], "PROCESS"));
                }
                catch (const json::type_error& e)
                {
                    _RPTF0(_CRT_WARN, e.what());
                }
                try
                {
                    const SYSTEMTIME timestamp_utc = ConvertFromUnixTime(job["lastBuild"]["timestamp"]);
                    SystemTimeToTzSpecificLocalTime(nullptr, &timestamp_utc, &j.timestamp);
                }
                catch (const json::type_error& e)
                {
                    _RPTF0(_CRT_WARN, e.what());
                }
                j.iIcon = IDI_ICON_INFO;
                if (j.status == TEXT("SUCCESS"))
                    j.iIcon = IDI_ICON_OK;
                else if (j.status == TEXT("FAILURE"))
                    j.iIcon = IDI_ICON_ERROR;
                else if (j.status == TEXT("PROCESS"))
                    j.iIcon = IDI_ICON_RUN;
                jobs.push_back(j);
            }
            break;
        }

        case ServiceType::APPVEYOR:
        {
            std::map<std::tstring, std::tstring> headers;
            if (!s.authorization.empty())
                headers[TEXT("Authorization")] = TEXT("Bearer ") + s.authorization;
            headers[TEXT("Content-Type")] = TEXT("application/json");
            const std::string str = LoadUrl(s.url.c_str(), headers);
            const json data = json::parse(str);

            for (const auto& job : data)
            {
                Job j = {};
                j.iGroupId = s.iGroupId;
                try
                {
                    j.name = convert(job["name"].get<std::string>());
                }
                catch (const json::type_error& e)
                {
                    _RPTF0(_CRT_WARN, e.what());
                }
                try
                {
                    const std::tstring accountName = convert(job["accountName"].get<std::string>());
                    const std::tstring slug = convert(job["slug"].get<std::string>());
                    j.url = TEXT("https://ci.appveyor.com/project/") + accountName + TEXT("/") + slug;
                }
                catch (const json::type_error& e)
                {
                    _RPTF0(_CRT_WARN, e.what());
                }
                try
                {
                    j.status = convert(job["builds"][0]["status"].get<std::string>());
                }
                catch (const json::type_error& e)
                {
                    _RPTF0(_CRT_WARN, e.what());
                }
                try
                {
                    const SYSTEMTIME timestamp_utc = ConvertFromISO8601(job["builds"][0]["committed"]);
                    SystemTimeToTzSpecificLocalTime(nullptr, &timestamp_utc, &j.timestamp);
                }
                catch (const json::type_error& e)
                {
                    _RPTF0(_CRT_WARN, e.what());
                }
                j.iIcon = IDI_ICON_INFO;
                if (j.status == TEXT("success"))
                    j.iIcon = IDI_ICON_OK;
                else if (j.status == TEXT("failure"))
                    j.iIcon = IDI_ICON_ERROR;
                jobs.push_back(j);
            }
            break;
        }

        default:
            _ASSERT(false);
            break;
    }
    return jobs;
}

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

    void Refresh();

    static LPCTSTR ClassName() { return TEXT("RadBuildStatus"); }

    HWND m_hWndChild = NULL;
    std::map<DWORD, int> m_IconMap;
    std::vector<Service> m_services;
    TrayIconHandler m_trayIcon = TEXT("Rad Build Status");
};

void RootWindow::GetCreateWindow(CREATESTRUCT& cs)
{
    Window::GetCreateWindow(cs);
    cs.lpszName = TEXT("Rad Build Status");
    cs.style = WS_OVERLAPPEDWINDOW;
    cs.dwExStyle = WS_EX_TOOLWINDOW;
}

BOOL RootWindow::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    //const HINSTANCE hInstance = GetWindowInstance(*this);
    const HINSTANCE hInstance = g_hInstance;

    auto hKey = MakeHandleDeleter<HKEY>(NULL, RegCloseKey);
    RegOpenKey(HKEY_CURRENT_USER, TEXT("SOFTWARE\\RadSoft\\RadBuildStatus"), out_ptr(hKey));
    if (hKey)
    {
        RECT rc;
        GetWindowRect(*this, &rc);
        rc.left = RegGetDWORD(hKey.get(), TEXT("x"), rc.left);
        rc.top = RegGetDWORD(hKey.get(), TEXT("y"), rc.top);
        rc.right = rc.left + RegGetDWORD(hKey.get(), TEXT("Width"), rc.right - rc.left);
        rc.bottom = rc.top + RegGetDWORD(hKey.get(), TEXT("Height"), rc.bottom - rc.top);
        SetWindowPos(*this, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOOWNERZORDER);

        auto hKeyServices = MakeHandleDeleter<HKEY>(NULL, RegCloseKey);
        RegOpenKey(hKey.get(), TEXT("Services"), out_ptr(hKeyServices));
        if (hKeyServices)
        {
            TCHAR keyname[1024];
            int i = 0;
            DWORD len;
            while (len = ARRAYSIZE(keyname), RegEnumKeyEx(hKeyServices.get(), i++, keyname, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
            {
                auto hKeyService = MakeHandleDeleter<HKEY>(NULL, RegCloseKey);
                RegOpenKey(hKeyServices.get(), keyname, out_ptr(hKeyService));
                if (hKeyService)
                {
                    Service s = {};
                    s.name = RegGetString(hKeyService.get(), TEXT("name"), keyname);
                    const std::tstring type = RegGetString(hKeyService.get(), TEXT("type"), TEXT(""));
                    if (type == TEXT("jenkins"))
                        s.type = ServiceType::JENKINS;
                    else if (type == TEXT("appveyor"))
                        s.type = ServiceType::APPVEYOR;
                    s.url = RegGetString(hKeyService.get(), TEXT("url"), TEXT(""));
                    s.authorization = RegGetString(hKeyService.get(), TEXT("authorization"), TEXT(""));
                    m_services.push_back(s);
                }
            }
        }
    }

    std::vector<HICON> hIcons;
    for (const DWORD id : { IDI_ICON_INFO, IDI_ICON_ERROR, IDI_ICON_OK, IDI_ICON_RUN })
    {
        const HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(id), ICON_SMALL);
        if (hIcon)
        {
            m_IconMap[id] = static_cast<int>(hIcons.size());
            hIcons.push_back(hIcon);
        }
    }

    const HIMAGELIST hImageList = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), ILC_MASK | ILC_COLOR32, static_cast<int>(hIcons.size()), 0);
    for (const HICON hIcon : hIcons)
        ImageList_AddIcon(hImageList, hIcon);

    m_hWndChild = ListView_Create(*this, RECT(), WS_CHILD | WS_VISIBLE | LVS_ALIGNTOP | LVS_REPORT, LVS_EX_FLATSB | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_COLUMNOVERFLOW, 101);
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

    SendMessage(*this, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON1), ICON_BIG));
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

    auto hKey = MakeHandleDeleter<HKEY>(NULL, RegCloseKey);
    RegCreateKey(HKEY_CURRENT_USER, TEXT("SOFTWARE\\RadSoft\\RadBuildStatus"), out_ptr(hKey));

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
    const auto hMenu = MakeHandleDeleter(LoadPopupMenu(g_hInstance, IDR_MENU1), DestroyMenu);
    TrackPopupMenu(hMenu.get(), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, xPos, yPos, 0, *this, nullptr);
}

void RootWindow::OnCommand(int id, HWND hwndCtl, UINT codeNotify)
{
    switch (id)
    {
        case ID_FILE_OPEN:
            ShowWindow(*this, SW_NORMAL);
            break;
        case ID_FILE_EXIT:
            DestroyWindow(*this);
            break;
    }
}

LRESULT RootWindow::HandleMessage(const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
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
    }

    m_trayIcon.HandleMessage(*this, uMsg, wParam, lParam);
    if (!IsHandled())
        ret = Window::HandleMessage(uMsg, wParam, lParam);

    return ret;
}

void RootWindow::Refresh()
{
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
        //ListView_InsertItemText(m_hWndChild, iItem, name.c_str());
        LVITEM item;
        ZeroMemory(&item, sizeof(LVITEM));
        item.iItem = INT_MAX;
        item.mask = LVIF_TEXT | LVIF_GROUPID | LVIF_IMAGE | LVIF_PARAM;
        item.pszText = const_cast<LPTSTR>(j.name.c_str());
        item.iGroupId = j.iGroupId;
        item.iImage = m_IconMap[j.iIcon];
        item.lParam = (LPARAM) (j.url.empty() ? nullptr : _wcsdup(j.url.c_str()));
        const int iItem = ListView_InsertItem(m_hWndChild, &item);
        ListView_SetItemText(m_hWndChild, iItem, 1, const_cast<LPTSTR>(j.status.c_str()));

        TCHAR bufTimestamp[200];
        int len = GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &j.timestamp, NULL, bufTimestamp, ARRAYSIZE(bufTimestamp));
        bufTimestamp[len - 1] = TEXT(' ');
        GetTimeFormat(LOCALE_USER_DEFAULT, 0, &j.timestamp, NULL, bufTimestamp + len, ARRAYSIZE(bufTimestamp) - len);
        ListView_SetItemText(m_hWndChild, iItem, 2, bufTimestamp);

        // TODO
        // GetDurationFormatEx

        if (j.iIcon > iMaxIcon)
            iMaxIcon = j.iIcon;
    }

    SendMessage(*this, WM_SETICON, ICON_BIG, (LPARAM) LoadIcon(g_hInstance, MAKEINTRESOURCE(iMaxIcon), ICON_BIG));
    m_trayIcon.Update();
}

bool Run(_In_ const LPCTSTR lpCmdLine, _In_ const int nShowCmd)
{
    if (RootWindow::Register() == 0)
    {
        MessageBox(NULL, TEXT("Error registering window class"), TEXT("Rad Build Status"), MB_ICONERROR | MB_OK);
        return false;
    }
    RootWindow* prw = RootWindow::Create();
    if (prw == nullptr)
    {
        MessageBox(NULL, TEXT("Error creating root window"), TEXT("Rad Build Status"), MB_ICONERROR | MB_OK);
        return false;
    }

    //ShowWindow(*prw, nShowCmd);
    return true;
}
