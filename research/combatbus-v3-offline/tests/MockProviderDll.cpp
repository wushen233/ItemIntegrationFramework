#include "CombatBusCABI.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

#define TEST_EXPORT extern "C" __declspec(dllexport)

namespace {
	struct ProviderState {
		std::mutex mutex;
		std::condition_variable condition;
		std::vector<std::uint32_t> order;
		std::uint32_t wrfCalls{};
		std::uint32_t csfCalls{};
		std::uint32_t pasCalls{};
		float wrfObservedHealth{};
		float wrfObservedPhysical{};
		float csfObservedHealth{};
		float csfObservedPhysical{};
		bool blockWrf{};
		bool wrfEntered{};
		bool wrfCallbackExitReached{};
		bool releaseWrf{};
		bool triggerShutdown{};
		std::uint32_t callbackShutdownStatus{ IIF_CB_STATUS_INTERNAL_ERROR };
		std::uint32_t validationMode{};
		std::uint32_t validationCalls{};
		std::uint32_t tieOrder[2]{};
		std::uint32_t tieCalls{};
	} g_state;
	IIF_CB_ShutdownFn g_shutdown{};
	char g_tieAlphaTag{};
	char g_tieBetaTag{};

	enum ValidationMode : std::uint32_t {
		ValidationNoChange = 0,
		ValidationApply = 1,
		ValidationBadStatus = 2,
		ValidationBadSize = 3,
		ValidationBadVersion = 4,
		ValidationBadMultiplier = 5,
		ValidationBadComponentMask = 6,
		ValidationBadMultiplierRange = 7
	};

	std::uint32_t IIF_CB_CALL WrfCallback(void* context, const IIF_CB_OutgoingContextV3* input,
		IIF_CB_OutgoingResultV3* result)
	{
		if (context != &g_state || !input || !result || input->version != IIF_CB_VERSION_3 ||
			input->struct_size != sizeof(*input) || input->damage.struct_size != sizeof(input->damage)) {
			return IIF_CB_CALLBACK_FAILURE;
		}
		std::unique_lock lock{ g_state.mutex };
		++g_state.wrfCalls;
		g_state.order.push_back(100);
		g_state.wrfObservedHealth = input->damage.health_damage;
		g_state.wrfObservedPhysical = input->damage.physical_damage;
		if (g_state.blockWrf) {
			g_state.wrfEntered = true;
			g_state.condition.notify_all();
			if (!g_state.condition.wait_for(lock, std::chrono::seconds(20),
				[] { return g_state.releaseWrf; })) {
				return IIF_CB_CALLBACK_FAILURE;
			}
		}
		const bool triggerShutdown = g_state.triggerShutdown;
		g_state.triggerShutdown = false;
		lock.unlock();
		if (triggerShutdown && g_shutdown) {
			const auto shutdownStatus = g_shutdown();
			std::scoped_lock shutdownLock{ g_state.mutex };
			g_state.callbackShutdownStatus = shutdownStatus;
		}
		result->status = IIF_CB_CALLBACK_APPLY;
		result->component_mask = IIF_CB_COMPONENT_HEALTH | IIF_CB_COMPONENT_PHYSICAL;
		result->multiplier = 0.5f;
		{
			std::scoped_lock exitLock{ g_state.mutex };
			g_state.wrfCallbackExitReached = true;
		}
		return IIF_CB_CALLBACK_APPLY;
	}

	std::uint32_t IIF_CB_CALL CsfCallback(void* context, const IIF_CB_OutgoingContextV3* input,
		IIF_CB_OutgoingResultV3* result)
	{
		if (context != &g_state || !input || !result || input->struct_size != sizeof(*input)) {
			return IIF_CB_CALLBACK_FAILURE;
		}
		std::scoped_lock lock{ g_state.mutex };
		++g_state.csfCalls;
		g_state.order.push_back(200);
		g_state.csfObservedHealth = input->damage.health_damage;
		g_state.csfObservedPhysical = input->damage.physical_damage;
		result->status = IIF_CB_CALLBACK_APPLY;
		result->component_mask = IIF_CB_COMPONENT_HEALTH | IIF_CB_COMPONENT_PHYSICAL;
		result->multiplier = 0.8f;
		return IIF_CB_CALLBACK_APPLY;
	}

