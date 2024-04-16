#include "Job.h"
#include <wininet.h>
#include <nlohmann/json.hpp>
#include "..\resource.h"
#include "Rad/Log.h"
#include "Rad/Format.h"
#include "Rad/MemoryPlus.h"
#include "Rad/WinError.h"

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

        const auto hInternetSession = MakeUniqueHandle(InternetOpen(TEXT("RadBuildStatus"), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0), InternetCloseHandle);
        const auto hURL = MakeUniqueHandle(InternetOpenUrl(hInternetSession.get(), strUrl, strHeader.c_str(), DWORD(strHeader.size()), INTERNET_FLAG_RELOAD, 0), InternetCloseHandle);

        std::string ret;
        if (hURL)
        {
            DWORD status = 0;
            DWORD statuslen = sizeof(status);
            CHECK_LE_THROW(HttpQueryInfo(hURL.get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statuslen, nullptr));

            if (status != 200)
            {
                TCHAR buf[1024] = TEXT("");
                DWORD len = ARRAYSIZE(buf);
                CHECK_LE_THROW(HttpQueryInfo(hURL.get(), HTTP_QUERY_STATUS_TEXT, buf, &len, nullptr));
                //RadLog(LogLevel::LOG_ERROR, Format(TEXT("%d: %s"), status, buf), SRC_LOC);
                throw std::runtime_error(Format("%d: %S", status, buf));    // TODO Use a wstring exception. Here we are converting bu(WCHAR) to Format(ASCII).
            }
            else
            {
                DWORD dwBytesRead = 0;
                char buf[1024];
                while (InternetReadFile(hURL.get(), buf, (DWORD) sizeof(buf), &dwBytesRead), dwBytesRead > 0)
                    ret.append(buf, dwBytesRead);
            }
        }
        else
        {
            DWORD dwError = GetLastError();
            if (dwError == ERROR_SUCCESS)
            {
                TCHAR buf[1024] = TEXT("");
                DWORD len = ARRAYSIZE(buf);
                InternetGetLastResponseInfo(&dwError, buf, &len);
                //RadLog(LogLevel::LOG_ERROR, Format(TEXT("%d: %s"), dwError, buf), SRC_LOC);
                throw std::runtime_error(Format("%d: %S", dwError, buf));    // TODO Use a wstring exception. Here we are converting bu(WCHAR) to Format(ASCII).
            }
            else if (dwError >= INTERNET_ERROR_BASE && dwError < INTERNET_ERROR_LAST)
                throw WinError({ dwError, TEXT("Wininet.dll"), __FUNCTIONW__ });
            else
                throw WinError({ dwError, nullptr, __FUNCTIONW__ });
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
        CHECK_LE_THROW(FileTimeToSystemTime(&ft, &timestamp_utc));
        return timestamp_utc;
    }
}

