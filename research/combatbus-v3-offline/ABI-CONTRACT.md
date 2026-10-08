# CombatBus V3 ABI Candidate Contract

**Status: candidate for final review; not frozen and not approved for production use.**

This document describes the C ABI candidate in `src/CombatBusV3/CombatBusCABI.h` and its production/mock Host adapters. It does not define Fallout 4 native hook locations, nor does a successful fixture establish compatibility with every compiler, runtime, loader state, or game call path.

## Version and structure handshake

- V3 is identified by the fixed-width value `3` (`IIF_CB_VERSION_3`). All public status, stage, kind, mask, and callback values cross the C boundary as `uint32_t`; priorities are `int32_t`; Provider handles are `uint64_t`.
- Query is strict: `requested_version == 3`, `caller_size == sizeof(IIF_CB_InterfaceV3)`, and the caller initialized `out_interface->struct_size` to that same exact size. A mismatch is rejected. V3 does not currently support prefix/forward-compatible larger records or older smaller table prefixes.
- Every descriptor/context/result/dispatch record has `struct_size` and `version`; the mock implementation requires exact V3 sizes. The nested damage snapshot is also size-checked. Unknown versions and invalid sizes fail closed.
- Negotiation is all-or-nothing: V3 has no separate optional-capability bitset, and consumers must require every function-table member. Prediction is an explicit Provider evaluation opt-in, not a negotiated Host feature. Unknown evaluation bits and undocumented optional semantics must be rejected; future interface capabilities require a separately reviewed version/contract change rather than optimistic use of unknown values.
- C11 and C++20 compile the public header. The intended binary target is Windows x64. On Windows callbacks and exports are declared `__cdecl`; Windows x64 uses the platform's unified x64 calling convention. This does not promise compatibility with non-Windows targets or arbitrary 32-bit ABI variants.

## Win64 layout

Sizes and alignments below are for the intended Win64 ABI. Offsets are byte offsets. Every listed public field offset, record size, and alignment is guarded by shared C11/C++ static assertions and compiled by the C11 smoke target and C++20 Host target.

| Public record | Size / alignment | Important offsets |
|---|---:|---|
| `IIF_CB_DamageSnapshotV3` | 28 / 4 | `struct_size` 0; `valid_mask` 4; `health_damage` 8; `physical_damage` 12; `total_damage` 16; `targeted_limb_damage` 20; `resistance_intermediate` 24 |
| `IIF_CB_OutgoingContextV3` | 80 / 8 | `struct_size` 0; `version` 4; `evaluation_kind` 8; `attacker_kind` 12; `profile` 16; `modifiable_mask` 20; `attacker` 24; `target` 32; `weapon` 40; `damage` 48 |
| `IIF_CB_OutgoingResultV3` | 20 / 4 | `struct_size` 0; `version` 4; `status` 8; `component_mask` 12; `multiplier` 16 |
| `IIF_CB_IncomingContextV3` | 56 / 8 | `struct_size` 0; `version` 4; `phase` 8; `confidence` 12; `target_kind` 16; `power_armor` 20; `attacker` 24; `target` 32; `weapon` 40; `health_damage` 48 |
| `IIF_CB_IncomingResultV3` | 20 / 4 | `struct_size` 0; `version` 4; `status` 8; `reserved` 12; `multiplier` 16 |
| `IIF_CB_OutgoingProviderV3` | 40 / 8 | `struct_size` 0; `version` 4; `provider_id` 8; `priority` 16; `evaluation_mask` 20; `provider_context` 24; `callback` 32 |
| `IIF_CB_IncomingProviderV3` | 40 / 8 | `struct_size` 0; `version` 4; `provider_id` 8; `priority` 16; `reserved` 20; `provider_context` 24; `callback` 32 |
| `IIF_CB_ProviderHandleV3` | 8 / 8 | `value` 0 |
| `IIF_CB_RegistrationV3` | 24 / 8 | `struct_size` 0; `version` 4; `status` 8; `added` 12; `handle` 16 |
| `IIF_CB_OutgoingDispatchV3` | 44 / 4 | `struct_size` 0; `version` 4; `status` 8; `changed_mask` 12; `damage` 16 |
| `IIF_CB_IncomingDispatchV3` | 16 / 4 | `struct_size` 0; `version` 4; `status` 8; `health_damage` 12 |
| `IIF_CB_InterfaceV3` | 64 / 8 | `struct_size` 0; `version` 4; `registry` 8; `register_outgoing` 16; `register_incoming` 24; `unregister_provider` 32; `wait_provider_quiescent` 40; `dispatch_outgoing` 48; `dispatch_incoming` 56 |

