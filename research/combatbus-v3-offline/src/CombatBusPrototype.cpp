#include "CombatBusPrototype.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <thread>
#include <utility>

namespace IIF::CombatBus::PrototypeV3 {
namespace {
	constexpr std::uint32_t kKnownDamageMask = ComponentHealth | ComponentPhysical | ComponentTotal |
		ComponentTargetedLimb | ComponentResistanceIntermediate;
	constexpr std::uint32_t kSupportedOutgoingMask = ComponentHealth | ComponentPhysical;
	constexpr std::uint32_t kKnownEvaluationMask = EvaluationCalculation | EvaluationPrediction;

	thread_local const void* g_dispatchStack[32]{};
	thread_local std::uint32_t g_dispatchDepth = 0;
	thread_local const void* g_callbackStack[64]{};
	thread_local std::uint32_t g_callbackDepth = 0;
	thread_local const void* g_apiCallStack[64]{};
	thread_local std::uint32_t g_apiCallDepth = 0;
	std::atomic<UnregisterFailurePoint> g_unregisterFailurePoint{ UnregisterFailurePoint::None };

	[[nodiscard]] bool ConsumeUnregisterFailure(UnregisterFailurePoint point) noexcept
	{
		auto expected = point;
		return g_unregisterFailurePoint.compare_exchange_strong(expected, UnregisterFailurePoint::None,
			std::memory_order_acq_rel);
	}

	class DispatchGuard final {
	public:
		explicit DispatchGuard(const void* stage) noexcept
		{
			if (g_dispatchDepth >= std::size(g_dispatchStack)) return;
			for (std::uint32_t index = 0; index < g_dispatchDepth; ++index) {
				if (g_dispatchStack[index] == stage) return;
			}
			g_dispatchStack[g_dispatchDepth++] = stage;
			_entered = true;
		}
		~DispatchGuard()
		{
			if (_entered) {
				g_dispatchStack[--g_dispatchDepth] = nullptr;
			}
		}
		[[nodiscard]] bool Entered() const noexcept { return _entered; }

	private:
		bool _entered{ false };
	};

	class CallbackGuard final {
	public:
		explicit CallbackGuard(const void* entry) noexcept
		{
			if (g_callbackDepth >= std::size(g_callbackStack)) return;
			g_callbackStack[g_callbackDepth++] = entry;
			_entered = true;
		}
		~CallbackGuard()
		{
			if (_entered) g_callbackStack[--g_callbackDepth] = nullptr;
		}
		[[nodiscard]] bool Entered() const noexcept { return _entered; }

	private:
		bool _entered{ false };
	};

	[[nodiscard]] bool IsActiveCallback(const void* entry) noexcept
	{
		for (std::uint32_t index = 0; index < g_callbackDepth; ++index) {
			if (g_callbackStack[index] == entry) return true;
		}
		return false;
	}

	[[nodiscard]] bool IsActiveApiCall(const void* owner) noexcept
	{
		for (std::uint32_t index = 0; index < g_apiCallDepth; ++index) {
			if (g_apiCallStack[index] == owner) return true;
		}
		return false;
	}

	class CallGate final {
		static constexpr std::uint64_t kClosed = std::uint64_t{ 1 } << 63;
		static constexpr std::uint64_t kCountMask = ~kClosed;

	public:
		[[nodiscard]] bool TryEnter() noexcept
		{
			auto state = _state.load(std::memory_order_acquire);
			for (;;) {
				if ((state & kClosed) != 0 || (state & kCountMask) == kCountMask) return false;
				if (_state.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel,
					std::memory_order_acquire)) return true;
			}
		}

		void Leave() noexcept
		{
			const auto previous = _state.fetch_sub(1, std::memory_order_acq_rel);
			if ((previous & kClosed) != 0 && (previous & kCountMask) == 1) {
				_state.notify_all();
			}
		}

