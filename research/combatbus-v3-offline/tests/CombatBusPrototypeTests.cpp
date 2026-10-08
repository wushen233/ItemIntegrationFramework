#include "CombatBusPrototype.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace IIF::CombatBus::PrototypeV3;

namespace {
	int g_failures = 0;
	std::mutex g_orderMutex;
	std::vector<std::string> g_order;
	std::atomic<std::uint32_t> g_parallelCalls{ 0 };
	std::atomic<Dispatcher*> g_reentryDispatcher{ nullptr };
	std::atomic<DispatchStatus> g_nestedStatus{ DispatchStatus::Applied };
	std::atomic<std::uint32_t> g_lateRegistrations{ 0 };
	std::atomic<bool> g_waitFinished{ false };
	std::mutex g_gateMutex;
	std::condition_variable g_gateCv;
	bool g_callbackEntered = false;
	bool g_callbackRelease = false;

	void Require(bool condition, const char* message)
	{
		if (!condition) {
			++g_failures;
			std::cerr << "FAIL: " << message << '\n';
		}
	}

	void Record(const char* value)
	{
		std::scoped_lock lock{ g_orderMutex };
		g_order.emplace_back(value);
	}

	void ClearOrder()
	{
		std::scoped_lock lock{ g_orderMutex };
		g_order.clear();
	}

	std::vector<std::string> Order()
	{
		std::scoped_lock lock{ g_orderMutex };
		return g_order;
	}

	struct Modifier {
		const char* name;
		float multiplier;
		std::uint32_t mask;
		bool apply{ true };
	};

	CallbackStatus Multiply(void* opaque, const OutgoingCalculationContextV3*, OutgoingResultV3* result)
	{
		const auto& modifier = *static_cast<Modifier*>(opaque);
		Record(modifier.name);
		if (!modifier.apply) return CallbackStatus::NoChange;
		result->status = CallbackStatus::Apply;
		result->componentMask = modifier.mask;
		result->multiplier = modifier.multiplier;
		return CallbackStatus::Apply;
	}

	CallbackStatus FailingProvider(void*, const OutgoingCalculationContextV3*, OutgoingResultV3*)
	{
		return CallbackStatus::Failure;
	}

	CallbackStatus SideEffectThenFail(void* opaque, const OutgoingCalculationContextV3*, OutgoingResultV3*)
	{
		static_cast<std::atomic<std::uint32_t>*>(opaque)->fetch_add(1, std::memory_order_relaxed);
		return CallbackStatus::Failure;
	}

	struct IndependentPolicy {
		const char* name;
		float multiplier;
		std::uint32_t componentMask;
		float observedHealth{};
		float observedPhysical{};
	};

	CallbackStatus IndependentMultiply(void* opaque, const OutgoingCalculationContextV3* context,
		OutgoingResultV3* result)
	{
		auto& policy = *static_cast<IndependentPolicy*>(opaque);
		policy.observedHealth = context->damage.healthDamage;
		policy.observedPhysical = context->damage.physicalDamage;
		Record(policy.name);
		result->status = CallbackStatus::Apply;
		result->componentMask = policy.componentMask;
		result->multiplier = policy.multiplier;
		return CallbackStatus::Apply;
	}

	struct CrossStageCycle {
		Dispatcher* dispatcher;
		OutgoingCalculationContextV3 outgoing;
		IncomingHealthContextV3 incoming;
		DispatchStatus nestedIncoming{ DispatchStatus::Applied };
		DispatchStatus nestedOutgoing{ DispatchStatus::Applied };
	};

	CallbackStatus CrossStageOutgoing(void* opaque, const OutgoingCalculationContextV3*, OutgoingResultV3* result)
	{
		auto& state = *static_cast<CrossStageCycle*>(opaque);
		state.nestedIncoming = state.dispatcher->DispatchIncoming(state.incoming).status;
		result->status = CallbackStatus::Apply;
		result->componentMask = ComponentHealth | ComponentPhysical;
		result->multiplier = 1.0f;
		return CallbackStatus::Apply;
	}

	CallbackStatus CrossStageIncoming(void* opaque, const IncomingHealthContextV3*, IncomingHealthResultV3*)
	{
		auto& state = *static_cast<CrossStageCycle*>(opaque);
		state.nestedOutgoing = state.dispatcher->DispatchOutgoing(state.outgoing).status;
		return CallbackStatus::NoChange;
	}

	CallbackStatus NanProvider(void*, const OutgoingCalculationContextV3*, OutgoingResultV3* result)
	{
		result->status = CallbackStatus::Apply;
		result->componentMask = ComponentHealth | ComponentPhysical;
		result->multiplier = std::numeric_limits<float>::quiet_NaN();
		return CallbackStatus::Apply;
	}

	CallbackStatus ThrowProvider(void*, const OutgoingCalculationContextV3*, OutgoingResultV3*)
	{
		throw 7;
	}

	CallbackStatus ReentrantProvider(void*, const OutgoingCalculationContextV3* context, OutgoingResultV3* result)
	{
		auto* dispatcher = g_reentryDispatcher.load(std::memory_order_acquire);
		if (dispatcher) {
			g_nestedStatus.store(dispatcher->DispatchOutgoing(*context).status, std::memory_order_release);
		}
		result->status = CallbackStatus::Apply;
		result->componentMask = ComponentHealth | ComponentPhysical;
		result->multiplier = 0.9f;
		return CallbackStatus::Apply;
	}

	CallbackStatus RegisterDuringCallback(void* opaque, const OutgoingCalculationContextV3*, OutgoingResultV3* result)
	{
		auto* dispatcher = static_cast<Dispatcher*>(opaque);
		static Modifier late{ "late", 1.0f, ComponentHealth, false };
		static OutgoingProviderV3 provider{ sizeof(OutgoingProviderV3), kInterfaceVersion,
			"late-provider", 250, EvaluationCalculation, &late, &Multiply };
		if (dispatcher->RegisterOutgoing(&provider).added) g_lateRegistrations.fetch_add(1, std::memory_order_relaxed);
		result->status = CallbackStatus::Apply;
		result->componentMask = ComponentHealth | ComponentPhysical;
		result->multiplier = 1.0f;
		return CallbackStatus::Apply;
	}

	CallbackStatus CountProvider(void*, const OutgoingCalculationContextV3*, OutgoingResultV3* result)
	{
		g_parallelCalls.fetch_add(1, std::memory_order_relaxed);
		result->status = CallbackStatus::NoChange;
		result->componentMask = ComponentNone;
		result->multiplier = 1.0f;
		return CallbackStatus::NoChange;
	}

