set_xmakever("3.0.0")
set_project("IIFCombatBusProviderLifecycleSmoke")
set_version("1.0.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")

local commonlibf4_path = os.getenv("COMMONLIBF4_PATH")
if not commonlibf4_path or not os.isdir(commonlibf4_path) then
    raise("COMMONLIBF4_PATH must point to the selected CommonLibF4 checkout")
end
includes(commonlibf4_path)

target("IIFCombatBusProviderLifecycleSmoke")
    set_kind("shared")
    add_rules("commonlibf4.plugin", {
        name = "IIFCombatBusProviderLifecycleSmoke",
        author = "ItemIntegrationFramework research",
        description = "Temporary no-hook CombatBus V3 Provider lifecycle probe",
        version = "1.0.0"
    })
    add_files("src/ProviderLifecycleSmoke.cpp", "src/ProviderLifecycleScenario.cpp")
    add_includedirs("src", "../../src/CombatBusV3")
