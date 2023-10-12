#pragma once

#ifdef UNICODE
#define tstring wstring
inline std::wstring convert(const std::string& m)
{
    wchar_t msg[1024];
    const int sz = MultiByteToWideChar(CP_UTF8, 0, m.c_str(), int(m.length()), msg, ARRAYSIZE(msg));
    msg[sz] = L'\0';
    return msg;
}
#else
#define tstring string
inline const std::string convert(const std::string& m)
{
    return m;
}
#endif

inline DWORD RegGetDWORD(HKEY hKey, LPCTSTR lpValue, DWORD Value)
{
    DWORD DataSize = sizeof(DWORD);
    RegGetValue(hKey, nullptr, lpValue, REG_DWORD, nullptr, &Value, &DataSize);
    return Value;
}

inline void RegSetDWORD(HKEY hKey, LPCTSTR lpValue, DWORD Value)
{
    const DWORD DataSize = sizeof(DWORD);
    RegSetValueEx(hKey, lpValue, 0, REG_DWORD, (BYTE*) &Value, DataSize);
}

inline std::tstring RegGetString(HKEY hKey, LPCTSTR sValue, const std::tstring& strDef)
{
    TCHAR buf[1024];
    DWORD len = (ARRAYSIZE(buf) - 1) * sizeof(CHAR);
    if (RegGetValue(hKey, nullptr, sValue, RRF_RT_REG_SZ, nullptr, buf, &len) == ERROR_SUCCESS)
    {
        buf[len / sizeof(CHAR)] = TEXT('\0');
        return buf;
    }
    else
        return strDef;
}
