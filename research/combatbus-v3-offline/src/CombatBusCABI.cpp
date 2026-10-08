#include "CombatBusCABI.h"
#include "CombatBusPrototype.h"
#if defined(IIF_CB_TEST_HOOKS)
#include "MockHostTestHooks.h"
#endif

#include <atomic>
#include <condition_variable>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

using namespace IIF::CombatBus::PrototypeV3;

namespace {
	constexpr std::uint64_t kClosedBit = std::uint64_t{ 1 } << 63;
	constexpr std::uint64_t kCallCountMask = ~kClosedBit;
	thread_local const void* g_hostCallStack[64]{};
	thread_local std::uint32_t g_hostCallDepth{};

	class HostCallGate final {
	public:
		bool TryEnter() noexcept
		{
			auto state = _state.load(std::memory_order_acquire);
			for (;;) {
				if ((state & kClosedBit) != 0 || (state & kCallCountMask) == kCallCountMask) return false;
				if (_state.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel,
					std::memory_order_acquire)) return true;
			}
		}

		void Leave() noexcept
		{
			const auto prior = _state.fetch_sub(1, std::memory_order_acq_rel);
			if ((prior & kClosedBit) != 0 && (prior & kCallCountMask) == 1) _state.notify_all();
		}

		bool CloseAndWait() noexcept
		{
			_state.fetch_or(kClosedBit, std::memory_order_acq_rel);
			auto state = _state.load(std::memory_order_acquire);
			while ((state & kCallCountMask) != 0) {
				_state.wait(state, std::memory_order_acquire);
				state = _state.load(std::memory_order_acquire);
			}
			return true;
		}

		bool IsClosed() const noexcept
		{
			return (_state.load(std::memory_order_acquire) & kClosedBit) != 0;
		}

