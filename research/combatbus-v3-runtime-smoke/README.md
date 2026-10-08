# IIF CombatBus V3 Runtime Smoke Probe

Temporary, test-only F4SE plugin for Fallout 4 OG 1.10.163. It is separate from the production IIF target and is never deployed by a build step.

## Default build: query-only

The default xmake option `outgoing_identity_smoke` is **OFF**. This build checks that the already-loaded IIF module exports `IIF_CombatBus_QueryInterface`, calls it at F4SE `kPostLoad`, verifies correct and incorrect version/size negotiation, and checks Incoming `CONFIDENCE_UNKNOWN` rejection with the value unchanged. It explicitly logs that synthetic Outgoing Dispatch was skipped. The default binary contains no call to `dispatch_outgoing`.

Build into a scratch-local directory from the repository root:

```powershell
$env:COMMONLIBF4_PATH = '<path to the existing CommonLibF4 checkout>'
xmake config --project=research/combatbus-v3-runtime-smoke -p windows -a x64 -m releasedbg --builddir=build/phase2c-query-only
xmake build --project=research/combatbus-v3-runtime-smoke --target=IIFCombatBusRuntimeSmoke --jobs=4
```

## Optional build: synthetic Outgoing identity

The opt-in build adds local sentinel values as simulated attacker/weapon pointers and calls Outgoing Dispatch. It is unsafe if any matching V3 Provider is registered, because the production ABI has no reliable Registry-emptiness query. Build or use this variant only after verifying a strict isolated test profile contains no V3 Provider DLLs. Never use real game object pointers and never forge Incoming adapter-callsite evidence.

It has a distinct DLL basename and must use a separate build directory and checksum:

```powershell
$env:COMMONLIBF4_PATH = '<path to the existing CommonLibF4 checkout>'
xmake config --project=research/combatbus-v3-runtime-smoke -p windows -a x64 -m releasedbg --outgoing_identity_smoke=y --builddir=build/phase2c-outgoing-opt-in
xmake build --project=research/combatbus-v3-runtime-smoke --target=IIFCombatBusRuntimeSmoke --jobs=4
```

If the dispatch result is anything other than `NO_PROVIDERS`, stop the test and retain the log. This synthetic probe is not real combat calculation validation.

## Safety and verification limits

The plugin never registers a Provider, installs a hook, writes game memory, starts threads/timers, creates game tasks, or dereferences Actor/Weapon objects. It registers one F4SE message listener and remains resident for process lifetime. Query-only is the only default build. `tests/VerifyDefaultQueryOnly.ps1` checks that the synthetic Outgoing call and pointers stay behind the disabled compile-time option and that no verified Incoming confidence is forged.

The GitHub Windows workflow tests the offline Registry/ABI fixtures; it does **not** compile or run this CommonLibF4 diagnostic plugin. Local xmake build results and DLL hashes must be reported separately. No DLL, EXE, PDB, or game log belongs in this source directory or public PR.