		[[nodiscard]] bool CloseAndWait() noexcept
		{
			_state.fetch_or(kClosed, std::memory_order_acq_rel);
			auto state = _state.load(std::memory_order_acquire);
			while ((state & kCountMask) != 0) {
				_state.wait(state, std::memory_order_acquire);
				state = _state.load(std::memory_order_acquire);
			}
			return true;
		}

		[[nodiscard]] bool IsClosed() const noexcept
		{
			return (_state.load(std::memory_order_acquire) & kClosed) != 0;
		}

	private:
		std::atomic<std::uint64_t> _state{};
	};

	class ApiCallGuard final {
	public:
		ApiCallGuard(CallGate& gate, const void* owner) noexcept : _gate(&gate)
		{
			if (!gate.TryEnter()) return;
			if (g_apiCallDepth >= std::size(g_apiCallStack)) {
				gate.Leave();
				return;
			}
			g_apiCallStack[g_apiCallDepth++] = owner;
			_entered = true;
		}
		~ApiCallGuard()
		{
			if (_entered) {
				g_apiCallStack[--g_apiCallDepth] = nullptr;
				_gate->Leave();
			}
		}
		[[nodiscard]] bool Entered() const noexcept { return _entered; }

	private:
		CallGate* _gate;
		bool _entered{};
	};

	[[nodiscard]] RegistrationStatus CopyAndValidateId(const char* id, std::string& copied) noexcept
	{
		if (!id || id[0] == '\0') return RegistrationStatus::InvalidId;
		std::size_t length = 0;
		while (length <= kMaxProviderIdLength && id[length] != '\0') ++length;
		if (length == 0 || length > kMaxProviderIdLength) return RegistrationStatus::InvalidId;
		try {
			copied.assign(id, length);
		} catch (...) {
			return RegistrationStatus::AllocationFailure;
		}
		for (const char value : copied) {
			const bool alphaNumeric = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
				(value >= '0' && value <= '9');
			if (!alphaNumeric && value != '-' && value != '_' && value != '.') return RegistrationStatus::InvalidId;
		}
		return RegistrationStatus::Added;
	}

	[[nodiscard]] bool IsFiniteDamage(float value) noexcept
	{
		return std::isfinite(value) && value >= 0.0f && value <= kMaxDamageValue;
	}

	[[nodiscard]] bool ValidSnapshot(const DamageSnapshotV3& damage) noexcept
	{
		if (damage.structSize < sizeof(DamageSnapshotV3) || (damage.validMask & ~kKnownDamageMask) != 0) return false;
		if ((damage.validMask & ComponentHealth) && !IsFiniteDamage(damage.healthDamage)) return false;
		if ((damage.validMask & ComponentPhysical) && !IsFiniteDamage(damage.physicalDamage)) return false;
		if ((damage.validMask & ComponentTotal) && !IsFiniteDamage(damage.totalDamage)) return false;
		if ((damage.validMask & ComponentTargetedLimb) && !IsFiniteDamage(damage.targetedLimbDamage)) return false;
		if ((damage.validMask & ComponentResistanceIntermediate) &&
			!IsFiniteDamage(damage.resistanceIntermediate)) return false;
		return true;
	}

	[[nodiscard]] bool ValidMultiplier(float value) noexcept
	{
		return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
	}

	[[nodiscard]] bool IsValidOutgoingContext(const OutgoingCalculationContextV3& context) noexcept
	{
		if (context.structSize < sizeof(OutgoingCalculationContextV3) || context.version != kInterfaceVersion) return false;
		if (context.evaluationKind != EvaluationKind::Calculation && context.evaluationKind != EvaluationKind::Prediction) return false;
		if (context.attackerKind == ActorKind::Unknown || context.attackerKind > ActorKind::Other) return false;
		if (context.profile == OutgoingProfile::Unknown || context.profile > OutgoingProfile::WeaponProjectile) return false;
		if (!context.attacker || !context.weapon) return false;
		if (context.modifiableMask == ComponentNone || (context.modifiableMask & ~kSupportedOutgoingMask) != 0) return false;
		if ((context.modifiableMask & ComponentHealth) == 0) return false;
		return true;
	}

