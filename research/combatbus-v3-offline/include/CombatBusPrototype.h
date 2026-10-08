#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <type_traits>
#include <iterator>
#include <mutex>

// Offline ABI candidate only. It models provider contracts and does not mirror
// Fallout 4's HitData layout. Opaque engine handles are borrowed for one callback.
namespace IIF::CombatBus::PrototypeV3 {
	// Versioned research candidate only. The target ABI is Windows x64; callbacks
	// use __cdecl (the unified Win64 calling convention). No exported DLL entrypoint
	// or cross-module ABI has been validated by these offline fixtures.
	inline constexpr std::uint32_t kInterfaceVersion = 3;
	inline constexpr std::uint32_t kMaxProviderIdLength = 127;
	inline constexpr float kMaxDamageValue = 100000000.0f;

	using Enum32 = std::uint32_t;

	enum class EvaluationKind : Enum32 { Unknown = 0, Calculation = 1, Prediction = 2 };
	enum class ActorKind : Enum32 { Unknown = 0, Player = 1, NPC = 2, Other = 3 };
	enum class OutgoingProfile : Enum32 { Unknown = 0, WeaponDirect = 1, WeaponMelee = 2, WeaponProjectile = 3 };
	enum class IncomingPhase : Enum32 { Unknown = 0, HealthAfterResistanceBeforeDifficulty = 1 };
	enum class ContextConfidence : Enum32 { Unknown = 0, VerifiedAdapterCallsite = 1 };
	enum class PowerArmorState : Enum32 { Unknown = 0, NotEquipped = 1, Equipped = 2 };
	enum class CallbackStatus : Enum32 { NoChange = 0, Apply = 1, Failure = 2 };
	// Applied: at least one eligible Provider returned a validated Apply result.
	// NoChange: eligible Provider(s) ran, all returned NoChange.
	// NoProviders: no Provider matched this stage/evaluation.
	// NoDamage: valid, modifiable Health/Physical damage components were all zero.
	enum class DispatchStatus : Enum32 {
		Applied = 0,
		NoProviders = 1,
		NoDamage = 2,
		InvalidContext = 3,
		InvalidInput = 4,
		ProviderFailure = 5,
		InvalidProviderResult = 6,
		RecursiveDispatch = 7,
		NoChange = 8
	};
	enum class RegistrationStatus : Enum32 { Added = 0, Duplicate = 1, InvalidDescriptor = 2, InvalidId = 3, AllocationFailure = 4, HandleExhausted = 5 };
	enum class UnregisterStatus : Enum32 { Removed = 0, NotFound = 1, AllocationFailure = 2 };
	enum class QuiescenceStatus : Enum32 { Quiescent = 0, NotFound = 1, WouldDeadlock = 2, WaitFailure = 3 };
	// InterfaceV3 unregister/wait routing values; Provider handles are Dispatcher-global.
	enum class ProviderStage : Enum32 { OutgoingCalculation = 1, IncomingHealthProcessing = 2 };
	enum class UnregisterFailurePoint : Enum32 { None = 0, SnapshotPreparation = 1, RetirementInsertion = 2 };

#if defined(_WIN32)
#define IIF_COMBATBUS_CALL __cdecl
#else
#define IIF_COMBATBUS_CALL
#endif

	enum DamageComponentMask : std::uint32_t {
		ComponentNone = 0,
		ComponentHealth = 1u << 0,
		ComponentPhysical = 1u << 1,
		ComponentTotal = 1u << 2,
		ComponentTargetedLimb = 1u << 3,
		ComponentResistanceIntermediate = 1u << 4
	};

	enum EvaluationMask : std::uint32_t {
		EvaluationCalculation = 1u << 0,
		EvaluationPrediction = 1u << 1
	};

	struct DamageSnapshotV3 {
		std::uint32_t structSize;
		std::uint32_t validMask;
		float healthDamage;
		float physicalDamage;
		float totalDamage;
		float targetedLimbDamage;
		float resistanceIntermediate;
	};

	// Outgoing Calculation is a side-effect-free policy calculation contract.
	// All Providers see this immutable original numeric snapshot; dispatch composes
	// their validated outputs separately in priority order.
	struct OutgoingCalculationContextV3 {
		std::uint32_t structSize;
		std::uint32_t version;
		EvaluationKind evaluationKind;
		ActorKind attackerKind;
		OutgoingProfile profile;
		std::uint32_t modifiableMask;
		void* attacker;  // borrowed opaque handle; callback must not retain it
		void* target;    // may be null and is borrowed for the callback only
		void* weapon;    // borrowed opaque handle; callback must not retain it
		DamageSnapshotV3 damage; // immutable original input; every Provider sees this same snapshot
	};

