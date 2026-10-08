# IIF CombatBus V3 Production Migration Plan (Read-Only Review)

**Status: design proposal only. No production source was modified. No native hook is approved.**

This plan is based on read-only inspection of the current workspace sources, chiefly `projects/ItemIntegrationFramework/src/main.cpp`, `IIF_API.h`, `CombatBusAPI.h`, `CombatBusRegistry.h`, and the current WRF/CSF callbacks. It is not a full build/release review and does not establish Fallout 4 callsite safety.

## Current implementation facts

- `IIF::CombatBus` in `ItemIntegrationFramework/src/main.cpp` owns two process-static vectors of raw callback pointers (`g_damageModifiers`, `g_armorModifiers`). Registration appends without identity, priority, removal, synchronization, or a quiescence protocol.
- `HandleEntryPoint_Hook` calls the original `BGSEntryPoint::HandleEntryPoint` first. It then dispatches damage callbacks only for a non-null player owner on EntryPoint `0x23`, taking weapon/target/damage from arguments `a3/a4/a5`. It handles armor rating EntryPoint `0x55`; it does not dispatch incoming damage `0x24` in the inspected body.
- `HasPerkEntries_Hook` forces `0x23` and `0x55` availability and otherwise calls its trampoline. `Install` writes the Actor vtable slot and creates the entry-point detour, then calls `MH_EnableHook(MH_ALL_HOOKS)`. This process-global MinHook operation may enable hooks created by other users of the same MinHook instance; the migration must use target-specific lifecycle operations and coordinate shared MinHook ownership rather than relying on `MH_ALL_HOOKS`.
- `Initialize` runs CombatBus installation before `IIF::Hooks::Install`; F4SE interface exchange happens on `kGameLoaded`. The exchange hands dependent plugins the legacy callback registration functions. There is no observed legacy callback unregistration path in the inspected API.
- `CombatBusAPI.h` and `CombatBusRegistry.h` define a V2-style value/context Registry candidate. The inspected `main.cpp` does not include or connect that Registry; do not describe it as the active route without finding another production owner/call site.
- WRF and CSF register `OnCombatDamageCalculate` through the legacy F4SE message interface. WRF scales the provided number by a skill multiplier after resolving equipped weapon state (`WeaponRequirementsFramework/src/Mechanics.cpp`). CSF scales by a durability multiplier for managed non-grenade/non-mine weapons (`ConditionSystemFramework/src/ConditionHooks.cpp`). Both callbacks mutate the same borrowed `float*` sequentially, in arrival order rather than explicit priority order.
- CSF also registers an armor-rating callback for Player armor condition. That non-CombatBus-facing behavior and unrelated IIF card/UI/configuration hooks must be preserved during any combat migration.
- The workspace has a `Fallout4PowerArmorSystems` project directory, but this review did not audit its provider logic. PAS migration requirements below rely only on the stated Player/NPC incoming Health need and the V3 candidate context; PAS source compatibility remains to be reviewed before implementation.

## Migration phases and safety gates

### A. Preserve existing noncombat IIF behavior

Inventory and regression-test F4SE messages, item-card registration/update dispatch, Scaleform translations, ImGui, JSON/config loading, UI hooks, and any other IIF APIs independently from CombatBus. Keep their install and callback ownership intact. Combat migration must not replace the general IIF F4SE interface exchange with a CombatBus-only table.

**Gate:** a baseline list of noncombat exports/messages/hooks and their current tests/observations exists before changing combat registration. This is a hard gate for a release containing migration.

### B. Isolate the legacy combat path

Before enabling any V3 native adapter, make the old mutating route and the new mutating route mutually exclusive by an explicit, default-safe mode switch owned by IIF. A mode transition must be performed at a quiescent lifecycle boundary: stop accepting provider registrations/dispatch, drain any active callbacks, then switch exactly one route. Do not install both and merely “hope” only one receives a call. Do not leave old WRF/CSF callbacks registered while V3 also applies their multipliers.

Retain the old interface during a compatibility window only if a single dispatcher gate selects one implementation. If dependent plugins cannot negotiate/ unregister the old callbacks, a process restart may be the only safe switch boundary; do not attempt live vector replacement while calls may be in flight. Replace broad `MH_ALL_HOOKS` enable/disable assumptions with hook-specific creation, enable, disable, and remove operations under a single documented MinHook owner protocol. Never disable or uninitialize unrelated IIF/UI or third-party MinHook hooks as a side effect.