	[[nodiscard]] bool IsValidIncomingContext(const IncomingHealthContextV3& context) noexcept
	{
		return context.structSize >= sizeof(IncomingHealthContextV3) && context.version == kInterfaceVersion &&
			context.phase == IncomingPhase::HealthAfterResistanceBeforeDifficulty &&
			context.confidence == ContextConfidence::VerifiedAdapterCallsite &&
			(context.targetKind == ActorKind::Player || context.targetKind == ActorKind::NPC ||
				context.targetKind == ActorKind::Other) &&
			context.powerArmor != PowerArmorState::Unknown && context.powerArmor <= PowerArmorState::Equipped &&
			context.target != nullptr;
	}

	[[nodiscard]] OutgoingDispatchV3 OutgoingIdentity(DispatchStatus status,
		const DamageSnapshotV3& damage) noexcept
	{
		return { status, ComponentNone, damage };
	}

	[[nodiscard]] IncomingDispatchV3 IncomingIdentity(DispatchStatus status, float damage) noexcept
	{
		return { status, damage };
	}

	struct OutgoingStage {};
	struct IncomingStage {};

	template <class Stage>
	struct StageTraits;

	template <>
	struct StageTraits<OutgoingStage> {
		using Descriptor = OutgoingProviderV3;
		using Callback = OutgoingCalculationCallbackV3;
		using Context = OutgoingCalculationContextV3;
		using Result = OutgoingResultV3;
		using Dispatch = OutgoingDispatchV3;
		static constexpr std::uint32_t kNoProvidersStatus = static_cast<std::uint32_t>(DispatchStatus::NoProviders);
		static std::uint32_t Filter(const Descriptor& descriptor) noexcept { return descriptor.evaluationMask; }
		static bool ValidDescriptor(const Descriptor& descriptor) noexcept
		{
			return descriptor.structSize >= sizeof(Descriptor) && descriptor.version == kInterfaceVersion &&
				descriptor.callback && descriptor.evaluationMask != 0 &&
				(descriptor.evaluationMask & ~kKnownEvaluationMask) == 0;
		}
		static bool Matches(const Descriptor& descriptor, const Context& context) noexcept
		{
			const auto bit = context.evaluationKind == EvaluationKind::Calculation ? EvaluationCalculation : EvaluationPrediction;
			return (descriptor.evaluationMask & bit) != 0;
		}
		static CallbackStatus Invoke(const Descriptor& descriptor, const Context& context, Result& result)
		{
			return descriptor.callback(descriptor.providerContext, &context, &result);
		}
		static bool ValidateContext(const Context& context) noexcept { return IsValidOutgoingContext(context); }
		static bool ValidateInput(const Context& context) noexcept
		{
			return ValidSnapshot(context.damage) && (context.modifiableMask & ~context.damage.validMask) == 0;
		}
		static bool HasDamage(const Context& context) noexcept
		{
			const auto applicable = context.damage.validMask & context.modifiableMask;
			if ((applicable & ComponentHealth) && context.damage.healthDamage > 0.0f) return true;
			if ((applicable & ComponentPhysical) && context.damage.physicalDamage > 0.0f) return true;
			return false;
		}
		static bool ValidatePolicy(const Context& context) noexcept
		{
			if (context.profile != OutgoingProfile::WeaponDirect ||
				(context.damage.validMask & ComponentPhysical) == 0 || context.damage.physicalDamage == 0.0f) return true;
			return (context.modifiableMask & (ComponentHealth | ComponentPhysical)) ==
				(ComponentHealth | ComponentPhysical);
		}
		static bool ValidateResult(const Context& context, const Result& result) noexcept
		{
			if (result.structSize < sizeof(Result) || result.version != kInterfaceVersion ||
				result.status != CallbackStatus::Apply || !ValidMultiplier(result.multiplier) ||
				result.componentMask == ComponentNone || (result.componentMask & ~context.modifiableMask) != 0) return false;
			if ((result.componentMask & ComponentHealth) == 0) return false;
			if (context.profile == OutgoingProfile::WeaponDirect &&
				(context.damage.validMask & ComponentPhysical) && context.damage.physicalDamage > 0.0f &&
				result.componentMask != (ComponentHealth | ComponentPhysical)) return false;
			return true;
		}
		static void Apply(const Result& result, DamageSnapshotV3& damage) noexcept
		{
			if (result.componentMask & ComponentHealth) damage.healthDamage *= result.multiplier;
			if (result.componentMask & ComponentPhysical) damage.physicalDamage *= result.multiplier;
		}
		static Dispatch Identity(std::uint32_t status, const Context& context) noexcept
		{
			return OutgoingIdentity(static_cast<DispatchStatus>(status), context.damage);
		}
		static Result InitialResult() noexcept
		{
			return { sizeof(Result), kInterfaceVersion, CallbackStatus::NoChange, ComponentNone, 1.0f };
		}
		static Dispatch Successful(const Context& context, const DamageSnapshotV3& damage,
			std::uint32_t changedMask) noexcept
		{
			(void)context;
			return { DispatchStatus::Applied, changedMask, damage };
		}
	};

