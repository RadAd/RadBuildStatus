#include "Job.h"
#include <wininet.h>
#include <nlohmann/json.hpp>
#include "..\resource.h"
#include "Rad/Log.h"

using json = nlohmann::json;

namespace
{

    std::string get_or(const json& a, const std::string& def)
    {
        return a.is_null() ? def : a.get<std::string>();
    }

    std::string LoadUrl(LPCTSTR strUrl, const std::map<std::tstring, std::tstring>& headers = {})
    {
        std::tstring strHeader;
        for (const auto& h : headers)
            strHeader += h.first + TEXT(": ") + h.second + TEXT("\n");

        const auto hInternetSession = MakeHandleDeleter(InternetOpen(TEXT("RadBuildStatus"), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0), InternetCloseHandle);
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
            RadLog(LogLevel::LOG_WARN, format(TEXT("%d:%s"), error, buf), SRC_LOC);
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
}

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
                RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
            }
            try
            {
                j.url = convert(job["url"].get<std::string>());
            }
            catch (const json::type_error& e)
            {
                RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
            }
            try
            {
                j.status = convert(get_or(job["lastBuild"]["result"], "PROCESS"));
            }
            catch (const json::type_error& e)
            {
                RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
            }
            try
            {
                const SYSTEMTIME timestamp_utc = ConvertFromUnixTime(job["lastBuild"]["timestamp"]);
                SystemTimeToTzSpecificLocalTime(nullptr, &timestamp_utc, &j.timestamp);
            }
            catch (const json::type_error& e)
            {
                RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
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
                RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
            }
            try
            {
                const std::tstring accountName = convert(job["accountName"].get<std::string>());
                const std::tstring slug = convert(job["slug"].get<std::string>());
                j.url = TEXT("https://ci.appveyor.com/project/") + accountName + TEXT("/") + slug;
            }
            catch (const json::type_error& e)
            {
                RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
            }
            try
            {
                j.status = convert(job["builds"][0]["status"].get<std::string>());
            }
            catch (const json::type_error& e)
            {
                RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
            }
            try
            {
                const SYSTEMTIME timestamp_utc = ConvertFromISO8601(job["builds"][0]["committed"]);
                SystemTimeToTzSpecificLocalTime(nullptr, &timestamp_utc, &j.timestamp);
            }
            catch (const json::type_error& e)
            {
                RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
            }
            j.iIcon = IDI_ICON_INFO;
            if (j.status == TEXT("success"))
                j.iIcon = IDI_ICON_OK;
            else if (j.status == TEXT("failure"))
                j.iIcon = IDI_ICON_ERROR;
            else if (j.status == TEXT("starting"))
                j.iIcon = IDI_ICON_RUN;
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