The function table is copied into caller-owned storage, but its registry token and function pointers refer to Host-owned state/code and are valid only while the Host is active and loaded. Copying the table does not retain the Host module.

## Stages and calculation semantics

- Stage `1` is **Outgoing Calculation**. It is a numeric policy calculation, not an ActualHit event. `evaluation_kind` is Calculation (`1`), Prediction (`2`), or Unknown (`0`). A Provider opts into Prediction through `evaluation_mask`; Unknown never matches. It may receive only validated, adapter-supplied context.
- Stage `2` is **Incoming Health Processing**. It is a distinct candidate Health adjustment stage; it is not EntryPoint `0x24`. The candidate phase value `1` means after resistance and before difficulty scaling, with confidence `1` meaning a verified adapter callsite. Unknown phase/confidence fails closed.
- Every Outgoing Provider receives the same immutable original numeric snapshot. Providers independently return a multiplier; priority orders callback execution and deterministic result composition, but a later Provider does not observe prior output. This is the WRF priority `100` then CSF priority `200` contract.
- `valid_mask` authorizes reading a damage field. `modifiable_mask` is separately supplied by an adapter. The mock prototype enforces only its explicitly scoped profiles, including the fixture-only `WeaponDirect` Health/Physical coupling rule. Unknown or incompatible contexts fail closed. No native HitData memory is represented by these structures.
- Callback outputs are validated before composition. Failure, unknown callback status, malformed result headers/masks, non-finite/out-of-range multipliers, or invalid final numbers return an error status and preserve the original numeric snapshot. This rollback cannot reverse external side effects; Outgoing calculation Providers must therefore be pure.

## Status contract and unload authority

All exported functions return a transport/lifecycle status. Dispatch functions additionally write a `dispatch.status`; callers must inspect both. A transport `OK` does not mean a dispatch applied a modifier.

Callback result values are `NO_CHANGE = 0`, `APPLY = 1`, and `FAILURE = 2`. Any other callback return or result status is invalid and fails closed. The callback status and result-record status must agree; only the exact documented `NO_CHANGE` shape or matching `APPLY` shape is accepted.

The embedded dispatch result status is a separate domain: `APPLIED = 0`, `NO_PROVIDERS = 1`, `NO_DAMAGE = 2`, `INVALID_CONTEXT = 3`, `INVALID_INPUT = 4`, `PROVIDER_FAILURE = 5`, `INVALID_PROVIDER_RESULT = 6`, `RECURSIVE = 7`, `NO_CHANGE = 8`, `CLOSED = 9`. A dispatch API return of transport `OK` only means the call was accepted and produced an output record; consumers must inspect its embedded dispatch status. Unknown future embedded values must be handled as failure and must not cause a native write.

| C status | Value | Meaning | Grants Provider unload authority? |
|---|---:|---|---|
| `OK` | 0 | Operation completed; for Unregister, removal was published; for Wait, the single wait claim completed quiescently | **Only when returned by stage-correct Wait after successful Unregister** |
| `UNSUPPORTED_VERSION` | 1 | Requested ABI version unsupported | No |
| `INVALID_ARGUMENT` | 2 | Null, invalid token, or invalid stage argument | No |
| `INVALID_STRUCT_SIZE` | 3 | Record/table size mismatch | No |
| `SHUTTING_DOWN` | 4 | Host/Dispatcher closed or rejecting calls | No |
| `INVALID_PROVIDER` | 5 | Invalid callback/descriptor | No |
| `DUPLICATE` | 6 | Duplicate Provider identity in a stage | No |
| `ALLOCATION_FAILURE` | 7 | Registration/unregister preparation failed without removal | No |
| `HANDLE_EXHAUSTED` | 8 | No nonzero unique handle can be allocated | No |
| `NOT_FOUND` | 9 | Wrong/stale stage handle or retirement already consumed | No |
| `WOULD_DEADLOCK` | 10 | Unsafe wait/shutdown from active callback/call context | No |
| `WAIT_FAILURE` | 11 | Wait failed; retirement remains retryable | No |
| `INTERNAL_ERROR` | 12 | Unexpected adapter error | No |
| `WAIT_IN_PROGRESS` | 13 | Another caller currently owns the single-consumer wait claim | No |