	CallbackStatus CountIncomingProvider(void* opaque, const IncomingHealthContextV3*, IncomingHealthResultV3*)
	{
		static_cast<std::atomic<std::uint32_t>*>(opaque)->fetch_add(1, std::memory_order_relaxed);
		return CallbackStatus::NoChange;
	}

	CallbackStatus BlockingProvider(void*, const OutgoingCalculationContextV3*, OutgoingResultV3* result)
	{
		{
			std::unique_lock lock{ g_gateMutex };
			g_callbackEntered = true;
			g_gateCv.notify_all();
			g_gateCv.wait(lock, [] { return g_callbackRelease; });
		}
		result->status = CallbackStatus::NoChange;
		result->componentMask = ComponentNone;
		result->multiplier = 1.0f;
		return CallbackStatus::NoChange;
	}

	CallbackStatus PASMultiplier(void*, const IncomingHealthContextV3* context, IncomingHealthResultV3* result)
	{
		if (context->powerArmor != PowerArmorState::Equipped) return CallbackStatus::NoChange;
		if (context->targetKind != ActorKind::Player && context->targetKind != ActorKind::NPC) {
			return CallbackStatus::NoChange;
		}
		result->status = CallbackStatus::Apply;
		result->multiplier = 0.5f;
		return CallbackStatus::Apply;
	}

	OutgoingCalculationContextV3 MakeOutgoing(EvaluationKind evaluation = EvaluationKind::Calculation,
		ActorKind attackerKind = ActorKind::Player, OutgoingProfile profile = OutgoingProfile::WeaponDirect,
		float health = 100.0f, float physical = 80.0f,
		std::uint32_t validMask = ComponentHealth | ComponentPhysical | ComponentTotal |
			ComponentTargetedLimb | ComponentResistanceIntermediate,
		std::uint32_t modifiableMask = ComponentHealth | ComponentPhysical)
	{
		static int attackerToken{};
		static int targetToken{};
		static int weaponToken{};
		return { sizeof(OutgoingCalculationContextV3), kInterfaceVersion, evaluation, attackerKind, profile,
			modifiableMask,
			&attackerToken, &targetToken, &weaponToken,
			{ sizeof(DamageSnapshotV3), validMask,
				health, physical, health, 12.0f, 20.0f } };
	}

	IncomingHealthContextV3 MakeIncoming(ActorKind targetKind, PowerArmorState powerArmor,
		float damage = 100.0f)
	{
		static int attackerToken{};
		static int targetToken{};
		static int weaponToken{};
		return { sizeof(IncomingHealthContextV3), kInterfaceVersion,
			IncomingPhase::HealthAfterResistanceBeforeDifficulty,
			ContextConfidence::VerifiedAdapterCallsite, targetKind, powerArmor,
			&attackerToken, &targetToken, &weaponToken, damage };
	}

	OutgoingProviderV3 MakeOutgoingProvider(const char* id, int priority, std::uint32_t evalMask,
		void* providerContext, OutgoingCalculationCallbackV3 callback = &Multiply)
	{
		return { sizeof(OutgoingProviderV3), kInterfaceVersion, id, priority, evalMask, providerContext, callback };
	}

	IncomingHealthProviderV3 MakeIncomingProvider(const char* id, int priority,
		IncomingHealthCallbackV3 callback = &PASMultiplier)
	{
		return { sizeof(IncomingHealthProviderV3), kInterfaceVersion, id, priority, nullptr, callback };
	}

	void TestPriorityAndComponents()
	{
		Dispatcher dispatcher;
		Modifier wrf{ "WRF", 0.8f, ComponentHealth | ComponentPhysical };
		Modifier csf{ "CSF", 0.5f, ComponentHealth | ComponentPhysical };
		const auto csfProvider = MakeOutgoingProvider("CSF", 200, EvaluationCalculation, &csf);
		const auto wrfProvider = MakeOutgoingProvider("WRF", 100, EvaluationCalculation, &wrf);
		Require(dispatcher.RegisterOutgoing(&csfProvider).added, "register CSF priority 200");
		Require(dispatcher.RegisterOutgoing(&wrfProvider).added, "register WRF priority 100");
		ClearOrder();
		const auto input = MakeOutgoing();
		const auto output = dispatcher.DispatchOutgoing(input);
		Require(output.status == DispatchStatus::Applied, "outgoing calculation dispatch succeeds");
		Require(Order() == std::vector<std::string>{ "WRF", "CSF" }, "WRF priority 100 runs before CSF 200");
		Require(std::abs(output.damage.healthDamage - 40.0f) < 0.001f, "Health and Physical receive composed 0.8 * 0.5 multiplier");
		Require(std::abs(output.damage.physicalDamage - 32.0f) < 0.001f, "physical input uses same multiplier");
		Require(output.damage.totalDamage == 100.0f && output.damage.targetedLimbDamage == 12.0f &&
			output.damage.resistanceIntermediate == 20.0f, "Total, Limb, and resistance intermediates stay unchanged");
		Require(output.changedMask == (ComponentHealth | ComponentPhysical), "changed mask reports only modified components");

		// Fixture-only model of the observed FC00B0 entry copy. This is not the native
		// resistance implementation and does not represent a HitData memory layout.
		auto afterNativeBoundary = output.damage;
		afterNativeBoundary.totalDamage = afterNativeBoundary.healthDamage;
		Require(afterNativeBoundary.totalDamage == 40.0f, "fixture rebuilds Total at its explicitly named resistance boundary");
	}

	void TestCrossStageCycleProtection()
	{
		Dispatcher dispatcher;
		CrossStageCycle state{ &dispatcher, MakeOutgoing(),
			MakeIncoming(ActorKind::NPC, PowerArmorState::Equipped) };
		const auto outgoing = MakeOutgoingProvider("cross-outgoing", 1, EvaluationCalculation,
			&state, &CrossStageOutgoing);
		const auto incoming = MakeIncomingProvider("cross-incoming", 1, &CrossStageIncoming);
		// Incoming callbacks use the Provider's context; both stages share this test state.
		IncomingHealthProviderV3 incomingWithState = incoming;
		incomingWithState.providerContext = &state;
		Require(dispatcher.RegisterOutgoing(&outgoing).added && dispatcher.RegisterIncoming(&incomingWithState).added,
			"register cross-stage cycle fixtures");
		const auto result = dispatcher.DispatchOutgoing(state.outgoing);
		Require(result.status == DispatchStatus::Applied && state.nestedIncoming == DispatchStatus::NoChange &&
			state.nestedOutgoing == DispatchStatus::RecursiveDispatch,
			"cross-stage dispatch is allowed while a cycle back into an active stage is rejected");
	}

