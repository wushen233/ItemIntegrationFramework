#include "CombatBusCABI.h"
#include "MockHostTestHooks.h"
#include "StartupTrace.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <thread>

extern "C" int CAbiHeaderSmoke(void);

namespace {
	using QueryFn = std::uint32_t (IIF_CB_CALL *)(std::uint32_t, std::uint32_t, IIF_CB_InterfaceV3*);
	using ShutdownFn = std::uint32_t (IIF_CB_CALL *)();
	using GetOutgoingFn = std::uint32_t (IIF_CB_CALL *)(IIF_CB_OutgoingProviderV3*);
	using GetIncomingFn = std::uint32_t (IIF_CB_CALL *)(IIF_CB_IncomingProviderV3*);
	using ResetFn = void (IIF_CB_CALL *)();
	using BlockFn = void (IIF_CB_CALL *)();
	using WaitEnteredFn = std::uint32_t (IIF_CB_CALL *)(std::uint32_t);
	using ReleaseFn = void (IIF_CB_CALL *)();
	using CounterFn = std::uint32_t (IIF_CB_CALL *)(std::uint32_t);
	using ObservedFn = float (IIF_CB_CALL *)(std::uint32_t, std::uint32_t);
	using OrderFn = std::uint32_t (IIF_CB_CALL *)(std::uint32_t);
	using SetShutdownFn = void (IIF_CB_CALL *)(IIF_CB_ShutdownFn);
	using TriggerShutdownFn = void (IIF_CB_CALL *)();
	using CallbackShutdownStatusFn = std::uint32_t (IIF_CB_CALL *)();
	using SetQuiescenceHookFn = void (IIF_CB_CALL *)(IIF_CB_TestQuiescenceClaimHook, void*);
	using FailNextWaitFn = void (IIF_CB_CALL *)();

	struct WaitTargetState {
		const IIF_CB_InterfaceV3* interfaceV3{};
		void* registry{};
		IIF_CB_ProviderHandleV3 handle{};
		std::uint32_t stage{};
		std::atomic<std::uint32_t> status{ IIF_CB_STATUS_INTERNAL_ERROR };
	};

	struct ClaimGate {
		HANDLE entered{};
		HANDLE release{};
	};

	void IIF_CB_CALL PauseAfterClaim(void* opaque) noexcept
	{
		auto& gate = *static_cast<ClaimGate*>(opaque);
		SetEvent(gate.entered);
		(void)WaitForSingleObject(gate.release, INFINITE);
	}

	std::uint32_t IIF_CB_CALL WaitForTargetFromProvider(void* opaque,
		const IIF_CB_OutgoingContextV3*, IIF_CB_OutgoingResultV3* result)
	{
		auto& target = *static_cast<WaitTargetState*>(opaque);
		target.status.store(target.interfaceV3->wait_provider_quiescent(target.registry, target.handle,
			target.stage), std::memory_order_release);
		result->status = IIF_CB_CALLBACK_NO_CHANGE;
		result->component_mask = 0;
		result->multiplier = 1.0f;
		return IIF_CB_CALLBACK_NO_CHANGE;
	}

	int failures{};

	void Check(bool condition, const char* message)
	{
		if (!condition) {
			++failures;
			std::cerr << "FAIL: " << message << '\n';
		}
	}

	template <class T>
	T Resolve(HMODULE module, const char* name)
	{
		const auto address = GetProcAddress(module, name);
		T function{};
		static_assert(sizeof(function) == sizeof(address));
		std::memcpy(&function, &address, sizeof(function));
		return function;
	}

	template <class T>
	bool ResolveRequired(HMODULE module, const char* name, T& function, const char*& missingExport)
	{
		function = Resolve<T>(module, name);
		if (function) return true;
		missingExport = name;
		std::cerr << "ERROR: missing required export '" << name << "'\n";
		return false;
	}

	IIF_CB_OutgoingContextV3 MakeOutgoing()
	{
		IIF_CB_OutgoingContextV3 context{};
		context.struct_size = sizeof(context);
		context.version = IIF_CB_VERSION_3;
		context.evaluation_kind = IIF_CB_EVALUATION_CALCULATION_KIND;
		context.attacker_kind = IIF_CB_ACTOR_PLAYER;
		context.profile = IIF_CB_PROFILE_WEAPON_DIRECT;
		context.modifiable_mask = IIF_CB_COMPONENT_HEALTH | IIF_CB_COMPONENT_PHYSICAL;
		context.attacker = reinterpret_cast<void*>(std::uintptr_t{ 0x1110 });
		context.target = reinterpret_cast<void*>(std::uintptr_t{ 0x2220 });
		context.weapon = reinterpret_cast<void*>(std::uintptr_t{ 0x3330 });
		context.damage = { sizeof(IIF_CB_DamageSnapshotV3),
			IIF_CB_COMPONENT_HEALTH | IIF_CB_COMPONENT_PHYSICAL | IIF_CB_COMPONENT_TOTAL,
			100.0f, 80.0f, 180.0f, 12.0f, 7.0f };
		return context;
	}

	IIF_CB_OutgoingDispatchV3 MakeOutgoingResult()
	{
		IIF_CB_OutgoingDispatchV3 result{};
		result.struct_size = sizeof(result);
		result.version = IIF_CB_VERSION_3;
		return result;
	}

