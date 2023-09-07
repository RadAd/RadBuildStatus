#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include "Utils.h"

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

std::vector<Job> GetJobs(const Service& s);
