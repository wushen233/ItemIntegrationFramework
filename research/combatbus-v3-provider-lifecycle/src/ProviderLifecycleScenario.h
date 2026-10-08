#pragma once

#include "CombatBusCABI.h"

using IIF_CB_LifecycleLogFn = void (*)(const char* message);

// Exercises only QueryInterface's provider lifecycle function table. It never
// calls either Dispatch function and never creates a game-object context.
bool RunProviderLifecycleScenario(const IIF_CB_InterfaceV3& api,
	IIF_CB_LifecycleLogFn log) noexcept;