	template <>
	struct StageTraits<IncomingStage> {
		using Descriptor = IncomingHealthProviderV3;
		using Callback = IncomingHealthCallbackV3;
		using Context = IncomingHealthContextV3;
		using Result = IncomingHealthResultV3;
		using Dispatch = IncomingDispatchV3;
		static constexpr std::uint32_t kNoProvidersStatus = static_cast<std::uint32_t>(DispatchStatus::NoProviders);
		static std::uint32_t Filter(const Descriptor&) noexcept { return 0; }
		static bool ValidDescriptor(const Descriptor& descriptor) noexcept
		{
			return descriptor.structSize >= sizeof(Descriptor) && descriptor.version == kInterfaceVersion && descriptor.callback;
		}
		static bool Matches(const Descriptor&, const Context&) noexcept { return true; }
		static CallbackStatus Invoke(const Descriptor& descriptor, const Context& context, Result& result)
		{
			return descriptor.callback(descriptor.providerContext, &context, &result);
		}
		static bool ValidateContext(const Context& context) noexcept { return IsValidIncomingContext(context); }
		static bool ValidateInput(const Context& context) noexcept { return IsFiniteDamage(context.healthDamage); }
		static bool HasDamage(const Context& context) noexcept { return context.healthDamage > 0.0f; }
		static bool ValidatePolicy(const Context&) noexcept { return true; }
		static bool ValidateResult(const Context&, const Result& result) noexcept
		{
			return result.structSize >= sizeof(Result) && result.version == kInterfaceVersion &&
				result.status == CallbackStatus::Apply && ValidMultiplier(result.multiplier);
		}
		static void Apply(const Result& result, float& damage) noexcept { damage *= result.multiplier; }
		static Dispatch Identity(std::uint32_t status, const Context& context) noexcept
		{
			return IncomingIdentity(static_cast<DispatchStatus>(status), context.healthDamage);
		}
		static Result InitialResult() noexcept
		{
			return { sizeof(Result), kInterfaceVersion, CallbackStatus::NoChange, 0, 1.0f };
		}
		static Dispatch Successful(const Context&, float damage, std::uint32_t) noexcept
		{
			return { DispatchStatus::Applied, damage };
		}
	};

	template <class Stage>
	class StageRegistry final {
		using Traits = StageTraits<Stage>;
		using Descriptor = typename Traits::Descriptor;
		using Callback = typename Traits::Callback;
		using Context = typename Traits::Context;
		using Result = typename Traits::Result;
		using Dispatch = typename Traits::Dispatch;

		struct Entry {
			std::string id;
			std::int32_t priority;
			std::uint32_t filter;
			void* providerContext;
			Callback callback;
			ProviderHandleV3 handle;
			mutable std::mutex gate;
			std::condition_variable quiescent;
			bool enabled{ true };
			std::uint32_t inFlight{ 0 };

