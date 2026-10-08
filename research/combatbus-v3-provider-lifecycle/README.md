# CombatBus V3 Provider Lifecycle Smoke (Phase 2D-A)

This is a temporary, independent F4SE diagnostic plugin for Fallout 4 OG 1.10.163. It verifies the real IIF V3 QueryInterface and per-Provider registration lifecycle. It does not verify combat calculation, callback execution, native damage callsites, or Provider DLL unloading.

## Runtime behavior

At F4SE `kPostLoad`, the plugin obtains the already-loaded `ItemIntegrationFramework.dll` with `GetModuleHandleW` and resolves `IIF_CombatBus_QueryInterface` with `GetProcAddress`. It never loads or unloads the IIF Host. It checks unsupported-version and wrong-size rejection, then reads the V3 table and validates only its registration/unregister/quiescence members.

The shared `ProviderLifecycleScenario.cpp` performs these lifecycle-only operations:

1. Reject malformed Outgoing and Incoming descriptor version/size values.
2. Register one Outgoing test Provider and one Incoming test Provider at priority 9000, each with a unique `iif.phase2d.*` ID and a process-lifetime static context.
3. Verify transport status, embedded registration status, `added`, nonzero handles, and distinct cross-stage handles.
4. Verify duplicate IDs return `DUPLICATE` without a new handle.
5. Verify wrong-stage Unregister/Wait and Wait-before-Unregister return `NOT_FOUND`.
6. Unregister each Provider through its correct stage, then make exactly one designated `WaitQuiescent` call per retired handle.
7. Verify expired handles cannot obtain a second successful removal or quiescence result.
8. Confirm callback counters remain zero before reporting PASS.

No Outgoing or Incoming Dispatch function is called. The callbacks are inert `NO_CHANGE` sentinels that only increment an atomic counter if unexpectedly entered; they never inspect callback contexts or game-object pointers. They and their contexts remain static for the diagnostic module's process lifetime. The plugin has `init.hook = false` and `init.trampoline = false`, creates no task/thread, and does not use `DllMain` lifecycle work.

If the scenario fails after registration, it retains each nonzero handle and attempts bounded, stage-correct cleanup. Only `Unregister == OK` followed by that handle's `WaitQuiescent == OK` closes the per-Provider lifecycle. A failed unregister/wait is logged and does not free its context or callback code. The F4SE plugin is never explicitly unloaded; it remains loaded until normal game process exit. No blocking wait or join runs during DLL detach.

## Build

The xmake project uses the existing CommonLibF4 checkout and targets OG 1.10.163. It has no optional dispatch mode.

```powershell
$env:COMMONLIBF4_PATH = '<existing CommonLibF4 checkout>'
xmake config --project=research/combatbus-v3-provider-lifecycle -p windows -a x64 -m releasedbg --builddir=build/phase2d-a-provider-lifecycle
xmake build --project=research/combatbus-v3-provider-lifecycle --target=IIFCombatBusProviderLifecycleSmoke --jobs=4
```

Keep all local outputs under workspace `scratch/`. Do not copy the DLL to MO2 or the game. The repository does not include compiled binaries.

## Offline verification

`tests/CMakeLists.txt` builds a small Host EXE and a separate DLL from the same production Registry/C ABI source files. The Host dynamically loads only that isolated contract DLL, registers the same test Providers, never calls Dispatch, never explicitly calls `FreeLibrary`, and exits with the OS tearing down both modules. This validates the C ABI lifecycle across a DLL boundary without loading the F4SE plugin into an ordinary test process.

```powershell
cmake -S research/combatbus-v3-provider-lifecycle/tests -B <scratch-build-dir> -A x64
cmake --build <scratch-build-dir> --config Release
ctest --test-dir <scratch-build-dir> -C Release --output-on-failure
```

`VerifyLifecycleNoDispatch.ps1` is a structural guard, not runtime evidence. The cross-DLL offline Fixture checks actual C ABI calls. Neither test establishes that F4SE or Fallout 4 loaded the diagnostic plugin.

## Phase 2D-B boundary

Phase 2D-B is not implemented or authorized here. It would introduce real callback invocation and concurrency while a Provider is registered. Before that work, obtain a separate ChatGPT review and explicit user authorization, define the safe thread/context contract, and keep callbacks free of game-object dereference or persistent side effects. Passing 2D-A does not grant that authorization.
