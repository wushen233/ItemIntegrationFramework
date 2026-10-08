# IIF CombatBus Phase 1O Offline Prototype

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
- Callback return failure, exception in this same-binary fixture, malformed output, NaN/Infinity, negative or greater-than-one multiplier, and out-of-range result all roll back to the original snapshot.
- Registration copies provider IDs. The callback and provider context remain borrowed; provider code/context must remain alive through unregistration and `Wait*Quiescent`. Unregistration prevents future callbacks; the wait is an external unload barrier. Calling that wait from the Provider's own callback returns `WouldDeadlock`.
- `Removed` means excluded from new snapshots and disabled; it does not mean callbacks have finished. `WaitQuiescent` must return `Quiescent` before callback code/context can be unloaded. An Unregister preparation/allocation failure returns `AllocationFailure` before disabling; the Provider remains active and the handle remains registered. A failed/unfinished wait never authorizes unload.
- Dispatch `Applied` means at least one eligible Provider returned a valid `Apply`, even if its multiplier is 1. `NoChange` means eligible Provider(s) ran but none applied a result. `NoProviders` means no Provider matched the stage/evaluation. `NoDamage` means valid, modifiable Health/Physical inputs were zero and no callbacks ran. Error results preserve the numeric input snapshot, but cannot undo external Provider side effects.
- The owner must stop new dispatch/register/unregister calls and join all dispatcher callers before destroying the `Dispatcher`; per-provider quiescence does not protect the dispatcher object's own lifetime.
- Callback pointers and InterfaceV3 function pointers use Win64 `__cdecl` (the unified Windows x64 calling convention). A callback must not throw across a DLL/C ABI boundary. The in-process fixture catch is defensive and is not a cross-runtime exception guarantee.
- `UnregisterStatus::AllocationFailure` preserves the old published snapshot and enabled Entry. Snapshot allocation and retirement-map insertion happen before disable; after disable, only non-allocating publication remains. Wait errors retain the retirement handle and return `WaitFailure`. Test-only failure injection exercises snapshot-preparation and retirement-insertion failures without exhausting system memory.
- Handles are allocated by a Dispatcher-wide monotonic atomic counter and never reused; exhaustion returns `RegistrationStatus::HandleExhausted` rather than wrapping to zero or colliding.
- `atomic<shared_ptr>` is used for snapshot publication, with no claim that it is hardware lock-free.
- Dispatch may be concurrent. Provider-owned mutable state must be synchronized.

Errors return the original numeric snapshot. This numeric rollback cannot undo a Provider's external side effects; calculation callbacks must remain pure.

## Native evidence versus simulation

Static evidence for this design includes: OG `Actor::DoHitMe` at `0x140E01630` snapshots HitData `+0x90` into `XMM13` at `0x140E01751`; `0x140E018E2` moves it to `XMM1`; `0x140E018F0` calls `FUN_140D79EB0`, which applies conditions/difficulty and calls the Health ActorValue path. `FUN_140FC00B0` copies `+0x90` into `+0x94` at its resistance-calculation entry and uses `+0x98` as resistance input. Those observations support candidate stage labels and fixture boundaries only.

Not established by this prototype: exact native adapter data for every attack type, a universal WRF/CSF field mask, Prediction business policy, event consistency if Incoming Health changes after TESHitEvent, runtime threading, or production hook safety. The header uses standard-layout/fixed-width records and asserts Win64 layout, but it remains a C++ research header; no real cross-DLL handshake, exported C entrypoint, compiler-matrix ABI test, or DLL unload test has been performed. All fixture PASS results prove only offline code behavior.

## Build and run

Configure with CMake using an available C++20 compiler, then build and run CTest. The generated executable and build tree must remain under this directory.
