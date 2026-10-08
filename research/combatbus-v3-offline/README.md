# IIF CombatBus Phase 1Q–1R Offline Prototype

This directory contains an isolated ABI/Dispatcher candidate and executable fixtures. It is not connected to IIF production code, does not contain a Fallout 4 hook, and does not claim to mirror native `HitData` memory.

## Two independent stages

- `OutgoingCalculationContextV3` distinguishes verified `Calculation`, verified `Prediction`, and `Unknown`. A Provider must opt into Prediction with its `evaluationMask`; Unknown is rejected before callbacks. There is deliberately no ActualHit flag because EntryPoint `0x23` is shared by prediction and non-prediction calculations.
- `IncomingHealthContextV3` has an independent `IncomingPhase` and `ContextConfidence`. It has no native EntryPoint field, so DoHitMe/Health processing cannot be represented as EntryPoint `0x24`. Unknown phase or confidence is rejected. PAS receives Player/NPC/Power Armor state and only returns a Health multiplier.

Each callback receives numeric snapshots and optional borrowed opaque handles. It returns a multiplier and, for Outgoing, an explicit component mask. **Every Outgoing Provider receives the same immutable original snapshot.** WRF and CSF compute their own policy multiplier from that input and their own business state; IIF validates and combines the results in priority order. Priority fixes deterministic callback order; it does not make the next Provider observe the previous Provider's output. The dispatcher applies results to a local accumulated value and only returns it after every Provider succeeds. It never exposes a writable game-memory pointer. Provider callbacks must not mutate the input or retain borrowed handles/context beyond their documented lifetime.

## Fixture-only field policy

For the ordinary direct weapon fixture, an adapter explicitly permits `Health | Physical`. When both fields are valid and Physical is positive, a Provider must return both bits and one multiplier so the coupled fields stay aligned. Physical-only is always rejected because Health is required. Health-only is rejected for this ordinary profile when positive Physical is valid. If Physical is valid zero or invalid, the explicit special rule permits Health-only; a zero Physical value stays zero. Other profiles use only their explicitly supplied adapter mask and require Health, with no general Health/Physical coupling claim. Total, targeted-limb damage, and the resistance intermediate are represented but are not modifiable by this prototype policy. A named fixture-only boundary copies Health into Total to model the one observed `FC00B0` entry operation. No complete resistance formula is simulated.

Outgoing `NoDamage` checks only `validMask & modifiableMask` for Health and Physical; it never inspects an invalid or unmodifiable field. It runs after structural/numeric input validation but before profile-coupling validation. Thus a zero modifiable Health value with only positive-but-unmodifiable Physical returns `NoDamage` without calling Providers. If a modifiable value is positive but `WeaponDirect` has valid positive Physical without permission to modify both fields, the context returns `InvalidContext` before callbacks. `InvalidContext` covers a profile/permission contract mismatch; `InvalidInput` covers malformed or out-of-range valid numeric fields.

Other profiles require a separately verified adapter mask. A profile name alone does not establish that fields share semantics. Unknown profile or unapproved component writes fail closed.

## Callback, Registry, and lifetime contract

- Separate stage registries use immutable snapshots and ascending numeric priority, then ascending provider ID for ties.
- `InterfaceV3` stage values are `ProviderStage::OutgoingCalculation = 1` and `ProviderStage::IncomingHealthProcessing = 2`. Handles are nonzero and unique across both stages within one Dispatcher; passing a handle to the wrong stage returns `NotFound`. Provider IDs remain unique within a stage. This corrects the earlier stage-local allocation rule; the struct remains 64-bit, but stale Phase 1P handle values must not be reused with this candidate.
- Outgoing is a pure calculation contract and may opt into Prediction. Incoming represents a candidate Health value after resistance and before difficulty scaling; it is not identified as native EntryPoint `0x24`.
- A callback can register another provider because no Registry write mutex is held during callback execution. The new provider appears on the next dispatch.
- Same-stage recursive dispatch is rejected. Cross-stage dispatch is allowed, but a cycle returning to an active stage is rejected.
- Callback return failure, exception in the same-binary C++ fixture, malformed output, NaN/Infinity, negative or greater-than-one multiplier, and out-of-range result all roll back to the original snapshot. Exceptions are not a cross-DLL contract: Provider implementations must catch their own exceptions and return a C status without unwinding across the module boundary.
- Registration copies provider IDs. The callback and provider context remain borrowed; provider code/context must remain alive through unregistration and `Wait*Quiescent`. Unregistration prevents future callbacks; the wait is an external unload barrier. Any quiescence wait from any Provider callback on the same Dispatcher returns `WouldDeadlock`, including waits for another Provider and waits across Outgoing/Incoming stages.
- `Removed` means excluded from new snapshots and disabled; it does not mean callbacks have finished. Exactly one waiter can atomically claim a retired Handle. The claimant's `Quiescent` result consumes that retirement record; a competitor receives `WaitInProgress` while the claim is active and `NotFound` after it is consumed. The Provider owner must retain the Handle for one designated unload waiter; the Registry's first successful compare-exchange is the linearization point that grants that single claim. The Registry prevents duplicate acknowledgements but does not authenticate caller identity, so callers must honor the owner protocol. `Quiescent` is the only per-Provider result that permits unload. An Unregister preparation/allocation failure returns `AllocationFailure` before disabling; the Provider remains active and the handle remains registered. A failed/unfinished wait never authorizes unload and releases its claim for retry.
- Dispatch `Applied` means at least one eligible Provider returned a valid `Apply`, even if its multiplier is 1. `NoChange` means eligible Provider(s) ran but none applied a result. `NoProviders` means no Provider matched the stage/evaluation. `NoDamage` means valid, modifiable Health/Physical inputs were zero and no callbacks ran. Error results preserve the numeric input snapshot, but cannot undo external Provider side effects.
- `Dispatcher::Shutdown` atomically closes the API call gate, rejects later calls, waits for entered calls, and releases active and retired snapshots without allocating. The owner must first stop new external ingress; after Shutdown it must join every caller before destroying the object. Shutdown does not make concurrent object destruction or a stale call through freed object memory safe.
- Callback pointers use Win64 `__cdecl` (the unified Windows x64 calling convention). A callback must not throw across a DLL/C ABI boundary. The in-process fixture catch is defensive and is not a cross-runtime exception guarantee.
- `UnregisterStatus::AllocationFailure` preserves the old published snapshot and enabled Entry. Snapshot allocation, retired-record allocation, and retirement-map insertion happen before disable; after disable, only non-allocating publication remains. Wait errors retain the retirement handle and return `WaitFailure`. Test-only failure injection exercises these preparation failures without exhausting system memory.
- Handles are allocated by a Dispatcher-wide monotonic atomic counter and never reused; exhaustion returns `RegistrationStatus::HandleExhausted` rather than wrapping to zero or colliding.
- `atomic<shared_ptr>` is used for snapshot publication, with no claim that it is hardware lock-free.
- Dispatch may be concurrent. Provider-owned mutable state must be synchronized.