	struct OutgoingResultV3 {
		std::uint32_t structSize;
		std::uint32_t version;
		CallbackStatus status;
		std::uint32_t componentMask;
		float multiplier;
	};

	// Incoming Health is a distinct candidate processing stage, not a native
	// EntryPoint identity. All opaque handles are borrowed for the callback only.
	struct IncomingHealthContextV3 {
		std::uint32_t structSize;
		std::uint32_t version;
		IncomingPhase phase; // after resistance, before difficulty scaling; candidate adapter stage only
		ContextConfidence confidence;
		ActorKind targetKind;
		PowerArmorState powerArmor;
		void* attacker; // borrowed opaque handle; callback must not retain it
		void* target;   // borrowed opaque handle; callback must not retain it
		void* weapon;   // optional borrowed opaque handle
		float healthDamage;
	};

	struct IncomingHealthResultV3 {
		std::uint32_t structSize;
		std::uint32_t version;
		CallbackStatus status;
		std::uint32_t reserved;
		float multiplier;
	};

	using OutgoingCalculationCallbackV3 = CallbackStatus (IIF_COMBATBUS_CALL *)(void* providerContext,
		const OutgoingCalculationContextV3* context, OutgoingResultV3* result);
	using IncomingHealthCallbackV3 = CallbackStatus (IIF_COMBATBUS_CALL *)(void* providerContext,
		const IncomingHealthContextV3* context, IncomingHealthResultV3* result);

	struct OutgoingProviderV3 {
		std::uint32_t structSize;
		std::uint32_t version;
		const char* providerId; // copied during registration; ID uniqueness is stage-local
		std::int32_t priority;
		std::uint32_t evaluationMask;
		void* providerContext; // borrowed until unregister + successful WaitQuiescent
		OutgoingCalculationCallbackV3 callback;
	};

	struct IncomingHealthProviderV3 {
		std::uint32_t structSize;
		std::uint32_t version;
		const char* providerId; // copied during registration; ID uniqueness is stage-local
		std::int32_t priority;
		void* providerContext; // borrowed until unregister + successful WaitQuiescent
		IncomingHealthCallbackV3 callback;
	};

	struct ProviderHandleV3 {
		std::uint64_t value; // nonzero and unique within one Dispatcher across both stages
	};

	struct OutgoingDispatchV3 {
		DispatchStatus status;
		std::uint32_t changedMask;
		DamageSnapshotV3 damage;
	};

	struct IncomingDispatchV3 {
		DispatchStatus status;
		float healthDamage;
	};

	struct InterfaceV3 {
		std::uint32_t structSize;
		std::uint32_t version;
		void* registry;
		std::uint32_t (IIF_COMBATBUS_CALL *registerOutgoing)(void* registry, const OutgoingProviderV3* provider, ProviderHandleV3* outHandle);
		std::uint32_t (IIF_COMBATBUS_CALL *registerIncoming)(void* registry, const IncomingHealthProviderV3* provider, ProviderHandleV3* outHandle);
		std::uint32_t (IIF_COMBATBUS_CALL *unregisterProvider)(void* registry, ProviderHandleV3 handle, std::uint32_t stage); // ProviderStage value
		std::uint32_t (IIF_COMBATBUS_CALL *waitProviderQuiescent)(void* registry, ProviderHandleV3 handle, std::uint32_t stage); // ProviderStage value
	};

	struct RegistrationResult {
		RegistrationStatus status;
		ProviderHandleV3 handle;
		bool added;
	};

	class Dispatcher final {
	public:
		Dispatcher();
		~Dispatcher();
		Dispatcher(const Dispatcher&) = delete;
		Dispatcher& operator=(const Dispatcher&) = delete;

		RegistrationResult RegisterOutgoing(const OutgoingProviderV3* provider);
		RegistrationResult RegisterIncoming(const IncomingHealthProviderV3* provider);
		UnregisterStatus UnregisterOutgoing(ProviderHandleV3 handle);
		UnregisterStatus UnregisterIncoming(ProviderHandleV3 handle);
		QuiescenceStatus WaitOutgoingQuiescent(ProviderHandleV3 handle);
		QuiescenceStatus WaitIncomingQuiescent(ProviderHandleV3 handle);

