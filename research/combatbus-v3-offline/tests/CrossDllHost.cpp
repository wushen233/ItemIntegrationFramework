#include "CombatBusCABI.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
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

	bool Run(const wchar_t* hostPath, const wchar_t* providerPath)
	{
		Check(CAbiHeaderSmoke() == 1, "C compiler accepted the shared public ABI header and Win64 layout assertions");
		HMODULE hostModule = LoadLibraryW(hostPath);
		Check(hostModule != nullptr, "LoadLibrary loaded the simulated IIF Host DLL");
		if (!hostModule) return false;
		const auto query = Resolve<QueryFn>(hostModule, "IIF_CombatBus_QueryInterface");
		const auto shutdown = Resolve<ShutdownFn>(hostModule, "IIF_CombatBus_Shutdown");
		Check(query && shutdown, "GetProcAddress resolved explicit C ABI exports");
		if (!query || !shutdown) { FreeLibrary(hostModule); return false; }

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
		Check(query(IIF_CB_VERSION_3, sizeof(interfaceV3), &interfaceV3) == IIF_CB_STATUS_OK &&
			interfaceV3.version == IIF_CB_VERSION_3 && interfaceV3.registry && interfaceV3.register_outgoing &&
			interfaceV3.dispatch_incoming,
			"supported V3 negotiation copies a complete Host-owned function table");

		HMODULE providerModule = LoadLibraryW(providerPath);
		Check(providerModule != nullptr, "LoadLibrary loaded an independent Provider DLL");
		if (!providerModule) {
			(void)shutdown();
			FreeLibrary(hostModule);
			return false;
		}
		GetOutgoingFn getWRF = Resolve<GetOutgoingFn>(providerModule, "TestProvider_GetWRF");
		const auto getCSF = Resolve<GetOutgoingFn>(providerModule, "TestProvider_GetCSF");
		const auto getPAS = Resolve<GetIncomingFn>(providerModule, "TestProvider_GetPAS");
		const auto getInvalid = Resolve<GetOutgoingFn>(providerModule, "TestProvider_GetInvalid");
		ResetFn resetProvider = Resolve<ResetFn>(providerModule, "TestProvider_Reset");
		BlockFn blockWRF = Resolve<BlockFn>(providerModule, "TestProvider_BlockWRF");
		WaitEnteredFn waitWRFEntered = Resolve<WaitEnteredFn>(providerModule, "TestProvider_WaitWRFEntered");
		ReleaseFn releaseWRF = Resolve<ReleaseFn>(providerModule, "TestProvider_ReleaseWRF");
		const auto getCounter = Resolve<CounterFn>(providerModule, "TestProvider_GetCounter");
		const auto getObserved = Resolve<ObservedFn>(providerModule, "TestProvider_GetObserved");
		const auto getOrder = Resolve<OrderFn>(providerModule, "TestProvider_GetOrder");
		const auto setShutdown = Resolve<SetShutdownFn>(providerModule, "TestProvider_SetShutdown");
		const auto triggerShutdown = Resolve<TriggerShutdownFn>(providerModule, "TestProvider_TriggerShutdown");
		const auto getCallbackShutdownStatus = Resolve<CallbackShutdownStatusFn>(providerModule,
			"TestProvider_GetCallbackShutdownStatus");
		Check(getWRF && getCSF && getPAS && getInvalid && resetProvider && blockWRF && waitWRFEntered &&
			releaseWRF && getCounter && getObserved && getOrder && setShutdown && triggerShutdown &&
			getCallbackShutdownStatus,
			"GetProcAddress resolved Provider DLL descriptor and synchronization functions");
		if (!(getWRF && getCSF && getPAS && getInvalid && resetProvider && blockWRF && waitWRFEntered &&
			releaseWRF && getCounter && getObserved && getOrder && setShutdown && triggerShutdown &&
			getCallbackShutdownStatus)) {
			(void)shutdown();
			FreeLibrary(providerModule);
			FreeLibrary(hostModule);
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
		Check(getWRF(&wrf) == IIF_CB_STATUS_OK && getCSF(&csf) == IIF_CB_STATUS_OK &&
			getPAS(&pas) == IIF_CB_STATUS_OK, "Provider DLL fills versioned C provider descriptors");

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
		Check(interfaceV3.register_outgoing(interfaceV3.registry, &wrf, &wrfRegistration) == IIF_CB_STATUS_OK &&
			wrfRegistration.added == 1 && wrfRegistration.handle.value != 0,
			"WRF Provider registers through the C ABI across DLL boundary");
		Check(interfaceV3.register_outgoing(interfaceV3.registry, &csf, &csfRegistration) == IIF_CB_STATUS_OK &&
			csfRegistration.handle.value != wrfRegistration.handle.value,
			"CSF registers with a globally distinct Dispatcher handle");
		Check(interfaceV3.register_incoming(interfaceV3.registry, &pas, &pasRegistration) == IIF_CB_STATUS_OK &&
			pasRegistration.handle.value != wrfRegistration.handle.value &&
			pasRegistration.handle.value != csfRegistration.handle.value,
			"PAS registers in the independent Incoming stage across DLL boundary");
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
		Check(getInvalid(&invalid) == IIF_CB_STATUS_OK, "Provider returns invalid-result test descriptor");
		IIF_CB_RegistrationV3 invalidProviderRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
		Check(interfaceV3.register_outgoing(interfaceV3.registry, &invalid, &invalidProviderRegistration) ==
			IIF_CB_STATUS_OK, "register invalid-result Provider for failure-path test");
		outgoing = MakeOutgoing();
		outgoingResult = MakeOutgoingResult();
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) == IIF_CB_STATUS_OK &&
			outgoingResult.status == IIF_CB_DISPATCH_INVALID_PROVIDER_RESULT &&
			Near(outgoingResult.damage.health_damage, 100.0f),
			"invalid cross-DLL callback status fails closed and restores the original numeric snapshot");
		Check(interfaceV3.unregister_provider(interfaceV3.registry, invalidProviderRegistration.handle,
			IIF_CB_STAGE_OUTGOING_CALCULATION) == IIF_CB_STATUS_OK &&
			interfaceV3.wait_provider_quiescent(interfaceV3.registry, invalidProviderRegistration.handle,
				IIF_CB_STAGE_OUTGOING_CALCULATION) == IIF_CB_STATUS_OK,
			"invalid-result Provider can still be safely unregistered and drained");

		blockWRF();
		std::atomic<std::uint32_t> activeDispatchStatus{ IIF_CB_STATUS_INTERNAL_ERROR };
		std::thread callbackThread([&] {
			auto context = MakeOutgoing();
			auto result = MakeOutgoingResult();
			const auto callStatus = interfaceV3.dispatch_outgoing(interfaceV3.registry, &context, &result);
			activeDispatchStatus.store(callStatus == IIF_CB_STATUS_OK ? result.status : callStatus,
				std::memory_order_release);
		});
		Check(waitWRFEntered(5000) == 1, "Provider DLL callback enters controlled blocking section");
		Check(interfaceV3.unregister_provider(interfaceV3.registry, wrfRegistration.handle,
			IIF_CB_STAGE_OUTGOING_CALCULATION) == IIF_CB_STATUS_OK,
			"unregister while callback is active returns Removed but not unload permission");
		HANDLE waitStarted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		HANDLE waitFinished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		std::atomic<std::uint32_t> waitStatus{ IIF_CB_STATUS_INTERNAL_ERROR };
		std::thread waitThread([&] {
			SetEvent(waitStarted);
			waitStatus.store(interfaceV3.wait_provider_quiescent(interfaceV3.registry, wrfRegistration.handle,
				IIF_CB_STAGE_OUTGOING_CALCULATION), std::memory_order_release);
			SetEvent(waitFinished);
		});
		Check(WaitForSingleObject(waitStarted, 5000) == WAIT_OBJECT_0,
			"quiescence waiter reached its controlled start barrier");
		Check(WaitForSingleObject(waitFinished, 100) == WAIT_TIMEOUT,
			"WaitQuiescent does not finish while the Provider callback is still running");
		Check(getCounter(1) >= 3, "active callback remains accounted before quiescence");
		releaseWRF();
		callbackThread.join();
		waitThread.join();
		CloseHandle(waitStarted);
		CloseHandle(waitFinished);
		Check(waitStatus.load(std::memory_order_acquire) == IIF_CB_STATUS_OK,
			"WaitQuiescent returns success after callback completion");
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
		Check(interfaceV3.unregister_provider(interfaceV3.registry, csfRegistration.handle,
			IIF_CB_STAGE_OUTGOING_CALCULATION) == IIF_CB_STATUS_OK &&
			interfaceV3.wait_provider_quiescent(interfaceV3.registry, csfRegistration.handle,
				IIF_CB_STAGE_OUTGOING_CALCULATION) == IIF_CB_STATUS_OK &&
			interfaceV3.unregister_provider(interfaceV3.registry, pasRegistration.handle,
				IIF_CB_STAGE_INCOMING_HEALTH) == IIF_CB_STATUS_OK &&
			interfaceV3.wait_provider_quiescent(interfaceV3.registry, pasRegistration.handle,
				IIF_CB_STAGE_INCOMING_HEALTH) == IIF_CB_STATUS_OK,
			"CSF and PAS each require successful stage-correct quiescence before unload");
		Check(FreeLibrary(providerModule) != 0,
			"Provider DLL unload succeeds only after all callback code is quiescent");
		outgoingResult = MakeOutgoingResult();
		outgoing = MakeOutgoing();
		Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) == IIF_CB_STATUS_OK &&
			outgoingResult.status == IIF_CB_DISPATCH_NO_PROVIDERS,
			"Host dispatch after Provider FreeLibrary has no stale callback target");

		providerModule = LoadLibraryW(providerPath);
		Check(providerModule != nullptr, "Provider DLL reloads for Host Shutdown drain test");
		if (providerModule) {
			getWRF = Resolve<GetOutgoingFn>(providerModule, "TestProvider_GetWRF");
			blockWRF = Resolve<BlockFn>(providerModule, "TestProvider_BlockWRF");
			waitWRFEntered = Resolve<WaitEnteredFn>(providerModule, "TestProvider_WaitWRFEntered");
			releaseWRF = Resolve<ReleaseFn>(providerModule, "TestProvider_ReleaseWRF");
			resetProvider = Resolve<ResetFn>(providerModule, "TestProvider_Reset");
			resetProvider();
			wrf = {};
			wrf.struct_size = sizeof(wrf); wrf.version = IIF_CB_VERSION_3;
			IIF_CB_RegistrationV3 shutdownRegistration{ sizeof(IIF_CB_RegistrationV3), IIF_CB_VERSION_3, 0, 0, { 0 } };
			Check(getWRF(&wrf) == IIF_CB_STATUS_OK &&
				interfaceV3.register_outgoing(interfaceV3.registry, &wrf, &shutdownRegistration) == IIF_CB_STATUS_OK,
				"register Provider before global Host shutdown");
			blockWRF();
			std::thread shutdownCaller([&] {
				auto context = MakeOutgoing();
				auto result = MakeOutgoingResult();
				(void)interfaceV3.dispatch_outgoing(interfaceV3.registry, &context, &result);
			});
			Check(waitWRFEntered(5000) == 1, "Host shutdown test callback enters Provider DLL");
			std::atomic<std::uint32_t> shutdownStatus{ IIF_CB_STATUS_INTERNAL_ERROR };
			std::atomic<bool> shutdownDone{ false };
			std::thread shutdownThread([&] {
				shutdownStatus.store(shutdown(), std::memory_order_release);
				shutdownDone.store(true, std::memory_order_release);
			});
			IIF_CB_InterfaceV3 afterClose{};
			afterClose.struct_size = sizeof(afterClose); afterClose.version = IIF_CB_VERSION_3;
			std::uint32_t queryStatus = IIF_CB_STATUS_OK;
			for (std::uint32_t attempt = 0; attempt < 10000 && queryStatus == IIF_CB_STATUS_OK; ++attempt) {
				queryStatus = query(IIF_CB_VERSION_3, sizeof(afterClose), &afterClose);
				if (queryStatus == IIF_CB_STATUS_OK) SwitchToThread();
			}
			Check(queryStatus == IIF_CB_STATUS_SHUTTING_DOWN,
				"QueryInterface stops returning tables as soon as Host shutdown closes its gate");
			Check(!shutdownDone.load(std::memory_order_acquire),
				"Host Shutdown waits for a callback already entered through the interface");
			outgoingResult = MakeOutgoingResult();
			outgoing = MakeOutgoing();
			Check(interfaceV3.dispatch_outgoing(interfaceV3.registry, &outgoing, &outgoingResult) ==
				IIF_CB_STATUS_SHUTTING_DOWN && outgoingResult.status == IIF_CB_DISPATCH_CLOSED,
				"previously copied Interface rejects calls during Host shutdown");
			releaseWRF();
			shutdownCaller.join();
			shutdownThread.join();
		Check(shutdownStatus.load(std::memory_order_acquire) == IIF_CB_STATUS_OK &&
			shutdownDone.load(std::memory_order_acquire),
			"Host Shutdown completes after its active callback and API call drain");
		Check(shutdown() == IIF_CB_STATUS_OK,
			"repeated exported Host Shutdown remains idempotent");
		Check(FreeLibrary(providerModule) != 0,
				"global shutdown drain permits Provider DLL unload after active callback returns");
		}
		Check(FreeLibrary(hostModule) != 0,
			"Host DLL unload occurs only after shutdown and all Host caller threads are joined");
		return failures == 0;
	}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc != 3) {
		std::cerr << "Usage: CrossDllHost.exe <IIF mock DLL> <Provider DLL>\n";
		return 2;
	}
	const bool ok = Run(argv[1], argv[2]);
	if (failures == 0 && ok) {
		std::cout << "PASS: Host/IIF/Provider cross-DLL ABI and unload fixtures\n";
		return 0;
	}
	std::cerr << "FAIL: " << failures << " cross-DLL assertion(s) failed\n";
	return 1;
}