	void TestIndependentSnapshotContract()
	{
		Dispatcher dispatcher;
		ClearOrder();
		IndependentPolicy wrf{ "WRF-original", 0.8f, ComponentHealth | ComponentPhysical };
		IndependentPolicy csf{ "CSF-original", 0.5f, ComponentHealth | ComponentPhysical };
		const auto wrfProvider = MakeOutgoingProvider("WRF-independent", 100,
			EvaluationCalculation, &wrf, &IndependentMultiply);
		const auto csfProvider = MakeOutgoingProvider("CSF-independent", 200,
			EvaluationCalculation, &csf, &IndependentMultiply);
		Require(dispatcher.RegisterOutgoing(&csfProvider).added && dispatcher.RegisterOutgoing(&wrfProvider).added,
			"register independent calculation policies");
		const auto input = MakeOutgoing();
		const auto output = dispatcher.DispatchOutgoing(input);
		Require(wrf.observedHealth == 100.0f && wrf.observedPhysical == 80.0f &&
			csf.observedHealth == 100.0f && csf.observedPhysical == 80.0f,
			"each Provider observes the original snapshot, not the prior Provider's accumulated result");
		Require(output.damage.healthDamage == 40.0f && output.damage.physicalDamage == 32.0f,
			"independent WRF and CSF multipliers compose in deterministic priority order");
		Require(input.damage.healthDamage == 100.0f && input.damage.physicalDamage == 80.0f,
			"callbacks leave the caller's original context unchanged");
		Require(Order() == std::vector<std::string>{ "WRF-original", "CSF-original" },
			"priority controls order without changing Provider input semantics");
	}

	void TestNoDamageUsesOnlyValidApplicableFields()
	{
		Dispatcher dispatcher;
		const auto countProvider = MakeOutgoingProvider("count", 1, EvaluationCalculation, nullptr, &CountProvider);
		Require(dispatcher.RegisterOutgoing(&countProvider).added, "register no-damage callback counter");
		g_parallelCalls.store(0, std::memory_order_relaxed);

		auto healthZeroPhysicalUnmodifiable = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponMelee, 0.0f, 80.0f, ComponentHealth | ComponentPhysical, ComponentHealth);
		const auto zeroAllowed = dispatcher.DispatchOutgoing(healthZeroPhysicalUnmodifiable);
		Require(zeroAllowed.status == DispatchStatus::NoDamage && g_parallelCalls.load(std::memory_order_relaxed) == 0,
			"zero modifiable Health ignores positive but unmodifiable Physical and invokes no Provider");