	IIF_CB_IncomingContextV3 MakeIncoming(std::uint32_t actorKind)
	{
		IIF_CB_IncomingContextV3 context{};
		context.struct_size = sizeof(context);
		context.version = IIF_CB_VERSION_3;
		context.phase = IIF_CB_INCOMING_HEALTH_AFTER_RESISTANCE_BEFORE_DIFFICULTY;
		context.confidence = IIF_CB_CONFIDENCE_VERIFIED_ADAPTER_CALLSITE;
		context.target_kind = actorKind;
		context.power_armor = IIF_CB_POWER_ARMOR_EQUIPPED;
		context.attacker = reinterpret_cast<void*>(std::uintptr_t{ 0x4440 });
		context.target = reinterpret_cast<void*>(std::uintptr_t{ 0x5550 });
		context.weapon = reinterpret_cast<void*>(std::uintptr_t{ 0x6660 });
		context.health_damage = 100.0f;
		return context;
	}

	IIF_CB_IncomingDispatchV3 MakeIncomingResult()
	{
		IIF_CB_IncomingDispatchV3 result{};
		result.struct_size = sizeof(result);
		result.version = IIF_CB_VERSION_3;
		return result;
	}

	bool Near(float left, float right)
	{
		return std::fabs(left - right) < 0.0001f;
	}

	bool ModulePathMatches(HMODULE module, const wchar_t* expectedPath)
	{
		wchar_t actualPath[MAX_PATH]{};
		wchar_t fullExpectedPath[MAX_PATH]{};
		const DWORD actualLength = GetModuleFileNameW(module, actualPath, MAX_PATH);
		const DWORD expectedLength = GetFullPathNameW(expectedPath, MAX_PATH,
			fullExpectedPath, nullptr);
		return actualLength != 0 && actualLength < MAX_PATH && expectedLength != 0 &&
			expectedLength < MAX_PATH && _wcsicmp(actualPath, fullExpectedPath) == 0;
	}

	bool InterfaceTableReady(const IIF_CB_InterfaceV3& table)
	{
		return table.struct_size == sizeof(table) && table.version == IIF_CB_VERSION_3 && table.registry &&
			table.register_outgoing && table.register_incoming && table.unregister_provider &&
			table.wait_provider_quiescent && table.dispatch_outgoing && table.dispatch_incoming;
	}

	bool ShutdownAndUnload(HMODULE hostModule, ShutdownFn shutdown, HMODULE providerModule)
	{
		if (!shutdown) {
			std::cerr << "ERROR: cannot drain Host because Shutdown export is unavailable; keeping modules loaded\n";
			return false;
		}
		const auto status = shutdown();
		if (status != IIF_CB_STATUS_OK) {
			std::cerr << "ERROR: Host Shutdown failed with status " << status
				<< "; keeping modules loaded to avoid unsafe unload\n";
			return false;
		}
		bool unloaded = true;
		if (providerModule && !FreeLibrary(providerModule)) {
			std::cerr << "ERROR: FreeLibrary failed for Provider after successful Host Shutdown\n";
			unloaded = false;
		}
		if (hostModule && !FreeLibrary(hostModule)) {
			std::cerr << "ERROR: FreeLibrary failed for Host after successful Shutdown\n";
			unloaded = false;
		}
		return unloaded;
	}