	private:
		std::atomic<std::uint64_t> _state{};
	};

	class Host;

	class HostCall final {
	public:
		explicit HostCall(Host& host) noexcept;
		~HostCall();
		bool Entered() const noexcept { return _entered; }
	private:
		Host* _host{};
		bool _entered{};
	};

	struct BridgeBase {
		virtual ~BridgeBase() = default;
		std::uint64_t handle{};
		std::uint32_t stage{};
		std::unique_ptr<BridgeBase> next;
	};

	struct OutgoingBridge final : BridgeBase {
		IIF_CB_OutgoingCallbackV3 callback{};
		void* providerContext{};

		static CallbackStatus IIF_CB_CALL Invoke(void* opaque,
			const OutgoingCalculationContextV3* context, OutgoingResultV3* result) noexcept
		{
			auto& bridge = *static_cast<OutgoingBridge*>(opaque);
			IIF_CB_OutgoingContextV3 cContext{};
			cContext.struct_size = sizeof(cContext);
			cContext.version = IIF_CB_VERSION_3;
			cContext.evaluation_kind = static_cast<std::uint32_t>(context->evaluationKind);
			cContext.attacker_kind = static_cast<std::uint32_t>(context->attackerKind);
			cContext.profile = static_cast<std::uint32_t>(context->profile);
			cContext.modifiable_mask = context->modifiableMask;
			cContext.attacker = context->attacker;
			cContext.target = context->target;
			cContext.weapon = context->weapon;
			cContext.damage = { sizeof(IIF_CB_DamageSnapshotV3), context->damage.validMask,
				context->damage.healthDamage, context->damage.physicalDamage, context->damage.totalDamage,
				context->damage.targetedLimbDamage, context->damage.resistanceIntermediate };
			IIF_CB_OutgoingResultV3 cResult{ sizeof(IIF_CB_OutgoingResultV3), IIF_CB_VERSION_3,
				IIF_CB_CALLBACK_FAILURE, 0, 1.0f };
			std::uint32_t callbackStatus = IIF_CB_CALLBACK_FAILURE;
			try {
				callbackStatus = bridge.callback(bridge.providerContext, &cContext, &cResult);
			} catch (...) {
				return CallbackStatus::Failure;
			}
			if (cResult.struct_size != sizeof(cResult) || cResult.version != IIF_CB_VERSION_3) {
				return CallbackStatus::Failure;
			}
			result->structSize = sizeof(*result);
			result->version = kInterfaceVersion;
			result->status = static_cast<CallbackStatus>(cResult.status);
			result->componentMask = cResult.component_mask;
			result->multiplier = cResult.multiplier;
			return static_cast<CallbackStatus>(callbackStatus);
		}
	};

	struct IncomingBridge final : BridgeBase {
		IIF_CB_IncomingCallbackV3 callback{};
		void* providerContext{};

		static CallbackStatus IIF_CB_CALL Invoke(void* opaque,
			const IncomingHealthContextV3* context, IncomingHealthResultV3* result) noexcept
		{
			auto& bridge = *static_cast<IncomingBridge*>(opaque);
			IIF_CB_IncomingContextV3 cContext{};
			cContext.struct_size = sizeof(cContext);
			cContext.version = IIF_CB_VERSION_3;
			cContext.phase = static_cast<std::uint32_t>(context->phase);
			cContext.confidence = static_cast<std::uint32_t>(context->confidence);
			cContext.target_kind = static_cast<std::uint32_t>(context->targetKind);
			cContext.power_armor = static_cast<std::uint32_t>(context->powerArmor);
			cContext.attacker = context->attacker;
			cContext.target = context->target;
			cContext.weapon = context->weapon;
			cContext.health_damage = context->healthDamage;
			IIF_CB_IncomingResultV3 cResult{ sizeof(IIF_CB_IncomingResultV3), IIF_CB_VERSION_3,
				IIF_CB_CALLBACK_FAILURE, 0, 1.0f };
			std::uint32_t callbackStatus = IIF_CB_CALLBACK_FAILURE;
			try {
				callbackStatus = bridge.callback(bridge.providerContext, &cContext, &cResult);
			} catch (...) {
				return CallbackStatus::Failure;
			}
			if (cResult.struct_size != sizeof(cResult) || cResult.version != IIF_CB_VERSION_3) {
				return CallbackStatus::Failure;
			}
			result->structSize = sizeof(*result);
			result->version = kInterfaceVersion;
			result->status = static_cast<CallbackStatus>(cResult.status);
			result->reserved = cResult.reserved;
			result->multiplier = cResult.multiplier;
			return static_cast<CallbackStatus>(callbackStatus);
		}
	};

	class Host final {
	public:
		~Host()
		{
			// A DLL may be unloaded under the loader lock. Require the explicit
			// exported Shutdown before that point; never start a blocking drain here.
			if (!shutdownComplete) std::terminate();
		}

		Dispatcher dispatcher;
		HostCallGate calls;
		std::mutex shutdownMutex;
		std::mutex bridgesMutex;
		std::unique_ptr<BridgeBase> bridges;
		bool shutdownComplete{};

		std::uint32_t Shutdown() noexcept
		{
			if (IsActiveCall()) return IIF_CB_STATUS_WOULD_DEADLOCK;
			try {
				std::scoped_lock shutdownLock{ shutdownMutex };
				if (shutdownComplete) return IIF_CB_STATUS_OK;
				if (!calls.CloseAndWait()) return IIF_CB_STATUS_WAIT_FAILURE;
				const auto dispatcherResult = dispatcher.Shutdown();
				if (dispatcherResult != ShutdownStatus::Complete) {
					return dispatcherResult == ShutdownStatus::WouldDeadlock ?
						IIF_CB_STATUS_WOULD_DEADLOCK : IIF_CB_STATUS_WAIT_FAILURE;
				}
				ClearBridgesNoAlloc();
				shutdownComplete = true;
				return IIF_CB_STATUS_OK;
			} catch (...) {
				return IIF_CB_STATUS_WAIT_FAILURE;
			}
		}

		bool IsActiveCall() const noexcept
		{
			for (std::uint32_t i = 0; i < g_hostCallDepth; ++i) {
				if (g_hostCallStack[i] == this) return true;
			}
			return false;
		}

		void LinkBridge(std::unique_ptr<BridgeBase> bridge) noexcept
		{
			std::scoped_lock lock{ bridgesMutex };
			bridge->next = std::move(bridges);
			bridges = std::move(bridge);
		}

		void RemoveBridge(std::uint64_t handle, std::uint32_t stage)
		{
			std::scoped_lock lock{ bridgesMutex };
			auto* link = &bridges;
			while (*link && ((*link)->handle != handle || (*link)->stage != stage)) {
				link = &((*link)->next);
			}
			if (*link) {
				auto removed = std::move(*link);
				*link = std::move(removed->next);
			}
		}

	private:
		void ClearBridgesNoAlloc() noexcept
		{
			// Shutdown has drained all Host calls, so no bridge can be in use.
			// Iterative destruction avoids recursive teardown for large registries.
			while (bridges) {
				auto next = std::move(bridges->next);
				bridges.reset();
				bridges = std::move(next);
			}
		}
	};

	Host& GetHost()
	{
		// Initialize on the first exported API call, after LoadLibrary returned;
		// do not allocate or construct the Dispatcher from DLL process attach.
		static Host host;
		return host;
	}