	std::uint32_t IIF_CB_CALL PasCallback(void* context, const IIF_CB_IncomingContextV3* input,
		IIF_CB_IncomingResultV3* result)
	{
		if (context != &g_state || !input || !result || input->struct_size != sizeof(*input) ||
			input->phase != IIF_CB_INCOMING_HEALTH_AFTER_RESISTANCE_BEFORE_DIFFICULTY ||
			input->power_armor != IIF_CB_POWER_ARMOR_EQUIPPED) return IIF_CB_CALLBACK_FAILURE;
		std::scoped_lock lock{ g_state.mutex };
		++g_state.pasCalls;
		result->status = IIF_CB_CALLBACK_APPLY;
		result->multiplier = input->target_kind == IIF_CB_ACTOR_PLAYER ? 0.75f :
			(input->target_kind == IIF_CB_ACTOR_NPC ? 0.5f : 1.0f);
		return IIF_CB_CALLBACK_APPLY;
	}

	std::uint32_t IIF_CB_CALL InvalidResultCallback(void*, const IIF_CB_OutgoingContextV3*,
		IIF_CB_OutgoingResultV3* result)
	{
		result->status = UINT32_C(99);
		result->component_mask = IIF_CB_COMPONENT_HEALTH | IIF_CB_COMPONENT_PHYSICAL;
		result->multiplier = 0.5f;
		return UINT32_C(99);
	}

	std::uint32_t IIF_CB_CALL ValidationOutgoingCallback(void* context,
		const IIF_CB_OutgoingContextV3* input, IIF_CB_OutgoingResultV3* result)
	{
		if (context != &g_state || !input || !result) return IIF_CB_CALLBACK_FAILURE;
		std::uint32_t mode{};
		{
			std::scoped_lock lock{ g_state.mutex };
			++g_state.validationCalls;
			mode = g_state.validationMode;
		}
		if (mode == ValidationNoChange) {
			result->status = IIF_CB_CALLBACK_NO_CHANGE;
			result->component_mask = 0;
			result->multiplier = 1.0f;
			return IIF_CB_CALLBACK_NO_CHANGE;
		}
		result->status = IIF_CB_CALLBACK_APPLY;
		result->component_mask = IIF_CB_COMPONENT_HEALTH | IIF_CB_COMPONENT_PHYSICAL;
		result->multiplier = 0.5f;
		switch (mode) {
		case ValidationBadStatus:
			result->status = UINT32_C(99);
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadSize:
			result->struct_size -= sizeof(std::uint32_t);
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadVersion:
			result->version = UINT32_C(99);
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadMultiplier:
			result->multiplier = std::numeric_limits<float>::quiet_NaN();
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadMultiplierRange:
			result->multiplier = 1.5f;
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadComponentMask:
			result->component_mask = IIF_CB_COMPONENT_RESISTANCE;
			return IIF_CB_CALLBACK_APPLY;
		default:
			return IIF_CB_CALLBACK_APPLY;
		}
	}

	std::uint32_t IIF_CB_CALL ValidationIncomingCallback(void* context,
		const IIF_CB_IncomingContextV3* input, IIF_CB_IncomingResultV3* result)
	{
		if (context != &g_state || !input || !result) return IIF_CB_CALLBACK_FAILURE;
		std::uint32_t mode{};
		{
			std::scoped_lock lock{ g_state.mutex };
			++g_state.validationCalls;
			mode = g_state.validationMode;
		}
		if (mode == ValidationNoChange) {
			result->status = IIF_CB_CALLBACK_NO_CHANGE;
			result->reserved = 0;
			result->multiplier = 1.0f;
			return IIF_CB_CALLBACK_NO_CHANGE;
		}
		result->status = IIF_CB_CALLBACK_APPLY;
		result->reserved = 0;
		result->multiplier = 0.5f;
		switch (mode) {
		case ValidationBadStatus:
			result->status = UINT32_C(99);
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadSize:
			result->struct_size -= sizeof(std::uint32_t);
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadVersion:
			result->version = UINT32_C(99);
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadMultiplier:
			result->multiplier = std::numeric_limits<float>::quiet_NaN();
			return IIF_CB_CALLBACK_APPLY;
		case ValidationBadMultiplierRange:
			result->multiplier = 1.5f;
			return IIF_CB_CALLBACK_APPLY;
		default:
			return IIF_CB_CALLBACK_APPLY;
		}
	}

