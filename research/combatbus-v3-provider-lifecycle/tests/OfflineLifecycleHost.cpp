#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "ProviderLifecycleScenario.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
	using QueryInterfaceFn = std::uint32_t(IIF_CB_CALL*)(std::uint32_t, std::uint32_t,
		IIF_CB_InterfaceV3*);

	void LogLine(const char* message)
	{
		std::cout << (message ? message : "(null)") << '\n';
	}

	int Fail(const char* message)
	{
		std::cerr << "FAIL: " << message << '\n';
		return 1;
	}
}

int wmain(int argc, wchar_t** argv)
{
	static_assert(sizeof(void*) == 8, "This lifecycle ABI fixture requires x64");
	if (argc != 2) return Fail("expected the isolated production-contract Host DLL path");

	const auto module = LoadLibraryW(argv[1]);
	if (!module) return Fail("LoadLibraryW failed for lifecycle Host DLL");
	const auto exportAddress = GetProcAddress(module, "IIF_CombatBus_QueryInterface");
	if (!exportAddress) return Fail("IIF_CombatBus_QueryInterface export missing");
	if (GetProcAddress(module, "IIF_CombatBus_Shutdown") ||
		GetProcAddress(module, "IIF_CombatBus_Test_SetQuiescenceClaimHook") ||
		GetProcAddress(module, "IIF_CombatBus_Test_FailNextQuiescenceWait") ||
		GetProcAddress(module, "IIF_CombatBus_Test_FailNextRegistrationAfterBridgeLink") ||
		GetProcAddress(module, "IIF_CombatBus_Test_GetBridgeCount")) {
		return Fail("production-contract Host DLL exposed a forbidden lifecycle/test export");
	}

	QueryInterfaceFn query{};
	static_assert(sizeof(query) == sizeof(exportAddress));
	std::memcpy(&query, &exportAddress, sizeof(query));
	if (!query) return Fail("QueryInterface pointer conversion failed");

	IIF_CB_InterfaceV3 wrongVersion{};
	wrongVersion.struct_size = sizeof(wrongVersion);
	wrongVersion.version = IIF_CB_VERSION_3;
	const auto wrongVersionStatus = query(IIF_CB_VERSION_3 + 1,
		static_cast<std::uint32_t>(sizeof(wrongVersion)), &wrongVersion);
	if (wrongVersionStatus != IIF_CB_STATUS_UNSUPPORTED_VERSION || wrongVersion.registry) {
		return Fail("unsupported ABI version was not rejected safely");
	}

	IIF_CB_InterfaceV3 wrongSize{};
	wrongSize.struct_size = sizeof(wrongSize);
	wrongSize.version = IIF_CB_VERSION_3;
	const auto wrongSizeStatus = query(IIF_CB_VERSION_3,
		static_cast<std::uint32_t>(sizeof(wrongSize) - sizeof(std::uint32_t)), &wrongSize);
	if (wrongSizeStatus != IIF_CB_STATUS_INVALID_STRUCT_SIZE) {
		return Fail("incorrect caller interface size was not rejected");
	}

	IIF_CB_InterfaceV3 api{};
	api.struct_size = sizeof(api);
	api.version = IIF_CB_VERSION_3;
	const auto queryStatus = query(IIF_CB_VERSION_3,
		static_cast<std::uint32_t>(sizeof(api)), &api);
	if (queryStatus != IIF_CB_STATUS_OK || api.struct_size != sizeof(api) ||
		api.version != IIF_CB_VERSION_3 || !api.registry || !api.register_outgoing ||
		!api.register_incoming || !api.unregister_provider || !api.wait_provider_quiescent) {
		return Fail("valid V3 lifecycle interface query failed");
	}

	if (!RunProviderLifecycleScenario(api, &LogLine)) {
		return Fail("Provider lifecycle scenario returned failure");
	}

	std::cout << "PASS: isolated cross-DLL Provider lifecycle; Dispatch was not invoked\n";
	// Intentionally do not call FreeLibrary. Both modules remain loaded until
	// process termination; the test models no F4SE plugin unload behavior.
	return 0;
}