			bool TryEnter()
			{
				std::scoped_lock lock{ gate };
				if (!enabled) return false;
				++inFlight;
				return true;
			}
			void Leave() noexcept
			{
				std::scoped_lock lock{ gate };
				if (inFlight > 0) --inFlight;
				if (inFlight == 0) quiescent.notify_all();
			}
			void Disable() noexcept
			{
				std::scoped_lock lock{ gate };
				enabled = false;
				if (inFlight == 0) quiescent.notify_all();
			}
			void Wait()
			{
				std::unique_lock lock{ gate };
				quiescent.wait(lock, [&] { return inFlight == 0; });
			}
		};

		using ProviderList = std::vector<std::shared_ptr<Entry>>;
		using Snapshot = std::shared_ptr<const ProviderList>;

	public:
		StageRegistry() : _snapshot(std::make_shared<const ProviderList>()) {}
		~StageRegistry() = default;

		// Must only run after the Dispatcher call gate is closed and drained.
		// Atomic exchange and container destruction release ownership without
		// constructing replacement snapshots or allocating memory.
		void ShutdownNoAlloc() noexcept
		{
			(void)_snapshot.exchange(Snapshot{}, std::memory_order_acq_rel);
			_retired.clear();
		}

		RegistrationResult Register(const Descriptor* descriptor, ProviderHandleV3 handle)
		{
			if (!descriptor || !Traits::ValidDescriptor(*descriptor) || handle.value == 0) {
				return { RegistrationStatus::InvalidDescriptor, {}, false };
			}
			std::string id;
			const auto idStatus = CopyAndValidateId(descriptor->providerId, id);
			if (idStatus != RegistrationStatus::Added) return { idStatus, {}, false };

			std::scoped_lock lock{ _writeMutex };
			const auto current = _snapshot.load(std::memory_order_acquire);
			if (std::any_of(current->begin(), current->end(), [&](const auto& item) { return item->id == id; })) {
				return { RegistrationStatus::Duplicate, {}, false };
			}
			if (std::any_of(_retired.begin(), _retired.end(), [&](const auto& item) { return item.second->id == id; })) {
				return { RegistrationStatus::Duplicate, {}, false };
			}
			std::shared_ptr<Entry> nextEntry;
			Snapshot nextSnapshot;
			try {
				nextEntry = std::make_shared<Entry>();
				nextEntry->id = std::move(id);
				nextEntry->priority = descriptor->priority;
				nextEntry->filter = Traits::Filter(*descriptor);
				nextEntry->providerContext = descriptor->providerContext;
				nextEntry->callback = descriptor->callback;
				nextEntry->handle = handle;
				auto next = std::make_shared<ProviderList>(*current);
				next->push_back(nextEntry);
				std::sort(next->begin(), next->end(), [](const auto& left, const auto& right) {
					if (left->priority != right->priority) return left->priority < right->priority;
					return left->id < right->id;
				});
				nextSnapshot = std::move(next);
			} catch (...) {
				return { RegistrationStatus::AllocationFailure, {}, false };
			}
			_snapshot.store(std::move(nextSnapshot), std::memory_order_release);
			return { RegistrationStatus::Added, handle, true };
		}

		UnregisterStatus Unregister(ProviderHandleV3 handle)
		{
			std::scoped_lock lock{ _writeMutex };
			const auto current = _snapshot.load(std::memory_order_acquire);
			const auto found = std::find_if(current->begin(), current->end(), [&](const auto& item) {
				return item->handle.value == handle.value;
			});
			if (found == current->end()) return UnregisterStatus::NotFound;
			const auto entry = *found;
			if (ConsumeUnregisterFailure(UnregisterFailurePoint::SnapshotPreparation)) {
				return UnregisterStatus::AllocationFailure;
			}
			Snapshot nextSnapshot;
			try {
				auto next = std::make_shared<ProviderList>();
				next->reserve(current->size() - 1);
				for (const auto& item : *current) if (item != entry) next->push_back(item);
				nextSnapshot = std::move(next);
			} catch (...) {
				return UnregisterStatus::AllocationFailure;
			}
			try {
				if (ConsumeUnregisterFailure(UnregisterFailurePoint::RetirementInsertion)) {
					return UnregisterStatus::AllocationFailure;
				}
				const auto [unused, inserted] = _retired.emplace(handle.value, entry);
				(void)unused;
				if (!inserted) return UnregisterStatus::AllocationFailure;
			} catch (...) {
				return UnregisterStatus::AllocationFailure;
			}
			try {
				entry->Disable();
			} catch (...) {
				_retired.erase(handle.value);
				return UnregisterStatus::AllocationFailure;
			}
			_snapshot.store(std::move(nextSnapshot), std::memory_order_release);
			return UnregisterStatus::Removed;
		}