#define g_host GetHost()

	HostCall::HostCall(Host& host) noexcept : _host(&host)
	{
		if (!host.calls.TryEnter()) return;
		if (g_hostCallDepth >= std::size(g_hostCallStack)) { host.calls.Leave(); return; }
		g_hostCallStack[g_hostCallDepth++] = &host;
		_entered = true;
	}

	HostCall::~HostCall()
	{
		if (_entered) {
			g_hostCallStack[--g_hostCallDepth] = nullptr;
			_host->calls.Leave();
		}
	}

	std::uint32_t HeaderStatus(std::uint32_t structSize, std::uint32_t version,
		std::size_t expected) noexcept
	{
		if (version != IIF_CB_VERSION_3) return IIF_CB_STATUS_UNSUPPORTED_VERSION;
		return structSize == expected ? IIF_CB_STATUS_OK : IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	}

	std::uint32_t MapRegistration(RegistrationStatus status) noexcept
	{
		switch (status) {
		case RegistrationStatus::Added: return IIF_CB_STATUS_OK;
		case RegistrationStatus::Duplicate: return IIF_CB_STATUS_DUPLICATE;
		case RegistrationStatus::InvalidDescriptor:
		case RegistrationStatus::InvalidId: return IIF_CB_STATUS_INVALID_PROVIDER;
		case RegistrationStatus::AllocationFailure: return IIF_CB_STATUS_ALLOCATION_FAILURE;
		case RegistrationStatus::HandleExhausted: return IIF_CB_STATUS_HANDLE_EXHAUSTED;
		case RegistrationStatus::DispatcherClosed: return IIF_CB_STATUS_SHUTTING_DOWN;
		}
		return IIF_CB_STATUS_INTERNAL_ERROR;
	}

	void InitializeRegistration(IIF_CB_RegistrationV3* output, std::uint32_t status) noexcept
	{
		if (!output) return;
		*output = { sizeof(*output), IIF_CB_VERSION_3, status, 0, { 0 } };
	}

	std::uint32_t FailRegistration(IIF_CB_RegistrationV3* output, std::uint32_t status) noexcept
	{
		InitializeRegistration(output, status);
		return status;
	}

	void InitializeOutgoingDispatch(IIF_CB_OutgoingDispatchV3* output, std::uint32_t status) noexcept
	{
		*output = { sizeof(*output), IIF_CB_VERSION_3, status, 0,
			{ sizeof(IIF_CB_DamageSnapshotV3), 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
	}

	void InitializeIncomingDispatch(IIF_CB_IncomingDispatchV3* output, std::uint32_t status) noexcept
	{
		*output = { sizeof(*output), IIF_CB_VERSION_3, status, 0.0f };
	}

	std::uint32_t IIF_CB_CALL RegisterOutgoing(void* registry,
		const IIF_CB_OutgoingProviderV3* provider, IIF_CB_RegistrationV3* output) noexcept
	{
		HostCall call{ g_host };
		if (!output) return IIF_CB_STATUS_INVALID_ARGUMENT;
		const auto outputHeader = HeaderStatus(output->struct_size, output->version, sizeof(*output));
		if (outputHeader != IIF_CB_STATUS_OK) return outputHeader;
		InitializeRegistration(output, IIF_CB_STATUS_INVALID_ARGUMENT);
		if (!call.Entered()) return FailRegistration(output, IIF_CB_STATUS_SHUTTING_DOWN);
		if (registry != &g_host || !provider) return FailRegistration(output, IIF_CB_STATUS_INVALID_ARGUMENT);
		const auto providerHeader = HeaderStatus(provider->struct_size, provider->version, sizeof(*provider));
		if (providerHeader != IIF_CB_STATUS_OK) return FailRegistration(output, providerHeader);
		if (!provider->callback || !provider->provider_id) {
			return FailRegistration(output, IIF_CB_STATUS_INVALID_PROVIDER);
		}
		try {
			auto bridge = std::make_unique<OutgoingBridge>();
			bridge->callback = provider->callback;
			bridge->providerContext = provider->provider_context;
			auto* rawBridge = bridge.get();
			g_host.LinkBridge(std::move(bridge));
			const OutgoingProviderV3 descriptor{ sizeof(OutgoingProviderV3), kInterfaceVersion,
				provider->provider_id, provider->priority, provider->evaluation_mask, rawBridge,
				&OutgoingBridge::Invoke };
			const auto result = g_host.dispatcher.RegisterOutgoing(&descriptor);
			if (!result.added) {
				std::scoped_lock lock{ g_host.bridgesMutex };
				auto* link = &g_host.bridges;
				while (*link && link->get() != rawBridge) link = &((*link)->next);
				if (*link) { auto removed = std::move(*link); *link = std::move(removed->next); }
				const auto status = MapRegistration(result.status);
				InitializeRegistration(output, status);
				return status;
			}
			rawBridge->handle = result.handle.value;
			rawBridge->stage = IIF_CB_STAGE_OUTGOING_CALCULATION;
			*output = { sizeof(*output), IIF_CB_VERSION_3, IIF_CB_STATUS_OK, 1,
				{ result.handle.value } };
			return IIF_CB_STATUS_OK;
		} catch (const std::bad_alloc&) {
			InitializeRegistration(output, IIF_CB_STATUS_ALLOCATION_FAILURE);
			return IIF_CB_STATUS_ALLOCATION_FAILURE;
		} catch (...) {
			InitializeRegistration(output, IIF_CB_STATUS_INTERNAL_ERROR);
			return IIF_CB_STATUS_INTERNAL_ERROR;
		}
	}

	std::uint32_t IIF_CB_CALL RegisterIncoming(void* registry,
		const IIF_CB_IncomingProviderV3* provider, IIF_CB_RegistrationV3* output) noexcept
	{
		HostCall call{ g_host };
		if (!output) return IIF_CB_STATUS_INVALID_ARGUMENT;
		const auto outputHeader = HeaderStatus(output->struct_size, output->version, sizeof(*output));
		if (outputHeader != IIF_CB_STATUS_OK) return outputHeader;
		InitializeRegistration(output, IIF_CB_STATUS_INVALID_ARGUMENT);
		if (!call.Entered()) return FailRegistration(output, IIF_CB_STATUS_SHUTTING_DOWN);
		if (registry != &g_host || !provider) return FailRegistration(output, IIF_CB_STATUS_INVALID_ARGUMENT);
		const auto providerHeader = HeaderStatus(provider->struct_size, provider->version, sizeof(*provider));
		if (providerHeader != IIF_CB_STATUS_OK) return FailRegistration(output, providerHeader);
		if (provider->reserved != 0) return FailRegistration(output, IIF_CB_STATUS_INVALID_STRUCT_SIZE);
		if (!provider->callback || !provider->provider_id) {
			return FailRegistration(output, IIF_CB_STATUS_INVALID_PROVIDER);
		}
		try {
			auto bridge = std::make_unique<IncomingBridge>();
			bridge->callback = provider->callback;
			bridge->providerContext = provider->provider_context;
			auto* rawBridge = bridge.get();
			g_host.LinkBridge(std::move(bridge));
			const IncomingHealthProviderV3 descriptor{ sizeof(IncomingHealthProviderV3), kInterfaceVersion,
				provider->provider_id, provider->priority, rawBridge, &IncomingBridge::Invoke };
			const auto result = g_host.dispatcher.RegisterIncoming(&descriptor);
			if (!result.added) {
				std::scoped_lock lock{ g_host.bridgesMutex };
				auto* link = &g_host.bridges;
				while (*link && link->get() != rawBridge) link = &((*link)->next);
				if (*link) { auto removed = std::move(*link); *link = std::move(removed->next); }
				const auto status = MapRegistration(result.status);
				InitializeRegistration(output, status);
				return status;
			}
			rawBridge->handle = result.handle.value;
			rawBridge->stage = IIF_CB_STAGE_INCOMING_HEALTH;
			*output = { sizeof(*output), IIF_CB_VERSION_3, IIF_CB_STATUS_OK, 1,
				{ result.handle.value } };
			return IIF_CB_STATUS_OK;
		} catch (const std::bad_alloc&) {
			InitializeRegistration(output, IIF_CB_STATUS_ALLOCATION_FAILURE);
			return IIF_CB_STATUS_ALLOCATION_FAILURE;
		} catch (...) {
			InitializeRegistration(output, IIF_CB_STATUS_INTERNAL_ERROR);
			return IIF_CB_STATUS_INTERNAL_ERROR;
		}
	}

	std::uint32_t IIF_CB_CALL UnregisterProvider(void* registry, IIF_CB_ProviderHandleV3 handle,
		std::uint32_t stage) noexcept
	{
		try {
		HostCall call{ g_host };
		if (!call.Entered()) return IIF_CB_STATUS_SHUTTING_DOWN;
		if (registry != &g_host || handle.value == 0) return IIF_CB_STATUS_INVALID_ARGUMENT;
		if (stage == IIF_CB_STAGE_OUTGOING_CALCULATION) {
			const auto status = g_host.dispatcher.UnregisterOutgoing({ handle.value });
			if (status == UnregisterStatus::Removed) return IIF_CB_STATUS_OK;
			if (status == UnregisterStatus::AllocationFailure) return IIF_CB_STATUS_ALLOCATION_FAILURE;
			return status == UnregisterStatus::DispatcherClosed ? IIF_CB_STATUS_SHUTTING_DOWN : IIF_CB_STATUS_NOT_FOUND;
		}
		if (stage == IIF_CB_STAGE_INCOMING_HEALTH) {
			const auto status = g_host.dispatcher.UnregisterIncoming({ handle.value });
			if (status == UnregisterStatus::Removed) return IIF_CB_STATUS_OK;
			if (status == UnregisterStatus::AllocationFailure) return IIF_CB_STATUS_ALLOCATION_FAILURE;
			return status == UnregisterStatus::DispatcherClosed ? IIF_CB_STATUS_SHUTTING_DOWN : IIF_CB_STATUS_NOT_FOUND;
		}
		return IIF_CB_STATUS_INVALID_ARGUMENT;
		} catch (...) {
			return IIF_CB_STATUS_INTERNAL_ERROR;
		}
	}

	std::uint32_t IIF_CB_CALL WaitProviderQuiescent(void* registry, IIF_CB_ProviderHandleV3 handle,
		std::uint32_t stage) noexcept
	{
		try {
		HostCall call{ g_host };
		if (!call.Entered()) return IIF_CB_STATUS_SHUTTING_DOWN;
		if (registry != &g_host || handle.value == 0) return IIF_CB_STATUS_INVALID_ARGUMENT;
		QuiescenceStatus status = QuiescenceStatus::NotFound;
		if (stage == IIF_CB_STAGE_OUTGOING_CALCULATION) status = g_host.dispatcher.WaitOutgoingQuiescent({ handle.value });
		else if (stage == IIF_CB_STAGE_INCOMING_HEALTH) status = g_host.dispatcher.WaitIncomingQuiescent({ handle.value });
		else return IIF_CB_STATUS_INVALID_ARGUMENT;
		if (status == QuiescenceStatus::Quiescent) {
			g_host.RemoveBridge(handle.value, stage);
			return IIF_CB_STATUS_OK;
		}
		if (status == QuiescenceStatus::WouldDeadlock) return IIF_CB_STATUS_WOULD_DEADLOCK;
		if (status == QuiescenceStatus::WaitInProgress) return IIF_CB_STATUS_WAIT_IN_PROGRESS;
		if (status == QuiescenceStatus::WaitFailure) return IIF_CB_STATUS_WAIT_FAILURE;
		return status == QuiescenceStatus::DispatcherClosed ? IIF_CB_STATUS_SHUTTING_DOWN : IIF_CB_STATUS_NOT_FOUND;
		} catch (...) {
			return IIF_CB_STATUS_INTERNAL_ERROR;
		}
	}

	std::uint32_t IIF_CB_CALL DispatchOutgoing(void* registry,
		const IIF_CB_OutgoingContextV3* context, IIF_CB_OutgoingDispatchV3* output) noexcept
	{
		HostCall call{ g_host };
		if (!output) return IIF_CB_STATUS_INVALID_ARGUMENT;
		const auto outputHeader = HeaderStatus(output->struct_size, output->version, sizeof(*output));
		if (outputHeader != IIF_CB_STATUS_OK) return outputHeader;
		if (!call.Entered()) {
			InitializeOutgoingDispatch(output, IIF_CB_DISPATCH_CLOSED);
			return IIF_CB_STATUS_SHUTTING_DOWN;
		}
		if (registry != &g_host || !context) {
			InitializeOutgoingDispatch(output, IIF_CB_DISPATCH_INVALID_CONTEXT);
			return IIF_CB_STATUS_INVALID_ARGUMENT;
		}
		const auto contextHeader = HeaderStatus(context->struct_size, context->version, sizeof(*context));
		if (contextHeader != IIF_CB_STATUS_OK) {
			InitializeOutgoingDispatch(output, IIF_CB_DISPATCH_INVALID_CONTEXT);
			return contextHeader;
		}
		if (context->damage.struct_size != sizeof(context->damage)) {
			InitializeOutgoingDispatch(output, IIF_CB_DISPATCH_INVALID_CONTEXT);
			return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
		}
		DamageSnapshotV3 damage{ sizeof(DamageSnapshotV3), context->damage.valid_mask, 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f };
		if ((damage.validMask & ComponentHealth) != 0) damage.healthDamage = context->damage.health_damage;
		if ((damage.validMask & ComponentPhysical) != 0) damage.physicalDamage = context->damage.physical_damage;
		if ((damage.validMask & ComponentTotal) != 0) damage.totalDamage = context->damage.total_damage;
		if ((damage.validMask & ComponentTargetedLimb) != 0) {
			damage.targetedLimbDamage = context->damage.targeted_limb_damage;
		}
		if ((damage.validMask & ComponentResistanceIntermediate) != 0) {
			damage.resistanceIntermediate = context->damage.resistance_intermediate;
		}
		const OutgoingCalculationContextV3 cppContext{ sizeof(OutgoingCalculationContextV3), kInterfaceVersion,
			static_cast<EvaluationKind>(context->evaluation_kind), static_cast<ActorKind>(context->attacker_kind),
			static_cast<OutgoingProfile>(context->profile), context->modifiable_mask, context->attacker,
			context->target, context->weapon, damage };
		const auto result = g_host.dispatcher.DispatchOutgoing(cppContext);
		*output = { sizeof(*output), IIF_CB_VERSION_3, static_cast<std::uint32_t>(result.status),
			result.changedMask, { sizeof(IIF_CB_DamageSnapshotV3), result.damage.validMask,
				result.damage.healthDamage, result.damage.physicalDamage, result.damage.totalDamage,
				result.damage.targetedLimbDamage, result.damage.resistanceIntermediate } };
		return IIF_CB_STATUS_OK;
	}

	std::uint32_t IIF_CB_CALL DispatchIncoming(void* registry,
		const IIF_CB_IncomingContextV3* context, IIF_CB_IncomingDispatchV3* output) noexcept
	{
		HostCall call{ g_host };
		if (!output) return IIF_CB_STATUS_INVALID_ARGUMENT;
		const auto outputHeader = HeaderStatus(output->struct_size, output->version, sizeof(*output));
		if (outputHeader != IIF_CB_STATUS_OK) return outputHeader;
		if (!call.Entered()) {
			InitializeIncomingDispatch(output, IIF_CB_DISPATCH_CLOSED);
			return IIF_CB_STATUS_SHUTTING_DOWN;
		}
		if (registry != &g_host || !context) {
			InitializeIncomingDispatch(output, IIF_CB_DISPATCH_INVALID_CONTEXT);
			return IIF_CB_STATUS_INVALID_ARGUMENT;
		}
		const auto contextHeader = HeaderStatus(context->struct_size, context->version, sizeof(*context));
		if (contextHeader != IIF_CB_STATUS_OK) {
			InitializeIncomingDispatch(output, IIF_CB_DISPATCH_INVALID_CONTEXT);
			return contextHeader;
		}
		const IncomingHealthContextV3 cppContext{ sizeof(IncomingHealthContextV3), kInterfaceVersion,
			static_cast<IncomingPhase>(context->phase), static_cast<ContextConfidence>(context->confidence),
			static_cast<ActorKind>(context->target_kind), static_cast<PowerArmorState>(context->power_armor),
			context->attacker, context->target, context->weapon, context->health_damage };
		const auto result = g_host.dispatcher.DispatchIncoming(cppContext);
		*output = { sizeof(*output), IIF_CB_VERSION_3, static_cast<std::uint32_t>(result.status),
			result.healthDamage };
		return IIF_CB_STATUS_OK;
	}

	const IIF_CB_InterfaceV3 kInterface{
		sizeof(IIF_CB_InterfaceV3), IIF_CB_VERSION_3, nullptr, &RegisterOutgoing, &RegisterIncoming,
		&UnregisterProvider, &WaitProviderQuiescent, &DispatchOutgoing, &DispatchIncoming };
}

extern "C" IIF_CB_API std::uint32_t IIF_CB_CALL IIF_CombatBus_QueryInterface(
	std::uint32_t requestedVersion, std::uint32_t callerSize, IIF_CB_InterfaceV3* output)
{
	try {
		if (!output) return IIF_CB_STATUS_INVALID_ARGUMENT;
		if (callerSize != sizeof(IIF_CB_InterfaceV3)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
		if (output->struct_size != callerSize) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
		if (requestedVersion != IIF_CB_VERSION_3) {
			*output = { sizeof(*output), requestedVersion, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
			return IIF_CB_STATUS_UNSUPPORTED_VERSION;
		}
		HostCall call{ g_host };
		if (!call.Entered()) return IIF_CB_STATUS_SHUTTING_DOWN;
		*output = kInterface;
		output->registry = &g_host;
		return IIF_CB_STATUS_OK;
	} catch (const std::bad_alloc&) {
		return IIF_CB_STATUS_ALLOCATION_FAILURE;
	} catch (...) {
		return IIF_CB_STATUS_INTERNAL_ERROR;
	}
}

extern "C" IIF_CB_API std::uint32_t IIF_CB_CALL IIF_CombatBus_Shutdown(void)
{
	try {
		return g_host.Shutdown();
	} catch (...) {
		return IIF_CB_STATUS_WAIT_FAILURE;
	}
}

#if defined(IIF_CB_TEST_HOOKS)
extern "C" IIF_CB_API void IIF_CB_CALL IIF_CombatBus_Test_SetQuiescenceClaimHook(
	IIF_CB_TestQuiescenceClaimHook hook, void* context)
{
	Testing::SetAfterQuiescenceClaimHook(hook, context);
}

extern "C" IIF_CB_API void IIF_CB_CALL IIF_CombatBus_Test_FailNextQuiescenceWait(void)
{
	Testing::FailNextQuiescenceWait();
}
#endif
