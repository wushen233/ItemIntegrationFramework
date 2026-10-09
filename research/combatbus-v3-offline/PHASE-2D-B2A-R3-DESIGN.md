# Phase 2D-B2A-R3 — Observation Offline Engineering Design

## Scope and acceptance boundary

The new fixtures model only the verified five-argument Win64 `HandleEntryPoint` call shape and byte-array algorithms. They do not patch a process, call Fallout 4, load an F4SE module, dispatch CombatBus V3, read or write Actor/Weapon objects, or establish a safe live installation window. Offline PASS means only that these models and compiled wrapper obey their test contracts.

## Observation call ABI model

At the four audited sites the ABI model is exactly: `RCX=entry point`, `RDX=owner/actor-like argument`, `R8` and `R9` are site-specific instance wrappers, and caller stack `[RSP+0x20]` is the fifth `float*`. The fixture uses a typed five-argument C++ wrapper and a MASM caller that sets sentinels in RBX/RBP/RSI/RDI/R12–R15, supplies the fifth argument on the stack, and verifies those nonvolatile registers after return.

The wrapper snapshots the float, calls the injected original exactly once, snapshots the float again, records only the two scalar values, and returns the original 64-bit result. It never writes the float. It has C++ language linkage (the internal callsite target needs the Win64 machine ABI, not an exported C symbol) so exception unwinding can cross the wrapper in the Clang-cl fixture as well as MSVC. If recording fails or the bounded sink is full, it drops the record and still returns from the one original call. If the original throws, the wrapper does not retry; normal C++ unwinding through its compiler-generated Win64 unwind metadata is tested. The MASM probe has explicit unwind directives as well.

Recursive invocation is allowed in the offline model. Each nested invocation performs its own single original call and gets a distinct record sequence. This tests the wrapper’s direct call discipline; it does not prove engine reentrancy or native object lifetime.

## CALL rel32 algorithm model

The offline decoder accepts only five available bytes beginning with `E8`, sign-extends the displacement, and resolves `instructionVA + 5 + rel32`. Encoding rejects targets outside signed 32-bit reach. The transaction model validates all expected bytes, targets, replacement reach and non-overlapping ranges before changing the isolated byte vector. It writes exactly five bytes per site and checks neighbors remain unchanged.

Fixtures cover bad opcode, fewer than five bytes, positive and negative target resolution, both rel32 range edges, signature drift, simulated third-party modification, multi-site all-or-none preflight, overlap rejection, second-site failure, partial five-byte write failure with exact byte rollback, and rollback failure reporting `RecoveryRequired`. The last result deliberately does not claim that the memory was restored.

This is an algorithm model over ordinary test memory. It proves nothing about live page protections, instruction-cache synchronization, atomicity, concurrent execution, process threads, trampolines or the safety of rolling back executable memory.

## Live installation thread-safety finding

**LIVE CALLSITE PATCH BLOCKED.** No evidence establishes an OG 1.10.163 installation window where all threads are excluded from the four instruction ranges. F4SEPlugin_Load is not such proof. A five-byte CALL replacement is not atomic; a running thread could observe a torn opcode/displacement or already be executing inside the affected bytes. This investigation has no validated mechanism to enumerate and coordinate every game/plugin thread, safely park a thread whose IP lies inside the range, coordinate third-party hook owners, or atomically roll back all four patches.

Before any implementation proposal can become an implementation, an independently reviewed protocol must prove: (1) every relevant thread is stopped or otherwise excluded; (2) no stopped thread IP lies in a patch range or relay being changed; (3) all sites and original targets are owned/unchanged; (4) patch and rollback occur under that same coordination; (5) executable protection and instruction-cache flushing are correct; (6) no callback can race with module teardown. If any condition is unavailable, do not patch. A partial patch whose rollback cannot be confirmed requires retaining the wrapper module resident and inert/pass-through; it must never be unloaded while a patched CALL may target it.

Other plugins can patch a callsite or the callee independently. Exact expected bytes and target/prologue checks can detect a mismatch, but cannot identify every owner. A cooperative hook-owner protocol is absent. Unknown modifications therefore fail closed; no chaining behavior is inferred.

## Legacy isolation build contract

The static source anchors in current `src/main.cpp` show one production `IIF::CombatBus::Install()` call from initialization. It installs the generic combat detour, writes Actor vtable slot `0x10B`, and enables `MH_ALL_HOOKS`; its body iterates old Damage/Armor callback vectors. Observation build design excludes that entire installer and all those callback paths, while preserving the public V3 ABI unchanged.

The historical `IIF_Interface` contains void registration callbacks; it cannot return “disabled” without an ABI change. An observation-only build can retain the same layout and expose test-mode no-op registration functions only if all legacy consumers are excluded from the isolated profile. The isolated profile must not load WRF, CSF or any plugin registering old callbacks; otherwise the consumer could believe registration succeeded. Do not silently run the production DLL and observation DLL together.

Noncombat candidate preservation is source-separated, not yet build-proven: `RegisterCPPCard`/message exchange, JSON configuration (`JsonReader`), localization (`Localizer`), UI/Scaleform (`UIHooks`), ImGui and their noncombat setup. `UIHooks::Install` has its own Pipboy vtable/trampoline changes; ImGui uses its own window/message setup. Those remain separate runtime risks and must be audited individually; do not preserve them through the combat installer or global `MH_EnableHook(MH_ALL_HOOKS)`.

The static fixture pins `src/main.cpp` and `src/IIF_API.h` SHA-256 to this exact source baseline, verifies the currently present Legacy anchors and tests a separate contract file declaring their exclusion. This validates the review contract only; it does not compile an Observation IIF variant or prove a future source projection obeys it.

