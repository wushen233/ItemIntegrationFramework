#include "GUIEditorManager.h"
#include "GUIEditorKeyHandler.h"
#include "JsonReader.h"
#include "PrismaUI_F4_API.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <algorithm>
#include <vector>
#include <nlohmann/json.hpp>

// NOTE: keep C++17 support and avoid API churn: this file now provides a lightweight JSON bridge for PrismaUI editors.

namespace IIF::GUIEditor {
    namespace {
        constexpr uint32_t kDefaultShortcutKey = 120;     // F9  (VK_F9 = 0x78)
        constexpr uint32_t kDefaultShortcutModifiers = 1; // shift
        constexpr const char* kMenuPath = "gui_editor.html";
        constexpr const char* kSettingsFile = "Data\\F4SE\\Plugins\\ItemIntegrationFramework\\gui_editor_settings.json";
        constexpr const char* kDefaultRuleFile = "Data\\F4SE\\Plugins\\ItemIntegrationFramework\\gui_editor_rules.json";

        enum ModifierFlag : uint32_t {
            kModifierShift = 1 << 0,
            kModifierCtrl  = 1 << 1,
            kModifierAlt   = 1 << 2,
        };

        PRISMA_UI_API::IVPrismaUI2* g_prismaUI = nullptr;
        PRISMA_UI_API::IVPrismaUI4* g_prismaUI4 = nullptr;
        PrismaView g_view = 0;
        bool g_viewReady = false;
        bool g_visible = false;
        bool g_openPending = false;
        bool g_sinkRegistered = false;
        bool g_listenersRegistered = false;
        bool g_captureMode = false;
        bool g_modifierShift = false;
        bool g_modifierCtrl = false;
        bool g_modifierAlt = false;
        std::string g_selectedRuleId;
        std::string g_selectedRuleSource = "json";

        uint32_t g_shortcutKey = kDefaultShortcutKey;
        uint32_t g_shortcutModifiers = kDefaultShortcutModifiers;

        std::optional<KeyHandlerEvent> g_toggleHandle;

        // Win32 subclass for global toggle hotkey (MenuControls::handlers only fires in menu context)
        HWND    g_gameHwnd    = nullptr;
        WNDPROC g_origWndProc = nullptr;

        void RequestToggleMenuState();  // defined later in this namespace

        LRESULT CALLBACK HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            if (msg == WM_KEYDOWN && (lp & 0x40000000) == 0) {  // no-repeat
                uint32_t vk = static_cast<uint32_t>(wp);
                bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                if (vk == g_shortcutKey && shiftHeld == !!(g_shortcutModifiers & kModifierShift) &&
                    !g_captureMode) {
                    F4SE::GetTaskInterface()->AddTask([]() { RequestToggleMenuState(); });
                }
            }
            return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
        }

