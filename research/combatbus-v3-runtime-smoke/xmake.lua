set_xmakever("3.0.0")
set_project("IIFCombatBusRuntimeSmoke")
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

option("outgoing_identity_smoke")
    set_default(false)
    set_showmenu(true)
    set_description("Enable the synthetic Outgoing identity probe for an isolated no-Provider test profile")
option_end()

target("IIFCombatBusRuntimeSmoke")
    set_kind("shared")
    add_rules("commonlibf4.plugin", {
        name = "IIFCombatBusRuntimeSmoke",
        author = "ItemIntegrationFramework research",
        description = "Temporary no-hook CombatBus V3 runtime smoke probe",
        version = "1.0.0"
    })
    if has_config("outgoing_identity_smoke") then
        add_defines("IIF_CB_RUNTIME_SMOKE_OUTGOING_IDENTITY")
        set_basename("IIFCombatBusRuntimeSmoke_OutgoingOptIn")
    end
    add_files("src/RuntimeSmoke.cpp")
    add_includedirs("../../src/CombatBusV3")
