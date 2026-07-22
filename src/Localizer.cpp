#include "pch.h"
#include "Localizer.h"
#include <nlohmann/json.hpp>

namespace IIF::L10n
{
    static std::unordered_map<std::string, std::string> g_dictionary;
    static std::string g_currentLanguage = "zh_CN";

    void LoadLanguage(const std::string& langCode) {
        g_currentLanguage = langCode;
        g_dictionary.clear();

        std::string path = "Data\\F4SE\\Plugins\\ItemIntegrationFramework\\lang\\" + langCode + ".json";

        if (std::filesystem::exists(path)) {
            try {
                std::ifstream file(path);
                nlohmann::json j = nlohmann::json::parse(file, nullptr, true, true);

                for (auto& [key, value] : j.items()) {
                    if (value.is_string()) {
                        g_dictionary[key] = value.get<std::string>();
                    }
                }
                REX::INFO("[IIF] 已加载/重载本地化语言文件: {}", langCode);
            }
            catch (const std::exception& e) {
                REX::ERROR("[IIF] 语言文件解析失败: {}", e.what());
            }
        }
        else {
            REX::WARN("[IIF] 找不到语言文件: {}，将显示字典 Key", path);
        }
    }

    const char* Get(const char* key) {
        auto it = g_dictionary.find(key);
        if (it != g_dictionary.end()) {
            return it->second.c_str();
        }
        return key;
    }

    std::string GetCurrentLanguage() {
        return g_currentLanguage;
    }

    std::vector<std::string> GetAvailableLanguages() {
        std::vector<std::string> languages;
        std::string dir = "Data\\F4SE\\Plugins\\ItemIntegrationFramework\\lang\\";

        if (std::filesystem::exists(dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.path().extension() == ".json") {
                    languages.push_back(entry.path().stem().string());
                }
            }
        }
        if (languages.empty()) {
            languages.push_back("zh_CN");
        }
        return languages;
    }
}