	std::uint32_t IIF_CB_CALL TieOrderCallback(void* context,
		const IIF_CB_OutgoingContextV3* input, IIF_CB_OutgoingResultV3* result)
	{
		if (!input || !result || (context != &g_tieAlphaTag && context != &g_tieBetaTag) ||
			input->damage.health_damage != 100.0f || input->damage.physical_damage != 80.0f) {
			return IIF_CB_CALLBACK_FAILURE;
		}
		const auto providerOrder = context == &g_tieAlphaTag ? 1u : 2u;
		{
			std::scoped_lock lock{ g_state.mutex };
			if (g_state.tieCalls < 2) g_state.tieOrder[g_state.tieCalls] = providerOrder;
			++g_state.tieCalls;
		}
		result->status = IIF_CB_CALLBACK_NO_CHANGE;
		result->component_mask = 0;
		result->multiplier = 1.0f;
		return IIF_CB_CALLBACK_NO_CHANGE;
	}

	bool ValidOutgoingOutput(const IIF_CB_OutgoingProviderV3* output)
	{
		return output && output->struct_size == sizeof(*output) && output->version == IIF_CB_VERSION_3;
	}

	bool ValidIncomingOutput(const IIF_CB_IncomingProviderV3* output)
	{
		return output && output->struct_size == sizeof(*output) && output->version == IIF_CB_VERSION_3;
	}
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetWRF(IIF_CB_OutgoingProviderV3* output)
{
	if (!ValidOutgoingOutput(output)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	*output = { sizeof(*output), IIF_CB_VERSION_3, "WRF", 100,
		IIF_CB_EVALUATION_CALCULATION | IIF_CB_EVALUATION_PREDICTION, &g_state, &WrfCallback };
	return IIF_CB_STATUS_OK;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetCSF(IIF_CB_OutgoingProviderV3* output)
{
	if (!ValidOutgoingOutput(output)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	*output = { sizeof(*output), IIF_CB_VERSION_3, "CSF", 200,
		IIF_CB_EVALUATION_CALCULATION | IIF_CB_EVALUATION_PREDICTION, &g_state, &CsfCallback };
	return IIF_CB_STATUS_OK;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetPAS(IIF_CB_IncomingProviderV3* output)
{
	if (!ValidIncomingOutput(output)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	*output = { sizeof(*output), IIF_CB_VERSION_3, "PAS", 100, 0, &g_state, &PasCallback };
	return IIF_CB_STATUS_OK;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetInvalid(IIF_CB_OutgoingProviderV3* output)
{
	if (!ValidOutgoingOutput(output)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	*output = { sizeof(*output), IIF_CB_VERSION_3, "InvalidResult", 300,
		IIF_CB_EVALUATION_CALCULATION, &g_state, &InvalidResultCallback };
	return IIF_CB_STATUS_OK;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetOutgoingValidation(
	IIF_CB_OutgoingProviderV3* output)
{
	if (!ValidOutgoingOutput(output)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	*output = { sizeof(*output), IIF_CB_VERSION_3, "ValidationOutgoing", 250,
		IIF_CB_EVALUATION_CALCULATION, &g_state, &ValidationOutgoingCallback };
	return IIF_CB_STATUS_OK;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetIncomingValidation(
	IIF_CB_IncomingProviderV3* output)
{
	if (!ValidIncomingOutput(output)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	*output = { sizeof(*output), IIF_CB_VERSION_3, "ValidationIncoming", 200, 0,
		&g_state, &ValidationIncomingCallback };
	return IIF_CB_STATUS_OK;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetTieAlpha(IIF_CB_OutgoingProviderV3* output)
{
	if (!ValidOutgoingOutput(output)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	*output = { sizeof(*output), IIF_CB_VERSION_3, "TieAlpha", 260,
		IIF_CB_EVALUATION_CALCULATION, &g_tieAlphaTag, &TieOrderCallback };
	return IIF_CB_STATUS_OK;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetTieBeta(IIF_CB_OutgoingProviderV3* output)
{
	if (!ValidOutgoingOutput(output)) return IIF_CB_STATUS_INVALID_STRUCT_SIZE;
	*output = { sizeof(*output), IIF_CB_VERSION_3, "TieBeta", 260,
		IIF_CB_EVALUATION_CALCULATION, &g_tieBetaTag, &TieOrderCallback };
	return IIF_CB_STATUS_OK;
}

TEST_EXPORT void IIF_CB_CALL TestProvider_SetValidationMode(std::uint32_t mode)
{
	std::scoped_lock lock{ g_state.mutex };
	g_state.validationMode = mode;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetValidationCalls(void)
{
	std::scoped_lock lock{ g_state.mutex };
	return g_state.validationCalls;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetTieOrder(std::uint32_t index)
{
	std::scoped_lock lock{ g_state.mutex };
	return index < 2 ? g_state.tieOrder[index] : 0;
}

TEST_EXPORT void IIF_CB_CALL TestProvider_Reset(void)
{
	std::scoped_lock lock{ g_state.mutex };
	g_state.order.clear();
	g_state.wrfCalls = 0;
	g_state.csfCalls = 0;
	g_state.pasCalls = 0;
	g_state.wrfObservedHealth = 0.0f;
	g_state.wrfObservedPhysical = 0.0f;
	g_state.csfObservedHealth = 0.0f;
	g_state.csfObservedPhysical = 0.0f;
	g_state.blockWrf = false;
	g_state.wrfEntered = false;
	g_state.wrfCallbackExitReached = false;
	g_state.releaseWrf = false;
	g_state.triggerShutdown = false;
	g_state.callbackShutdownStatus = IIF_CB_STATUS_INTERNAL_ERROR;
	g_state.validationMode = ValidationNoChange;
	g_state.validationCalls = 0;
	g_state.tieOrder[0] = 0;
	g_state.tieOrder[1] = 0;
	g_state.tieCalls = 0;
}

TEST_EXPORT void IIF_CB_CALL TestProvider_BlockWRF(void)
{
	std::scoped_lock lock{ g_state.mutex };
	g_state.blockWrf = true;
	g_state.wrfEntered = false;
	g_state.wrfCallbackExitReached = false;
	g_state.releaseWrf = false;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_WaitWRFEntered(std::uint32_t timeoutMs)
{
	std::unique_lock lock{ g_state.mutex };
	return g_state.condition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
		[] { return g_state.wrfEntered; }) ? 1u : 0u;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_WasWRFCallbackExitReached(void)
{
	std::scoped_lock lock{ g_state.mutex };
	return g_state.wrfCallbackExitReached ? 1u : 0u;
}

TEST_EXPORT void IIF_CB_CALL TestProvider_ReleaseWRF(void)
{
	{
		std::scoped_lock lock{ g_state.mutex };
		g_state.releaseWrf = true;
		g_state.blockWrf = false;
	}
	g_state.condition.notify_all();
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetCounter(std::uint32_t id)
{
	std::scoped_lock lock{ g_state.mutex };
	if (id == 1) return g_state.wrfCalls;
	if (id == 2) return g_state.csfCalls;
	if (id == 3) return g_state.pasCalls;
	return 0;
}

TEST_EXPORT float IIF_CB_CALL TestProvider_GetObserved(std::uint32_t provider, std::uint32_t component)
{
	std::scoped_lock lock{ g_state.mutex };
	if (provider == 1) return component == 1 ? g_state.wrfObservedHealth : g_state.wrfObservedPhysical;
	if (provider == 2) return component == 1 ? g_state.csfObservedHealth : g_state.csfObservedPhysical;
	return 0.0f;
}

TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetOrder(std::uint32_t index)
{
	std::scoped_lock lock{ g_state.mutex };
	return index < g_state.order.size() ? g_state.order[index] : 0;
}

TEST_EXPORT void IIF_CB_CALL TestProvider_SetShutdown(IIF_CB_ShutdownFn shutdown)
{
	g_shutdown = shutdown;
}

TEST_EXPORT void IIF_CB_CALL TestProvider_TriggerShutdown(void)
{
	std::scoped_lock lock{ g_state.mutex };
	g_state.triggerShutdown = true;
}

#if !defined(COMBATBUS_TEST_OMIT_CALLBACK_SHUTDOWN_STATUS)
TEST_EXPORT std::uint32_t IIF_CB_CALL TestProvider_GetCallbackShutdownStatus(void)
{
	std::scoped_lock lock{ g_state.mutex };
	return g_state.callbackShutdownStatus;
}
#endif