Identity rules for any later test artifact: distinguish product/release name, loaded F4SE DLL filename, plugin metadata name, and the `GetModuleHandleW` lookup name; audit every module lookup and message exchange against the selected filename. The planned output label `IIFObservationOfflineOnly.dll` is a build-contract example only, not a deployable file. Manifest must state exact source HEAD, runtime target, build mode, actual loaded path, hash and exclusive-profile condition. Production and observation DLLs are mutually exclusive. Use a different build directory; verify production DLL hash before/after the test build.

## Bounded observation record model

`MpscObservationRing` is preallocated at construction and accepts trivially-copyable scalar records. A producer acquires a bounded CAS lease, reserves one sequence slot, writes the record, then publishes the slot sequence with release ordering. The single consumer reads sequence with acquire ordering, copies the record and frees the slot with release ordering. Failed/full/closed reservation increments dropped/overflow counters and returns without waiting indefinitely. The consumer does not skip an earlier unpublished reservation; a later completed record remains queued until the earlier slot is committed or cancelled. Cancelled slots are bounded tombstones.

`WaitForProducers` returns success only after `Close` has made further reservations impossible and the active producer lease count reaches zero. An open ring returns failure even when currently empty. The offline control-thread waiter uses bounded 1 ms polling with an explicit deadline rather than a condition variable: the producer hot path takes no wait mutex and cannot lose a final notification. A waiter/final-release race fixture checks that completion remains observable before its deadline. This wait proves producer quiescence only; storage may be destroyed only after the consumer has also drained the closed queue and `IsClosedAndDrained()` is true. The fixture includes open-empty rejection, active reservation rejection, commit and cancel completion, and a four-producer/one-consumer run whose initial-record handshake proves actual overlap. That run validates 2,048 successful unique records, retries/drops, repeated slot reuse, and complete drain under a CTest timeout. A delayed-head reservation/cancel fixture also proves a concurrent consumer does not pass an unpublished slot.

The Win64 MASM caller now checks RBX/RBP/RSI/RDI/R12-R15 and the low 128 bits of XMM6-XMM15 across the wrapper call. It saves and restores the test caller's original XMM values and declares XMM save locations in unwind metadata. This is an offline ABI fixture only; it does not establish a safe live callsite patch or cover all possible compiler/runtime behavior.

Close sets a closed bit in the same producer-state atomic that counts active leases. This linearizes “new producers rejected” against in-flight producers. An external owner may wait with a finite timeout for current leases to finish, then drain. A timeout means retain the ring/module; do not destroy it. The outer owner still has to prevent future calls from entering the containing module before destruction; a queue cannot make its own object lifetime safe after its caller unloads.

The production wrapper candidate must not retain object pointers or dereference them. Records may contain callsite ID, entrypoint/component ID, current thread ID, float before/after, monotonic record sequence, loss and overflow counts, and an opaque per-session HitData correlation token derived only from audited scalar pointer arithmetic. Raw pointers are neither exported nor used asynchronously. The current fixtures use fake integer addresses only for the rel32 model, not as game objects.

Log I/O belongs to one non-game consumer. No file I/O, wait, allocation or logger lock is allowed inside the observed call. The worker must not run in DllMain; teardown is permitted only after callsite callbacks and consumer are proven quiescent. If that cannot be proven, keep the module resident through process exit.

## Static evidence and test boundaries

Three distinct facts remain distinct: a `0x24` call executes at an audited calculation site; a processed HitData Health field reaches DoHitMe; a gated positive path can write through a Health ActorValueInfo-associated storage path. The new machine-code appendix supports the ActorValue identity using `ActorValue::health` at singleton `+0xD8`; none of these facts makes the `0x24` call a final Health commit or a V3 VerifiedAdapterCallsite.

Prediction TLS and HitData bit14 are set before calling the shared weapon initializer, so Outgoing `0x23` can be prediction work. Do not label observations ActualHit.

## Future isolated runtime test plan — not run in this phase

1. User creates a separate MO2 profile and disables the installed production/older IIF mod entry. Confirm there is exactly one IIF provider module and no competing observation DLL. Disable WRF, CSF and all unknown V3/legacy combat providers.
2. User verifies the game runtime is OG 1.10.163. Codex does not launch the game. Before a test, compare the actual game executable and loaded image code fingerprint to a supported-runtime manifest, not merely the old unpacked-input SHA.
3. Record observation DLL file name, F4SE plugin identity, full path resolved by module APIs, exact SHA-256 and source/build manifest. Check no `GetModuleHandleW` consumer assumes a different filename.
4. Any mismatch in runtime, call bytes, decoded target, expected callee signature, hook ownership, or thread-coordination precondition stops before the first patch.
5. First observation candidate is Incoming Health only at `0x140FC13DB`; no dispatch and no writeback. A logged `0x24` call is not proof of final Health commit. Runtime pass requires expected callsite/entry/component, before/after values, thread and monotonic sequence, zero unexplained loss, and no crash/error; even then it is observation evidence only.
6. User saves F4SE and observation logs and crash log if present, exits the game, then restores the original IIF DLL manually and checks its saved hash. Do not keep a process that might contain a partial patch as a reusable environment. Any install/rollback uncertainty means keep the wrapper resident until game exit and mark the run failed/indeterminate.

## Remaining gates

- Live thread exclusion/patch/rollback protocol: unresolved and blocking.
- Runtime raw image identity vs enriched unpacked input: not matched in this phase.
- Generic third-party hook ownership: no universal protocol; mismatch can only fail closed.
- Noncombat IIF variant has not been built or regression-tested.
- ActualHit classification and whole incoming task lifetime are not proved.
- V3 Native Adapter/Dispatch, game test and ABI freeze remain unapproved.
