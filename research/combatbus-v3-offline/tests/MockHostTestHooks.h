#pragma once

#include "CombatBusCABI.h"

#ifdef __cplusplus
using IIF_CB_TestQuiescenceClaimHook = void (IIF_CB_CALL *)(void*) noexcept;

extern "C" IIF_CB_API void IIF_CB_CALL IIF_CombatBus_Test_SetQuiescenceClaimHook(
	IIF_CB_TestQuiescenceClaimHook hook, void* context);
extern "C" IIF_CB_API void IIF_CB_CALL IIF_CombatBus_Test_FailNextQuiescenceWait(void);
#endif