std::vector<Job> GetJobs(const Service& s)
{
    std::vector<Job> jobs;
    try
    {
        switch (s.type)
        {
        case ServiceType::JENKINS:
        {
            const std::string str = LoadUrl((s.url + TEXT("?tree=jobs[name,buildable,url,lastBuild[number,duration,timestamp,result,changeSet[items[msg,author[fullName]]]]]")).c_str());
            if (str.empty())
                break;

            const json data = json::parse(str);

            for (const auto& job : data["jobs"])
            {
                Job j = {};
                j.iGroupId = s.iGroupId;
                try
                {
                    j.name = convert(job["name"].get<std::string>());
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }
                try
                {
                    j.url = convert(job["url"].get<std::string>());
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }
                try
                {
                    const json lastbuild = job["lastBuild"];
                    if (!lastbuild.is_null())
                    {
                        try
                        {
                            j.status = convert(get_or(lastbuild["result"], "PROCESS"));
                        }
                        catch (const json::exception& e)
                        {
                            RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                        }
                        try
                        {
                            const SYSTEMTIME timestamp_utc = ConvertFromUnixTime(lastbuild["timestamp"]);
                            CHECK_LE_THROW(SystemTimeToTzSpecificLocalTime(nullptr, &timestamp_utc, &j.timestamp));
                        }
                        catch (const json::exception& e)
                        {
                            RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                        }
                    }
                }
                catch (const json::exception& e)
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
                else if (j.status == TEXT("ABORTED"))
                    j.iIcon = IDI_ICON_ERROR;
                jobs.push_back(j);
            }
            break;
        }

        case ServiceType::GITLAB:
        {
            std::map<std::tstring, std::tstring> headers;
            if (!s.token.empty())
                headers[TEXT("PRIVATE-TOKEN")] = s.token;
            const std::string str = LoadUrl(s.url.c_str(), headers);
            if (str.empty())
                break;

            const json data = json::parse(str);

            for (const auto& job : data)
            {
                Job j = {};
                j.iGroupId = s.iGroupId;
                try
                {
                    j.name = convert(job["ref"].get<std::string>());
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }
                try
                {
                    j.url = convert(job["web_url"].get<std::string>());
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }
                try
                {
                    j.status = convert(job["status"].get<std::string>());
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }
                try
                {
                    const SYSTEMTIME timestamp_utc = ConvertFromISO8601(job["updated_at"]);
                    CHECK_LE_THROW(SystemTimeToTzSpecificLocalTime(nullptr, &timestamp_utc, &j.timestamp));
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }

                j.iIcon = IDI_ICON_INFO;
                if (j.status == TEXT("success"))
                    j.iIcon = IDI_ICON_OK;
                else if (j.status == TEXT("failed"))
                    j.iIcon = IDI_ICON_ERROR;
                else if (j.status == TEXT("running"))
                    j.iIcon = IDI_ICON_RUN;
                else if (j.status == TEXT("pending"))
                    j.iIcon = IDI_ICON_PENDING;
                else if (j.status == TEXT("canceled"))
                    j.iIcon = IDI_ICON_ERROR;
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
            if (str.empty())
                break;

            const json data = json::parse(str);

            for (const auto& job : data)
            {
                Job j = {};
                j.iGroupId = s.iGroupId;
                try
                {
                    j.name = convert(job["name"].get<std::string>());
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }
                try
                {
                    const std::tstring accountName = convert(job["accountName"].get<std::string>());
                    const std::tstring slug = convert(job["slug"].get<std::string>());
                    j.url = TEXT("https://ci.appveyor.com/project/") + accountName + TEXT("/") + slug;
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }
                try
                {
                    j.status = convert(job["builds"][0]["status"].get<std::string>());
                }
                catch (const json::exception& e)
                {
                    RadLogA(LogLevel::LOG_WARN, e.what(), SRC_LOC_A);
                }
                try
                {
                    const SYSTEMTIME timestamp_utc = ConvertFromISO8601(job["builds"][0]["committed"]);
                    CHECK_LE_THROW(SystemTimeToTzSpecificLocalTime(nullptr, &timestamp_utc, &j.timestamp));
                }
                catch (const json::exception& e)
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
                else if (j.status == TEXT("running"))
                    j.iIcon = IDI_ICON_RUN;
                jobs.push_back(j);
            }
            break;
        }

        default:
            _ASSERT(false);
            break;
        }
    }
#if 0
    catch (const json::exception& e)
    {
        Job j = {};
        j.iGroupId = s.iGroupId;
        j.name = e.what();
        j.status = TEXT("Error");
        j.iIcon = IDI_MAIN;
        jobs.push_back(j);
        RadLogA(LogLevel::LOG_DEBUG, e.what(), SRC_LOC_A);
    }
#endif
    catch (const WinError& e)
    {
        Job j = {};
        j.iGroupId = s.iGroupId;
        j.name = e.getMessage();
        j.status = TEXT("Error");
        j.iIcon = IDI_MAIN;
        jobs.push_back(j);
        RadLog(LogLevel::LOG_DEBUG, e.getMessage(), SRC_LOC);
    }
    catch (const std::exception& e)
    {
        Job j = {};
        j.iGroupId = s.iGroupId;
        j.name = Format(TEXT("%S"), e.what());  // TODO Convert to WCHAR
        j.status = TEXT("Error");
        j.iIcon = IDI_MAIN;
        jobs.push_back(j);
        RadLogA(LogLevel::LOG_DEBUG, e.what(), SRC_LOC_A);
    }
    return jobs;
}
