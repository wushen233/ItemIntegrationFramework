#pragma once
#include "pch.h"

namespace IIF::L10n
{
    void LoadLanguage(const std::string& langCode = "zh_CN");
    const char* Get(const char* key);
    std::string GetCurrentLanguage();
    std::vector<std::string> GetAvailableLanguages();
}

#define _L(key) IIF::L10n::Get(key)