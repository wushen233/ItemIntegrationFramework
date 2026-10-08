# Phase 2B — CombatBus V3 infrastructure without native Hooks

## Scope and current production path

Production IIF `src/main.cpp` remains the owner of the existing Legacy CombatBus. Its EntryPoint Hook and player-only callback dispatch are unchanged. The old `IIF_API::IIF_Interface` layout, F4SE message exchange, damage and armor registration functions, and non-combat systems are unchanged.

`src/CombatBusV3/` now contains the reviewed V3 Registry core and C ABI adapter. Production xmake includes these sources in the IIF plugin. They provide versioned interface discovery, independent Outgoing and Incoming registries, registration, unregister, quiescence, and explicit calculation dispatch through the returned table. No V3 adapter is installed, and no production game path calls either dispatch function. No EntryPoint `0x23`, `0x24`, `0x55`, or `Actor::DoHitMe` is translated into a V3 context. The existing legacy path therefore remains the only active combat damage/armor mutation route.

The source-level no-Hook fixture checks that V3 production sources contain no MinHook/vtable installation and that `main.cpp` still connects only to the unchanged Legacy CombatBus. The production-contract DLL fixture compiles the same Registry and C ABI sources with production lifecycle flags and verifies the query/export surface; it is not the full F4SE plugin build.

## V3 discovery and version checks

`IIF_CombatBus_QueryInterface` is exported from the IIF module. A consumer resolves the loaded IIF module, resolves this export, initializes `IIF_CB_InterfaceV3.struct_size`, and requests version 3 using the exact structure size. Unsupported versions, bad sizes, and invalid pointers fail closed. The returned function table and opaque Registry pointer are borrowed from IIF and remain valid only while the IIF module is loaded.

The query export is separate from the existing F4SE message exchange. It does not change or replace `IIF_API::IIF_Interface`; old providers continue using the old callback registration path. V3 is additive.

## Registry and Host ownership

The production Host is owned by IIF and allocated lazily on the first interface query after the module is loaded. It is intentionally process-lifetime state. Its singleton is not destroyed during DLL detach, and production does not run a blocking drain under the Windows loader lock. A Provider cannot call a global Host Shutdown function. The mock Host Shutdown and test hooks compile only in isolated fixtures.

Provider callbacks and context remain Provider-owned. A Provider stopping use of a registration must call stage-correct unregister and then wait. Only `OK` returned for that same handle/stage by the single successful quiescence consumer authorizes releasing callback code/context. `WAIT_IN_PROGRESS`, `WAIT_FAILURE`, `NOT_FOUND`, invalid stage, and all other failures do not authorize unload. A Provider must not synchronously wait for any Provider in the same Dispatcher from one of its callbacks. No STL object or C++ exception crosses the C ABI.

This phase does not promise safe dynamic unload of the IIF Host itself. The production model assumes the F4SE plugin remains loaded for the game-process lifetime. If a supported host can unload IIF before process exit, a separately owned shutdown/ingress protocol must be designed and reviewed before that unload is supported.

## Adapter state and damage application

V3 Native Adapter state is **Disabled / Not Installed**. The V3 interface permits consumers to request pure calculations, but IIF does not apply returned values to engine memory. Outgoing Calculation and Incoming Health function tables do not themselves constitute engine hooks. No offline adapter flags are accepted as game evidence.

WRF priority 100 and CSF priority 200 remain the intended calculation order. Providers independently inspect the original snapshot and return their multiplier; the Dispatcher validates and combines results in deterministic priority order. No WRF, CSF, or PAS production Provider changes are included. PAS remains an Incoming Health policy target only; no native hit/health callsite is connected in this phase.

## Compatibility and remaining risks

- The legacy `IIF_API` layout and F4SE message exchange are not modified.
- V3 is additive and remains a candidate ABI; this phase does not freeze it.
- Legacy `MH_EnableHook(MH_ALL_HOOKS)` behavior remains unchanged and retains its previously documented shared-MinHook risk.
- Consumers must resolve the correct loaded IIF module and stop calls before the Host module could ever unload.
- V3 thread safety and Provider quiescence rely on Providers honoring callback/context lifetime rules.
- The production F4SE plugin build and game runtime are not validated by the production-contract DLL.
- No Fallout 4 native callsite, field mapping, thread context, or damage application behavior is approved here.
