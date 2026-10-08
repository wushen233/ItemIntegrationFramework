#include <F4SE/F4SE.h>

#include "ProviderLifecycleScenario.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// Windows headers define ERROR as a logging-level macro, which collides with
// CommonLibF4's REX::ERROR logging function.
#ifdef ERROR
#undef ERROR
#endif

#include <cstdint>
#include <cstring>

namespace {
	using QueryInterfaceFn = std::uint32_t(IIF_CB_CALL*)(std::uint32_t, std::uint32_t,
		IIF_CB_InterfaceV3*);

	bool g_ran{};

	template <class Function>
	Function Resolve(HMODULE module, const char* name) noexcept
	{
		const auto address = GetProcAddress(module, name);
		Function function{};
		static_assert(sizeof(function) == sizeof(address));
		std::memcpy(&function, &address, sizeof(function));
		return function;
	}

	void LogMessage(const char* message)
	{
		REX::INFO("[IIF-CB-Provider-Smoke] {}", message ? message : "(null log message)");
	}

	void RunProbe() noexcept
	{
		if (g_ran) return;
		g_ran = true;
		REX::INFO("[IIF-CB-Provider-Smoke] plugin_version=1 runtime=Fallout 4 OG 1.10.163");
		REX::INFO("[IIF-CB-Provider-Smoke] F4SE kPostLoad reached");

		const auto iifModule = GetModuleHandleW(L"ItemIntegrationFramework.dll");
		if (!iifModule) {
			REX::ERROR("[IIF-CB-Provider-Smoke] IIF module not loaded; stopping without registration");
			return;
		}
		REX::INFO("[IIF-CB-Provider-Smoke] IIF module handle found");

		const auto query = Resolve<QueryInterfaceFn>(iifModule, "IIF_CombatBus_QueryInterface");
		if (!query) {
			REX::ERROR("[IIF-CB-Provider-Smoke] QueryInterface export missing; stopping");
			return;
		}
		REX::INFO("[IIF-CB-Provider-Smoke] QueryInterface export found");

		IIF_CB_InterfaceV3 wrongVersion{};
		wrongVersion.struct_size = sizeof(wrongVersion);
		wrongVersion.version = IIF_CB_VERSION_3;
		const auto versionStatus = query(IIF_CB_VERSION_3 + 1,
			static_cast<std::uint32_t>(sizeof(wrongVersion)), &wrongVersion);
		const bool versionRejected = versionStatus == IIF_CB_STATUS_UNSUPPORTED_VERSION &&
			wrongVersion.registry == nullptr;
		REX::INFO("[IIF-CB-Provider-Smoke] unsupported version status={}, pass={}",
			versionStatus, versionRejected);
		if (!versionRejected) {
			REX::ERROR("[IIF-CB-Provider-Smoke] version negotiation failed; stopping");
			return;
		}

		IIF_CB_InterfaceV3 wrongSize{};
		wrongSize.struct_size = sizeof(wrongSize);
		wrongSize.version = IIF_CB_VERSION_3;
		const auto sizeStatus = query(IIF_CB_VERSION_3,
			static_cast<std::uint32_t>(sizeof(wrongSize) - sizeof(std::uint32_t)), &wrongSize);
		const bool sizeRejected = sizeStatus == IIF_CB_STATUS_INVALID_STRUCT_SIZE;
		REX::INFO("[IIF-CB-Provider-Smoke] wrong caller size status={}, pass={}",
			sizeStatus, sizeRejected);
		if (!sizeRejected) {
			REX::ERROR("[IIF-CB-Provider-Smoke] interface size negotiation failed; stopping");
			return;
		}

		IIF_CB_InterfaceV3 api{};
		api.struct_size = sizeof(api);
		api.version = IIF_CB_VERSION_3;
		const auto queryStatus = query(IIF_CB_VERSION_3,
			static_cast<std::uint32_t>(sizeof(api)), &api);
		const bool tableReady = queryStatus == IIF_CB_STATUS_OK && api.struct_size == sizeof(api) &&
			api.version == IIF_CB_VERSION_3 && api.registry && api.register_outgoing &&
			api.register_incoming && api.unregister_provider && api.wait_provider_quiescent;
		REX::INFO("[IIF-CB-Provider-Smoke] V3 QueryInterface api_status={}, table_size={}, version={}, pass={}",
			queryStatus, api.struct_size, api.version, tableReady);
		if (!tableReady) {
			REX::ERROR("[IIF-CB-Provider-Smoke] invalid V3 table; stopping before registration");
			return;
		}

		(void) RunProviderLifecycleScenario(api, &LogMessage);
	}

	void F4SEAPI OnF4SEMessage(F4SE::MessagingInterface::Message* message)
	{
		if (message && message->type == F4SE::MessagingInterface::kPostLoad) RunProbe();
	}
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(
	const F4SE::QueryInterface* f4se, F4SE::PluginInfo* info)
{
	if (!f4se || !info || f4se->IsEditor() || f4se->RuntimeVersion() != F4SE::RUNTIME_1_10_163) return false;
	info->infoVersion = F4SE::PluginInfo::kVersion;
	info->name = "IIFCombatBusProviderLifecycleSmoke";
	info->version = 1;
	return true;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* f4se)
{
	if (!f4se || f4se->RuntimeVersion() != F4SE::RUNTIME_1_10_163) {
		return false;
	}
	F4SE::InitInfo init{};
	init.logName = "IIFCombatBusProviderLifecycleSmoke";
	init.logLevel = REX::ELogLevel::Info;
	init.hook = false;
	init.trampoline = false;
	F4SE::Init(f4se, init);
	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnF4SEMessage)) {
		REX::ERROR("[IIF-CB-Provider-Smoke] failed to register F4SE messaging listener");
		return false;
	}
	REX::INFO("[IIF-CB-Provider-Smoke] loaded; waiting for F4SE kPostLoad");
	return true;
}