		QuiescenceStatus WaitQuiescent(ProviderHandleV3 handle)
		{
			std::shared_ptr<Entry> entry;
			{
				std::scoped_lock lock{ _writeMutex };
				const auto found = _retired.find(handle.value);
				if (found == _retired.end()) return QuiescenceStatus::NotFound;
				entry = found->second;
			}
			if (IsActiveCallback(entry.get())) return QuiescenceStatus::WouldDeadlock;
			try {
				entry->Wait();
			} catch (...) {
				return QuiescenceStatus::WaitFailure;
			}
			{
				std::scoped_lock lock{ _writeMutex };
				_retired.erase(handle.value);
			}
			return QuiescenceStatus::Quiescent;
		}

		Dispatch DispatchContext(const Context& context) const noexcept
		{
			if (!Traits::ValidateContext(context)) return Traits::Identity(
				static_cast<std::uint32_t>(DispatchStatus::InvalidContext), context);
			if (!Traits::ValidateInput(context)) return Traits::Identity(
				static_cast<std::uint32_t>(DispatchStatus::InvalidInput), context);
			if (!Traits::HasDamage(context)) {
				return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::NoDamage), context);
			}
			if (!Traits::ValidatePolicy(context)) return Traits::Identity(
				static_cast<std::uint32_t>(DispatchStatus::InvalidContext), context);
			DispatchGuard recursionGuard{ this };
			if (!recursionGuard.Entered()) return Traits::Identity(
				static_cast<std::uint32_t>(DispatchStatus::RecursiveDispatch), context);

