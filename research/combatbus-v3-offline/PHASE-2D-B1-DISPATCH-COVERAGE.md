# Phase 2D-B1 — Offline Dispatch and Concurrency Coverage

## Scope and evidence boundary

This phase exercises the production `src/CombatBusV3/CombatBusPrototype.cpp` Registry and
`src/CombatBusV3/CombatBusCABI.cpp` adapter in an ordinary Windows x64 process. The cross-DLL
fixture builds those exact source files into an isolated mock Host DLL, loads an independent
Provider DLL, and calls the copied C ABI function table. The mock Host is compiled with
offline-only test controls and an explicit process-test shutdown export; neither is part of the
production IIF export surface. The separate production-contract target continues to compile with
`IIF_CB_PRODUCTION_HOST` and checks the actual production-shaped query/export surface and empty
Registry behavior.

All Actor, weapon, and registry pointers passed by these fixtures are offline opaque sentinels.
Provider callbacks compare or carry them only as values and never dereference them. In particular,
the Incoming `VerifiedAdapterCallsite`/phase values are deliberately simulated contract inputs;
they do not prove that a Fallout 4 native caller exists, that the callsite is correct, or that the
engine invokes it on a safe thread. No game process, MO2 profile, native Hook, damage function, or
game memory is used.

## Coverage matrix

| Contract or race | Evidence in this branch | Result boundary |
|---|---|---|
| Production Registry/C ABI sources run across DLLs | `combatbus_iif_mock.dll` compiles the production source files; `combatbus_test_provider.dll` owns callbacks/context; `combatbus_cross_dll_host.exe` uses `LoadLibrary`/`GetProcAddress` | Offline Windows ABI only |
| Outgoing callback invocation, `APPLY`, output composition | WRF 100 and CSF 200 each return multipliers; output composes Health/Physical while Total remains unchanged | Does not validate native HitData adaptation |
| Original snapshot for every calculation callback | WRF, CSF, and the equal-priority callbacks reject any input other than the original Health/Physical values | Numeric fixture values only |
| Priority and ID ordering | WRF then CSF by priority; `TieAlpha` then `TieBeta` at equal priority | Fixture providers only |
| Evaluation filter and Prediction | Calculation-only validation/tie Providers are skipped for Prediction; WRF/CSF opt into it; Unknown evaluation invokes no callback | Does not establish game prediction policy |
| Outgoing `NO_CHANGE` | After applying Providers are retired, the Provider DLL's no-change callback runs and dispatch returns `NO_CHANGE` | No external game side effects |
| Incoming callback invocation, `APPLY`, `NO_CHANGE` | PAS simulation applies to Player and NPC contexts; isolated Incoming validation Provider separately exercises Apply and NoChange | Context and power-armor values are simulated opaque data |
| Invalid output and rollback | Bad callback status, result size/version, NaN/out-of-range multiplier, and unauthorized component mask are rejected; errors return the original numeric snapshot | `ProviderFailure` is expected when the C ABI bridge rejects malformed result headers; Registry-invalid values report `InvalidProviderResult` |
| Invalid input fails closed | Nested Outgoing damage size and Incoming context size are rejected before callbacks; Unknown Incoming confidence preserves the numeric input and skips callbacks | C ABI validation only |
| Callback blocked while Unregister proceeds | Provider DLL callback is held by a condition-variable barrier; Unregister removes it while the callback is in flight | Deterministic offline interleaving |
| WaitQuiescent is a callback exit barrier | WRF remains blocked while the first waiter passes the post-claim hook; a bounded completion check confirms Wait has not returned; only then is WRF released, after which the designated waiter returns `OK` | No Provider module release before success; a premature return is explicitly recorded as failure |
| No duplicate unload permission | Concurrent same-Handle waiter returns `WAIT_IN_PROGRESS`; post-consumption repeat returns `NOT_FOUND` | C ABI state mapping is exercised |
| Failed Wait and Bridge ownership | Injected Wait failure leaves the C ABI Bridge count unchanged and does not release the Provider DLL; successful retry consumes one Bridge | Test-only failure control is not public ABI |
| Callback-originated Wait | Cross-stage callback attempt is rejected as `WOULD_DEADLOCK`; external thread Wait succeeds after callback return | Covers the tested Dispatcher scope |
| Different Handles progress independently | Existing single-process fixture blocks one Entry while another Handle reaches quiescence | This race is covered at Registry level, not by a second C ABI Host waiter |
| Stale Handle cannot affect a new registration | After the old Provider is quiescent and reloaded, old Handle operations return `NOT_FOUND`; the new callback remains callable | Same process-lifetime Host Registry |
| Host Shutdown ordering | Existing cross-DLL fixture closes ingress, rejects copied-table calls, drains the active callback, then releases Provider before Host | Explicitly mock-host lifecycle, not F4SE unload behavior |
| Test and Hook boundary | Production export probe and no-Hook structure check remain in the offline suite; Phase 2D-A lifecycle smoke still makes no Dispatch call | Neither is native runtime evidence |

## Callback/ownership ordering exercised

The in-flight case uses explicit Provider-entered, claim-entered, claim-release, hook-exiting, and
wait-completed events, plus a Provider-side callback-exit marker immediately before callback return.
Its order is: dispatch enters the Provider DLL callback and blocks;
external Unregister succeeds; the first waiter claims retirement; a competing waiter returns
`WAIT_IN_PROGRESS`; the claim hook is released and signals that it is exiting; while WRF remains
blocked, the test performs a bounded wait-completion check and records any premature result,
including an early `OK`; then WRF is released and dispatch exits; exactly the designated waiter
returns `OK`; the stale Handle returns `NOT_FOUND`; and later Dispatch does not enter the removed
callback. The designated waiter also verifies the Provider callback-exit marker before accepting
`OK`. The hook and Provider callback have bounded waits, all failure paths release both gates
before joining created threads, and CTest applies a 45-second per-process timeout to cross-DLL
fixtures. The test does not use `TerminateThread` or rely on sleep alone for event ordering.

Unregister allocation-failure injection and independent-Handle waiting are also retained in the
single-process Registry fixtures. Phase 2D-A's separate runtime-smoke source remains no-Dispatch;
the new callback tests run only in offline Host/Provider DLLs.

## Not established

- No real Fallout 4 callback/callsite, actor or weapon pointer, engine thread, native task lifetime,
  damage result, Health write, or game runtime behavior was exercised.
- The Incoming `VerifiedAdapterCallsite` marker in this test is a synthetic input and cannot be
  carried forward as evidence for a production adapter.
- The tests do not establish safety for arbitrary toolchains, CRT combinations, host unload,
  re-entrant loader callbacks, or concurrent destruction outside the explicit owner protocol.
- This does not authorize a native adapter, change ABI V3, or approve production Hook deployment.