**Rollback:** return to the legacy path only after V3 ingress is stopped and drained and V3 adapters can no longer mutate a damage value. If that cannot be proven in-process, rollback requires restart rather than toggling a flag while callbacks are active.

### C. Add versioned V3 discovery and Registry ownership

The Host should expose an explicit C-compatible factory/query and Shutdown surface with exact version/size checks. The current candidate negotiates V3 all-or-nothing; it has no optional capability-bitset, so consumers must verify every function pointer and fail closed on missing/mismatched interface. Do not infer optional capabilities from unknown bits. Consumers resolve the interface without linking C++ implementation details. Keep Host and Provider ownership explicit: provider identity/context and module lifetime, stage-specific registration, one designated Wait owner, and Shutdown owner-side stop-ingress/join order. Do not advertise the current candidate as frozen before independent ABI review and separate-DLL lifecycle evidence.

### D. Migrate WRF and CSF as pure Outgoing calculation Providers

Register WRF at priority `100` and CSF at `200`. Each computes its own multiplier from the original snapshot plus its own business state; results are composed deterministically. Neither callback may mutate engine state, consume weapon durability, emit hit events, or retain borrowed handles. Prediction behavior is a separate opt-in policy decision: calculation logic may be shared only if the game-facing meaning is desired in prediction and the supplied weapon/actor state is valid there. Unknown contexts must not run Providers.

Do not translate the legacy `float*` to a V3 component until a native adapter proves which field it points to for each accepted attack profile and proves all coupled fields. `0x23` is not automatically an ActualHit event. The old callbacks' Player-only assumption must not accidentally broaden WRF/CSF to NPCs until separately authorized; CSF's future NPC support remains a later policy change.

### E. Migrate PAS as a separate Incoming Health Provider

PAS receives a generic target Actor kind and incoming Health numeric value, with Player/NPC support and fail-closed unknown context. Keep the candidate Health stage distinct from Outgoing calculation and from armor-part condition changes. Current PAS Health damage reduction does not depend on resolving Power Armor part condition writes; do not block this requirement on a future component-durability feature. Preserve the candidate event-order limitation: if native hit events observe the unmodified value before the proposed Incoming point, that behavior must be surfaced and accepted before production integration.

### F. Independently verify OG 1.10.163 native adapters last

For each exact supported runtime, verify the target function signature, calling convention, register/stack arguments, writable field identity, CFG, continuation behavior, hook ownership, install/uninstall lifecycle, thread assumptions, and every write's downstream consumers. The historical `0x23/0x24` EntryPoint paths and `Actor::DoHitMe` are research candidates, not approved V3 Hook sites. Do not derive runtime safety from this offline mock, PDB names, or the API's stage labels.

## Main migration risks

| Risk | Current evidence | Required control |
|---|---|---|
| Double modification by legacy and V3 routes | Legacy callbacks mutate `float*`; candidate V3 dispatch also returns modified numeric damage | One exclusive route gate; drain before switching; test per callback invocation count and final scalar |
| Legacy callback lifetime and races | Raw function pointers in vectors; no unregister or quiescence; unsynchronized append/iteration | Stop new callback registrations and dispatch; retain provider DLLs until process shutdown or migrate with a restart-safe boundary |
| Incorrectly treating `0x23` as ActualHit | Hook runs after native EntryPoint original and sees a mutable argument; earlier static work shows prediction reuse risk | Never call this an ActualHit signal; verify outgoing calculation semantics separately and keep prediction explicit |
| Wrong component mapping | Legacy ABI exposes a single `float*`; V3 uses field masks and profiles | Native adapter proof per profile before setting valid/modifiable masks; reject unknown profile |
| Global MinHook side effects | IIF `Install` invokes `MH_EnableHook(MH_ALL_HOOKS)` | Hook-specific operations and shared MinHook ownership; regression-check unrelated hooks |
| Noncombat regression | IIF main also owns F4SE exchange, UI/config/card, translation and hook initialization | Preserve noncombat code path and run its independent regression suite |
| ABI/loader lifecycle | Current V3 is candidate; DLL mocks do not model F4SE shutdown or production modules | Separate DLL tests, caller-thread join, Provider owner stop/join, no teardown work in `DllMain` |
| PAS event consistency | Candidate Incoming stage is late relative to some hit observers | Static callsite proof plus controlled Player/NPC runtime comparison before approval |

