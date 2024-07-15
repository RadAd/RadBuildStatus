#pragma once

#ifdef _UNICODE
#define tstring wstring
#else
#define tstring string
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
