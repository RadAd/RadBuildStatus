#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include "Utils.h"

enum class ServiceType
{
    NONE,
    JENKINS,
    GITLAB,
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
    std::tstring token;

    int iGroupId;
};

std::vector<Job> GetJobs(const Service& s);