## Production readiness gates

“Static” below means OG 1.10.163 disassembly/call graph evidence, not naming inference. Runtime tests require the user's isolated game setup; this plan does not start the game.

| Gate | Preconditions | Offline validation | Static disassembly | User runtime validation | Blocks production use? |
|---|---|---|---|---|---|
| 1. V3 ABI interface | Candidate contract reviewed; test harness isolated | C11/C++20 layout, status, DLL, lifetime, failure and compiler matrix | No | Cross-DLL host can validate ABI shape | Yes, until stable ABI and independent review |
| 2. IIF isolation/migration | Noncombat inventory and rollback design | Exclusive dispatcher gate, compatibility negotiation, noncombat fixture/regression plan | Audit active imports and hook paths | Noncombat IIF features in game after migration | Yes |
| 3. Native Hook ABI/install lifetime | Candidate native function and target runtime selected | Adapter shim/ABI fixtures only | Prove prologue, calling convention, arguments, continuation, thread and ownership | Install, disable, unload/reload, recovery | Yes |
| 4. Native context/field consistency | Accepted attack profiles enumerated | Per-profile pure math fixtures | Prove actor/target/weapon, fields, coupling, downstream reads | Compare outputs for each supported profile | Yes for each supported profile |
| 5. Player firearms/melee | Gates 1–4 for these profiles | Deterministic multipliers and boundary values | Confirm distinct ranged/melee adapters | Exercise ordinary and special weapons, misses and actual hits | Yes for release claims |
| 6. WRF/CSF skill, condition, resistance | Weapon state and profile contract defined | Pure Provider policies, independent multipliers, no mutation | Prove pre-resistance field mapping and prediction route | Skill threshold, durability bands, DR, prediction versus hit | Yes for behavioral correctness |
| 7. PAS Player/NPC PA Health | Incoming candidate phase and actor identity proved | Player/NPC rule fixtures, unknown fail-closed | Prove common Health submission and prior event consumers | Player and NPC PA/non-PA damage comparison | Yes for PAS release; PA part condition is not a prerequisite |
| 8. Prediction/Health comparison | Outgoing adapter and prediction classification known | Opt-in/out policy fixtures | Trace prediction and real submission sources | Compare AI estimate, UI/DPS estimate, misses, blocked hits and Health delta | Yes before claiming prediction equivalence or excluding it |
| 9. Legacy IIF noncombat regression | Migration switch exists and old route is drained | Unit tests for messages/config/card dispatch where possible | No | UI, translations, card rendering, JSON/config, other hooks | Yes before shipping IIF migration |
| 10. Shutdown/rollback/recovery | Host/Provider ownership contract agreed | Concurrent shutdown/wait/failure/retry and DLL load/unload fixtures | Review production install/uninstall graph | Stop/restart, provider unload, rollback/re-enable, exception recovery | Yes |

## Recommended implementation order after approval

1. Finish offline V3 ABI approval and separate-DLL validation; do not alter production code yet.
2. Implement an IIF-side exclusive combat-route gate and tests while leaving the legacy path as the only active route. Preserve all noncombat behavior.
3. Add V3 interface discovery/Registry ownership, initially with no production native adapter enabled.
4. Convert WRF and CSF callback logic to pure Provider calculations, keep their current Player scope, and test against an explicit adapter fixture. Do not register both legacy mutators and V3 mutators for the same runtime route.
5. Integrate PAS Incoming Health policy separately for Player and NPC after its adapter is proved. Part condition work remains a distinct future phase.
6. Only then implement and validate one OG 1.10.163 native adapter at a time. The actual hook location and old/new exclusivity must be reviewed before any runtime deployment.

No phase in this document authorizes production changes. No phase approves EntryPoint `0x23`, EntryPoint `0x24`, `DoHitMe`, or any MinHook installation site.