        void InstallHotkeySubclass()
        {
            if (g_gameHwnd) return;

            struct HwndFinder {
                static BOOL CALLBACK Proc(HWND hwnd, LPARAM lp) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(hwnd, &pid);
                    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
                        *reinterpret_cast<HWND*>(lp) = hwnd;
                        return FALSE;
                    }
                    return TRUE;
                }
            };
            EnumWindows(HwndFinder::Proc, reinterpret_cast<LPARAM>(&g_gameHwnd));

            if (!g_gameHwnd) {
                REX::WARN("[IIF GUIEditor] Hotkey subclass: game window not found.");
                return;
            }
            g_origWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(HotkeyWndProc)));
            REX::INFO("[IIF GUIEditor] Hotkey subclass installed on HWND 0x{:X}.", reinterpret_cast<uintptr_t>(g_gameHwnd));
        }

        KeyHandlerEvent g_shiftDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_shiftLeftDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_shiftRightDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_shiftUpHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_shiftLeftUpHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_shiftRightUpHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_ctrlDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_ctrlLeftDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_ctrlRightDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_ctrlUpHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_ctrlLeftUpHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_ctrlRightUpHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_altDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_altLeftDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_altRightDownHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_altUpHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_altLeftUpHandle = INVALID_REGISTRATION_HANDLE;
        KeyHandlerEvent g_altRightUpHandle = INVALID_REGISTRATION_HANDLE;
        std::vector<KeyHandlerEvent> g_captureKeyHandles;
        std::uint32_t g_lastToggleMs = 0;
        bool g_menuApplying = false;

        enum class FilterScene : std::uint32_t {
            Guns = 0,
            Melee = 1,
            Armor = 2,
            Aid = 3,
            Custom = 4
        };

        bool IsInScene(const std::string& id, FilterScene scene)
        {
            if (scene == FilterScene::Guns) {
                return id == "$dmg" || id == "$ammo" || id == "$fireRate" || id == "$rng" || id == "$acc" || id == "$wt" || id == "$val";
            }
            if (scene == FilterScene::Melee) {
                return id == "$Melee" || id == "$speed" || id == "$wt" || id == "$val";
            }
            if (scene == FilterScene::Armor) {
                return id == "$Armor" || id == "$wt" || id == "$val";
            }
            if (scene == FilterScene::Aid) {
                return id == "$wt" || id == "$val";
            }
            return false;
        }

        const char* SceneFromId(const std::string& id)
        {
            if (IsInScene(id, FilterScene::Guns)) return "guns";
            if (IsInScene(id, FilterScene::Melee)) return "melee";
            if (IsInScene(id, FilterScene::Armor)) return "armor";
            if (IsInScene(id, FilterScene::Aid)) return "aid";
            return "custom";
        }

        bool IsModifierKey(uint32_t key)
        {
            return key == 0x10 || key == 0xA0 || key == 0xA1 ||
                key == 0x11 || key == 0xA2 || key == 0xA3 ||
                key == 0x12 || key == 0xA4 || key == 0xA5;
        }

        uint32_t CurrentModifierMask()
        {
            uint32_t flags = 0;
            if (g_modifierShift) flags |= kModifierShift;
            if (g_modifierCtrl) flags |= kModifierCtrl;
            if (g_modifierAlt) flags |= kModifierAlt;
            return flags;
        }

        void RunOnMainThread(const std::function<void()>& action)
        {
            if (!action) return;
            if (auto* task = F4SE::GetTaskInterface()) {
                task->AddTask(action);
            } else {
                action();
            }
        }

        bool HasValidView()
        {
            return g_view != 0 && g_prismaUI && g_prismaUI->IsValid(g_view);
        }

        bool CanSendToView()
        {
            return g_prismaUI != nullptr && g_view != 0;
        }

        void SendRuleDetailsToUI(const std::string& ruleId, const std::string& source = "json");
        void RegisterMenuJSListeners(PrismaView view);

        std::string EscapeForJsString(const std::string& input)
        {
            std::string out;
            out.reserve(input.size() + 24);
            for (char ch : input) {
                switch (ch) {
                case '\\':
                    out += "\\\\";
                    break;
                case '\'':
                    out += "\\'";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                default:
                    out.push_back(ch);
                    break;
                }
            }
            return out;
        }

        void SendToUI(const char* functionName, const std::string& argument)
        {
            if (!CanSendToView() || !functionName || !*functionName) return;

            REX::DEBUG("[IIF GUIEditor] SendToUI {} ({} bytes)", functionName, argument.size());

            bool delivered = false;
            try {
                std::string safeName = functionName;
                auto escapedArg = EscapeForJsString(argument);
                std::string script =
                    "(function() {\n"
                    "  var payload = '" + escapedArg + "';\n"
                    "  if (typeof window.__iifBridgePush === 'function') {\n"
                    "    window.__iifBridgePush('" + safeName + "', payload);\n"
                    "  } else if (typeof window['" + safeName + "'] === 'function') {\n"
                    "    window['" + safeName + "'](payload);\n"
                    "  }\n"
                    "})();\n";
                g_prismaUI->Invoke(g_view, script.c_str());
                delivered = true;
            } catch (...) {
                REX::WARN("[IIF GUIEditor] Invoke bridge failed for {}", functionName);
            }

            if (!delivered) {
                try {
                    g_prismaUI->InteropCall(g_view, functionName, argument.c_str());
                    delivered = true;
                } catch (...) {
                    REX::WARN("[IIF GUIEditor] InteropCall fallback failed for {}", functionName);
                }
            }

            if (!delivered) {
                REX::WARN("[IIF GUIEditor] SendToUI failed for {}", functionName);
            }
        }

        std::uint32_t GetMsNow()
        {
            return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        std::uint32_t ParseColorValue(const nlohmann::json& value, std::uint32_t fallback = 0)
        {
            if (value.is_number_unsigned()) return value.get<std::uint32_t>();
            if (value.is_number_integer()) {
                auto tmp = value.get<std::int64_t>();
                if (tmp < 0) return fallback;
                return static_cast<std::uint32_t>(tmp);
            }

            if (!value.is_string()) return fallback;
            std::string s = value.get<std::string>();
            if (s.empty()) return fallback;
            try {
                if (s.find("0x") == 0 || s.find("0X") == 0) return static_cast<std::uint32_t>(std::stoul(s, nullptr, 16));
                if (s.find("#") == 0) return static_cast<std::uint32_t>(std::stoul(s.substr(1), nullptr, 16));
                return static_cast<std::uint32_t>(std::stoul(s, nullptr, 16));
            } catch (...) {
                return fallback;
            }
        }

        bool ParseTriStatePayload(const nlohmann::json& value, int& dst)
        {
            if (value.is_boolean()) {
                dst = value.get<bool>() ? 1 : 0;
                return true;
            }
            if (value.is_number_integer()) {
                dst = value.get<int>() > 0 ? 1 : 0;
                return true;
            }
            if (value.is_string()) {
                auto s = value.get<std::string>();
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                if (s == "true" || s == "1") {
                    dst = 1;
                    return true;
                }
                if (s == "false" || s == "0") {
                    dst = 0;
                    return true;
                }
            }
            return false;
        }

        void ApplyDataSourcePayload(const nlohmann::json& payload, IIF::JsonReader::DataSource& dataSource)
        {
            if (!payload.is_object()) return;

            if (payload.contains("Type") && payload["Type"].is_string()) dataSource.Type = payload["Type"].get<std::string>();
            if (payload.contains("type") && payload["type"].is_string()) dataSource.Type = payload["type"].get<std::string>();
            if (payload.contains("ID") && payload["ID"].is_string()) dataSource.ID = payload["ID"].get<std::string>();
            if (payload.contains("id") && payload["id"].is_string()) dataSource.ID = payload["id"].get<std::string>();
            if (payload.contains("Required") && payload["Required"].is_number_integer()) dataSource.Required = payload["Required"].get<int>();
            if (payload.contains("required") && payload["required"].is_number_integer()) dataSource.Required = payload["required"].get<int>();
            if (payload.contains("Offset") && payload["Offset"].is_number()) dataSource.Offset = payload["Offset"].get<float>();
            if (payload.contains("offset") && payload["offset"].is_number()) dataSource.Offset = payload["offset"].get<float>();
            if (payload.contains("Multiplier") && payload["Multiplier"].is_number()) dataSource.Multiplier = payload["Multiplier"].get<float>();
            if (payload.contains("multiplier") && payload["multiplier"].is_number()) dataSource.Multiplier = payload["multiplier"].get<float>();
            if (payload.contains("Suffix") && payload["Suffix"].is_string()) dataSource.Suffix = payload["Suffix"].get<std::string>();
            if (payload.contains("suffix") && payload["suffix"].is_string()) dataSource.Suffix = payload["suffix"].get<std::string>();
            if (payload.contains("active") && payload["active"].is_boolean()) dataSource.active = payload["active"].get<bool>();
            if (payload.contains("Active") && payload["Active"].is_boolean()) dataSource.active = payload["Active"].get<bool>();
        }

        void ApplyBoxPayload(const nlohmann::json& payload, IIF::JsonReader::BoxConfig& box)
        {
            if (!payload.is_object()) return;

            if (payload.contains("tag") && payload["tag"].is_string()) box.tag = payload["tag"].get<std::string>();
            if (payload.contains("isIcon") && payload["isIcon"].is_boolean()) box.isIcon = payload["isIcon"].get<bool>();
            if (payload.contains("value") && payload["value"].is_string()) box.value = payload["value"].get<std::string>();
            if (payload.contains("state") && payload["state"].is_string()) box.state = payload["state"].get<std::string>();
            if (payload.contains("align") && payload["align"].is_string()) box.align = payload["align"].get<std::string>();
            if (payload.contains("active") && payload["active"].is_boolean()) box.active = payload["active"].get<bool>();
            else box.active = true;
            if (payload.contains("dataSource")) ApplyDataSourcePayload(payload["dataSource"], box.dataSource);
            if (payload.contains("DataSource")) ApplyDataSourcePayload(payload["DataSource"], box.dataSource);
        }

        void ApplyResultBlockPayload(const nlohmann::json& payload, IIF::JsonReader::ResultBlock& block)
        {
            if (!payload.is_object()) return;

            if (payload.contains("value") && payload["value"].is_string()) block.value = payload["value"].get<std::string>();
            if (payload.contains("tag") && payload["tag"].is_string()) block.tag = payload["tag"].get<std::string>();
            if (payload.contains("state") && payload["state"].is_string()) block.state = payload["state"].get<std::string>();
            if (payload.contains("align") && payload["align"].is_string()) block.align = payload["align"].get<std::string>();
            if (payload.contains("hideDifference")) ParseTriStatePayload(payload["hideDifference"], block.hideDifference);
            if (payload.contains("invertDiffColor")) ParseTriStatePayload(payload["invertDiffColor"], block.invertDiffColor);
            if (payload.contains("isIcon") && payload["isIcon"].is_boolean()) block.isIcon = payload["isIcon"].get<bool>();
            if (payload.contains("dataSource")) ApplyDataSourcePayload(payload["dataSource"], block.dataSource);
            if (payload.contains("DataSource")) ApplyDataSourcePayload(payload["DataSource"], block.dataSource);
            if (payload.contains("fillPct") && payload["fillPct"].is_number()) block.fillPct = payload["fillPct"].get<float>();
            if (payload.contains("shieldPct") && payload["shieldPct"].is_number()) block.shieldPct = payload["shieldPct"].get<float>();
            if (payload.contains("fillColor")) block.fillColor = ParseColorValue(payload["fillColor"], block.fillColor);
            if (payload.contains("showBar")) ParseTriStatePayload(payload["showBar"], block.showBar);
            if (payload.contains("showValue")) ParseTriStatePayload(payload["showValue"], block.showValue);
            if (payload.contains("valueText") && payload["valueText"].is_string()) block.valueText = payload["valueText"].get<std::string>();
            if (payload.contains("valueAlign") && payload["valueAlign"].is_string()) block.valueAlign = payload["valueAlign"].get<std::string>();
            if (payload.contains("valueColor")) block.valueColor = ParseColorValue(payload["valueColor"], block.valueColor);
            if (payload.contains("valueStandard")) ParseTriStatePayload(payload["valueStandard"], block.valueStandard);
            if (payload.contains("leftBox")) ApplyBoxPayload(payload["leftBox"], block.leftBox);
            if (payload.contains("rightBox")) ApplyBoxPayload(payload["rightBox"], block.rightBox);
            if (payload.contains("hasContent") && payload["hasContent"].is_boolean()) block.hasContent = payload["hasContent"].get<bool>();
            else block.hasContent = true;
        }

        void ApplyAnchorPayload(const nlohmann::json& payload, IIF::JsonReader::JsonRule& rule)
        {
            rule.anchorTargets.clear();
            rule.anchorModes.clear();

            if (payload.contains("anchors") && payload["anchors"].is_array()) {
                for (const auto& item : payload["anchors"]) {
                    if (!item.is_object()) continue;
                    auto target = item.value("target", std::string{});
                    auto mode = item.value("mode", std::string("after"));
                    if (target.empty()) continue;
                    rule.anchorTargets.push_back(target);
                    rule.anchorModes.push_back(mode);
                }
                return;
            }

            if (payload.contains("anchorTargets") && payload["anchorTargets"].is_array()) {
                for (const auto& item : payload["anchorTargets"]) {
                    if (item.is_string()) rule.anchorTargets.push_back(item.get<std::string>());
                }
            }
            if (payload.contains("anchorModes") && payload["anchorModes"].is_array()) {
                for (const auto& item : payload["anchorModes"]) {
                    if (item.is_string()) rule.anchorModes.push_back(item.get<std::string>());
                }
            }
        }

        void ApplyAnchorPayload(const nlohmann::json& payload, std::vector<std::string>& anchorTargets, std::vector<std::string>& anchorModes)
        {
            anchorTargets.clear();
            anchorModes.clear();

            if (payload.contains("anchors") && payload["anchors"].is_array()) {
                for (const auto& item : payload["anchors"]) {
                    if (!item.is_object()) continue;
                    auto target = item.value("target", std::string{});
                    auto mode = item.value("mode", std::string("after"));
                    if (target.empty()) continue;
                    anchorTargets.push_back(target);
                    anchorModes.push_back(mode);
                }
                return;
            }

            if (payload.contains("anchorTargets") && payload["anchorTargets"].is_array()) {
                for (const auto& item : payload["anchorTargets"]) {
                    if (item.is_string()) anchorTargets.push_back(item.get<std::string>());
                }
            }
            if (payload.contains("anchorModes") && payload["anchorModes"].is_array()) {
                for (const auto& item : payload["anchorModes"]) {
                    if (item.is_string()) anchorModes.push_back(item.get<std::string>());
                }
            }
        }

        std::string KeyLabel(uint32_t key)
        {
            if (key >= 0x70 && key <= 0x7B) {
                return "F" + std::to_string((key - 0x6F));
            }
            if (key >= 0x30 && key <= 0x39) {
                return std::string(1, static_cast<char>(key));
            }
            if (key >= 0x41 && key <= 0x5A) {
                return std::string(1, static_cast<char>(key));
            }
            return std::to_string(key);
        }

        std::string ShortcutText()
        {
            std::string text;
            uint32_t mods = CurrentModifierMask();
            if (mods & kModifierCtrl) text += "Ctrl+";
            if (mods & kModifierShift) text += "Shift+";
            if (mods & kModifierAlt) text += "Alt+";
            text += KeyLabel(g_shortcutKey);
            return text;
        }

        void UpdateShortcutUi()
        {
            if (!HasValidView()) return;
            try {
                nlohmann::json state;
                state["key"] = g_shortcutKey;
                state["modifiers"] = g_shortcutModifiers;
                state["label"] = ShortcutText();
                SendToUI("onShortcutUpdated", state.dump());
            } catch (...) {}
        }

        nlohmann::json SerializeAnchorList(const std::vector<std::string>& targets, const std::vector<std::string>& modes)
        {
            nlohmann::json anchors = nlohmann::json::array();
            for (size_t i = 0; i < targets.size(); ++i) {
                nlohmann::json anchor;
                anchor["target"] = targets[i];
                if (i < modes.size()) {
                    anchor["mode"] = modes[i];
                }
                anchors.push_back(anchor);
            }
            return anchors;
        }

        nlohmann::json SerializeDataSource(const IIF::JsonReader::DataSource& dataSource)
        {
            nlohmann::json j;
            j["type"] = dataSource.Type;
            j["id"] = dataSource.ID;
            j["required"] = dataSource.Required;
            j["offset"] = dataSource.Offset;
            j["multiplier"] = dataSource.Multiplier;
            j["suffix"] = dataSource.Suffix;
            j["active"] = dataSource.active;
            return j;
        }

        nlohmann::json SerializeBoxConfig(const IIF::JsonReader::BoxConfig& box)
        {
            nlohmann::json j;
            j["active"] = box.active;
            j["tag"] = box.tag;
            j["isIcon"] = box.isIcon;
            j["value"] = box.value;
            j["state"] = box.state;
            j["align"] = box.align;
            j["dataSource"] = SerializeDataSource(box.dataSource);
            return j;
        }

        nlohmann::json SerializeResultBlock(const IIF::JsonReader::ResultBlock& block)
        {
            nlohmann::json j;
            j["value"] = block.value;
            j["tag"] = block.tag;
            j["state"] = block.state;
            j["align"] = block.align;
            j["hideDifference"] = block.hideDifference;
            j["invertDiffColor"] = block.invertDiffColor;
            j["isIcon"] = block.isIcon;
            j["dataSource"] = SerializeDataSource(block.dataSource);
            j["fillPct"] = block.fillPct;
            j["shieldPct"] = block.shieldPct;
            j["fillColor"] = block.fillColor;
            j["showBar"] = block.showBar;
            j["showValue"] = block.showValue;
            j["valueText"] = block.valueText;
            j["valueAlign"] = block.valueAlign;
            j["valueColor"] = block.valueColor;
            j["valueStandard"] = block.valueStandard;
            j["leftBox"] = SerializeBoxConfig(block.leftBox);
            j["rightBox"] = SerializeBoxConfig(block.rightBox);
            j["hasContent"] = block.hasContent;
            return j;
        }

        void SaveSettings()
        {
            try {
                nlohmann::json state;
                state["menuShortcut"] = {
                    { "key", g_shortcutKey },
                    { "modifiers", g_shortcutModifiers }
                };
                std::filesystem::create_directories(std::filesystem::path(kSettingsFile).parent_path());
                std::ofstream out(kSettingsFile);
                out << state.dump(2);
            } catch (...) {}
        }

        nlohmann::json SerializeRuleSummary(const IIF::JsonReader::JsonRule& rule)
        {
            nlohmann::json item;
            item["id"] = rule.id;
            item["title"] = rule.titleText;
            item["priority"] = rule.priority;
            item["displayType"] = rule.displayType;
            item["source"] = "json";
            item["isOverridden"] = false;
            item["scene"] = SceneFromId(rule.id);
            item["anchors"] = SerializeAnchorList(rule.anchorTargets, rule.anchorModes);
            item["anchorTargets"] = rule.anchorTargets;
            item["anchorModes"] = rule.anchorModes;
            if (!rule.originPath.empty()) {
                item["originPath"] = rule.originPath;
            }
            return item;
        }

        nlohmann::json SerializeCPPOverrideSummary(const IIF::JsonReader::CPPOverride& over)
        {
            nlohmann::json item;
            item["id"] = over.id;
            item["title"] = over.id;
            item["priority"] = over.userPriority;
            item["displayType"] = -1;
            item["source"] = "cpp";
            item["isOverridden"] = over.isOverridden;
            item["scene"] = SceneFromId(over.id);
            item["originPath"] = "CPP_Override";
            item["isRegistered"] = over.isRegistered;
            item["anchors"] = SerializeAnchorList(over.userAnchorTargets, over.userAnchorModes);
            item["anchorTargets"] = over.userAnchorTargets;
            item["anchorModes"] = over.userAnchorModes;
            return item;
        }

        nlohmann::json SerializeRuleDetails(const IIF::JsonReader::JsonRule& rule)
        {
            nlohmann::json obj;
            obj["id"] = rule.id;
            obj["source"] = "json";
            obj["originPath"] = rule.originPath;
            obj["titleText"] = rule.titleText;
            obj["priority"] = rule.priority;
            obj["displayType"] = rule.displayType;
            obj["highlightLabel"] = rule.highlightLabel;
            obj["hasBackground"] = rule.hasBackground;
            obj["state"] = rule.state;
            obj["showBar"] = rule.showBar;
            obj["showValue"] = rule.showValue;
            obj["fillPct"] = rule.fillPct;
            obj["shieldPct"] = rule.shieldPct;
            obj["fillColor"] = rule.fillColor;
            obj["valueAlign"] = rule.valueAlign;
            obj["valueText"] = rule.valueText;
            obj["valueColor"] = rule.valueColor;
            obj["backgroundColor"] = rule.backgroundColor;
            obj["valueStandard"] = rule.valueStandard;
            obj["hideDifference"] = rule.hideDifference;
            obj["invertDiffColor"] = rule.invertDiffColor;
            obj["conditionType"] = rule.conditionType;
            obj["matchType"] = rule.matchType;
            obj["conditionIDs"] = rule.conditionIDs;
            obj["sortValueFrom"] = rule.sortValueFrom;
            obj["result"] = SerializeResultBlock(rule.result);
            nlohmann::json valuesMapping = nlohmann::json::object();
            for (const auto& [key, block] : rule.valuesMapping) {
                valuesMapping[key] = SerializeResultBlock(block);
            }
            obj["valuesMapping"] = valuesMapping;

            auto anchors = SerializeAnchorList(rule.anchorTargets, rule.anchorModes);
            obj["anchors"] = anchors;

            auto addBox = [](const IIF::JsonReader::BoxConfig& box) {
                nlohmann::json j;
                j["active"] = box.active;
                j["tag"] = box.tag;
                j["isIcon"] = box.isIcon;
                j["value"] = box.value;
                j["state"] = box.state;
                j["align"] = box.align;
                j["dataSource"] = SerializeDataSource(box.dataSource);
                return j;
            };

            obj["globalLeftBox"] = addBox(rule.globalLeftBox);
            obj["globalRightBox"] = addBox(rule.globalRightBox);
            return obj;
        }

        nlohmann::json SerializeCPPOverrideDetails(const IIF::JsonReader::CPPOverride& over)
        {
            nlohmann::json obj;
            obj["id"] = over.id;
            obj["source"] = "cpp";
            obj["titleText"] = over.id;
            obj["priority"] = over.userPriority;
            obj["defaultPriority"] = over.defaultPriority;
            obj["isOverridden"] = over.isOverridden;
            obj["isRegistered"] = over.isRegistered;
            obj["scene"] = SceneFromId(over.id);
            obj["originPath"] = "CPP_Override";
            obj["defaultAnchorTargets"] = over.defaultAnchorTargets;
            obj["defaultAnchorModes"] = over.defaultAnchorModes;

            auto anchors = SerializeAnchorList(over.userAnchorTargets, over.userAnchorModes);
            obj["anchors"] = anchors;
            obj["anchorTargets"] = over.userAnchorTargets;
            obj["anchorModes"] = over.userAnchorModes;
            return obj;
        }

        void SendRuleSummaryToUI()
        {
            nlohmann::json payload;
            payload["selected"] = {
                { "id", g_selectedRuleId },
                { "source", g_selectedRuleSource.empty() ? "json" : g_selectedRuleSource }
            };
            payload["rules"] = nlohmann::json::array();
            for (const auto& rule : IIF::JsonReader::g_rules) {
                payload["rules"].push_back(SerializeRuleSummary(rule));
            }
            for (auto& pair : IIF::JsonReader::g_cppOverrides) {
                if (!pair.second.isRegistered) continue;
                auto over = pair.second;
                if (over.id.empty()) over.id = pair.first;
                payload["rules"].push_back(SerializeCPPOverrideSummary(over));
            }
            const auto payloadText = payload.dump();
            REX::INFO("[IIF GUIEditor] SendRuleSummaryToUI count={} selectedId={} selectedSource={}", payload["rules"].size(), g_selectedRuleId, g_selectedRuleSource);
            SendToUI("onRuleSummary", payloadText);
        }

        void HandleUIRequestRuleList(const char* /*payload*/)
        {
            REX::DEBUG("[IIF GUIEditor] UI requested rule list");
            SendRuleSummaryToUI();
        }

        void HandleUIRequestRuleDetails(const char* payload)
        {
            if (!payload) return;
            std::string id;
            std::string source = "json";
            try {
                auto j = nlohmann::json::parse(payload);
                if (j.is_object()) {
                    id = j.value("id", std::string{});
                    source = j.value("source", source);
                } else if (j.is_string()) id = j.get<std::string>();
            } catch (...) {
                return;
            }

            if (id.empty()) {
                SendRuleSummaryToUI();
                return;
            }
            SendRuleDetailsToUI(id, source);
        }

        void SendRuleDetailsToUI(const std::string& ruleId, const std::string& source)
        {
            if (source == "cpp") {
                auto it = IIF::JsonReader::g_cppOverrides.find(ruleId);
                if (it == IIF::JsonReader::g_cppOverrides.end()) {
                    SendToUI("onRuleDetails", "{}");
                    return;
                }
                auto over = it->second;
                if (over.id.empty()) over.id = it->first;
                g_selectedRuleId = over.id;
                g_selectedRuleSource = "cpp";
                auto j = SerializeCPPOverrideDetails(over);
                SendToUI("onRuleDetails", j.dump());
                return;
            }

            auto it = std::find_if(IIF::JsonReader::g_rules.begin(), IIF::JsonReader::g_rules.end(),
                [&ruleId](const IIF::JsonReader::JsonRule& rule) {
                    return rule.id == ruleId;
                });

            if (it == IIF::JsonReader::g_rules.end()) {
                SendToUI("onRuleDetails", "{}");
                return;
            }

            g_selectedRuleId = ruleId;
            g_selectedRuleSource = "json";
            auto j = SerializeRuleDetails(*it);
            SendToUI("onRuleDetails", j.dump());
        }

        void ApplyRuleUpdate(const char* payload)
        {
            if (!payload) return;
            try {
                auto payloadJson = nlohmann::json::parse(payload);
                if (!payloadJson.is_object()) return;
                if (payloadJson.contains("source") && payloadJson["source"].is_string()) {
                    if (payloadJson["source"].get<std::string>() == "cpp") return;
                }

                const auto id = payloadJson.value("id", std::string{});
                if (id.empty()) return;

                auto it = std::find_if(IIF::JsonReader::g_rules.begin(), IIF::JsonReader::g_rules.end(),
                    [&id](const IIF::JsonReader::JsonRule& rule) {
                        return rule.id == id;
                    });
                if (it == IIF::JsonReader::g_rules.end()) return;

                if (payloadJson.contains("titleText") && payloadJson["titleText"].is_string()) {
                    it->titleText = payloadJson["titleText"].get<std::string>();
                }
                if (payloadJson.contains("priority") && payloadJson["priority"].is_number_integer()) {
                    it->priority = payloadJson["priority"].get<int>();
                }
                if (payloadJson.contains("displayType") && payloadJson["displayType"].is_number_integer()) {
                    it->displayType = payloadJson["displayType"].get<int>();
                }
                if (payloadJson.contains("conditionType") && payloadJson["conditionType"].is_string()) {
                    it->conditionType = payloadJson["conditionType"].get<std::string>();
                }
                if (payloadJson.contains("matchType") && payloadJson["matchType"].is_string()) {
                    it->matchType = payloadJson["matchType"].get<std::string>();
                }
                if (payloadJson.contains("conditionIDs") && payloadJson["conditionIDs"].is_array()) {
                    it->conditionIDs.clear();
                    for (const auto& item : payloadJson["conditionIDs"]) {
                        if (item.is_string()) it->conditionIDs.push_back(item.get<std::string>());
                    }
                }
                if (payloadJson.contains("sortValueFrom") && payloadJson["sortValueFrom"].is_string()) {
                    it->sortValueFrom = payloadJson["sortValueFrom"].get<std::string>();
                }
                if (payloadJson.contains("highlightLabel") && payloadJson["highlightLabel"].is_boolean()) {
                    it->highlightLabel = payloadJson["highlightLabel"].get<bool>();
                }
                if (payloadJson.contains("hasBackground") && payloadJson["hasBackground"].is_boolean()) {
                    it->hasBackground = payloadJson["hasBackground"].get<bool>();
                }
                if (payloadJson.contains("state") && payloadJson["state"].is_string()) {
                    it->state = payloadJson["state"].get<std::string>();
                }
                if (payloadJson.contains("valueStandard")) {
                    if (payloadJson["valueStandard"].is_boolean()) {
                        it->valueStandard = payloadJson["valueStandard"].get<bool>();
                    } else if (payloadJson["valueStandard"].is_number_integer()) {
                        it->valueStandard = payloadJson["valueStandard"].get<int>() > 0;
                    }
                }
                if (payloadJson.contains("showBar") && payloadJson["showBar"].is_boolean()) {
                    it->showBar = payloadJson["showBar"].get<bool>();
                }
                if (payloadJson.contains("showValue") && payloadJson["showValue"].is_boolean()) {
                    it->showValue = payloadJson["showValue"].get<bool>();
                }
                if (payloadJson.contains("valueAlign") && payloadJson["valueAlign"].is_string()) {
                    it->valueAlign = payloadJson["valueAlign"].get<std::string>();
                }
                if (payloadJson.contains("hideDifference")) {
                    auto value = payloadJson["hideDifference"];
                    ParseTriStatePayload(value, it->hideDifference);
                }
                if (payloadJson.contains("invertDiffColor")) {
                    auto value = payloadJson["invertDiffColor"];
                    ParseTriStatePayload(value, it->invertDiffColor);
                }
                if (payloadJson.contains("valueText") && payloadJson["valueText"].is_string()) {
                    it->valueText = payloadJson["valueText"].get<std::string>();
                }
                if (payloadJson.contains("fillPct")) {
                    const auto& value = payloadJson["fillPct"];
                    if (value.is_number()) it->fillPct = value.get<float>();
                    else if (value.is_string()) it->fillPct = std::stof(value.get<std::string>());
                }
                if (payloadJson.contains("shieldPct")) {
                    const auto& value = payloadJson["shieldPct"];
                    if (value.is_number()) it->shieldPct = value.get<float>();
                    else if (value.is_string()) it->shieldPct = std::stof(value.get<std::string>());
                }
                if (payloadJson.contains("fillColor")) {
                    it->fillColor = ParseColorValue(payloadJson["fillColor"], it->fillColor);
                }
                if (payloadJson.contains("valueColor")) {
                    it->valueColor = ParseColorValue(payloadJson["valueColor"], it->valueColor);
                }
                if (payloadJson.contains("backgroundColor")) {
                    it->backgroundColor = ParseColorValue(payloadJson["backgroundColor"], it->backgroundColor);
                }
                if (payloadJson.contains("globalLeftBox")) {
                    ApplyBoxPayload(payloadJson["globalLeftBox"], it->globalLeftBox);
                }
                if (payloadJson.contains("globalRightBox")) {
                    ApplyBoxPayload(payloadJson["globalRightBox"], it->globalRightBox);
                }
                if (payloadJson.contains("result")) {
                    ApplyResultBlockPayload(payloadJson["result"], it->result);
                }
                if (payloadJson.contains("valuesMapping") && payloadJson["valuesMapping"].is_object()) {
                    it->valuesMapping.clear();
                    for (const auto& item : payloadJson["valuesMapping"].items()) {
                        IIF::JsonReader::ResultBlock block;
                        ApplyResultBlockPayload(item.value(), block);
                        it->valuesMapping[item.key()] = block;
                    }
                }
                ApplyAnchorPayload(payloadJson, *it);

                if (payloadJson.contains("originPath") && payloadJson["originPath"].is_string()) {
                    auto path = payloadJson["originPath"].get<std::string>();
                    if (!path.empty()) it->originPath = path;
                }

                IIF::JsonReader::SaveConfigs();
            } catch (...) {
                return;
            }

            if (!g_selectedRuleId.empty()) SendRuleDetailsToUI(g_selectedRuleId);
            SendRuleSummaryToUI();
        }

        void ApplyCPPOverrideUpdate(const char* payload)
        {
            if (!payload) return;
            try {
                auto payloadJson = nlohmann::json::parse(payload);
                if (!payloadJson.is_object()) return;
                const auto id = payloadJson.value("id", std::string{});
                if (id.empty()) return;

                auto it = IIF::JsonReader::g_cppOverrides.find(id);
                if (it == IIF::JsonReader::g_cppOverrides.end()) return;

                auto& over = it->second;
                if (payloadJson.contains("priority") && payloadJson["priority"].is_number_integer()) {
                    over.userPriority = payloadJson["priority"].get<int>();
                }
                ApplyAnchorPayload(payloadJson, over.userAnchorTargets, over.userAnchorModes);
                over.isOverridden = true;

                IIF::JsonReader::SaveConfigs();
                SendRuleDetailsToUI(id, "cpp");
                SendRuleSummaryToUI();
            } catch (...) {}
        }

        void ApplyCPPOverrideReset(const char* payload)
        {
            if (!payload) return;
            try {
                auto payloadJson = nlohmann::json::parse(payload);
                if (!payloadJson.is_object()) return;
                const auto id = payloadJson.value("id", std::string{});
                if (id.empty()) return;

                auto it = IIF::JsonReader::g_cppOverrides.find(id);
                if (it == IIF::JsonReader::g_cppOverrides.end()) return;

                auto& over = it->second;
                over.userPriority = over.defaultPriority;
                over.userAnchorTargets = over.defaultAnchorTargets;
                over.userAnchorModes = over.defaultAnchorModes;
                over.isOverridden = false;

                IIF::JsonReader::SaveConfigs();
                SendRuleDetailsToUI(id, "cpp");
                SendRuleSummaryToUI();
            } catch (...) {}
        }

        void ApplyRuleCreate(const char* payload)
        {
            if (!payload) return;
            try {
                auto payloadJson = nlohmann::json::parse(payload);
                if (!payloadJson.is_object()) return;

                const auto id = payloadJson.value("id", std::string{});
                if (id.empty() || id == "TOP" || id == "BOTTOM") return;

                auto it = std::find_if(IIF::JsonReader::g_rules.begin(), IIF::JsonReader::g_rules.end(),
                    [&id](const IIF::JsonReader::JsonRule& rule) {
                        return rule.id == id;
                    });
                if (it != IIF::JsonReader::g_rules.end()) return;

                IIF::JsonReader::JsonRule newRule;
                const auto cloneFrom = payloadJson.value("cloneFrom", std::string{});
                if (!cloneFrom.empty()) {
                    auto src = std::find_if(IIF::JsonReader::g_rules.begin(), IIF::JsonReader::g_rules.end(),
                        [&cloneFrom](const IIF::JsonReader::JsonRule& rule) {
                            return rule.id == cloneFrom;
                        });
                    if (src != IIF::JsonReader::g_rules.end()) {
                        newRule = *src;
                    }
                }

                if (newRule.conditionType.empty()) {
                    newRule.conditionType = "FormType";
                    newRule.matchType = "OR";
                    newRule.conditionIDs = { "Weapon", "Armor", "Ammo", "Alchemy" };
                    newRule.anchorTargets.push_back("BOTTOM");
                    newRule.anchorModes.push_back("after");
                }

                newRule.id = id;
                if (payloadJson.contains("titleText") && payloadJson["titleText"].is_string()) {
                    newRule.titleText = payloadJson["titleText"].get<std::string>();
                }
                if (newRule.titleText.empty()) newRule.titleText = id;
                if (payloadJson.contains("priority") && payloadJson["priority"].is_number_integer()) {
                    newRule.priority = payloadJson["priority"].get<int>();
                }
                if (payloadJson.contains("displayType") && payloadJson["displayType"].is_number_integer()) {
                    newRule.displayType = payloadJson["displayType"].get<int>();
                }
                if (payloadJson.contains("anchors")) {
                    ApplyAnchorPayload(payloadJson, newRule);
                }

                if (payloadJson.contains("originPath") && payloadJson["originPath"].is_string()) {
                    newRule.originPath = payloadJson["originPath"].get<std::string>();
                }
                if (newRule.originPath.empty()) {
                    newRule.originPath = kDefaultRuleFile;
                }

                IIF::JsonReader::g_rules.push_back(newRule);
                g_selectedRuleId = id;
                IIF::JsonReader::SaveConfigs();
                SendRuleSummaryToUI();
                SendRuleDetailsToUI(id);
            } catch (...) {}
        }

        void ApplyRuleDelete(const char* payload)
        {
            if (!payload) return;
            try {
                auto payloadJson = nlohmann::json::parse(payload);
                if (!payloadJson.is_object()) return;
                auto id = payloadJson.value("id", std::string{});
                if (id.empty()) return;

                auto it = std::find_if(IIF::JsonReader::g_rules.begin(), IIF::JsonReader::g_rules.end(),
                    [&id](const IIF::JsonReader::JsonRule& rule) {
                        return rule.id == id;
                    });
                if (it == IIF::JsonReader::g_rules.end()) return;

                IIF::JsonReader::g_rules.erase(it);
                if (g_selectedRuleId == id) {
                    g_selectedRuleId.clear();
                    g_selectedRuleSource = "json";
                }
                IIF::JsonReader::SaveConfigs();
                SendRuleSummaryToUI();
                SendToUI("onRuleDetails", "{}");
            } catch (...) {}
        }

        void ApplyRuleRename(const char* payload)
        {
            if (!payload) return;
            try {
                auto payloadJson = nlohmann::json::parse(payload);
                if (!payloadJson.is_object()) return;

                const auto oldId = payloadJson.value("oldId", std::string{});
                const auto newId = payloadJson.value("newId", std::string{});
                const bool updateAnchors = payloadJson.value("updateAnchors", true);
                if (oldId.empty() || newId.empty() || oldId == newId) return;
                if (newId == "TOP" || newId == "BOTTOM") return;

                auto target = std::find_if(IIF::JsonReader::g_rules.begin(), IIF::JsonReader::g_rules.end(),
                    [&oldId](const IIF::JsonReader::JsonRule& rule) {
                        return rule.id == oldId;
                    });
                if (target == IIF::JsonReader::g_rules.end()) return;

                auto duplicate = std::find_if(IIF::JsonReader::g_rules.begin(), IIF::JsonReader::g_rules.end(),
                    [&newId](const IIF::JsonReader::JsonRule& rule) {
                        return rule.id == newId;
                    });
                if (duplicate != IIF::JsonReader::g_rules.end()) return;

                target->id = newId;
                if (payloadJson.contains("titleText") && payloadJson["titleText"].is_string()) {
                    target->titleText = payloadJson["titleText"].get<std::string>();
                }

                if (updateAnchors) {
                    for (auto& rule : IIF::JsonReader::g_rules) {
                        for (auto& anchor : rule.anchorTargets) {
                            if (anchor == oldId) {
                                anchor = newId;
                            }
                        }
                    }
                    for (auto& [_, over] : IIF::JsonReader::g_cppOverrides) {
                        bool touched = false;
                        for (auto& anchor : over.userAnchorTargets) {
                            if (anchor == oldId) {
                                anchor = newId;
                                touched = true;
                            }
                        }
                        if (touched) {
                            over.isOverridden = true;
                        }
                    }
                }

                if (g_selectedRuleId == oldId) {
                    g_selectedRuleId = newId;
                    g_selectedRuleSource = "json";
                }

                IIF::JsonReader::SaveConfigs();
                SendRuleSummaryToUI();
                SendRuleDetailsToUI(newId, "json");
            } catch (...) {}
        }

        void LoadSettings()
        {
            try {
                if (!std::filesystem::exists(kSettingsFile)) {
                    g_shortcutKey = kDefaultShortcutKey;
                    g_shortcutModifiers = kDefaultShortcutModifiers;
                    return;
                }

                std::ifstream in(kSettingsFile);
                auto j = nlohmann::json::parse(in, nullptr, true, true);
                if (!j.is_object() || !j.contains("menuShortcut") || !j["menuShortcut"].is_object()) {
                    return;
                }

                auto shortcut = j["menuShortcut"];
                if (shortcut.contains("key") && shortcut["key"].is_number_unsigned()) {
                    g_shortcutKey = shortcut["key"].get<uint32_t>();
                }
                if (shortcut.contains("modifiers") && shortcut["modifiers"].is_number_unsigned()) {
                    g_shortcutModifiers = shortcut["modifiers"].get<uint32_t>();
                }
            } catch (...) {
                g_shortcutKey = kDefaultShortcutKey;
                g_shortcutModifiers = kDefaultShortcutModifiers;
            }
        }

        void ApplyMenuState(bool open)
        {
            if (!g_prismaUI) return;
            if (g_menuApplying) {
                REX::WARN("[IIF GUIEditor] menu state apply in progress, skipping nested request");
                return;
            }
            if (!HasValidView() || !g_viewReady) {
                g_visible = open;
                g_openPending = true;
                REX::INFO("[IIF GUIEditor] menu state pending (view not ready) open={}", open);
                return;
            }

            g_menuApplying = true;

            try {
                RegisterMenuJSListeners(g_view);
                if (open) {
                    if (g_prismaUI->IsHidden(g_view)) {
                        g_prismaUI->Show(g_view);
                    }
                    if (!g_prismaUI->HasFocus(g_view)) {
                        g_prismaUI->Focus(g_view, /*pauseGame=*/false, /*disableFocusMenu=*/false);
                    }
                    SendRuleSummaryToUI();
                    if (!g_selectedRuleId.empty()) {
                        SendRuleDetailsToUI(g_selectedRuleId);
                    }
                } else {
                    if (g_prismaUI->HasFocus(g_view)) {
                        g_prismaUI->Unfocus(g_view);
                    }
                    if (!g_prismaUI->IsHidden(g_view)) {
                        g_prismaUI->Hide(g_view);
                    }
                }

                g_visible = open;
                g_openPending = false;
                SendToUI("onMenuStateChanged", g_visible ? "true" : "false");
            } catch (...) {
                REX::WARN("[IIF GUIEditor] ApplyMenuState threw when toggling menu visibility.");
            }

            g_menuApplying = false;
        }

        void RequestToggleMenuState()
        {
            const auto now = GetMsNow();
            if (now - g_lastToggleMs < 120) {
                REX::DEBUG("[IIF GUIEditor] toggle suppressed by debounce");
                return;
            }
            g_lastToggleMs = now;
            RunOnMainThread([]() {
                ApplyMenuState(!g_visible);
            });
        }

        void UnregisterAllHandles()
        {
            auto handler = GUIEditorKeyHandler::GetSingleton();
            if (!handler) return;
            for (auto h : g_captureKeyHandles) handler->Unregister(h);
            g_captureKeyHandles.clear();
        }

        void EndCaptureMode()
        {
            if (!g_captureMode) return;
            g_captureMode = false;
            UnregisterAllHandles();
            if (HasValidView()) {
                SendToUI("onShortcutCaptureStateChanged", "false");
            }
        }

        void BindModifierState()
        {
            auto handler = GUIEditorKeyHandler::GetSingleton();
            if (!handler) return;

            if (g_shiftDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_shiftDownHandle = handler->Register(0x10, KeyEventType::KEY_DOWN, []() { g_modifierShift = true; });
            }
            if (g_shiftLeftDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_shiftLeftDownHandle = handler->Register(0xA0, KeyEventType::KEY_DOWN, []() { g_modifierShift = true; });
            }
            if (g_shiftRightDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_shiftRightDownHandle = handler->Register(0xA1, KeyEventType::KEY_DOWN, []() { g_modifierShift = true; });
            }

            if (g_shiftUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_shiftUpHandle = handler->Register(0x10, KeyEventType::KEY_UP, []() { g_modifierShift = false; });
            }
            if (g_shiftLeftUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_shiftLeftUpHandle = handler->Register(0xA0, KeyEventType::KEY_UP, []() { g_modifierShift = false; });
            }
            if (g_shiftRightUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_shiftRightUpHandle = handler->Register(0xA1, KeyEventType::KEY_UP, []() { g_modifierShift = false; });
            }

            if (g_ctrlDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_ctrlDownHandle = handler->Register(0x11, KeyEventType::KEY_DOWN, []() { g_modifierCtrl = true; });
            }
            if (g_ctrlLeftDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_ctrlLeftDownHandle = handler->Register(0xA2, KeyEventType::KEY_DOWN, []() { g_modifierCtrl = true; });
            }
            if (g_ctrlRightDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_ctrlRightDownHandle = handler->Register(0xA3, KeyEventType::KEY_DOWN, []() { g_modifierCtrl = true; });
            }
            if (g_ctrlUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_ctrlUpHandle = handler->Register(0x11, KeyEventType::KEY_UP, []() { g_modifierCtrl = false; });
            }
            if (g_ctrlLeftUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_ctrlLeftUpHandle = handler->Register(0xA2, KeyEventType::KEY_UP, []() { g_modifierCtrl = false; });
            }
            if (g_ctrlRightUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_ctrlRightUpHandle = handler->Register(0xA3, KeyEventType::KEY_UP, []() { g_modifierCtrl = false; });
            }

            if (g_altDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_altDownHandle = handler->Register(0x12, KeyEventType::KEY_DOWN, []() { g_modifierAlt = true; });
            }
            if (g_altLeftDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_altLeftDownHandle = handler->Register(0xA4, KeyEventType::KEY_DOWN, []() { g_modifierAlt = true; });
            }
            if (g_altRightDownHandle == INVALID_REGISTRATION_HANDLE) {
                g_altRightDownHandle = handler->Register(0xA5, KeyEventType::KEY_DOWN, []() { g_modifierAlt = true; });
            }
            if (g_altUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_altUpHandle = handler->Register(0x12, KeyEventType::KEY_UP, []() { g_modifierAlt = false; });
            }
            if (g_altLeftUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_altLeftUpHandle = handler->Register(0xA4, KeyEventType::KEY_UP, []() { g_modifierAlt = false; });
            }
            if (g_altRightUpHandle == INVALID_REGISTRATION_HANDLE) {
                g_altRightUpHandle = handler->Register(0xA5, KeyEventType::KEY_UP, []() { g_modifierAlt = false; });
            }
        }

        void BindToggleShortcut()
        {
            auto handler = GUIEditorKeyHandler::GetSingleton();
            if (!handler) return;
            if (!HasValidView()) {
                REX::WARN("[IIF GUIEditor] BindToggleShortcut skipped: no valid view.");
                return;
            }

            if (g_toggleHandle.has_value()) {
                handler->Unregister(*g_toggleHandle);
                g_toggleHandle.reset();
            }

            g_toggleHandle = handler->Register(g_shortcutKey, KeyEventType::KEY_DOWN, []() {
                if (g_captureMode) return;
                if (!g_prismaUI || CurrentModifierMask() != g_shortcutModifiers) return;
                RequestToggleMenuState();
            });
            REX::INFO("[IIF GUIEditor] toggle shortcut bound key={} mods={}", g_shortcutKey, g_shortcutModifiers);
        }

        void OnShortcutCapture(uint32_t key)
        {
            if (IsModifierKey(key)) {
                if (key == 0x10 || key == 0xA0 || key == 0xA1) g_modifierShift = true;
                if (key == 0x11 || key == 0xA2 || key == 0xA3) g_modifierCtrl = true;
                if (key == 0x12 || key == 0xA4 || key == 0xA5) g_modifierAlt = true;
                return;
            }

            const uint32_t newKey = key;
            const uint32_t newModifiers = CurrentModifierMask();
            RunOnMainThread([newKey, newModifiers]() {
                g_shortcutKey = newKey;
                g_shortcutModifiers = newModifiers;
                BindToggleShortcut();
                SaveSettings();
                UpdateShortcutUi();
                EndCaptureMode();

                if (HasValidView()) {
                    try {
                        nlohmann::json r;
                        r["key"] = g_shortcutKey;
                        r["modifiers"] = g_shortcutModifiers;
                        r["label"] = ShortcutText();
                        SendToUI("onShortcutCaptured", r.dump());
                    } catch (...) {}
                }
            });
        }

        void StartCaptureMode()
        {
            if (g_captureMode || !HasValidView()) return;
            g_captureMode = true;
            SendToUI("onShortcutCaptureStateChanged", "true");

            auto handler = GUIEditorKeyHandler::GetSingleton();
            if (!handler) return;

            g_captureKeyHandles.reserve(256);
            for (uint32_t key = 1; key < 256; ++key) {
                if (IsModifierKey(key)) continue;
                auto h = handler->Register(key, KeyEventType::KEY_DOWN, [key]() {
                    if (!g_captureMode) return;
                    OnShortcutCapture(key);
                });
                g_captureKeyHandles.push_back(h);
            }
        }

        void RequestStartCaptureMode()
        {
            RunOnMainThread([]() { StartCaptureMode(); });
        }

        void RequestEndCaptureMode()
        {
            RunOnMainThread([]() { EndCaptureMode(); });
        }

        void ApplyConsoleLog(const char* message, PRISMA_UI_API::ConsoleMessageLevel level)
        {
            if (level == PRISMA_UI_API::ConsoleMessageLevel::Error) {
                REX::ERROR("[IIF GUIEditor] {}", message);
            } else if (level == PRISMA_UI_API::ConsoleMessageLevel::Warning) {
                REX::WARN("[IIF GUIEditor] {}", message);
            } else if (level == PRISMA_UI_API::ConsoleMessageLevel::Info) {
                REX::INFO("[IIF GUIEditor] {}", message);
            } else if (level == PRISMA_UI_API::ConsoleMessageLevel::Debug) {
                REX::DEBUG("[IIF GUIEditor] {}", message);
            } else {
                REX::INFO("[IIF GUIEditor] {}", message);
            }
        }

        void RegisterMenuJSListeners(PrismaView view)
        {
            if (g_listenersRegistered) return;
            if (!g_prismaUI || view == 0 || !g_prismaUI->IsValid(view)) return;

            auto bind = [view](const char* name, PRISMA_UI_API::JSListenerCallback callback) {
                if (g_prismaUI4) {
                    g_prismaUI4->BindUIEvent(view, name, callback);
                } else {
                    g_prismaUI->RegisterJSListener(view, name, callback);
                }
            };

            bind("uiRequestClose", [](const char*) {
                RunOnMainThread([]() {
                    if (HasValidView()) {
                        g_prismaUI->Unfocus(g_view);
                        g_prismaUI->Hide(g_view);
                        g_visible = false;
                        g_openPending = false;
                        SendToUI("onMenuStateChanged", "false");
                    }
                });
            });

            bind("uiRequestCaptureShortcut", [](const char*) {
                RequestStartCaptureMode();
            });

            bind("uiRequestReloadRules", [](const char*) {
                REX::INFO("[IIF GUIEditor] requestReloadRules from UI");
                IIF::JsonReader::LoadConfigs();
                SendRuleSummaryToUI();
                if (!g_selectedRuleId.empty()) SendRuleDetailsToUI(g_selectedRuleId);
            });

            bind("uiRequestRuleList", [](const char* payload) {
                HandleUIRequestRuleList(payload);
            });
            bind("uiRequestRules", [](const char* payload) {
                HandleUIRequestRuleList(payload);
            });
            bind("requestRuleList", [](const char* payload) {
                HandleUIRequestRuleList(payload);
            });

            bind("uiRequestRuleDetails", [](const char* payload) {
                if (!payload) return;

                std::string id;
                std::string source = "json";
                try {
                    auto j = nlohmann::json::parse(payload);
                    if (j.is_object()) {
                        id = j.value("id", std::string{});
                        source = j.value("source", source);
                    }
                    else if (j.is_string()) id = j.get<std::string>();
                } catch (...) {}

                if (id.empty()) {
                    SendRuleSummaryToUI();
                    return;
                }
                SendRuleDetailsToUI(id, source);
            });
            bind("requestRuleDetails", [](const char* payload) {
                HandleUIRequestRuleDetails(payload);
            });

            bind("uiRequestRuleUpdate", [](const char* payload) {
                if (!payload) return;
                ApplyRuleUpdate(payload);
            });

            bind("uiRequestCppOverrideUpdate", [](const char* payload) {
                if (!payload) return;
                ApplyCPPOverrideUpdate(payload);
            });

            bind("uiRequestCppOverrideReset", [](const char* payload) {
                if (!payload) return;
                ApplyCPPOverrideReset(payload);
            });

            bind("uiRequestRuleCreate", [](const char* payload) {
                if (!payload) return;
                ApplyRuleCreate(payload);
            });

            bind("uiRequestRuleDelete", [](const char* payload) {
                if (!payload) return;
                ApplyRuleDelete(payload);
            });

            bind("uiRequestRuleRename", [](const char* payload) {
                if (!payload) return;
                ApplyRuleRename(payload);
            });

            bind("uiRequestCloseCapture", [](const char*) {
                RequestEndCaptureMode();
            });

            bind("uiRequestSaveNow", [](const char*) {
                try {
                    IIF::JsonReader::SaveConfigs();
                    SendToUI("onSaveResult", "{\"ok\":true}");
                } catch (...) {
                    SendToUI("onSaveResult", "{\"ok\":false}");
                }
            });

            g_listenersRegistered = true;
        }

        void OnDomReady(PrismaView view)
        {
            if (!view || view != g_view) return;
            g_viewReady = true;
            REX::INFO("[IIF GUIEditor] DOM ready for view {}", view);
            BindToggleShortcut();
            RegisterMenuJSListeners(view);

            UpdateShortcutUi();
            if (g_openPending) {
                ApplyMenuState(g_visible);
            } else {
                SendToUI("onMenuStateChanged", g_visible ? "true" : "false");
            }
            SendRuleSummaryToUI();
            if (!g_selectedRuleId.empty()) {
                SendRuleDetailsToUI(g_selectedRuleId);
            }
            SendToUI("onShortcutCaptureStateChanged", g_captureMode ? "true" : "false");
        }

        void CreateView()
        {
            if (!g_prismaUI) return;
            if (g_view != 0 && g_prismaUI->IsValid(g_view)) {
                g_viewReady = true;
                return;
            }

            if (g_view != 0) {
                g_view = 0;
                g_viewReady = false;
                g_openPending = g_visible;
                g_listenersRegistered = false;
            }

            g_view = g_prismaUI->CreateView(kMenuPath, OnDomReady);
            if (g_view == 0) {
                REX::ERROR("[IIF GUIEditor] CreateView returned 0");
                return;
            }

            if (g_prismaUI4) {
                g_prismaUI4->RegisterTranslations(g_view, "ItemIntegrationFramework");
                REX::INFO("[IIF GUIEditor] PrismaUI translations registered.");
            }

            g_prismaUI->RegisterConsoleCallback(g_view, [](PrismaView, PRISMA_UI_API::ConsoleMessageLevel level, const char* msg) {
                ApplyConsoleLog(msg, level);
            });
            g_prismaUI->SetOrder(g_view, 100);
            g_prismaUI->SetScrollingPixelSize(g_view, 44);
            g_prismaUI->Hide(g_view);
            g_viewReady = false;
            if (!g_openPending) {
                g_visible = false;
            }
            REX::INFO("[IIF GUIEditor] PrismaUI view created.");
        }

        void RegisterInput()
        {
            if (g_sinkRegistered || !g_prismaUI) return;

            GUIEditorKeyHandler::RegisterSink();
            InstallHotkeySubclass();
            g_sinkRegistered = true;

            BindModifierState();
            LoadSettings();
            if (g_viewReady) {
                BindToggleShortcut();
            }

            REX::INFO("[IIF GUIEditor] shortcut key={} modifiers={}", g_shortcutKey, g_shortcutModifiers);
        }

        bool EnsurePrismaUI()
        {
            if (g_prismaUI) {
                return true;
            }

            g_prismaUI4 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>();
            if (g_prismaUI4) {
                g_prismaUI = static_cast<PRISMA_UI_API::IVPrismaUI2*>(g_prismaUI4);
                REX::INFO("[IIF GUIEditor] PrismaUI V4 API acquired.");
                return true;
            }

            g_prismaUI = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI2>();
            if (!g_prismaUI) {
                REX::WARN("[IIF GUIEditor] PrismaUI API not available yet.");
                return false;
            }

            REX::INFO("[IIF GUIEditor] PrismaUI V2 API acquired; V4 features disabled.");
            return true;
        }
    }

    void OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) return;

        if (a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
            if (!EnsurePrismaUI()) return;
            RegisterInput();
            return;
        }

        if (a_msg->type == F4SE::MessagingInterface::kPostLoadGame || a_msg->type == F4SE::MessagingInterface::kNewGame ||
            a_msg->type == F4SE::MessagingInterface::kGameLoaded) {
            if (!EnsurePrismaUI()) return;

            RegisterInput();
            CreateView();
            REX::INFO("[IIF GUIEditor] message {} -> input + view initialization", a_msg->type);
        }
    }
}