`Unregister == OK` means only that the Provider is disabled for future snapshots. It does not mean all callbacks have returned. For an individual Provider unload protocol, the owner first calls stage-correct Unregister, then designates **one** owner thread to call stage-correct Wait, and may unload callback code/context only after that call returns `OK`. `WAIT_IN_PROGRESS`, `WAIT_FAILURE`, `NOT_FOUND`, `WOULD_DEADLOCK`, `SHUTTING_DOWN`, invalid arguments, or any unknown future status do not authorize unload. The Registry gives one caller the wait claim but does not authenticate caller identity; Provider owners must enforce a single designated unload waiter themselves.

Global Host Shutdown is a separate mock-fixture lifecycle path. The offline mock owner first stops new ingress through copied tables, then calls its test-only `IIF_CombatBus_Shutdown`. A successful mock Shutdown closes the Host gate, drains entered calls/callbacks, and releases mock bridges/registries. Production IIF does not export this operation: its Host is process-lifetime state and arbitrary Providers cannot close it. A Provider must stop and join its own work before unloading; successful per-Provider quiescence only drains callbacks initiated through the Registry. A failed/reentrant mock Shutdown grants no unload permission, and mock Shutdown is not a per-Provider Wait result.

## Ownership, exceptions, and module lifetime

- `provider_id` is borrowed only for the registration call; the Host copies it.
- `provider_context`, callback function address, opaque Actor/Weapon handles, context records, and result records are borrowed. Context/result storage is valid only for the synchronous callback. Providers must not retain or free Host/engine pointers.
- The Provider owns callback code and `provider_context`; it must keep them alive through removal plus successful quiescence. Only the isolated mock fixture permits its test owner to use successful global Host Shutdown as an alternative drain; production IIF has no Provider-accessible global Shutdown.
- No C++ exception, STL object, allocator-owned object, CRT object, or cross-module ownership transfer is part of this ABI. Provider callbacks catch their own exceptions and return `FAILURE`; catching on the Host side is defensive only and does not promise cross-runtime unwinding safety.
- No lifecycle work may depend on `DllMain`. The Query export is called explicitly after module load; the mock-only Shutdown export is not part of production IIF.
- Mock Host Shutdown is valid only under the owner-side stop-ingress/join-caller protocol. It cannot make a stale function pointer or concurrent call through freed Host storage safe.

## Registry linearization

- Registration publishes a Provider snapshot and returns a nonzero Dispatcher-global handle. Handles are unique across Outgoing and Incoming within one Dispatcher and are not reused; wrong-stage use returns `NOT_FOUND` and cannot affect a Provider in the other stage.
- Unregister prepares all potentially allocating state before the irreversible disable/publication step. Allocation failure leaves the Provider registered and callable.
- Wait claims the retired record with a single compare-exchange and performs the potentially blocking Entry drain without holding the Registry write lock. A competitor gets `WAIT_IN_PROGRESS`; failed waits release their claim and preserve retry; the successful claim drains and consumes the record once.
- A Wait must follow removal. Only `Quiescent` is mapped to C `OK`; every other Wait result maps to a non-success C status. Unknown numeric statuses must be treated by consumers as failure/no-unload.
- A Provider callback may register Providers, but it must not synchronously wait for any Provider in the same Dispatcher, including the opposite stage. Such waits return `WOULD_DEADLOCK` to prevent cross-Provider wait cycles.
- The internal Dispatcher shutdown rejects new leases, drains current leases, and clears state without allocating. It is used by the offline mock owner; production IIF uses process-lifetime ownership. Any owner that destroys a Dispatcher must stop ingress and join all call threads first; concurrent destruction is unsupported.

## Freeze blockers / review notes

This document is a candidate contract, not a freeze decision. Before freezing, independently review the exact C ABI surface and add/retain separate-DLL tests for every exported status/lifetime rule. The mock tests do not prove native callsite semantics, production hook installation, game-thread behavior, or all compiler/CRT combinations.