Errors return the original numeric snapshot. This numeric rollback cannot undo a Provider's external side effects; calculation callbacks must remain pure.

## Native evidence versus simulation

Static evidence for this design includes: OG `Actor::DoHitMe` at `0x140E01630` snapshots HitData `+0x90` into `XMM13` at `0x140E01751`; `0x140E018E2` moves it to `XMM1`; `0x140E018F0` calls `FUN_140D79EB0`, which applies conditions/difficulty and calls the Health ActorValue path. `FUN_140FC00B0` copies `+0x90` into `+0x94` at its resistance-calculation entry and uses `+0x98` as resistance input. Those observations support candidate stage labels and fixture boundaries only.

Not established by this prototype: exact native adapter data for every attack type, a universal WRF/CSF field mask, Prediction business policy, event consistency if Incoming Health changes after TESHitEvent, runtime threading, or production hook safety. The original C++ research interface is not itself a cross-DLL ABI. The separate C-compatible candidate and Windows DLL fixtures cover only the tested local MSVC/Clang-cl combinations, not all compilers, CRTs, loader states, or game behavior.

## Dispatcher shutdown contract

`Dispatcher::Shutdown()` closes a packed atomic call gate before draining it. Each public register, unregister, quiescence, and dispatch call must acquire a gate lease before touching a registry. Shutdown waits until those leases reach zero, then exchanges each atomic snapshot with an empty shared pointer and clears retirement storage. The cleanup path creates no replacement vector, map node, or `shared_ptr` and performs no `push_back`/`make_shared`. A fixture overrides global allocation and makes every allocation fail during shutdown; shutdown still completes.

Calling Shutdown from a callback on the same Dispatcher returns `WouldDeadlock`. A failed wait leaves the gate closed and retains registry state so the owner may retry. The destructor calls Shutdown as a final guard and terminates rather than freeing state if a safe drain cannot be established. This does **not** support concurrent destruction: the owner must stop issuing new calls, call Shutdown, and join all threads that could have entered the object before destruction. In particular, no C++ object can protect itself from a caller that dereferences it after its storage has been freed.

For the mock Host DLL, the Dispatcher is initialized lazily on the first supported interface query, after `LoadLibrary` returns. The owner must stop creating calls through copied interface tables, call exported `IIF_CombatBus_Shutdown`, wait for success, join its caller threads, and only then call `FreeLibrary` on the Host. The Host destructor requires prior successful Shutdown and never starts a drain while the DLL is unloading. Provider DLL code and context stay owned by the Provider until stage-correct `unregister_provider` followed by successful `wait_provider_quiescent`, or until successful global Host shutdown has drained every Host call. `Removed` alone never permits unload. No lifecycle work runs in `DllMain`.

## C ABI candidate and DLL fixtures

`include/CombatBusCABI.h` is a C11/C++-compatible Win64 candidate. All enums/statuses are represented by `uint32_t` constants; public function parameters use fixed-width values and plain structs. The exact V3 query rejects unsupported versions and non-exact `struct_size` values. The exported entrypoints are:

- `IIF_CombatBus_QueryInterface(version, caller_size, out_interface)` — copies the function table into caller-owned storage.
- `IIF_CombatBus_Shutdown()` — closes the Host gate and drains entered calls.

