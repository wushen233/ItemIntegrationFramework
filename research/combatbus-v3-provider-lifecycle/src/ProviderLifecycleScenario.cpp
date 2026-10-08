#include "ProviderLifecycleScenario.h"

#include <atomic>
#include <cstddef>
#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace {
	constexpr std::uint32_t kOutgoingStage = IIF_CB_STAGE_OUTGOING_CALCULATION;
	constexpr std::uint32_t kIncomingStage = IIF_CB_STAGE_INCOMING_HEALTH;
	constexpr std::size_t kMaxLeases = 4;

	struct ProviderContext {
		std::atomic<std::uint32_t> callbackCount{};
	};

	struct Lease {
		IIF_CB_ProviderHandleV3 handle{};
		std::uint32_t stage{};
		bool present{};
		bool unregistered{};
		bool waitAttempted{};
		bool quiescent{};
	};

	ProviderContext g_outgoingContext;
	ProviderContext g_incomingContext;

	void Emit(IIF_CB_LifecycleLogFn log, const char* message) noexcept
	{
		if (log) log(message);
	}

	bool Check(IIF_CB_LifecycleLogFn log, const char* label, std::uint32_t actual,
		std::uint32_t expected) noexcept
	{
		char message[192]{};
		const bool pass = actual == expected;
		std::snprintf(message, sizeof(message), "%s status=%" PRIu32 " expected=%" PRIu32 " pass=%u",
			label, actual, expected, pass ? 1U : 0U);
		Emit(log, message);
		return pass;
	}

	bool CheckRegistration(IIF_CB_LifecycleLogFn log, const char* label, std::uint32_t transport,
		const IIF_CB_RegistrationV3& registration, std::uint32_t expectedStatus,
		bool expectedAdded) noexcept
	{
		char message[256]{};
		const bool pass = transport == expectedStatus && registration.status == expectedStatus &&
			(registration.added != 0) == expectedAdded &&
			(expectedAdded ? registration.handle.value != 0 : registration.handle.value == 0);
		std::snprintf(message, sizeof(message),
			"%s api=%" PRIu32 " registration=%" PRIu32 " added=%" PRIu32
			" handle=%" PRIu64 " pass=%u",
			label, transport, registration.status, registration.added,
			registration.handle.value, pass ? 1U : 0U);
		Emit(log, message);
		return pass;
	}

	bool RememberLease(Lease* leases, std::size_t& count, IIF_CB_LifecycleLogFn log,
		IIF_CB_ProviderHandleV3 handle, std::uint32_t stage, const char* label) noexcept
	{
		if (handle.value == 0) return true;
		if (count == kMaxLeases) {
			Emit(log, "FAIL: lease tracking capacity exhausted; callback code/context remain resident");
			return false;
		}
		leases[count++] = { handle, stage, true, false, false, false };
		char message[160]{};
		std::snprintf(message, sizeof(message), "%s stage=%" PRIu32 " tracked_handle=%" PRIu64,
			label, stage, handle.value);
		Emit(log, message);
		return true;
	}

	bool Cleanup(Lease* leases, std::size_t count, const IIF_CB_InterfaceV3& api,
		IIF_CB_LifecycleLogFn log) noexcept
	{
		bool complete = true;
		for (std::size_t i = 0; i < count; ++i) {
			auto& lease = leases[i];
			if (!lease.present || lease.quiescent) continue;
			if (!lease.unregistered) {
				const auto status = api.unregister_provider(api.registry, lease.handle, lease.stage);
				char label[96]{};
				std::snprintf(label, sizeof(label), "failure-cleanup unregister[%zu]", i);
				if (!Check(log, label, status, IIF_CB_STATUS_OK)) {
					complete = false;
					continue;
				}
				lease.unregistered = true;
			}
			if (!lease.waitAttempted) {
				lease.waitAttempted = true;
				const auto status = api.wait_provider_quiescent(api.registry, lease.handle, lease.stage);
				char label[96]{};
				std::snprintf(label, sizeof(label), "failure-cleanup wait[%zu]", i);
				if (Check(log, label, status, IIF_CB_STATUS_OK)) lease.quiescent = true;
				else complete = false;
			}
		}
		return complete;
	}

	std::uint32_t IIF_CB_CALL OutgoingCallback(void* context,
		const IIF_CB_OutgoingContextV3*, IIF_CB_OutgoingResultV3* result)
	{
		if (context != &g_outgoingContext || !result) return IIF_CB_CALLBACK_FAILURE;
		g_outgoingContext.callbackCount.fetch_add(1, std::memory_order_relaxed);
		*result = { sizeof(*result), IIF_CB_VERSION_3, IIF_CB_CALLBACK_NO_CHANGE, 0, 1.0F };
		return IIF_CB_CALLBACK_NO_CHANGE;
	}

	std::uint32_t IIF_CB_CALL IncomingCallback(void* context,
		const IIF_CB_IncomingContextV3*, IIF_CB_IncomingResultV3* result)
	{
		if (context != &g_incomingContext || !result) return IIF_CB_CALLBACK_FAILURE;
		g_incomingContext.callbackCount.fetch_add(1, std::memory_order_relaxed);
		*result = { sizeof(*result), IIF_CB_VERSION_3, IIF_CB_CALLBACK_NO_CHANGE, 0, 1.0F };
		return IIF_CB_CALLBACK_NO_CHANGE;
	}

	bool InterfaceReady(const IIF_CB_InterfaceV3& api) noexcept
	{
		return api.struct_size == sizeof(api) && api.version == IIF_CB_VERSION_3 && api.registry &&
			api.register_outgoing && api.register_incoming && api.unregister_provider &&
			api.wait_provider_quiescent;
	}
}

