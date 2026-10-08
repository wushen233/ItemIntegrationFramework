#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "CombatBusCABI.h"

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

	bool InterfaceReady(const IIF_CB_InterfaceV3& api) noexcept
	{
		return api.struct_size == sizeof(api) && api.version == IIF_CB_VERSION_3 && api.registry &&
			api.register_outgoing && api.register_incoming && api.unregister_provider &&
			api.wait_provider_quiescent && api.dispatch_outgoing && api.dispatch_incoming;
	}

	void RunProbe() noexcept
	{
		if (g_ran) return;
		g_ran = true;

		const auto iifModule = GetModuleHandleW(L"ItemIntegrationFramework.dll");
		if (!iifModule) {
			REX::ERROR("[IIF-CB-Smoke] IIF module was not loaded at F4SE kPostLoad");
			return;
		}
		const auto query = Resolve<QueryInterfaceFn>(iifModule, "IIF_CombatBus_QueryInterface");
		if (!query) {
			REX::ERROR("[IIF-CB-Smoke] GetProcAddress could not find IIF_CombatBus_QueryInterface");
			return;
		}

		IIF_CB_InterfaceV3 wrongVersion{};
		wrongVersion.struct_size = sizeof(wrongVersion);
		wrongVersion.version = IIF_CB_VERSION_3;
		const auto wrongVersionStatus = query(IIF_CB_VERSION_3 + 1,
			static_cast<std::uint32_t>(sizeof(wrongVersion)), &wrongVersion);
		const bool rejectedVersion = wrongVersionStatus == IIF_CB_STATUS_UNSUPPORTED_VERSION &&
			wrongVersion.registry == nullptr && wrongVersion.dispatch_outgoing == nullptr;
		REX::INFO("[IIF-CB-Smoke] unsupported-version negotiation: status={}, pass={}",
			wrongVersionStatus, rejectedVersion);

		IIF_CB_InterfaceV3 wrongCallerSize{};
		wrongCallerSize.struct_size = sizeof(wrongCallerSize);
		wrongCallerSize.version = IIF_CB_VERSION_3;
		const auto callerSizeStatus = query(IIF_CB_VERSION_3,
			static_cast<std::uint32_t>(sizeof(wrongCallerSize) - sizeof(std::uint32_t)), &wrongCallerSize);
		const bool rejectedCallerSize = callerSizeStatus == IIF_CB_STATUS_INVALID_STRUCT_SIZE;
		REX::INFO("[IIF-CB-Smoke] caller-size negotiation: status={}, pass={}",
			callerSizeStatus, rejectedCallerSize);

		IIF_CB_InterfaceV3 wrongEmbeddedSize{};
		wrongEmbeddedSize.struct_size = static_cast<std::uint32_t>(sizeof(wrongEmbeddedSize) - sizeof(std::uint32_t));
		wrongEmbeddedSize.version = IIF_CB_VERSION_3;
		const auto embeddedSizeStatus = query(IIF_CB_VERSION_3,
			static_cast<std::uint32_t>(sizeof(wrongEmbeddedSize)), &wrongEmbeddedSize);
		const bool rejectedEmbeddedSize = embeddedSizeStatus == IIF_CB_STATUS_INVALID_STRUCT_SIZE;
		REX::INFO("[IIF-CB-Smoke] embedded-size negotiation: status={}, pass={}",
			embeddedSizeStatus, rejectedEmbeddedSize);

		IIF_CB_InterfaceV3 api{};
		api.struct_size = sizeof(api);
		api.version = IIF_CB_VERSION_3;
		const auto queryStatus = query(IIF_CB_VERSION_3, static_cast<std::uint32_t>(sizeof(api)), &api);
		if (queryStatus != IIF_CB_STATUS_OK || !InterfaceReady(api)) {
			REX::ERROR("[IIF-CB-Smoke] V3 query failed: status={}, complete_table={}",
				queryStatus, InterfaceReady(api));
			return;
		}
		REX::INFO("[IIF-CB-Smoke] V3 QueryInterface succeeded; table_size={}, version={}",
			api.struct_size, api.version);

		// These addresses are opaque, local sentinels. Dispatch must not dereference them.
		std::uint8_t opaqueStorage{};
		IIF_CB_OutgoingContextV3 outgoing{};
		outgoing.struct_size = sizeof(outgoing);
		outgoing.version = IIF_CB_VERSION_3;
		outgoing.evaluation_kind = IIF_CB_EVALUATION_CALCULATION_KIND;
		outgoing.attacker_kind = IIF_CB_ACTOR_PLAYER;
		outgoing.profile = IIF_CB_PROFILE_WEAPON_MELEE;
		outgoing.modifiable_mask = IIF_CB_COMPONENT_HEALTH;
		outgoing.attacker = &opaqueStorage;
		outgoing.weapon = &opaqueStorage;
		outgoing.damage = { sizeof(IIF_CB_DamageSnapshotV3), IIF_CB_COMPONENT_HEALTH,
			37.5f, 0.0f, 0.0f, 0.0f, 0.0f };
		IIF_CB_OutgoingDispatchV3 outgoingResult{};
		outgoingResult.struct_size = sizeof(outgoingResult);
		outgoingResult.version = IIF_CB_VERSION_3;
		const auto outgoingCallStatus = api.dispatch_outgoing(api.registry, &outgoing, &outgoingResult);
		const bool outgoingIdentity = outgoingCallStatus == IIF_CB_STATUS_OK &&
			outgoingResult.status == IIF_CB_DISPATCH_NO_PROVIDERS &&
			outgoingResult.damage.valid_mask == IIF_CB_COMPONENT_HEALTH &&
			outgoingResult.damage.health_damage == outgoing.damage.health_damage;
		REX::INFO("[IIF-CB-Smoke] empty-Registry Outgoing identity: call_status={}, dispatch_status={}, health={}, pass={}",
			outgoingCallStatus, outgoingResult.status, outgoingResult.damage.health_damage, outgoingIdentity);

		// A valid Incoming dispatch requires actual VerifiedAdapterCallsite evidence.
		// This diagnostic plugin has no native adapter and deliberately does not forge it.
		IIF_CB_IncomingContextV3 incoming{};
		incoming.struct_size = sizeof(incoming);
		incoming.version = IIF_CB_VERSION_3;
		incoming.phase = IIF_CB_INCOMING_HEALTH_AFTER_RESISTANCE_BEFORE_DIFFICULTY;
		incoming.confidence = IIF_CB_CONFIDENCE_UNKNOWN;
		incoming.target_kind = IIF_CB_ACTOR_PLAYER;
		incoming.power_armor = IIF_CB_POWER_ARMOR_EQUIPPED;
		incoming.target = &opaqueStorage;
		incoming.health_damage = 53.25f;
		IIF_CB_IncomingDispatchV3 incomingResult{};
		incomingResult.struct_size = sizeof(incomingResult);
		incomingResult.version = IIF_CB_VERSION_3;
		const auto incomingCallStatus = api.dispatch_incoming(api.registry, &incoming, &incomingResult);
		const bool incomingRejected = incomingCallStatus == IIF_CB_STATUS_OK &&
			incomingResult.status == IIF_CB_DISPATCH_INVALID_CONTEXT &&
			incomingResult.health_damage == incoming.health_damage;
		REX::INFO("[IIF-CB-Smoke] unverified Incoming context rejected without changing value: call_status={}, dispatch_status={}, health={}, pass={}",
			incomingCallStatus, incomingResult.status, incomingResult.health_damage, incomingRejected);
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
	info->name = "IIFCombatBusRuntimeSmoke";
	info->version = 1;
	return true;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* f4se)
{
	if (!f4se) return false;
	F4SE::InitInfo init{};
	init.logName = "IIFCombatBusRuntimeSmoke";
	init.logLevel = REX::ELogLevel::Info;
	init.hook = false;
	init.trampoline = false;
	F4SE::Init(f4se, init);
	const auto messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnF4SEMessage)) {
		REX::ERROR("[IIF-CB-Smoke] failed to register F4SE messaging listener");
		return false;
	}
	REX::INFO("[IIF-CB-Smoke] loaded; waiting for F4SE kPostLoad");
	return true;
}