			const auto providers = _snapshot.load(std::memory_order_acquire);
			bool invoked = false;
			std::uint32_t changedMask = ComponentNone;
			if constexpr (std::is_same_v<Stage, OutgoingStage>) {
				DamageSnapshotV3 working = context.damage;
				for (const auto& entry : *providers) {
					if (!Traits::Matches(DescriptorView(*entry), context)) continue;
					if (!entry->TryEnter()) continue;
					CallbackGuard callbackGuard{ entry.get() };
					if (!callbackGuard.Entered()) {
						entry->Leave();
						return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::RecursiveDispatch), context);
					}
					Result result = Traits::InitialResult();
					CallbackStatus callbackStatus = CallbackStatus::Failure;
					try {
						callbackStatus = entry->callback(entry->providerContext, &context, &result);
					} catch (...) {
						entry->Leave();
						return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::ProviderFailure), context);
					}
					entry->Leave();
					if (callbackStatus == CallbackStatus::NoChange && result.status == CallbackStatus::NoChange &&
						result.structSize >= sizeof(Result) && result.version == kInterfaceVersion &&
						result.componentMask == ComponentNone && result.multiplier == 1.0f) {
						invoked = true;
						continue;
					}
					if (callbackStatus == CallbackStatus::Failure || result.status == CallbackStatus::Failure) {
						return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::ProviderFailure), context);
					}
					if (callbackStatus != CallbackStatus::Apply || result.status != CallbackStatus::Apply ||
						!Traits::ValidateResult(context, result)) {
						return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::InvalidProviderResult), context);
					}
					DamageSnapshotV3 candidate = working;
					Traits::Apply(result, candidate);
					if (!ValidSnapshot(candidate)) return Traits::Identity(
						static_cast<std::uint32_t>(DispatchStatus::InvalidProviderResult), context);
					working = candidate;
					changedMask |= result.componentMask;
					invoked = true;
				}
				if (!invoked) return Traits::Identity(Traits::kNoProvidersStatus, context);
				if (changedMask == ComponentNone) return Traits::Identity(
					static_cast<std::uint32_t>(DispatchStatus::NoChange), context);
				return Traits::Successful(context, working, changedMask);
			} else {
				float working = context.healthDamage;
				bool applied = false;
				for (const auto& entry : *providers) {
					if (!entry->TryEnter()) continue;
					CallbackGuard callbackGuard{ entry.get() };
					if (!callbackGuard.Entered()) {
						entry->Leave();
						return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::RecursiveDispatch), context);
					}
					Result result = Traits::InitialResult();
					CallbackStatus callbackStatus = CallbackStatus::Failure;
					try {
						callbackStatus = entry->callback(entry->providerContext, &context, &result);
					} catch (...) {
						entry->Leave();
						return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::ProviderFailure), context);
					}
					entry->Leave();
					if (callbackStatus == CallbackStatus::NoChange && result.status == CallbackStatus::NoChange &&
						result.structSize >= sizeof(Result) && result.version == kInterfaceVersion && result.multiplier == 1.0f) {
						invoked = true;
						continue;
					}
					if (callbackStatus == CallbackStatus::Failure || result.status == CallbackStatus::Failure) {
						return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::ProviderFailure), context);
					}
					if (callbackStatus != CallbackStatus::Apply || result.status != CallbackStatus::Apply ||
						!Traits::ValidateResult(context, result)) {
						return Traits::Identity(static_cast<std::uint32_t>(DispatchStatus::InvalidProviderResult), context);
					}
					const float candidate = working * result.multiplier;
					if (!IsFiniteDamage(candidate)) return Traits::Identity(
						static_cast<std::uint32_t>(DispatchStatus::InvalidProviderResult), context);
					working = candidate;
					invoked = true;
					applied = true;
				}
				if (!invoked) return Traits::Identity(Traits::kNoProvidersStatus, context);
				if (!applied) return Traits::Identity(
					static_cast<std::uint32_t>(DispatchStatus::NoChange), context);
				return Traits::Successful(context, working, ComponentHealth);
			}
		}

	private:
		// Only used by outgoing matching; it reconstructs the descriptor fields held by Entry.
		Descriptor DescriptorView(const Entry& entry) const noexcept
		{
			if constexpr (std::is_same_v<Stage, OutgoingStage>) {
				return { sizeof(Descriptor), kInterfaceVersion, entry.id.c_str(), entry.priority,
					entry.filter, entry.providerContext, entry.callback };
			} else {
				return { sizeof(Descriptor), kInterfaceVersion, entry.id.c_str(), entry.priority,
					entry.providerContext, entry.callback };
			}
		}

		mutable std::mutex _writeMutex;
		std::atomic<Snapshot> _snapshot;
		std::map<std::uint64_t, std::shared_ptr<Entry>> _retired;
	};
}

struct Dispatcher::Impl {
	StageRegistry<OutgoingStage> outgoing;
	StageRegistry<IncomingStage> incoming;
	std::atomic<std::uint64_t> nextHandle{ 1 };
	CallGate calls;
	std::mutex shutdownMutex;
	bool shutdownComplete{};

	bool AllocateHandle(ProviderHandleV3& handle) noexcept
	{
		auto current = nextHandle.load(std::memory_order_relaxed);
		while (current != 0) {
			const auto next = current == std::numeric_limits<std::uint64_t>::max() ? 0 : current + 1;
			if (nextHandle.compare_exchange_weak(current, next, std::memory_order_relaxed,
				std::memory_order_relaxed)) {
				handle.value = current;
				return true;
			}
		}
		return false;
	}
};

void Testing::FailNextUnregisterAt(UnregisterFailurePoint point) noexcept
{
	g_unregisterFailurePoint.store(point, std::memory_order_release);
}