		OutgoingDispatchV3 DispatchOutgoing(const OutgoingCalculationContextV3& context) const noexcept;
		IncomingDispatchV3 DispatchIncoming(const IncomingHealthContextV3& context) const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> _impl;
	};

	namespace Testing {
		// Deterministic failure injection for the offline Unregister fixtures only.
		void FailNextUnregisterAt(UnregisterFailurePoint point) noexcept;
	}

	// Explicit ABI layout checks; enum storage is fixed by enum class : uint32_t.
	static_assert(sizeof(EvaluationKind) == 4);
	static_assert(sizeof(ActorKind) == 4);
	static_assert(sizeof(OutgoingProfile) == 4);
	static_assert(sizeof(IncomingPhase) == 4);
	static_assert(sizeof(ContextConfidence) == 4);
	static_assert(sizeof(PowerArmorState) == 4);
	static_assert(sizeof(CallbackStatus) == 4);
	static_assert(sizeof(RegistrationStatus) == 4);
	static_assert(sizeof(ProviderStage) == 4);
	static_assert(sizeof(UnregisterFailurePoint) == 4);
	static_assert(static_cast<Enum32>(ProviderStage::OutgoingCalculation) == 1);
	static_assert(static_cast<Enum32>(ProviderStage::IncomingHealthProcessing) == 2);
#if defined(_WIN64)
	static_assert(sizeof(void*) == 8, "CombatBus V3 candidate targets Win64 only");
#endif
	static_assert(alignof(DamageSnapshotV3) == 4);
	static_assert(offsetof(DamageSnapshotV3, healthDamage) == 8);
	static_assert(offsetof(DamageSnapshotV3, resistanceIntermediate) == 24);
	static_assert(sizeof(DamageSnapshotV3) == 28);
	static_assert(alignof(OutgoingCalculationContextV3) == 8);
	static_assert(offsetof(OutgoingCalculationContextV3, attacker) == 24);
	static_assert(offsetof(OutgoingCalculationContextV3, damage) == 48);
	static_assert(sizeof(OutgoingCalculationContextV3) == 80);
	static_assert(alignof(OutgoingResultV3) == 4);
	static_assert(offsetof(OutgoingResultV3, multiplier) == 16);
	static_assert(sizeof(OutgoingResultV3) == 20);
	static_assert(alignof(IncomingHealthContextV3) == 8);
	static_assert(offsetof(IncomingHealthContextV3, attacker) == 24);
	static_assert(offsetof(IncomingHealthContextV3, healthDamage) == 48);
	static_assert(sizeof(IncomingHealthContextV3) == 56);
	static_assert(sizeof(IncomingHealthResultV3) == 20);
	static_assert(offsetof(OutgoingProviderV3, providerId) == 8);
	static_assert(offsetof(OutgoingProviderV3, callback) == 32);
	static_assert(sizeof(OutgoingProviderV3) == 40);
	static_assert(offsetof(IncomingHealthProviderV3, providerContext) == 24);
	static_assert(offsetof(IncomingHealthProviderV3, callback) == 32);
	static_assert(sizeof(IncomingHealthProviderV3) == 40);
	static_assert(sizeof(ProviderHandleV3) == 8);
	static_assert(offsetof(InterfaceV3, registerOutgoing) == 16);
	static_assert(offsetof(InterfaceV3, waitProviderQuiescent) == 40);
	static_assert(sizeof(InterfaceV3) == 48);
	static_assert(std::is_standard_layout_v<DamageSnapshotV3> && std::is_trivially_copyable_v<DamageSnapshotV3>);
	static_assert(std::is_standard_layout_v<OutgoingCalculationContextV3> &&
		std::is_trivially_copyable_v<OutgoingCalculationContextV3>);
	static_assert(std::is_standard_layout_v<IncomingHealthContextV3> &&
		std::is_trivially_copyable_v<IncomingHealthContextV3>);
	static_assert(std::is_standard_layout_v<OutgoingProviderV3> && std::is_trivially_copyable_v<OutgoingProviderV3>);
	static_assert(std::is_standard_layout_v<IncomingHealthProviderV3> &&
		std::is_trivially_copyable_v<IncomingHealthProviderV3>);
	static_assert(std::is_standard_layout_v<InterfaceV3> && std::is_trivially_copyable_v<InterfaceV3>);
}

#undef IIF_COMBATBUS_CALL
