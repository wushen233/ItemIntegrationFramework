#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "CombatBusCABI.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
	using QueryFn = std::uint32_t (IIF_CB_CALL *)(std::uint32_t, std::uint32_t, IIF_CB_InterfaceV3*);

	int Fail(const char* message)
	{
		std::cerr << "FAIL: " << message << '\n';
		return 1;
	}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc != 2) return Fail("expected production-contract DLL path");
	HMODULE module = LoadLibraryW(argv[1]);
	if (!module) return Fail("LoadLibraryW failed");

	const FARPROC queryExport = GetProcAddress(module, "IIF_CombatBus_QueryInterface");
	if (!queryExport) {
		FreeLibrary(module);
		return Fail("required QueryInterface export missing");
	}
	static_assert(sizeof(QueryFn) == sizeof(FARPROC));
	QueryFn query{};
	std::memcpy(&query, &queryExport, sizeof(query));
	if (!query) return Fail("required QueryInterface export missing");
	if (GetProcAddress(module, "IIF_CombatBus_Shutdown")) {
		return Fail("production DLL exposes Provider-accessible Host Shutdown");
	}
	if (GetProcAddress(module, "IIF_CombatBus_Test_SetQuiescenceClaimHook") ||
		GetProcAddress(module, "IIF_CombatBus_Test_FailNextQuiescenceWait")) {
		return Fail("production DLL exposes test-only exports");
	}

	IIF_CB_InterfaceV3 api{};
	api.struct_size = sizeof(api);
	if (query(IIF_CB_VERSION_3, sizeof(api), &api) != IIF_CB_STATUS_OK ||
		api.version != IIF_CB_VERSION_3 || api.struct_size != sizeof(api) || !api.registry ||
		!api.register_outgoing || !api.register_incoming || !api.unregister_provider ||
		!api.wait_provider_quiescent || !api.dispatch_outgoing || !api.dispatch_incoming) {
		return Fail("V3 interface query returned an invalid table");
	}

	IIF_CB_InterfaceV3 unsupported{};
	unsupported.struct_size = sizeof(unsupported);
	if (query(IIF_CB_VERSION_3 + 1, sizeof(unsupported), &unsupported) !=
		IIF_CB_STATUS_UNSUPPORTED_VERSION) {
		return Fail("unsupported version was not rejected");
	}

	IIF_CB_OutgoingContextV3 outgoing{};
	outgoing.struct_size = sizeof(outgoing);
	outgoing.version = IIF_CB_VERSION_3;
	outgoing.evaluation_kind = IIF_CB_EVALUATION_CALCULATION_KIND;
	outgoing.attacker_kind = IIF_CB_ACTOR_PLAYER;
	outgoing.profile = IIF_CB_PROFILE_WEAPON_MELEE;
	outgoing.modifiable_mask = IIF_CB_COMPONENT_HEALTH;
	outgoing.attacker = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
	outgoing.weapon = reinterpret_cast<void*>(static_cast<std::uintptr_t>(2));
	outgoing.damage = { sizeof(IIF_CB_DamageSnapshotV3), IIF_CB_COMPONENT_HEALTH, 17.0f, 0.0f,
		0.0f, 0.0f, 0.0f };
	IIF_CB_OutgoingDispatchV3 outgoingResult{};
	outgoingResult.struct_size = sizeof(outgoingResult);
	outgoingResult.version = IIF_CB_VERSION_3;
	if (api.dispatch_outgoing(api.registry, &outgoing, &outgoingResult) != IIF_CB_STATUS_OK ||
		outgoingResult.status != IIF_CB_DISPATCH_NO_PROVIDERS ||
		outgoingResult.damage.health_damage != 17.0f) {
		return Fail("empty production Registry changed Outgoing damage");
	}

	IIF_CB_IncomingContextV3 incoming{};
	incoming.struct_size = sizeof(incoming);
	incoming.version = IIF_CB_VERSION_3;
	incoming.phase = IIF_CB_INCOMING_HEALTH_AFTER_RESISTANCE_BEFORE_DIFFICULTY;
	incoming.confidence = IIF_CB_CONFIDENCE_VERIFIED_ADAPTER_CALLSITE;
	incoming.target_kind = IIF_CB_ACTOR_NPC;
	incoming.power_armor = IIF_CB_POWER_ARMOR_EQUIPPED;
	incoming.target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(3));
	incoming.health_damage = 23.0f;
	IIF_CB_IncomingDispatchV3 incomingResult{};
	incomingResult.struct_size = sizeof(incomingResult);
	incomingResult.version = IIF_CB_VERSION_3;
	if (api.dispatch_incoming(api.registry, &incoming, &incomingResult) != IIF_CB_STATUS_OK ||
		incomingResult.status != IIF_CB_DISPATCH_NO_PROVIDERS || incomingResult.health_damage != 23.0f) {
		return Fail("empty production Registry changed Incoming Health damage");
	}

	std::cout << "PASS: production V3 query/version checks, export surface, and empty-registry identity\n";
	return 0;
}