		auto physicalNanInvalid = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponDirect, 100.0f, std::numeric_limits<float>::quiet_NaN(),
			ComponentHealth, ComponentHealth);
		const auto nanInvalidResult = dispatcher.DispatchOutgoing(physicalNanInvalid);
		Require(nanInvalidResult.status == DispatchStatus::NoChange && g_parallelCalls.load(std::memory_order_relaxed) == 1,
			"Physical NaN without a valid bit is ignored while positive valid Health reaches the Provider");

		auto positiveHealthPhysicalInvalid = physicalNanInvalid;
		positiveHealthPhysicalInvalid.damage.healthDamage = 20.0f;
		const auto positive = dispatcher.DispatchOutgoing(positiveHealthPhysicalInvalid);
		Require(positive.status == DispatchStatus::NoChange && g_parallelCalls.load(std::memory_order_relaxed) == 2,
			"positive valid Health and invalid Physical dispatch without inspecting the invalid field");

		auto allZero = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponDirect, 0.0f, 0.0f);
		Require(dispatcher.DispatchOutgoing(allZero).status == DispatchStatus::NoDamage,
			"valid zero Health and Physical skip Provider callbacks");

		auto physicalZero = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponDirect, 60.0f, 0.0f);
		(void)physicalZero;
		Require(g_parallelCalls.load(std::memory_order_relaxed) == 2,
			"unmodified fixture provider call count remains explicit");

		Dispatcher scaling;
		Modifier half{ "half-health", 0.5f, ComponentHealth | ComponentPhysical };
		const auto halfProvider = MakeOutgoingProvider("half-health", 1, EvaluationCalculation, &half);
		Require(scaling.RegisterOutgoing(&halfProvider).added, "register zero-Physical scaling provider");
		const auto noGeneratedPhysical = scaling.DispatchOutgoing(physicalZero);
		Require(noGeneratedPhysical.damage.healthDamage == 30.0f && noGeneratedPhysical.damage.physicalDamage == 0.0f,
			"Health attenuation never synthesizes Physical damage from zero");

		auto positivePhysicalBothAllowed = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponDirect, 0.0f, 80.0f,
			ComponentHealth | ComponentPhysical, ComponentHealth | ComponentPhysical);
		g_parallelCalls.store(0, std::memory_order_relaxed);
		const auto positivePhysical = dispatcher.DispatchOutgoing(positivePhysicalBothAllowed);
		Require(positivePhysical.status == DispatchStatus::NoChange && g_parallelCalls.load(std::memory_order_relaxed) == 1,
			"valid, modifiable positive Physical continues dispatch even when Health is zero");
	}

	void TestOutgoingFieldConsistencyPolicy()
	{
		const auto verifyMask = [](std::uint32_t validMask, std::uint32_t modifiableMask,
			float physical, std::uint32_t resultMask) {
			Dispatcher dispatcher;
			Modifier provider{ "mask-policy", 0.5f, resultMask };
			const auto descriptor = MakeOutgoingProvider("mask-policy", 1, EvaluationCalculation, &provider);
			Require(dispatcher.RegisterOutgoing(&descriptor).added, "register field-consistency fixture provider");
			auto context = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
				OutgoingProfile::WeaponDirect, 100.0f, physical, validMask, modifiableMask);
			return dispatcher.DispatchOutgoing(context);
		};

		const auto joint = verifyMask(ComponentHealth | ComponentPhysical, ComponentHealth | ComponentPhysical,
			80.0f, ComponentHealth | ComponentPhysical);
		Require(joint.status == DispatchStatus::Applied && joint.damage.healthDamage == 50.0f &&
			joint.damage.physicalDamage == 40.0f, "ordinary direct weapon jointly scales valid positive Health and Physical");

		const auto healthOnlyPositive = verifyMask(ComponentHealth | ComponentPhysical,
			ComponentHealth | ComponentPhysical, 80.0f, ComponentHealth);
		Require(healthOnlyPositive.status == DispatchStatus::InvalidProviderResult,
			"Health-only result is rejected when positive Physical is coupled by the direct-weapon fixture policy");

		const auto physicalOnly = verifyMask(ComponentHealth | ComponentPhysical,
			ComponentHealth | ComponentPhysical, 80.0f, ComponentPhysical);
		Require(physicalOnly.status == DispatchStatus::InvalidProviderResult,
			"Physical-only result is rejected because the direct-weapon policy requires Health");

		Dispatcher permissionConflict;
		g_parallelCalls.store(0, std::memory_order_relaxed);
		const auto countingProvider = MakeOutgoingProvider("permission-conflict", 1,
			EvaluationCalculation, nullptr, &CountProvider);
		Require(permissionConflict.RegisterOutgoing(&countingProvider).added,
			"register Provider to count permission-conflict callbacks");
		auto conflictContext = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponDirect, 100.0f, 80.0f,
			ComponentHealth | ComponentPhysical, ComponentHealth);
		const auto conflict = permissionConflict.DispatchOutgoing(conflictContext);
		Require(conflict.status == DispatchStatus::InvalidContext &&
			g_parallelCalls.load(std::memory_order_relaxed) == 0,
			"direct positive Physical with Health-only permission is rejected before callbacks");
		auto validContext = conflictContext;
		validContext.modifiableMask = ComponentHealth | ComponentPhysical;
		const auto permitted = permissionConflict.DispatchOutgoing(validContext);
		Require(permitted.status == DispatchStatus::NoChange &&
			g_parallelCalls.load(std::memory_order_relaxed) == 1,
			"direct positive Physical with both fields permitted enters normal dispatch");

		const auto healthOnlyZeroPhysical = verifyMask(ComponentHealth | ComponentPhysical,
			ComponentHealth | ComponentPhysical, 0.0f, ComponentHealth);
		Require(healthOnlyZeroPhysical.status == DispatchStatus::Applied &&
			healthOnlyZeroPhysical.damage.healthDamage == 50.0f && healthOnlyZeroPhysical.damage.physicalDamage == 0.0f,
			"zero Physical permits the explicit Health-only rule and remains zero");

		const auto invalidPhysical = verifyMask(ComponentHealth, ComponentHealth,
			std::numeric_limits<float>::quiet_NaN(), ComponentHealth);
		Require(invalidPhysical.status == DispatchStatus::Applied && invalidPhysical.damage.healthDamage == 50.0f,
			"invalid Physical is excluded from the required component set");

		Dispatcher otherProfileDispatcher;
		Modifier otherProfileProvider{ "melee-health-only", 0.5f, ComponentHealth };
		const auto otherProfileDesc = MakeOutgoingProvider("melee-health-only", 1, EvaluationCalculation,
			&otherProfileProvider);
		Require(otherProfileDispatcher.RegisterOutgoing(&otherProfileDesc).added,
			"register profile-specific melee fixture provider");
		auto meleeContext = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponMelee, 100.0f, 80.0f, ComponentHealth | ComponentPhysical, ComponentHealth);
		const auto meleeHealthOnly = otherProfileDispatcher.DispatchOutgoing(meleeContext);
		Require(meleeHealthOnly.status == DispatchStatus::Applied && meleeHealthOnly.damage.healthDamage == 50.0f &&
			meleeHealthOnly.damage.physicalDamage == 80.0f,
			"unproven melee semantics use their explicit Health-only adapter mask");

		Dispatcher profileDispatcher;
		Modifier profileProvider{ "profile", 0.5f, ComponentHealth | ComponentPhysical };
		const auto profileDesc = MakeOutgoingProvider("profile", 1, EvaluationCalculation, &profileProvider);
		Require(profileDispatcher.RegisterOutgoing(&profileDesc).added, "register profile-specific fixture provider");
		auto unknown = MakeOutgoing();
		unknown.profile = OutgoingProfile::Unknown;
		const auto unknownResult = profileDispatcher.DispatchOutgoing(unknown);
		Require(unknownResult.status == DispatchStatus::InvalidContext && unknownResult.damage.healthDamage == 100.0f,
			"Unknown profile is rejected before field adjustment");
		Require(joint.damage.totalDamage == 100.0f && joint.damage.targetedLimbDamage == 12.0f &&
			joint.damage.resistanceIntermediate == 20.0f && joint.changedMask == (ComponentHealth | ComponentPhysical),
			"Total, Limb, and Resistance remain outside the modifiable field policy");
	}

	void TestPredictionPolicyAndProfiles()
	{
		Dispatcher dispatcher;
		Modifier wrf{ "WRF", 0.75f, ComponentHealth | ComponentPhysical };
		Modifier csf{ "CSF", 0.5f, ComponentHealth | ComponentPhysical };
		const auto wrfProvider = MakeOutgoingProvider("WRF", 100,
			EvaluationCalculation | EvaluationPrediction, &wrf);
		const auto csfProvider = MakeOutgoingProvider("CSF", 200, EvaluationCalculation, &csf);
		Require(dispatcher.RegisterOutgoing(&wrfProvider).added && dispatcher.RegisterOutgoing(&csfProvider).added,
			"register providers with distinct prediction policy");
		const auto prediction = dispatcher.DispatchOutgoing(MakeOutgoing(EvaluationKind::Prediction));
		Require(prediction.status == DispatchStatus::Applied &&
			std::abs(prediction.damage.healthDamage - 75.0f) < 0.001f,
			"Prediction invokes only the provider that opted in");
		const auto melee = dispatcher.DispatchOutgoing(MakeOutgoing(EvaluationKind::Calculation,
			ActorKind::NPC, OutgoingProfile::WeaponMelee));
		Require(melee.status == DispatchStatus::Applied &&
			std::abs(melee.damage.healthDamage - 37.5f) < 0.001f,
			"generic calculation context supports an NPC and a distinct melee profile");
		const auto repeatedA = dispatcher.DispatchOutgoing(MakeOutgoing());
		const auto repeatedB = dispatcher.DispatchOutgoing(MakeOutgoing());
		Require(repeatedA.damage.healthDamage == repeatedB.damage.healthDamage,
			"identical immutable input produces deterministic output");
		Require(MakeOutgoing().damage.healthDamage == 100.0f,
			"dispatch never mutates the caller's immutable simulated record");
	}

	void TestUnknownAndPhysicalZero()
	{
		Dispatcher dispatcher;
		Modifier modifier{ "WRF", 0.5f, ComponentHealth | ComponentPhysical };
		const auto provider = MakeOutgoingProvider("WRF", 100, EvaluationCalculation | EvaluationPrediction, &modifier);
		Require(dispatcher.RegisterOutgoing(&provider).added, "register unknown-stage fixture provider");
		auto unknown = MakeOutgoing(EvaluationKind::Unknown);
		const auto unknownResult = dispatcher.DispatchOutgoing(unknown);
		Require(unknownResult.status == DispatchStatus::InvalidContext && unknownResult.damage.healthDamage == 100.0f,
			"Unknown evaluation fails closed to the original snapshot");
		auto zeroPhysical = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponDirect, 60.0f, 0.0f);
		const auto zeroResult = dispatcher.DispatchOutgoing(zeroPhysical);
		Require(zeroResult.damage.healthDamage == 30.0f && zeroResult.damage.physicalDamage == 0.0f,
			"Physical zero stays zero and no Physical damage is synthesized");
		auto unknownProfile = MakeOutgoing();
		unknownProfile.profile = OutgoingProfile::Unknown;
		Require(dispatcher.DispatchOutgoing(unknownProfile).status == DispatchStatus::InvalidContext,
			"unclassified outgoing profile fails closed");
	}

	void TestIncomingPlayerNPCAndArmorState()
	{
		Dispatcher dispatcher;
		const auto provider = MakeIncomingProvider("PAS", 100);
		Require(dispatcher.RegisterIncoming(&provider).added, "register PAS IncomingHealth provider");
		const auto noArmor = dispatcher.DispatchIncoming(MakeIncoming(ActorKind::Player, PowerArmorState::NotEquipped));
		Require(noArmor.status == DispatchStatus::NoChange && noArmor.healthDamage == 100.0f,
			"PAS leaves a non-PA player unchanged");
		const auto playerPA = dispatcher.DispatchIncoming(MakeIncoming(ActorKind::Player, PowerArmorState::Equipped));
		const auto npcPA = dispatcher.DispatchIncoming(MakeIncoming(ActorKind::NPC, PowerArmorState::Equipped));
		Require(playerPA.healthDamage == 50.0f && npcPA.healthDamage == 50.0f,
			"PAS attenuation applies to both Player and NPC PA wearers");
		const auto zeroDamage = dispatcher.DispatchIncoming(MakeIncoming(ActorKind::Player,
			PowerArmorState::Equipped, 0.0f));
		Require(zeroDamage.status == DispatchStatus::NoDamage && zeroDamage.healthDamage == 0.0f,
			"PAS multiplier preserves zero incoming Health damage");
		for (const float invalid : { std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(), -1.0f, kMaxDamageValue * 2.0f }) {
			auto invalidHealth = MakeIncoming(ActorKind::NPC, PowerArmorState::Equipped);
			invalidHealth.healthDamage = invalid;
			Require(dispatcher.DispatchIncoming(invalidHealth).status == DispatchStatus::InvalidInput,
				"invalid incoming Health values fail closed before PAS callbacks");
		}
		auto unknownPhase = MakeIncoming(ActorKind::Player, PowerArmorState::Equipped);
		unknownPhase.phase = IncomingPhase::Unknown;
		Require(dispatcher.DispatchIncoming(unknownPhase).status == DispatchStatus::InvalidContext,
			"unknown phase cannot enter IncomingHealth providers");
		auto unknownConfidence = MakeIncoming(ActorKind::NPC, PowerArmorState::Equipped);
		unknownConfidence.confidence = ContextConfidence::Unknown;
		Require(dispatcher.DispatchIncoming(unknownConfidence).healthDamage == 100.0f,
			"unverified native context fails closed");
		auto unknownPA = MakeIncoming(ActorKind::NPC, PowerArmorState::Unknown);
		Require(dispatcher.DispatchIncoming(unknownPA).healthDamage == 100.0f,
			"unknown PA state cannot accidentally receive PAS attenuation");
		auto otherActor = MakeIncoming(ActorKind::Other, PowerArmorState::Equipped);
		Require(dispatcher.DispatchIncoming(otherActor).healthDamage == 100.0f,
			"PAS's Player/NPC policy leaves other Actor categories unchanged");
	}

	void TestInvalidNumbersAndRollback()
	{
		Dispatcher dispatcher;
		Modifier first{ "first", 0.5f, ComponentHealth | ComponentPhysical };
		const auto firstProvider = MakeOutgoingProvider("first", 10, EvaluationCalculation, &first);
		const auto badProvider = MakeOutgoingProvider("bad", 20, EvaluationCalculation, nullptr, &NanProvider);
		Require(dispatcher.RegisterOutgoing(&firstProvider).added && dispatcher.RegisterOutgoing(&badProvider).added,
			"register rollback fixtures");
		const auto rollback = dispatcher.DispatchOutgoing(MakeOutgoing());
		Require(rollback.status == DispatchStatus::InvalidProviderResult && rollback.damage.healthDamage == 100.0f,
			"invalid later result rolls back earlier modifiers to the original input");
		for (const float invalid : { std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(), -1.0f, kMaxDamageValue * 2.0f }) {
			auto context = MakeOutgoing();
			context.damage.healthDamage = invalid;
			const auto invalidStatus = dispatcher.DispatchOutgoing(context).status;
			Require(invalidStatus == DispatchStatus::InvalidInput,
				"NaN, infinity, negative, and out-of-range damage fail closed");
		}
		for (const float invalidMultiplier : { std::numeric_limits<float>::infinity(), -0.1f, 1.01f }) {
			Modifier invalid{ "invalid", invalidMultiplier, ComponentHealth | ComponentPhysical };
			Dispatcher separate;
			const auto desc = MakeOutgoingProvider("invalid", 1, EvaluationCalculation, &invalid);
			Require(separate.RegisterOutgoing(&desc).added, "register invalid-multiplier provider");
			const auto result = separate.DispatchOutgoing(MakeOutgoing());
			Require(result.status == DispatchStatus::InvalidProviderResult && result.damage.healthDamage == 100.0f,
				"invalid multiplier fails closed to unchanged damage");
		}
		const auto failureProvider = MakeOutgoingProvider("failure", 10, EvaluationCalculation, nullptr, &FailingProvider);
		Dispatcher failureDispatcher;
		Require(failureDispatcher.RegisterOutgoing(&failureProvider).added, "register explicit failure provider");
		const auto failureResult = failureDispatcher.DispatchOutgoing(MakeOutgoing());
		Require(failureResult.status == DispatchStatus::ProviderFailure && failureResult.damage.healthDamage == 100.0f,
			"provider failure cannot change the simulated damage");
		const auto throwProvider = MakeOutgoingProvider("thrower", 10, EvaluationCalculation, nullptr, &ThrowProvider);
		Dispatcher throwDispatcher;
		Require(throwDispatcher.RegisterOutgoing(&throwProvider).added, "register throwing fixture provider");
		const auto threw = throwDispatcher.DispatchOutgoing(MakeOutgoing());
		Require(threw.status == DispatchStatus::ProviderFailure && threw.damage.healthDamage == 100.0f,
			"same-binary fixture exception is caught and rolls back");
		std::atomic<std::uint32_t> sideEffects{ 0 };
		const auto sideEffectProvider = MakeOutgoingProvider("side-effect-failure", 10,
			EvaluationCalculation, &sideEffects, &SideEffectThenFail);
		Dispatcher sideEffectDispatcher;
		Require(sideEffectDispatcher.RegisterOutgoing(&sideEffectProvider).added, "register failure with external side effect");
		const auto sideEffectResult = sideEffectDispatcher.DispatchOutgoing(MakeOutgoing());
		Require(sideEffectResult.status == DispatchStatus::ProviderFailure &&
			sideEffectResult.damage.healthDamage == 100.0f && sideEffects.load(std::memory_order_relaxed) == 1,
			"numeric rollback restores the snapshot but cannot undo Provider side effects");
	}

	void TestNoChangeAndNoProvidersStatuses()
	{
		Dispatcher dispatcher;
		Modifier noChange{ "no-change", 1.0f, ComponentHealth | ComponentPhysical, false };
		const auto noChangeProvider = MakeOutgoingProvider("no-change", 1, EvaluationCalculation, &noChange);
		Require(dispatcher.RegisterOutgoing(&noChangeProvider).added, "register explicit NoChange provider");
		const auto result = dispatcher.DispatchOutgoing(MakeOutgoing());
		Require(result.status == DispatchStatus::NoChange && result.changedMask == ComponentNone &&
			result.damage.healthDamage == 100.0f,
			"NoChange means at least one applicable Provider ran and none applied a result");
		const auto prediction = dispatcher.DispatchOutgoing(MakeOutgoing(EvaluationKind::Prediction));
		Require(prediction.status == DispatchStatus::NoProviders,
			"NoProviders means no registered Provider matched this evaluation kind");
		Dispatcher empty;
		Require(empty.DispatchOutgoing(MakeOutgoing()).status == DispatchStatus::NoProviders,
			"empty stage reports NoProviders");
		Require(empty.DispatchIncoming(MakeIncoming(ActorKind::Player, PowerArmorState::Equipped)).status ==
			DispatchStatus::NoProviders, "empty Incoming stage reports NoProviders");
	}

	void TestComponentPermissionsAndNoDamage()
	{
		Dispatcher dispatcher;
		Modifier modifier{ "illegal-limb", 0.2f, ComponentTargetedLimb };
		const auto provider = MakeOutgoingProvider("illegal-limb", 10, EvaluationCalculation, &modifier);
		Require(dispatcher.RegisterOutgoing(&provider).added, "register provider attempting an unproven component");
		const auto denied = dispatcher.DispatchOutgoing(MakeOutgoing());
		Require(denied.status == DispatchStatus::InvalidProviderResult && denied.damage.targetedLimbDamage == 12.0f,
			"provider cannot modify Limb without adapter authorization");
		auto noDamage = MakeOutgoing(EvaluationKind::Calculation, ActorKind::Player,
			OutgoingProfile::WeaponDirect, 0.0f, 0.0f);
		Require(dispatcher.DispatchOutgoing(noDamage).status == DispatchStatus::NoDamage,
			"zero outgoing record does not invoke modifiers");
	}

	void TestCallbackRegistrationAndRecursion()
	{
		Dispatcher dispatcher;
		ClearOrder();
		const auto registrar = MakeOutgoingProvider("registrar", 10, EvaluationCalculation,
			&dispatcher, &RegisterDuringCallback);
		Require(dispatcher.RegisterOutgoing(&registrar).added, "register callback-time registrar");
		g_lateRegistrations.store(0, std::memory_order_relaxed);
		const auto input = MakeOutgoing();
		(void)dispatcher.DispatchOutgoing(input);
		Require(g_lateRegistrations.load(std::memory_order_relaxed) == 1,
			"callback may register another Provider without holding the registry write lock");
		Require(Order().empty(), "provider registered inside a callback is absent from that active snapshot");
		ClearOrder();
		(void)dispatcher.DispatchOutgoing(input);
		Require(Order() == std::vector<std::string>{ "late" }, "new provider appears on the next dispatch snapshot");

		Dispatcher recursive;
		g_reentryDispatcher.store(&recursive, std::memory_order_release);
		const auto reentrant = MakeOutgoingProvider("recursive", 10, EvaluationCalculation,
			nullptr, &ReentrantProvider);
		Require(recursive.RegisterOutgoing(&reentrant).added, "register recursion fixture");
		g_nestedStatus.store(DispatchStatus::Applied, std::memory_order_relaxed);
		const auto outer = recursive.DispatchOutgoing(input);
		Require(outer.status == DispatchStatus::Applied && g_nestedStatus.load(std::memory_order_acquire) ==
			DispatchStatus::RecursiveDispatch, "same-stage recursive dispatch is rejected without corrupting outer result");
		g_reentryDispatcher.store(nullptr, std::memory_order_release);
	}

	void TestIdentityValidationAndSafeUnload()
	{
		Dispatcher dispatcher;
		Modifier first{ "first", 1.0f, ComponentHealth, false };
		char idBuffer[] = "copied-id";
		auto descriptor = MakeOutgoingProvider(idBuffer, 10, EvaluationCalculation, &first);
		const auto registered = dispatcher.RegisterOutgoing(&descriptor);
		Require(registered.added && registered.handle.value != 0, "registration returns a nonzero Dispatcher-wide handle");
		idBuffer[0] = 'X';
		const auto duplicate = MakeOutgoingProvider("copied-id", 20, EvaluationCalculation, &first);
		Require(!dispatcher.RegisterOutgoing(&duplicate).added, "provider ID is copied and duplicate identity rejected");
		const auto invalidId = MakeOutgoingProvider("invalid id", 20, EvaluationCalculation, &first);
		Require(!dispatcher.RegisterOutgoing(&invalidId).added, "invalid provider identity is rejected");
		Require(dispatcher.UnregisterOutgoing(registered.handle) == UnregisterStatus::Removed,
			"unregister removes provider from future snapshots");
		Require(dispatcher.UnregisterOutgoing(registered.handle) == UnregisterStatus::NotFound,
			"repeated unregister reports NotFound after removal");
		Require(dispatcher.DispatchOutgoing(MakeOutgoing()).status == DispatchStatus::NoProviders,
			"unregistered Provider is absent from later dispatches");
		Require(dispatcher.WaitOutgoingQuiescent(registered.handle) == QuiescenceStatus::Quiescent,
			"provider can unload after the quiescence barrier");
		Require(dispatcher.WaitOutgoingQuiescent(registered.handle) == QuiescenceStatus::NotFound,
			"retirement handle is consumed after successful quiescence");

		Dispatcher blocked;
		const auto blocking = MakeOutgoingProvider("blocking", 1, EvaluationCalculation, nullptr, &BlockingProvider);
		const auto blockedHandle = blocked.RegisterOutgoing(&blocking);
		Require(blockedHandle.added, "register blocking lifecycle Provider");
		{
			std::scoped_lock lock{ g_gateMutex };
			g_callbackEntered = false;
			g_callbackRelease = false;
		}
		std::thread dispatchThread([&] { (void)blocked.DispatchOutgoing(MakeOutgoing()); });
		{
			std::unique_lock lock{ g_gateMutex };
			g_gateCv.wait(lock, [] { return g_callbackEntered; });
		}
		Require(blocked.UnregisterOutgoing(blockedHandle.handle) == UnregisterStatus::Removed,
			"unregister disables Provider while an earlier callback is active");
		std::atomic<QuiescenceStatus> waitStatus{ QuiescenceStatus::NotFound };
		std::atomic<bool> waitStarted{ false };
		g_waitFinished.store(false, std::memory_order_release);
		std::thread waiter([&] {
			waitStarted.store(true, std::memory_order_release);
			waitStatus.store(blocked.WaitOutgoingQuiescent(blockedHandle.handle), std::memory_order_release);
			g_waitFinished.store(true, std::memory_order_release);
		});
		while (!waitStarted.load(std::memory_order_acquire)) std::this_thread::yield();
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
		Require(!g_waitFinished.load(std::memory_order_acquire),
			"WaitQuiescent remains blocked while the callback is still active");
		{
			std::scoped_lock lock{ g_gateMutex };
			g_callbackRelease = true;
		}
		g_gateCv.notify_all();
		dispatchThread.join();
		waiter.join();
		Require(waitStatus.load(std::memory_order_acquire) == QuiescenceStatus::Quiescent,
			"quiescence barrier waits for the active callback before module unload");
		Require(blocked.WaitOutgoingQuiescent(blockedHandle.handle) == QuiescenceStatus::NotFound,
			"a second wait after retirement consumption reports NotFound");
	}

	void TestUnregisterFailureAtomicity()
	{
		for (const auto point : { UnregisterFailurePoint::SnapshotPreparation,
			UnregisterFailurePoint::RetirementInsertion }) {
			Dispatcher dispatcher;
			ClearOrder();
			Modifier observer{ "still-active", 1.0f, ComponentHealth | ComponentPhysical, false };
			const auto provider = MakeOutgoingProvider("still-active", 1, EvaluationCalculation, &observer);
			const auto registration = dispatcher.RegisterOutgoing(&provider);
			Require(registration.added, "register provider for injected Unregister failure");
			Testing::FailNextUnregisterAt(point);
			Require(dispatcher.UnregisterOutgoing(registration.handle) == UnregisterStatus::AllocationFailure,
				"injected preparation failure is reported before disabling the Provider");
			Require(dispatcher.WaitOutgoingQuiescent(registration.handle) == QuiescenceStatus::NotFound,
				"failed unregister does not claim that the active Provider is quiescent or unloadable");
			ClearOrder();
			const auto active = dispatcher.DispatchOutgoing(MakeOutgoing());
			Require(active.status == DispatchStatus::NoChange && Order() == std::vector<std::string>{ "still-active" },
				"Provider remains callable and tracked after an allocation failure");
			Require(dispatcher.UnregisterOutgoing(registration.handle) == UnregisterStatus::Removed,
				"retry succeeds after prepared resources are available");
			Require(dispatcher.WaitOutgoingQuiescent(registration.handle) == QuiescenceStatus::Quiescent,
				"successful retry retains the handle through the quiescence barrier");
		}
	}

	void TestGlobalHandleIdentityAcrossStages()
	{
		Dispatcher dispatcher;
		std::atomic<std::uint32_t> incomingCalls{ 0 };
		const auto outgoingProvider = MakeOutgoingProvider("shared-stage-local-id", 10,
			EvaluationCalculation, nullptr, &CountProvider);
		auto incomingProvider = MakeIncomingProvider("shared-stage-local-id", 10, &CountIncomingProvider);
		incomingProvider.providerContext = &incomingCalls;
		const auto outgoing = dispatcher.RegisterOutgoing(&outgoingProvider);
		const auto incoming = dispatcher.RegisterIncoming(&incomingProvider);
		Require(outgoing.added && incoming.added && outgoing.handle.value != incoming.handle.value,
			"one Dispatcher issues globally distinct nonzero handles across independent stages");

		g_parallelCalls.store(0, std::memory_order_relaxed);
		Require(dispatcher.UnregisterIncoming(outgoing.handle) == UnregisterStatus::NotFound &&
			dispatcher.UnregisterOutgoing(incoming.handle) == UnregisterStatus::NotFound,
			"wrong-stage unregister cannot remove the other stage's Provider");
		Require(dispatcher.WaitIncomingQuiescent(outgoing.handle) == QuiescenceStatus::NotFound &&
			dispatcher.WaitOutgoingQuiescent(incoming.handle) == QuiescenceStatus::NotFound,
			"wrong-stage wait cannot authorize unloading either Provider");

		const auto outgoingResult = dispatcher.DispatchOutgoing(MakeOutgoing());
		const auto incomingResult = dispatcher.DispatchIncoming(MakeIncoming(ActorKind::NPC,
			PowerArmorState::NotEquipped));
		Require(outgoingResult.status == DispatchStatus::NoChange && incomingResult.status == DispatchStatus::NoChange &&
			g_parallelCalls.load(std::memory_order_relaxed) == 1 && incomingCalls.load(std::memory_order_relaxed) == 1,
			"both Providers remain callable after wrong-stage unregister and wait attempts");

		Require(dispatcher.UnregisterOutgoing(outgoing.handle) == UnregisterStatus::Removed &&
			dispatcher.WaitOutgoingQuiescent(outgoing.handle) == QuiescenceStatus::Quiescent,
			"correct Outgoing handle still unregisters and reaches quiescence");
		Require(dispatcher.UnregisterIncoming(incoming.handle) == UnregisterStatus::Removed &&
			dispatcher.WaitIncomingQuiescent(incoming.handle) == QuiescenceStatus::Quiescent,
			"correct Incoming handle still unregisters and reaches quiescence");
		Require(dispatcher.UnregisterOutgoing(outgoing.handle) == UnregisterStatus::NotFound &&
			dispatcher.UnregisterIncoming(incoming.handle) == UnregisterStatus::NotFound &&
			dispatcher.WaitOutgoingQuiescent(outgoing.handle) == QuiescenceStatus::NotFound &&
			dispatcher.WaitIncomingQuiescent(incoming.handle) == QuiescenceStatus::NotFound,
			"repeated unregister and wait report NotFound after each stage consumes its handle");
	}

	void TestSelfUnregisterCannotWaitInsideCallback()
	{
		// The stage dispatcher rejects recursive dispatch; a provider's unload wait is
		// intentionally an external action and cannot complete from its own callback.
		Dispatcher dispatcher;
		struct SelfWait { Dispatcher* dispatcher; ProviderHandleV3 handle; } state{ &dispatcher, {} };
		auto callback = +[](void* opaque, const OutgoingCalculationContextV3*, OutgoingResultV3* result) -> CallbackStatus {
			auto& self = *static_cast<SelfWait*>(opaque);
			(void)self.dispatcher->UnregisterOutgoing(self.handle);
			const auto status = self.dispatcher->WaitOutgoingQuiescent(self.handle);
			if (status != QuiescenceStatus::WouldDeadlock) return CallbackStatus::Failure;
			result->status = CallbackStatus::NoChange;
			result->componentMask = ComponentNone;
			result->multiplier = 1.0f;
			return CallbackStatus::NoChange;
		};
		const auto provider = MakeOutgoingProvider("self-wait", 1, EvaluationCalculation, &state, callback);
		const auto registered = dispatcher.RegisterOutgoing(&provider);
		state.handle = registered.handle;
		const auto result = dispatcher.DispatchOutgoing(MakeOutgoing());
		Require(result.status == DispatchStatus::NoChange, "self-unregister returns without deadlocking dispatch");
		Require(dispatcher.WaitOutgoingQuiescent(state.handle) == QuiescenceStatus::Quiescent,
			"external wait completes after the callback has returned");
	}

	void TestConcurrentPublishAndDispatch()
	{
		Dispatcher dispatcher;
		const auto initial = MakeOutgoingProvider("parallel-base", 0, EvaluationCalculation, nullptr, &CountProvider);
		Require(dispatcher.RegisterOutgoing(&initial).added, "register parallel baseline");
		g_parallelCalls.store(0, std::memory_order_relaxed);
		std::atomic<bool> start{ false };
		std::vector<std::thread> workers;
		for (int worker = 0; worker < 4; ++worker) {
			workers.emplace_back([&] {
				while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
				for (int count = 0; count < 250; ++count) {
					const auto result = dispatcher.DispatchOutgoing(MakeOutgoing());
					if (result.status != DispatchStatus::NoChange) std::abort();
				}
			});
		}
		start.store(true, std::memory_order_release);
		for (int index = 0; index < 24; ++index) {
			const std::string id = "published-" + std::to_string(index);
			const auto descriptor = MakeOutgoingProvider(id.c_str(), index + 1, EvaluationCalculation,
				nullptr, &CountProvider);
			Require(dispatcher.RegisterOutgoing(&descriptor).added, "publish provider during concurrent dispatch");
		}
		for (auto& worker : workers) worker.join();
		Require(g_parallelCalls.load(std::memory_order_relaxed) >= 1000,
			"concurrent immutable snapshots dispatch callbacks safely");
	}

	void TestStableTieOrderingAndSeparateInterfaces()
	{
		Dispatcher dispatcher;
		Modifier lexicalZ{ "zeta", 1.0f, ComponentHealth, false };
		Modifier lexicalA{ "alpha", 1.0f, ComponentHealth, false };
		const auto zProvider = MakeOutgoingProvider("zeta", 100, EvaluationCalculation, &lexicalZ);
		const auto aProvider = MakeOutgoingProvider("alpha", 100, EvaluationCalculation, &lexicalA);
		const auto incoming = MakeIncomingProvider("same-id-other-stage", 100);
		Require(dispatcher.RegisterOutgoing(&zProvider).added && dispatcher.RegisterOutgoing(&aProvider).added,
			"register equal-priority outgoing Providers");
		Require(dispatcher.RegisterIncoming(&incoming).added,
			"same registry independently accepts an Incoming provider");
		ClearOrder();
		(void)dispatcher.DispatchOutgoing(MakeOutgoing());
		Require(Order() == std::vector<std::string>{ "alpha", "zeta" },
			"equal priority is stable by ascending provider identity");
		const auto incomingResult = dispatcher.DispatchIncoming(MakeIncoming(ActorKind::NPC,
			PowerArmorState::NotEquipped));
		Require(incomingResult.status == DispatchStatus::NoChange,
			"IncomingHealth uses its distinct phase-specific registry");
	}
}

int main()
{
	TestPriorityAndComponents();
	TestCrossStageCycleProtection();
	TestIndependentSnapshotContract();
	TestNoDamageUsesOnlyValidApplicableFields();
	TestOutgoingFieldConsistencyPolicy();
	TestPredictionPolicyAndProfiles();
	TestUnknownAndPhysicalZero();
	TestIncomingPlayerNPCAndArmorState();
	TestInvalidNumbersAndRollback();
	TestNoChangeAndNoProvidersStatuses();
	TestComponentPermissionsAndNoDamage();
	TestCallbackRegistrationAndRecursion();
	TestIdentityValidationAndSafeUnload();
	TestUnregisterFailureAtomicity();
	TestGlobalHandleIdentityAcrossStages();
	TestSelfUnregisterCannotWaitInsideCallback();
	TestConcurrentPublishAndDispatch();
	TestStableTieOrderingAndSeparateInterfaces();
	if (g_failures == 0) {
		std::cout << "PASS: all Phase 1O offline fixtures\n";
		return EXIT_SUCCESS;
	}
	std::cerr << "FAIL: " << g_failures << " fixture assertion(s) failed\n";
	return EXIT_FAILURE;
}
