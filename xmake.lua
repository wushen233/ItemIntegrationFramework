set_xmakever("3.0.0")

set_project("ItemIntegrationFramework")
set_version("1.0.5")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")
set_policy("package.requires_lock", true)

local commonlibf4_path = os.getenv("COMMONLIBF4_PATH")
local commonlibf4_is_valid = commonlibf4_path and os.isdir(commonlibf4_path)
local commonlibf4_error = "COMMONLIBF4_PATH must point to an existing CommonLibF4 checkout; got " ..
    (commonlibf4_path or "<unset>")
if commonlibf4_is_valid then
    includes(commonlibf4_path)
end

add_requires("simpleini v4.25")
add_requires("nlohmann_json v3.12.0")
add_requires("tinyxml2 11.0.0")
add_requires("imgui v1.92.5-docking", { configs = { win32 = true, dx11 = true } })
add_requires("microsoft-detours 2023.6.8")
add_requires("minhook v1.3.4")

target("ItemIntegrationFramework")
    on_load(function()
        if not commonlibf4_is_valid then
            raise(commonlibf4_error)
        end
    end)

    if commonlibf4_is_valid then
        add_rules("commonlibf4.plugin", {
            name = "ItemIntegrationFramework",
            author = "h_wushen",
            description = "Shared item card injection and hook framework for Fallout 4",
            version = "1.0.5",
            plugin_template = path.join(os.scriptdir(), "res/commonlibf4-plugin.cpp.in")
        })

        add_packages("simpleini", "nlohmann_json", "tinyxml2", "imgui", "microsoft-detours", "minhook")
        add_files("src/**.cpp")
        remove_files("src/GUIEditor/**.cpp")
        add_headerfiles("src/**.h")
        remove_files("src/GUIEditor/**.h")
        add_includedirs("src")
        set_pcxxheader("src/pch.h")
        set_encodings("utf-8")
        add_cxxflags("/utf-8", { force = true, tools = { "msvc", "clang-cl" } })
        add_defines("IIF_CB_HOST_EXPORTS", "IIF_CB_PRODUCTION_HOST")

        add_defines(
            'PLUGIN_NAME="ItemIntegrationFramework"',
            "PLUGIN_VERSION_MAJOR=1",
            "PLUGIN_VERSION_MINOR=0",
            "PLUGIN_VERSION_PATCH=5"
        )
    end