	bool Run(const wchar_t* hostPath, const wchar_t* providerPath,
		const wchar_t* reloadProviderPath = nullptr, const wchar_t* expectedMissingReloadExport = nullptr)
	{
		Check(CAbiHeaderSmoke() == 1, "C compiler accepted the shared public ABI header and Win64 layout assertions");
		Check(combatbus_test::WriteStartupMarker("host.before_host_dll_load"),
			"startup marker records immediately before Host DLL load");
		HMODULE hostModule = LoadLibraryW(hostPath);
		Check(hostModule != nullptr, "LoadLibrary loaded the simulated IIF Host DLL");
		if (!hostModule) return false;
		Check(combatbus_test::WriteStartupMarker("host.host_dll_loaded"),
			"startup marker records Host DLL load completion");
		if (!ModulePathMatches(hostModule, hostPath)) {
			++failures;
			std::cerr << "FAIL: Host HMODULE does not resolve to the requested absolute DLL path\n";
			FreeLibrary(hostModule);
			return false;
		}
		QueryFn query{};
		ShutdownFn shutdown{};
		const char* missingHostExport{};
		if (!ResolveRequired(hostModule, "IIF_CombatBus_QueryInterface", query, missingHostExport) ||
			!ResolveRequired(hostModule, "IIF_CombatBus_Shutdown", shutdown, missingHostExport)) {
			FreeLibrary(hostModule);
			return false;
		}

		IIF_CB_InterfaceV3 interfaceV3{};
		interfaceV3.struct_size = sizeof(interfaceV3);
		interfaceV3.version = IIF_CB_VERSION_3;
		Check(query(99, sizeof(interfaceV3), &interfaceV3) == IIF_CB_STATUS_UNSUPPORTED_VERSION,
			"unsupported interface version fails with a stable status");
		Check(interfaceV3.registry == nullptr && interfaceV3.register_outgoing == nullptr,
			"failed version negotiation clears stale interface pointers");
		Check(query(IIF_CB_VERSION_3, sizeof(interfaceV3) - 4, &interfaceV3) == IIF_CB_STATUS_INVALID_STRUCT_SIZE,
			"caller-provided interface size mismatch is rejected");
		interfaceV3.struct_size = sizeof(interfaceV3) - 4;
		Check(query(IIF_CB_VERSION_3, sizeof(interfaceV3), &interfaceV3) == IIF_CB_STATUS_INVALID_STRUCT_SIZE,
			"interface struct_size field mismatch is rejected");
		interfaceV3 = {};
		interfaceV3.struct_size = sizeof(interfaceV3);
		interfaceV3.version = IIF_CB_VERSION_3;
		const auto queryStatus = query(IIF_CB_VERSION_3, sizeof(interfaceV3), &interfaceV3);
		if (queryStatus != IIF_CB_STATUS_OK || !InterfaceTableReady(interfaceV3)) {
			++failures;
			std::cerr << "FAIL: QueryInterface did not return a complete V3 function table; status="
				<< queryStatus << '\n';
			(void)ShutdownAndUnload(hostModule, shutdown, nullptr);
			return false;
		}
		Check(true, "supported V3 negotiation copies a complete Host-owned function table");

		Check(combatbus_test::WriteStartupMarker("host.before_provider_dll_load"),
			"startup marker records immediately before initial Provider DLL load");
		HMODULE providerModule = LoadLibraryW(providerPath);
		Check(providerModule != nullptr, "LoadLibrary loaded an independent Provider DLL");
		if (!providerModule) {
			(void)ShutdownAndUnload(hostModule, shutdown, nullptr);
			return false;
		}
		Check(combatbus_test::WriteStartupMarker("host.provider_dll_loaded"),
			"startup marker records initial Provider DLL load completion");
		if (!ModulePathMatches(providerModule, providerPath)) {
			++failures;
			std::cerr << "FAIL: initial Provider HMODULE does not resolve to the requested absolute DLL path\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		GetOutgoingFn getWRF{};
		GetOutgoingFn getCSF{};
		GetIncomingFn getPAS{};
		GetOutgoingFn getInvalid{};
		ResetFn resetProvider{};
		BlockFn blockWRF{};
		WaitEnteredFn waitWRFEntered{};
		ReleaseFn releaseWRF{};
		CounterFn getCounter{};
		ObservedFn getObserved{};
		OrderFn getOrder{};
		SetShutdownFn setShutdown{};
		TriggerShutdownFn triggerShutdown{};
		CallbackShutdownStatusFn getCallbackShutdownStatus{};
		const char* missingProviderExport{};
		const bool initialProviderExportsResolved =
			ResolveRequired(providerModule, "TestProvider_GetWRF", getWRF, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetCSF", getCSF, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetPAS", getPAS, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetInvalid", getInvalid, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_Reset", resetProvider, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_BlockWRF", blockWRF, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_WaitWRFEntered", waitWRFEntered, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_ReleaseWRF", releaseWRF, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetCounter", getCounter, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetObserved", getObserved, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetOrder", getOrder, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_SetShutdown", setShutdown, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_TriggerShutdown", triggerShutdown, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetCallbackShutdownStatus",
				getCallbackShutdownStatus, missingProviderExport);
		SetQuiescenceHookFn setQuiescenceHook{};
		FailNextWaitFn failNextWait{};
		const bool hostTestHooksResolved =
			ResolveRequired(hostModule, "IIF_CombatBus_Test_SetQuiescenceClaimHook",
				setQuiescenceHook, missingHostExport) &&
			ResolveRequired(hostModule, "IIF_CombatBus_Test_FailNextQuiescenceWait",
				failNextWait, missingHostExport);
		if (!initialProviderExportsResolved || !hostTestHooksResolved) {
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		resetProvider();
		setShutdown(shutdown);

		IIF_CB_OutgoingProviderV3 wrf{};
		wrf.struct_size = sizeof(wrf); wrf.version = IIF_CB_VERSION_3;
		IIF_CB_OutgoingProviderV3 csf{};
		csf.struct_size = sizeof(csf); csf.version = IIF_CB_VERSION_3;
		IIF_CB_IncomingProviderV3 pas{};
		pas.struct_size = sizeof(pas); pas.version = IIF_CB_VERSION_3;
		const auto wrfDescriptorStatus = getWRF(&wrf);
		const auto csfDescriptorStatus = getCSF(&csf);
		const auto pasDescriptorStatus = getPAS(&pas);
		if (wrfDescriptorStatus != IIF_CB_STATUS_OK || csfDescriptorStatus != IIF_CB_STATUS_OK ||
			pasDescriptorStatus != IIF_CB_STATUS_OK) {
			++failures;
			std::cerr << "FAIL: Provider descriptor status failure (WRF=" << wrfDescriptorStatus
				<< ", CSF=" << csfDescriptorStatus << ", PAS=" << pasDescriptorStatus << ")\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}

		IIF_CB_RegistrationV3 wrfRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
		IIF_CB_RegistrationV3 csfRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
		IIF_CB_RegistrationV3 pasRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
		IIF_CB_OutgoingProviderV3 wrongVersion = wrf;
		wrongVersion.version = 77;
		IIF_CB_RegistrationV3 invalidRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
		Check(interfaceV3.register_outgoing(interfaceV3.registry, &wrongVersion, &invalidRegistration) ==
			IIF_CB_STATUS_UNSUPPORTED_VERSION && invalidRegistration.status == IIF_CB_STATUS_UNSUPPORTED_VERSION &&
			invalidRegistration.added == 0,
			"unsupported provider version fails closed before registration");
		invalidRegistration.struct_size -= 4;
		Check(interfaceV3.register_outgoing(interfaceV3.registry, &wrf, &invalidRegistration) ==
			IIF_CB_STATUS_INVALID_STRUCT_SIZE,
			"registration output with wrong size fails closed");
		wrfRegistration.struct_size = sizeof(wrfRegistration);
		const auto wrfRegistrationStatus =
			interfaceV3.register_outgoing(interfaceV3.registry, &wrf, &wrfRegistration);
		const auto csfRegistrationStatus =
			interfaceV3.register_outgoing(interfaceV3.registry, &csf, &csfRegistration);
		const auto pasRegistrationStatus =
			interfaceV3.register_incoming(interfaceV3.registry, &pas, &pasRegistration);
		const bool registrationsValid = wrfRegistrationStatus == IIF_CB_STATUS_OK &&
			wrfRegistration.status == IIF_CB_STATUS_OK && wrfRegistration.added == 1 &&
			wrfRegistration.handle.value != 0 && csfRegistrationStatus == IIF_CB_STATUS_OK &&
			csfRegistration.status == IIF_CB_STATUS_OK && csfRegistration.added == 1 &&
			csfRegistration.handle.value != 0 && csfRegistration.handle.value != wrfRegistration.handle.value &&
			pasRegistrationStatus == IIF_CB_STATUS_OK && pasRegistration.status == IIF_CB_STATUS_OK &&
			pasRegistration.added == 1 && pasRegistration.handle.value != 0 &&
			pasRegistration.handle.value != wrfRegistration.handle.value &&
			pasRegistration.handle.value != csfRegistration.handle.value;
		if (!registrationsValid) {
			++failures;
			std::cerr << "FAIL: one or more Provider registrations returned an invalid status/handle; "
				"draining Host before cleanup\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		Check(interfaceV3.unregister_provider(interfaceV3.registry, wrfRegistration.handle,
			IIF_CB_STAGE_INCOMING_HEALTH) == IIF_CB_STATUS_NOT_FOUND &&
			interfaceV3.wait_provider_quiescent(interfaceV3.registry, wrfRegistration.handle,
				IIF_CB_STAGE_INCOMING_HEALTH) == IIF_CB_STATUS_NOT_FOUND,
			"wrong-stage handle operations cannot unregister or authorize unload of WRF");

		auto outgoing = MakeOutgoing();
		auto outgoingResult = MakeOutgoingResult();
		triggerShutdown();
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) == IIF_CB_STATUS_OK &&
			outgoingResult.status == IIF_CB_DISPATCH_APPLIED &&
			Near(outgoingResult.damage.health_damage, 40.0f) &&
			Near(outgoingResult.damage.physical_damage, 32.0f) &&
			Near(outgoingResult.damage.total_damage, 180.0f),
			"cross-DLL WRF/CSF independent multipliers compose on Health and Physical only");
		Check(getOrder(0) == 100 && getOrder(1) == 200,
			"priority invokes WRF 100 before CSF 200 across modules");
		Check(getCallbackShutdownStatus() == IIF_CB_STATUS_WOULD_DEADLOCK,
			"Provider callback cannot synchronously shut down its own in-flight Host call");
		Check(Near(getObserved(1, 1), 100.0f) && Near(getObserved(2, 1), 100.0f) &&
			Near(getObserved(1, 2), 80.0f) && Near(getObserved(2, 2), 80.0f),
			"both Provider DLL callbacks receive the original input snapshot");

		outgoing.evaluation_kind = IIF_CB_EVALUATION_UNKNOWN;
		outgoingResult = MakeOutgoingResult();
		const auto callsBeforeUnknown = getCounter(1) + getCounter(2);
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) == IIF_CB_STATUS_OK &&
			outgoingResult.status == IIF_CB_DISPATCH_INVALID_CONTEXT &&
			getCounter(1) + getCounter(2) == callsBeforeUnknown,
			"Unknown evaluation fails closed without callbacks");
		outgoing = MakeOutgoing();
		outgoing.evaluation_kind = IIF_CB_EVALUATION_PREDICTION_KIND;
		outgoingResult = MakeOutgoingResult();
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) == IIF_CB_STATUS_OK &&
			outgoingResult.status == IIF_CB_DISPATCH_APPLIED,
			"explicit Prediction opt-in is delivered through the cross-DLL Calculation interface");

		auto incoming = MakeIncoming(IIF_CB_ACTOR_PLAYER);
		auto incomingResult = MakeIncomingResult();
		Check(interfaceV3.dispatch_incoming(interfaceV3.registry, &incoming, &incomingResult) == IIF_CB_STATUS_OK &&
			incomingResult.status == IIF_CB_DISPATCH_APPLIED && Near(incomingResult.health_damage, 75.0f),
			"PAS Provider DLL applies Incoming Health rule to simulated Player power armor");
		incoming = MakeIncoming(IIF_CB_ACTOR_NPC);
		incomingResult = MakeIncomingResult();
		Check(interfaceV3.dispatch_incoming(interfaceV3.registry, &incoming, &incomingResult) == IIF_CB_STATUS_OK &&
			incomingResult.status == IIF_CB_DISPATCH_APPLIED && Near(incomingResult.health_damage, 50.0f),
			"PAS Provider DLL applies Incoming Health rule to simulated NPC power armor");

		IIF_CB_OutgoingProviderV3 invalid{};
		invalid.struct_size = sizeof(invalid); invalid.version = IIF_CB_VERSION_3;
		if (getInvalid(&invalid) != IIF_CB_STATUS_OK) {
			++failures;
			std::cerr << "FAIL: Provider invalid-result descriptor failed\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		IIF_CB_RegistrationV3 invalidProviderRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
		const auto invalidRegistrationStatus = interfaceV3.register_outgoing(interfaceV3.registry,
			&invalid, &invalidProviderRegistration);
		if (invalidRegistrationStatus != IIF_CB_STATUS_OK ||
			invalidProviderRegistration.status != IIF_CB_STATUS_OK ||
			invalidProviderRegistration.added != 1 || invalidProviderRegistration.handle.value == 0) {
			++failures;
			std::cerr << "FAIL: invalid-result Provider registration did not return a usable handle\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		outgoing = MakeOutgoing();
		outgoingResult = MakeOutgoingResult();
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) == IIF_CB_STATUS_OK &&
			outgoingResult.status == IIF_CB_DISPATCH_INVALID_PROVIDER_RESULT &&
			Near(outgoingResult.damage.health_damage, 100.0f),
			"invalid cross-DLL callback status fails closed and restores the original numeric snapshot");
		const auto invalidUnregisterStatus = interfaceV3.unregister_provider(interfaceV3.registry,
			invalidProviderRegistration.handle, IIF_CB_STAGE_OUTGOING_CALCULATION);
		if (invalidUnregisterStatus != IIF_CB_STATUS_OK) {
			++failures;
			std::cerr << "FAIL: invalid-result Provider unregister failed; refusing to test its wait handle\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		failNextWait();
		const auto injectedWaitFailure = interfaceV3.wait_provider_quiescent(interfaceV3.registry,
			invalidProviderRegistration.handle, IIF_CB_STAGE_OUTGOING_CALCULATION);
		if (injectedWaitFailure != IIF_CB_STATUS_WAIT_FAILURE || GetModuleHandleW(providerPath) != providerModule) {
			++failures;
			std::cerr << "FAIL: injected wait did not produce the expected non-unload status\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		const auto retryWaitStatus = interfaceV3.wait_provider_quiescent(interfaceV3.registry,
			invalidProviderRegistration.handle, IIF_CB_STAGE_OUTGOING_CALCULATION);
		if (retryWaitStatus != IIF_CB_STATUS_OK) {
			++failures;
			std::cerr << "FAIL: retry WaitQuiescent failed with status " << retryWaitStatus << '\n';
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}

		blockWRF();
		std::atomic<std::uint32_t> activeDispatchStatus{ IIF_CB_STATUS_INTERNAL_ERROR };
		std::thread callbackThread([&] {
			auto context = MakeOutgoing();
			auto result = MakeOutgoingResult();
			const auto callStatus = interfaceV3.dispatch_outgoing(interfaceV3.registry, &context, &result);
			activeDispatchStatus.store(callStatus == IIF_CB_STATUS_OK ? result.status : callStatus,
				std::memory_order_release);
		});
		const bool callbackEntered = waitWRFEntered(5000) == 1;
		if (!callbackEntered) {
			++failures;
			std::cerr << "FAIL: Provider callback did not enter the controlled blocking section\n";
			releaseWRF();
			callbackThread.join();
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		const auto wrfUnregisterStatus = interfaceV3.unregister_provider(interfaceV3.registry,
			wrfRegistration.handle, IIF_CB_STAGE_OUTGOING_CALCULATION);
		if (wrfUnregisterStatus != IIF_CB_STATUS_OK) {
			++failures;
			std::cerr << "FAIL: active WRF unregister failed with status " << wrfUnregisterStatus << '\n';
			releaseWRF();
			callbackThread.join();
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		HANDLE claimEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE claimRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE waitStarted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE waitFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE secondWaitStarted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE secondWaitFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!claimEntered || !claimRelease || !waitStarted || !waitFinished ||
			!secondWaitStarted || !secondWaitFinished) {
			++failures;
			std::cerr << "FAIL: could not create deterministic quiescence barrier events\n";
			if (claimEntered) CloseHandle(claimEntered);
			if (claimRelease) CloseHandle(claimRelease);
			if (waitStarted) CloseHandle(waitStarted);
			if (waitFinished) CloseHandle(waitFinished);
			if (secondWaitStarted) CloseHandle(secondWaitStarted);
			if (secondWaitFinished) CloseHandle(secondWaitFinished);
			releaseWRF();
			callbackThread.join();
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		std::atomic<std::uint32_t> waitStatus{ IIF_CB_STATUS_INTERNAL_ERROR };
		std::atomic<std::uint32_t> secondWaitStatus{ IIF_CB_STATUS_INTERNAL_ERROR };
		ClaimGate claimGate{ claimEntered, claimRelease };
		setQuiescenceHook(&PauseAfterClaim, &claimGate);
		std::thread waitThread([&] {
			SetEvent(waitStarted);
			waitStatus.store(interfaceV3.wait_provider_quiescent(interfaceV3.registry, wrfRegistration.handle,
				IIF_CB_STAGE_OUTGOING_CALCULATION), std::memory_order_release);
			SetEvent(waitFinished);
		});
		Check(WaitForSingleObject(waitStarted, 5000) == WAIT_OBJECT_0,
			"quiescence waiter reached its controlled start barrier");
		Check(WaitForSingleObject(claimEntered, 5000) == WAIT_OBJECT_0,
			"first waiter owns the retirement claim before the competing call begins");
		std::thread secondWaitThread([&] {
			SetEvent(secondWaitStarted);
			secondWaitStatus.store(interfaceV3.wait_provider_quiescent(interfaceV3.registry,
				wrfRegistration.handle, IIF_CB_STAGE_OUTGOING_CALCULATION), std::memory_order_release);
			SetEvent(secondWaitFinished);
		});
		Check(WaitForSingleObject(secondWaitStarted, 5000) == WAIT_OBJECT_0,
			"competing quiescence waiter reaches its barrier");
		Check(WaitForSingleObject(secondWaitFinished, 2000) == WAIT_OBJECT_0 &&
			secondWaitStatus.load(std::memory_order_acquire) == IIF_CB_STATUS_WAIT_IN_PROGRESS,
			"second DLL caller is denied duplicate unload authorization while first owns the claim");
		Check(WaitForSingleObject(waitFinished, 100) == WAIT_TIMEOUT,
			"claim owner cannot finish while the Provider callback is still running");
		Check(getCounter(1) >= 3, "active callback remains accounted before quiescence");
		setQuiescenceHook(nullptr, nullptr);
		SetEvent(claimRelease);
		releaseWRF();
		callbackThread.join();
		waitThread.join();
		secondWaitThread.join();
		CloseHandle(claimEntered);
		CloseHandle(claimRelease);
		CloseHandle(waitStarted);
		CloseHandle(waitFinished);
		CloseHandle(secondWaitStarted);
		CloseHandle(secondWaitFinished);
		if (waitStatus.load(std::memory_order_acquire) != IIF_CB_STATUS_OK ||
			secondWaitStatus.load(std::memory_order_acquire) != IIF_CB_STATUS_WAIT_IN_PROGRESS) {
			++failures;
			std::cerr << "FAIL: WRF quiescence did not produce exactly one successful unload authorization\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		const auto wrfCallsAtQuiescence = getCounter(1);
		outgoing = MakeOutgoing();
		outgoingResult = MakeOutgoingResult();
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) == IIF_CB_STATUS_OK &&
			getCounter(1) == wrfCallsAtQuiescence,
			"removed Provider callback never executes again after successful quiescence");
		Check(interfaceV3.unregister_provider(interfaceV3.registry, wrfRegistration.handle,
			IIF_CB_STAGE_OUTGOING_CALCULATION) == IIF_CB_STATUS_NOT_FOUND &&
			interfaceV3.wait_provider_quiescent(interfaceV3.registry, wrfRegistration.handle,
				IIF_CB_STAGE_OUTGOING_CALCULATION) == IIF_CB_STATUS_NOT_FOUND,
			"repeated unregister and wait do not recreate unload permission");
		const auto pasUnregisterStatus = interfaceV3.unregister_provider(interfaceV3.registry,
			pasRegistration.handle, IIF_CB_STAGE_INCOMING_HEALTH);
		if (pasUnregisterStatus != IIF_CB_STATUS_OK) {
			++failures;
			std::cerr << "FAIL: PAS unregister failed with status " << pasUnregisterStatus << '\n';
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		WaitTargetState waitIncomingState{ &interfaceV3, interfaceV3.registry, pasRegistration.handle,
			IIF_CB_STAGE_INCOMING_HEALTH };
		IIF_CB_OutgoingProviderV3 callbackWaitProvider{};
		callbackWaitProvider.struct_size = sizeof(callbackWaitProvider);
		callbackWaitProvider.version = IIF_CB_VERSION_3;
		callbackWaitProvider.provider_id = "callback-wait-cross-stage";
		callbackWaitProvider.priority = 300;
		callbackWaitProvider.evaluation_mask = IIF_CB_EVALUATION_CALCULATION;
		callbackWaitProvider.provider_context = &waitIncomingState;
		callbackWaitProvider.callback = &WaitForTargetFromProvider;
		IIF_CB_RegistrationV3 callbackWaitRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
		const auto callbackWaitRegistrationStatus = interfaceV3.register_outgoing(interfaceV3.registry,
			&callbackWaitProvider, &callbackWaitRegistration);
		if (callbackWaitRegistrationStatus != IIF_CB_STATUS_OK ||
			callbackWaitRegistration.status != IIF_CB_STATUS_OK || callbackWaitRegistration.added != 1 ||
			callbackWaitRegistration.handle.value == 0) {
			++failures;
			std::cerr << "FAIL: callback-wait test registration returned an invalid token/status\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		outgoing = MakeOutgoing();
		outgoingResult = MakeOutgoingResult();
		const auto crossStageDispatchStatus = interfaceV3.dispatch_outgoing(interfaceV3.registry,
			&outgoing, &outgoingResult);
		if (crossStageDispatchStatus != IIF_CB_STATUS_OK ||
			waitIncomingState.status.load(std::memory_order_acquire) != IIF_CB_STATUS_WOULD_DEADLOCK) {
			++failures;
			std::cerr << "FAIL: callback-originated cross-stage Wait was not safely rejected\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		const auto pasWaitStatus = interfaceV3.wait_provider_quiescent(interfaceV3.registry,
			pasRegistration.handle, IIF_CB_STAGE_INCOMING_HEALTH);
		const auto callbackWaitUnregisterStatus = interfaceV3.unregister_provider(interfaceV3.registry,
			callbackWaitRegistration.handle, IIF_CB_STAGE_OUTGOING_CALCULATION);
		const auto callbackWaitStatus = callbackWaitUnregisterStatus == IIF_CB_STATUS_OK ?
			interfaceV3.wait_provider_quiescent(interfaceV3.registry, callbackWaitRegistration.handle,
				IIF_CB_STAGE_OUTGOING_CALCULATION) : IIF_CB_STATUS_INTERNAL_ERROR;
		const auto csfUnregisterStatus = interfaceV3.unregister_provider(interfaceV3.registry,
			csfRegistration.handle, IIF_CB_STAGE_OUTGOING_CALCULATION);
		const auto csfWaitStatus = csfUnregisterStatus == IIF_CB_STATUS_OK ?
			interfaceV3.wait_provider_quiescent(interfaceV3.registry, csfRegistration.handle,
				IIF_CB_STAGE_OUTGOING_CALCULATION) : IIF_CB_STATUS_INTERNAL_ERROR;
		if (pasWaitStatus != IIF_CB_STATUS_OK || callbackWaitUnregisterStatus != IIF_CB_STATUS_OK ||
			callbackWaitStatus != IIF_CB_STATUS_OK || csfUnregisterStatus != IIF_CB_STATUS_OK ||
			csfWaitStatus != IIF_CB_STATUS_OK) {
			++failures;
			std::cerr << "FAIL: Provider unregister/quiescence failed (PAS wait=" << pasWaitStatus
				<< ", callback unregister/wait=" << callbackWaitUnregisterStatus << '/' << callbackWaitStatus
				<< ", CSF unregister/wait=" << csfUnregisterStatus << '/' << csfWaitStatus
				<< "); refusing FreeLibrary\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		if (!FreeLibrary(providerModule)) {
			++failures;
			std::cerr << "FAIL: Provider FreeLibrary failed after successful quiescence\n";
			(void)ShutdownAndUnload(hostModule, shutdown, nullptr);
			return false;
		}
		outgoingResult = MakeOutgoingResult();
		outgoing = MakeOutgoing();
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) == IIF_CB_STATUS_OK &&
			outgoingResult.status == IIF_CB_DISPATCH_NO_PROVIDERS,
			"Host dispatch after Provider FreeLibrary has no stale callback target");

		Check(combatbus_test::WriteStartupMarker("host.before_provider_reload"),
			"startup marker records immediately before Provider reload");
		providerModule = LoadLibraryW(reloadProviderPath ? reloadProviderPath : providerPath);
		if (!providerModule) {
			++failures;
			std::cerr << "FAIL: Provider DLL reload failed\n";
			(void)ShutdownAndUnload(hostModule, shutdown, nullptr);
			return false;
		}
		if (!combatbus_test::WriteStartupMarker("host.provider_reloaded")) {
			++failures;
			std::cerr << "FAIL: could not record Provider reload completion\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		if (!ModulePathMatches(providerModule, reloadProviderPath ? reloadProviderPath : providerPath)) {
			++failures;
			std::cerr << "FAIL: reloaded Provider HMODULE does not match requested absolute DLL path\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		// Every export address belongs to the loaded module instance. The first
		// Provider instance was unloaded above, so refresh the entire export set;
		// retaining even a test-only function pointer can call unloaded code.
		const bool reloadedProviderExportsResolved =
			ResolveRequired(providerModule, "TestProvider_GetWRF", getWRF, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetCSF", getCSF, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetPAS", getPAS, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetInvalid", getInvalid, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_BlockWRF", blockWRF, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_WaitWRFEntered", waitWRFEntered, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_ReleaseWRF", releaseWRF, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_Reset", resetProvider, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetCounter", getCounter, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetObserved", getObserved, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetOrder", getOrder, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_SetShutdown", setShutdown, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_TriggerShutdown", triggerShutdown, missingProviderExport) &&
			ResolveRequired(providerModule, "TestProvider_GetCallbackShutdownStatus",
				getCallbackShutdownStatus, missingProviderExport);
		if (!reloadedProviderExportsResolved) {
				const bool cleanupSucceeded = ShutdownAndUnload(hostModule, shutdown, providerModule);
				bool isExpectedMissingExport = expectedMissingReloadExport != nullptr;
				if (isExpectedMissingExport && cleanupSucceeded) {
					std::size_t index{};
					for (; missingProviderExport[index] != '\0' && expectedMissingReloadExport[index] != L'\0';
						++index) {
						if (static_cast<unsigned char>(missingProviderExport[index]) !=
							static_cast<unsigned short>(expectedMissingReloadExport[index])) {
							isExpectedMissingExport = false;
							break;
						}
					}
					if (missingProviderExport[index] != '\0' || expectedMissingReloadExport[index] != L'\0') {
						isExpectedMissingExport = false;
					}
				}
				if (isExpectedMissingExport) {
					std::cout << "PASS: controlled reload rejection for missing export '"
						<< missingProviderExport << "'; no unresolved function pointer was called\n";
					return true;
				}
				++failures;
				std::cerr << "FAIL: reload aborted safely at missing export '"
					<< (missingProviderExport ? missingProviderExport : "<unknown>") << "'\n";
				if (!cleanupSucceeded) std::cerr << "FAIL: cleanup did not obtain successful Host Shutdown\n";
				return false;
			}
		resetProvider();
		setShutdown(shutdown);
		wrf = {};
		wrf.struct_size = sizeof(wrf); wrf.version = IIF_CB_VERSION_3;
		IIF_CB_RegistrationV3 shutdownRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
		const auto shutdownDescriptorStatus = getWRF(&wrf);
		const auto shutdownRegistrationStatus = shutdownDescriptorStatus == IIF_CB_STATUS_OK ?
			interfaceV3.register_outgoing(interfaceV3.registry, &wrf, &shutdownRegistration) :
			IIF_CB_STATUS_INTERNAL_ERROR;
		if (shutdownDescriptorStatus != IIF_CB_STATUS_OK || shutdownRegistrationStatus != IIF_CB_STATUS_OK ||
			shutdownRegistration.status != IIF_CB_STATUS_OK || shutdownRegistration.added != 1 ||
			shutdownRegistration.handle.value == 0) {
			++failures;
			std::cerr << "FAIL: shutdown-test Provider registration failed; refusing to use its handle\n";
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		blockWRF();
		std::thread shutdownCaller([&] {
			auto context = MakeOutgoing();
			auto result = MakeOutgoingResult();
			(void)interfaceV3.dispatch_outgoing(interfaceV3.registry, &context, &result);
		});
		if (waitWRFEntered(5000) != 1) {
			++failures;
			std::cerr << "FAIL: Host shutdown callback did not enter Provider DLL\n";
			releaseWRF();
			shutdownCaller.join();
			(void)ShutdownAndUnload(hostModule, shutdown, providerModule);
			return false;
		}
		const auto callbacksBeforeShutdownClose = getCounter(1);
		std::atomic<std::uint32_t> shutdownStatus{ IIF_CB_STATUS_INTERNAL_ERROR };
		std::atomic<bool> shutdownDone{ false };
		std::thread shutdownThread([&] {
			shutdownStatus.store(shutdown(), std::memory_order_release);
			shutdownDone.store(true, std::memory_order_release);
		});
		IIF_CB_InterfaceV3 afterClose{};
		afterClose.struct_size = sizeof(afterClose); afterClose.version = IIF_CB_VERSION_3;
		std::uint32_t shutdownQueryStatus = IIF_CB_STATUS_OK;
		for (std::uint32_t attempt = 0; attempt < 10000 && shutdownQueryStatus == IIF_CB_STATUS_OK; ++attempt) {
			shutdownQueryStatus = query(IIF_CB_VERSION_3, sizeof(afterClose), &afterClose);
			if (shutdownQueryStatus == IIF_CB_STATUS_OK) SwitchToThread();
		}
		Check(shutdownQueryStatus == IIF_CB_STATUS_SHUTTING_DOWN,
			"QueryInterface stops returning tables as soon as Host shutdown closes its gate");
		Check(!shutdownDone.load(std::memory_order_acquire),
			"Host Shutdown waits for a callback already entered through the interface");
		outgoingResult = MakeOutgoingResult();
		outgoing = MakeOutgoing();
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) ==
			IIF_CB_STATUS_SHUTTING_DOWN && outgoingResult.status == IIF_CB_DISPATCH_CLOSED,
			"previously copied Interface rejects calls during Host shutdown");
		Check(getCounter(1) == callbacksBeforeShutdownClose,
			"closed Host gate prevents another callback from entering the Provider DLL");
		releaseWRF();
		shutdownCaller.join();
		shutdownThread.join();
		const auto completedShutdownStatus = shutdownStatus.load(std::memory_order_acquire);
		if (completedShutdownStatus != IIF_CB_STATUS_OK || !shutdownDone.load(std::memory_order_acquire) ||
			getCounter(1) != callbacksBeforeShutdownClose || shutdown() != IIF_CB_STATUS_OK) {
			++failures;
			std::cerr << "FAIL: Host Shutdown did not successfully drain; keeping Provider and Host loaded\n";
			return false;
		}
		if (!combatbus_test::WriteStartupMarker("host.shutdown_drained")) {
			++failures;
			std::cerr << "FAIL: could not record successful Host Shutdown\n";
			return false;
		}
		if (!FreeLibrary(providerModule)) {
			++failures;
			std::cerr << "FAIL: Provider FreeLibrary failed after successful Host Shutdown\n";
			return false;
		}
		if (!FreeLibrary(hostModule)) {
			++failures;
			std::cerr << "FAIL: Host FreeLibrary failed after successful Shutdown\n";
			return false;
		}
		if (!combatbus_test::WriteStartupMarker("host.modules_unloaded")) {
			++failures;
			std::cerr << "FAIL: could not record completed module unload\n";
			return false;
		}
		return failures == 0;
	}
}

int wmain(int argc, wchar_t** argv)
{
	if (!combatbus_test::WriteStartupMarker("host.wmain.entered", true)) {
		std::cerr << "FAIL: could not initialize the startup trace file\n";
		return 90;
	}
	if (argc != 3 && argc != 5) {
		std::cerr << "Usage: CrossDllHost.exe <IIF mock DLL> <Provider DLL> "
			"[<reload Provider DLL> <expected missing export>]\n";
		return 2;
	}
	const bool ok = Run(argv[1], argv[2], argc == 5 ? argv[3] : nullptr,
		argc == 5 ? argv[4] : nullptr);
	if (!combatbus_test::WriteStartupMarker("host.test_finished")) {
		std::cerr << "FAIL: could not record test-flow completion\n";
		return 91;
	}
	if (failures == 0 && ok) {
		if (!combatbus_test::WriteStartupMarker("host.before_normal_exit")) {
			std::cerr << "FAIL: could not record normal process exit marker\n";
			return 92;
		}
		std::cout << "PASS: Host/IIF/Provider cross-DLL ABI and unload fixtures\n";
		return 0;
	}
	std::cerr << "FAIL: " << failures << " cross-DLL assertion(s) failed\n";
	return 1;
}