bool RunProviderLifecycleScenario(const IIF_CB_InterfaceV3& api,
	IIF_CB_LifecycleLogFn log) noexcept
{
	if (!InterfaceReady(api)) {
		Emit(log, "FAIL: lifecycle interface table is incomplete");
		return false;
	}

	g_outgoingContext.callbackCount.store(0, std::memory_order_relaxed);
	g_incomingContext.callbackCount.store(0, std::memory_order_relaxed);
	Lease leases[kMaxLeases]{};
	std::size_t leaseCount{};
	bool allChecksPassed = true;

	IIF_CB_OutgoingProviderV3 invalidOutgoing{};
	invalidOutgoing.struct_size = sizeof(invalidOutgoing);
	invalidOutgoing.version = IIF_CB_VERSION_3 + 1;
	invalidOutgoing.provider_id = "iif.phase2d.invalid.outgoing";
	invalidOutgoing.evaluation_mask = IIF_CB_EVALUATION_CALCULATION;
	invalidOutgoing.provider_context = &g_outgoingContext;
	invalidOutgoing.callback = &OutgoingCallback;
	IIF_CB_RegistrationV3 invalidOutgoingResult{ sizeof(invalidOutgoingResult), IIF_CB_VERSION_3,
		IIF_CB_STATUS_INTERNAL_ERROR, 0, { 0 } };
	auto status = api.register_outgoing(api.registry, &invalidOutgoing, &invalidOutgoingResult);
	allChecksPassed &= CheckRegistration(log, "invalid Outgoing descriptor version", status,
		invalidOutgoingResult, IIF_CB_STATUS_UNSUPPORTED_VERSION, false);

	invalidOutgoing.version = IIF_CB_VERSION_3;
	invalidOutgoing.struct_size -= sizeof(std::uint32_t);
	invalidOutgoingResult = { sizeof(invalidOutgoingResult), IIF_CB_VERSION_3,
		IIF_CB_STATUS_INTERNAL_ERROR, 0, { 0 } };
	status = api.register_outgoing(api.registry, &invalidOutgoing, &invalidOutgoingResult);
	allChecksPassed &= CheckRegistration(log, "invalid Outgoing descriptor size", status,
		invalidOutgoingResult, IIF_CB_STATUS_INVALID_STRUCT_SIZE, false);

	IIF_CB_IncomingProviderV3 invalidIncoming{};
	invalidIncoming.struct_size = sizeof(invalidIncoming);
	invalidIncoming.version = IIF_CB_VERSION_3 + 1;
	invalidIncoming.provider_id = "iif.phase2d.invalid.incoming";
	invalidIncoming.provider_context = &g_incomingContext;
	invalidIncoming.callback = &IncomingCallback;
	IIF_CB_RegistrationV3 invalidIncomingResult{ sizeof(invalidIncomingResult), IIF_CB_VERSION_3,
		IIF_CB_STATUS_INTERNAL_ERROR, 0, { 0 } };
	status = api.register_incoming(api.registry, &invalidIncoming, &invalidIncomingResult);
	allChecksPassed &= CheckRegistration(log, "invalid Incoming descriptor version", status,
		invalidIncomingResult, IIF_CB_STATUS_UNSUPPORTED_VERSION, false);

	invalidIncoming.version = IIF_CB_VERSION_3;
	invalidIncoming.struct_size -= sizeof(std::uint32_t);
	invalidIncomingResult = { sizeof(invalidIncomingResult), IIF_CB_VERSION_3,
		IIF_CB_STATUS_INTERNAL_ERROR, 0, { 0 } };
	status = api.register_incoming(api.registry, &invalidIncoming, &invalidIncomingResult);
	allChecksPassed &= CheckRegistration(log, "invalid Incoming descriptor size", status,
		invalidIncomingResult, IIF_CB_STATUS_INVALID_STRUCT_SIZE, false);

	IIF_CB_OutgoingProviderV3 outgoing{};
	outgoing.struct_size = sizeof(outgoing);
	outgoing.version = IIF_CB_VERSION_3;
	outgoing.provider_id = "iif.phase2d.outgoing";
	outgoing.priority = 9000;
	outgoing.evaluation_mask = IIF_CB_EVALUATION_CALCULATION;
	outgoing.provider_context = &g_outgoingContext;
	outgoing.callback = &OutgoingCallback;
	IIF_CB_RegistrationV3 outgoingResult{ sizeof(outgoingResult), IIF_CB_VERSION_3,
		IIF_CB_STATUS_INTERNAL_ERROR, 0, { 0 } };
	const auto outgoingStatus = api.register_outgoing(api.registry, &outgoing, &outgoingResult);
	const bool outgoingSuccess = CheckRegistration(log, "register Outgoing", outgoingStatus,
		outgoingResult, IIF_CB_STATUS_OK, true);
	allChecksPassed &= outgoingSuccess;
	const bool outgoingTracked = RememberLease(leases, leaseCount, log, outgoingResult.handle,
		kOutgoingStage, "Outgoing");
	allChecksPassed &= outgoingTracked;

	IIF_CB_IncomingProviderV3 incoming{};
	incoming.struct_size = sizeof(incoming);
	incoming.version = IIF_CB_VERSION_3;
	incoming.provider_id = "iif.phase2d.incoming";
	incoming.priority = 9000;
	incoming.reserved = 0;
	incoming.provider_context = &g_incomingContext;
	incoming.callback = &IncomingCallback;
	IIF_CB_RegistrationV3 incomingResult{ sizeof(incomingResult), IIF_CB_VERSION_3,
		IIF_CB_STATUS_INTERNAL_ERROR, 0, { 0 } };
	const auto incomingStatus = api.register_incoming(api.registry, &incoming, &incomingResult);
	const bool incomingSuccess = CheckRegistration(log, "register Incoming", incomingStatus,
		incomingResult, IIF_CB_STATUS_OK, true);
	allChecksPassed &= incomingSuccess;
	const bool incomingTracked = RememberLease(leases, leaseCount, log, incomingResult.handle,
		kIncomingStage, "Incoming");
	allChecksPassed &= incomingTracked;

	const bool uniqueHandles = outgoingResult.handle.value != 0 && incomingResult.handle.value != 0 &&
		outgoingResult.handle.value != incomingResult.handle.value;
	char handleMessage[160]{};
	std::snprintf(handleMessage, sizeof(handleMessage),
		"stage handles outgoing=%" PRIu64 " incoming=%" PRIu64 " nonzero_and_distinct=%u pass=%u",
		outgoingResult.handle.value, incomingResult.handle.value, uniqueHandles ? 1U : 0U,
		uniqueHandles ? 1U : 0U);
	Emit(log, handleMessage);
	allChecksPassed &= uniqueHandles;

	if (outgoingSuccess) {
		IIF_CB_RegistrationV3 duplicate{ sizeof(duplicate), IIF_CB_VERSION_3,
			IIF_CB_STATUS_INTERNAL_ERROR, 0, { 0 } };
		const auto duplicateStatus = api.register_outgoing(api.registry, &outgoing, &duplicate);
		const bool duplicatePassed = CheckRegistration(log, "duplicate Outgoing id", duplicateStatus,
			duplicate, IIF_CB_STATUS_DUPLICATE, false);
		allChecksPassed &= duplicatePassed;
		const bool duplicateTracked = RememberLease(leases, leaseCount, log, duplicate.handle,
			kOutgoingStage, "unexpected duplicate result");
		allChecksPassed &= duplicateTracked;
	}
	if (incomingSuccess) {
		IIF_CB_RegistrationV3 duplicate{ sizeof(duplicate), IIF_CB_VERSION_3,
			IIF_CB_STATUS_INTERNAL_ERROR, 0, { 0 } };
		const auto duplicateStatus = api.register_incoming(api.registry, &incoming, &duplicate);
		const bool duplicatePassed = CheckRegistration(log, "duplicate Incoming id", duplicateStatus,
			duplicate, IIF_CB_STATUS_DUPLICATE, false);
		allChecksPassed &= duplicatePassed;
		const bool duplicateTracked = RememberLease(leases, leaseCount, log, duplicate.handle,
			kIncomingStage, "unexpected duplicate result");
		allChecksPassed &= duplicateTracked;
	}

	if (uniqueHandles && outgoingSuccess && incomingSuccess) {
		allChecksPassed &= Check(log, "wrong-stage Unregister Outgoing handle", api.unregister_provider(
			api.registry, outgoingResult.handle, kIncomingStage), IIF_CB_STATUS_NOT_FOUND);
		allChecksPassed &= Check(log, "wrong-stage Unregister Incoming handle", api.unregister_provider(
			api.registry, incomingResult.handle, kOutgoingStage), IIF_CB_STATUS_NOT_FOUND);
		allChecksPassed &= Check(log, "wrong-stage Wait Outgoing handle", api.wait_provider_quiescent(
			api.registry, outgoingResult.handle, kIncomingStage), IIF_CB_STATUS_NOT_FOUND);
		allChecksPassed &= Check(log, "wrong-stage Wait Incoming handle", api.wait_provider_quiescent(
			api.registry, incomingResult.handle, kOutgoingStage), IIF_CB_STATUS_NOT_FOUND);
		allChecksPassed &= Check(log, "Wait before Outgoing Unregister", api.wait_provider_quiescent(
			api.registry, outgoingResult.handle, kOutgoingStage), IIF_CB_STATUS_NOT_FOUND);
		allChecksPassed &= Check(log, "Wait before Incoming Unregister", api.wait_provider_quiescent(
			api.registry, incomingResult.handle, kIncomingStage), IIF_CB_STATUS_NOT_FOUND);

		const auto outgoingUnregister = api.unregister_provider(api.registry, outgoingResult.handle,
			kOutgoingStage);
		const bool outgoingRemoved = Check(log, "correct-stage Unregister Outgoing",
			outgoingUnregister, IIF_CB_STATUS_OK);
		allChecksPassed &= outgoingRemoved;
		if (outgoingRemoved && leaseCount > 0) leases[0].unregistered = true;

		const auto incomingUnregister = api.unregister_provider(api.registry, incomingResult.handle,
			kIncomingStage);
		const bool incomingRemoved = Check(log, "correct-stage Unregister Incoming",
			incomingUnregister, IIF_CB_STATUS_OK);
		allChecksPassed &= incomingRemoved;
		if (incomingRemoved && leaseCount > 1) leases[1].unregistered = true;

		if (outgoingRemoved) {
			if (leaseCount > 0) leases[0].waitAttempted = true;
			const auto waitStatus = api.wait_provider_quiescent(api.registry, outgoingResult.handle,
				kOutgoingStage);
			const bool waited = Check(log, "single-owner WaitQuiescent Outgoing", waitStatus,
				IIF_CB_STATUS_OK);
			allChecksPassed &= waited;
			if (waited && leaseCount > 0) leases[0].quiescent = true;
		}
		if (incomingRemoved) {
			if (leaseCount > 1) leases[1].waitAttempted = true;
			const auto waitStatus = api.wait_provider_quiescent(api.registry, incomingResult.handle,
				kIncomingStage);
			const bool waited = Check(log, "single-owner WaitQuiescent Incoming", waitStatus,
				IIF_CB_STATUS_OK);
			allChecksPassed &= waited;
			if (waited && leaseCount > 1) leases[1].quiescent = true;
		}

		allChecksPassed &= Check(log, "expired Outgoing Unregister", api.unregister_provider(
			api.registry, outgoingResult.handle, kOutgoingStage), IIF_CB_STATUS_NOT_FOUND);
		allChecksPassed &= Check(log, "expired Outgoing Wait", api.wait_provider_quiescent(
			api.registry, outgoingResult.handle, kOutgoingStage), IIF_CB_STATUS_NOT_FOUND);
		allChecksPassed &= Check(log, "expired Incoming Unregister", api.unregister_provider(
			api.registry, incomingResult.handle, kIncomingStage), IIF_CB_STATUS_NOT_FOUND);
		allChecksPassed &= Check(log, "expired Incoming Wait", api.wait_provider_quiescent(
			api.registry, incomingResult.handle, kIncomingStage), IIF_CB_STATUS_NOT_FOUND);
	}

	const bool cleanupComplete = Cleanup(leases, leaseCount, api, log);
	const auto outgoingCalls = g_outgoingContext.callbackCount.load(std::memory_order_relaxed);
	const auto incomingCalls = g_incomingContext.callbackCount.load(std::memory_order_relaxed);
	char countMessage[160]{};
	std::snprintf(countMessage, sizeof(countMessage), "callback counts outgoing=%" PRIu32
		" incoming=%" PRIu32 " pass=%u", outgoingCalls, incomingCalls,
		(outgoingCalls == 0 && incomingCalls == 0) ? 1U : 0U);
	Emit(log, countMessage);
	const bool callbacksSilent = outgoingCalls == 0 && incomingCalls == 0;
	const bool leasesClosed = cleanupComplete;
	const bool passed = allChecksPassed && leasesClosed && callbacksSilent;
	Emit(log, passed ? "[IIF-CB-Provider-Smoke] Phase 2D-A PASS" :
		"[IIF-CB-Provider-Smoke] Phase 2D-A FAIL; retain callback code/context until process exit");
	return passed;
}
