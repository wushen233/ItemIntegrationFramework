#pragma once

#include "CombatBusCABI.h"

#include <cstdint>

#ifdef __cplusplus
using IIF_CB_TestQuiescenceClaimHook = void (IIF_CB_CALL *)(void*) noexcept;

extern "C" IIF_CB_API void IIF_CB_CALL IIF_CombatBus_Test_SetQuiescenceClaimHook(
	IIF_CB_TestQuiescenceClaimHook hook, void* context);
extern "C" IIF_CB_API void IIF_CB_CALL IIF_CombatBus_Test_FailNextQuiescenceWait(void);
extern "C" IIF_CB_API void IIF_CB_CALL IIF_CombatBus_Test_FailNextRegistrationAfterBridgeLink(void);
extern "C" IIF_CB_API std::uint32_t IIF_CB_CALL IIF_CombatBus_Test_GetBridgeCount(void);
#endif