The table's `registry` pointer and function pointers are borrowed from the Host DLL. The caller must not use them after Host shutdown, and must not call through them after `FreeLibrary`. Provider IDs are copied during registration. Callback and `provider_context` addresses remain owned by the Provider DLL and must stay valid until unregister plus quiescence or a successful global shutdown drain. No C++ container, string, exception, allocator-owned object, or CRT object crosses the boundary.

The x64 layout assertions are compiled in both C and C++. Key records include `DamageSnapshot` (28 bytes, alignment 4), Outgoing context (80 bytes, alignment 8), Incoming context (56 bytes, alignment 8), Provider descriptors (40 bytes), and Interface (64 bytes, alignment 8). `CrossDllHost.exe` uses `LoadLibrary` and `GetProcAddress`; it does not link against C++ Dispatcher methods. `combatbus_iif_mock.dll` contains the Dispatcher and C adapter. `combatbus_test_provider.dll` independently owns WRF, CSF, and PAS callbacks and their context.

The callback contract forbids C++ exceptions from escaping a Provider callback. A Provider written in C++ must catch exceptions inside its own module and return `Failure`; the Host-side adapter's catch is only defensive and is not an ABI promise for cross-runtime unwinding.

## Build and run

This research tree uses xmake.lua as its build definition. Keep temporary build output under the workspace scratch tree; do not use the system drive TEMP directory. In the isolated Phase 1V run, clone and xmake outputs were contained in a dedicated directory beneath workspace scratch. CMakeLists.txt is retained only as a CTest runner/manifest when COMBATBUS_EXTERNAL_BINARY_DIR points at already-built xmake artifacts; no CMake build was used for the Phase 1V matrix.

Windows x64 targets include the single-process fixture, simulated Host DLL, full and missing-export Provider DLLs, dynamic-loading Host, and startup-only probe. Results are local offline evidence, not GitHub CI or game-runtime validation.
## Phase 1S quiescence ownership

Each retired record contains an atomic wait-claim bit. A waiter copies the record under the registry mutex, releases that mutex, then uses compare-exchange to claim the one permitted wait. A competing waiter returns `WaitInProgress` without blocking on the Provider Entry and without receiving unload permission. The owner that designated itself to consume the Handle receives `Quiescent` only after the Entry drain and retirement-record removal. A wait failure resets the claim and retains the retirement record for retry. The C ABI maps `WaitInProgress` to the appended status value 13; no public structure layout or function-table layout changed.

The Dispatcher tracks active callback frames by Dispatcher identity, not only by Entry identity. A callback cannot synchronously wait on any Provider retired in the same Dispatcher, including the opposite stage. External callers can wait normally after callbacks return. The C ABI appends `IIF_CB_STATUS_WAIT_IN_PROGRESS` (numeric status 13); public struct sizes and the V3 function-table layout do not change. Consumers must treat this status as no unload permission. Two test-only exports on the mock IIF DLL control a post-claim barrier and one-shot wait-failure injection; they are not members of the public V3 function table and are compiled only for offline tests.

## Phase 1T design review documents

- [`ABI-CONTRACT.md`](ABI-CONTRACT.md) records the candidate C ABI layout, stage/status meanings, Provider unload authority, Shutdown ownership, and freeze blockers. It is a review contract, not an ABI freeze.
- [`PRODUCTION-MIGRATION-PLAN.md`](PRODUCTION-MIGRATION-PLAN.md) is a read-only review of the current IIF legacy combat callbacks plus a staged migration and production validation plan. No production source is changed and no native hook is approved.
- [`CLANGCL-CRASH-INVESTIGATION.md`](CLANGCL-CRASH-INVESTIGATION.md) records the stale `TestProvider_GetCounter` use-after-unload already fixed, plus the later Clang-cl `0xC0000005` Host failure. Phase 1U parsed its existing minidump with DbgHelp: ExceptionAddress/RIP are Host RVA `0x4DB0`, but the captured write target is inconsistent with the exact entry instruction, so root cause remains UNKNOWN. New missing-export guards have a negative reload fixture; fresh MSVC/Clang-cl 25-repeat suites, seven mixed compiler combinations, and MSVC ASan passed. The historical failure is NOT REPRODUCED, not resolved; ABI freeze and production hooks remain blocked/not approved.

Phase 1T remains offline-only. The public ABI candidate is not frozen; there is no game runtime validation, production migration, or production Hook approval.

## Phase 1V startup and stability follow-up

CLANGCL-CRASH-INVESTIGATION.md records the bounded startup review and current test evidence. The historical dump still lacks a reliable call stack and remains UNKNOWN. Current startup probes establish only which markers were reached in these new runs. Both compiler families reached wmain and normal return; the full Host fixture reached the expected Host/Provider loads, reload, Shutdown drain, unload, and normal exit.

WINDOWS-CI-PLAN.md describes the MSVC/Clang-cl Windows GitHub Actions matrix installed for this offline prototype. Build outputs, CTest logs, and marker files were kept under workspace scratch and are removed after testing; no binaries are committed.