Dispatcher::Dispatcher() : _impl(std::make_unique<Impl>()) {}
Dispatcher::~Dispatcher()
{
	if (_impl && !_impl->shutdownComplete && Shutdown() != ShutdownStatus::Complete) std::terminate();
}

ShutdownStatus Dispatcher::Shutdown() noexcept
{
	if (!_impl) return ShutdownStatus::Complete;
	if (IsActiveApiCall(_impl.get())) return ShutdownStatus::WouldDeadlock;
	try {
		std::scoped_lock lock{ _impl->shutdownMutex };
		if (_impl->shutdownComplete) return ShutdownStatus::Complete;
		if (!_impl->calls.CloseAndWait()) return ShutdownStatus::WaitFailure;
		_impl->outgoing.ShutdownNoAlloc();
		_impl->incoming.ShutdownNoAlloc();
		_impl->shutdownComplete = true;
		return ShutdownStatus::Complete;
	} catch (...) {
		return ShutdownStatus::WaitFailure;
	}
}

bool Dispatcher::IsAcceptingCallsForTesting() const noexcept
{
	return _impl && !_impl->calls.IsClosed();
}

RegistrationResult Dispatcher::RegisterOutgoing(const OutgoingProviderV3* provider)
{
	ApiCallGuard call{ _impl->calls, _impl.get() };
	if (!call.Entered()) return { RegistrationStatus::DispatcherClosed, {}, false };
	ProviderHandleV3 handle{};
	if (!_impl->AllocateHandle(handle)) return { RegistrationStatus::HandleExhausted, {}, false };
	return _impl->outgoing.Register(provider, handle);
}
RegistrationResult Dispatcher::RegisterIncoming(const IncomingHealthProviderV3* provider)
{
	ApiCallGuard call{ _impl->calls, _impl.get() };
	if (!call.Entered()) return { RegistrationStatus::DispatcherClosed, {}, false };
	ProviderHandleV3 handle{};
	if (!_impl->AllocateHandle(handle)) return { RegistrationStatus::HandleExhausted, {}, false };
	return _impl->incoming.Register(provider, handle);
}
UnregisterStatus Dispatcher::UnregisterOutgoing(ProviderHandleV3 handle)
{
	ApiCallGuard call{ _impl->calls, _impl.get() };
	if (!call.Entered()) return UnregisterStatus::DispatcherClosed;
	return _impl->outgoing.Unregister(handle);
}
UnregisterStatus Dispatcher::UnregisterIncoming(ProviderHandleV3 handle)
{
	ApiCallGuard call{ _impl->calls, _impl.get() };
	if (!call.Entered()) return UnregisterStatus::DispatcherClosed;
	return _impl->incoming.Unregister(handle);
}
QuiescenceStatus Dispatcher::WaitOutgoingQuiescent(ProviderHandleV3 handle)
{
	ApiCallGuard call{ _impl->calls, _impl.get() };
	if (!call.Entered()) return QuiescenceStatus::DispatcherClosed;
	return _impl->outgoing.WaitQuiescent(handle);
}
QuiescenceStatus Dispatcher::WaitIncomingQuiescent(ProviderHandleV3 handle)
{
	ApiCallGuard call{ _impl->calls, _impl.get() };
	if (!call.Entered()) return QuiescenceStatus::DispatcherClosed;
	return _impl->incoming.WaitQuiescent(handle);
}
OutgoingDispatchV3 Dispatcher::DispatchOutgoing(const OutgoingCalculationContextV3& context) const noexcept
{
	ApiCallGuard call{ _impl->calls, _impl.get() };
	if (!call.Entered()) return OutgoingIdentity(DispatchStatus::DispatcherClosed, context.damage);
	return _impl->outgoing.DispatchContext(context);
}
IncomingDispatchV3 Dispatcher::DispatchIncoming(const IncomingHealthContextV3& context) const noexcept
{
	ApiCallGuard call{ _impl->calls, _impl.get() };
	if (!call.Entered()) return IncomingIdentity(DispatchStatus::DispatcherClosed, context.healthDamage);
	return _impl->incoming.DispatchContext(context);
}
}
