# Phase 1T Clang-cl Cross-DLL Crash Investigation

## Result

The intermittent Clang-cl cross-DLL fixture crash was reproduced and one concrete test-harness use-after-unload was fixed. The fixed source passed fresh serial runs and 25 paired concurrent MSVC/Clang-cl CTest runs. After the public header gained complete shared C/C++ layout assertions and the Clang targets were rebuilt, another serial Clang-cl cross-DLL Access Violation occurred at a different location. That later failure is unresolved; overall crash status is **BLOCKED**, not resolved. This is not evidence of native Hook safety.

The reproduction was an Access Violation (`0xC0000005`) in `combatbus_test_provider.dll_unloaded`, offset/RVA `0x1AC0`. `dumpbin /exports` on the exact Clang-cl Provider DLL maps RVA `0x1AC0` to `TestProvider_GetCounter`. The C++ source kept the first Provider instance's `getCounter` function pointer across `FreeLibrary(providerModule)`, then reloaded the DLL and re-resolved only some exports. Later shutdown checks called the stale `getCounter` address. If the loader mapped the new DLL instance at another address, that stale pointer called into unloaded module code. The source now refreshes the entire Provider export set after reload, checks those resolutions, and reconnects the reloaded Provider to the Host Shutdown function.

The initial reproduced fault is a demonstrated stale test-export pointer and an exact match for that crash. The fixed Clang-cl and MSVC binaries passed their serial suite and 25 concurrent pair runs (all 50 CTest processes exited zero); their six Host/DLL hashes stayed stable across those runs. A subsequent rebuild with the complete shared C/C++ layout assertions produced a second Clang-cl cross-DLL crash in a serial run. Direct execution of that exact Host binary also exited with `-1073741819` (`0xC0000005`). WER reports the faulting module as the Host EXE at RVA `0x4DB0`, which `dumpbin /headers` identifies as that build's PE entry-point RVA. A minidump exists outside the repository; it was not analyzed with WinDbg, which is prohibited for this task. Root cause and call stack remain unknown. Earlier Application Error records for other test runs have other Host EXE offsets and likewise remain unattributed.

## Reproduction record

- Source revision before the fix: `a807a515616ac33201d2bde66a9d7477a8ede2d7` plus the two Phase 1T design documents (the test source had not yet changed).
- Separate build roots: `%TEMP%\iif-phase1t-msvc-release-20261009` and `%TEMP%\iif-phase1t-clangcl-release-20261009`; each CTest run passed its own build root's absolute executable/DLL paths. No build process was active during this run. No compiler wrote into the other compiler's directory.
- MSVC command: configure with VS 2022 x64; `cmake --build <msvc-root> --config Release --parallel 4`; `ctest --test-dir <msvc-root> -C Release --output-on-failure` — `2/2 PASS`.
- Clang-cl command: under VS 2022 x64 developer environment, configure `NMake Makefiles` with `clang-cl.exe` for C and C++; `cmake --build <clang-root> --parallel 4`; `ctest --test-dir <clang-root> --output-on-failure` — fixture EXE passed; cross-DLL test `SEGFAULT`.
- The two CTest processes ran concurrently after both builds had completed. MSVC exited `0`; Clang-cl CTest exited nonzero due to the Access Violation. The pre/post SHA-256 values of each compiler's Host EXE, mock Host DLL, and Provider DLL were identical within each build root, excluding concurrent binary overwrite during the test interval.

Equivalent command forms used for the distinct build roots (the actual invocations supplied the full local source/build paths):

```text
cmake -S research/combatbus-v3-offline -B %TEMP%\iif-phase1t-msvc-release-20261009 -G "Visual Studio 17 2022" -A x64
cmake --build %TEMP%\iif-phase1t-msvc-release-20261009 --config Release --parallel 4
ctest --test-dir %TEMP%\iif-phase1t-msvc-release-20261009 -C Release --output-on-failure

cmake -S research/combatbus-v3-offline -B %TEMP%\iif-phase1t-clangcl-release-20261009 -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="C:\Program Files\LLVM\bin\clang-cl.exe" -DCMAKE_CXX_COMPILER="C:\Program Files\LLVM\bin\clang-cl.exe" -DCMAKE_MAKE_PROGRAM=nmake
cmake --build %TEMP%\iif-phase1t-clangcl-release-20261009
ctest --test-dir %TEMP%\iif-phase1t-clangcl-release-20261009 --output-on-failure
```

### Exact pre-fix binary SHA-256

| Build | Artifact | SHA-256 |
|---|---|---|
| MSVC Release | `combatbus_cross_dll_host.exe` | `F67478343C3D37CBC7390F99DE5545A4D02C26EB7D1E1993312BA2C651EBD13F` |
| MSVC Release | `combatbus_iif_mock.dll` | `EF93A7174D8EBDE607A656FA9FC59E051578ADFA3B0FC66165608350B019F700` |
| MSVC Release | `combatbus_test_provider.dll` | `E1B19858D5D2A6D5B378D491AEC513028E047932C575A5D4160449423F0EC022` |
| Clang-cl Release | `combatbus_cross_dll_host.exe` | `612F7D1C93293C83C626F8CC697CD73DD4AAB6D47430089EB8A17D14BA0A2CB5` |
| Clang-cl Release | `combatbus_iif_mock.dll` | `1174BF09C401C53DC38B308B38264EA922855AF996CBFDFC9AB7BF5829ED7821` |
| Clang-cl Release | `combatbus_test_provider.dll` | `E9E4BFBB848CCF4FC24E0DF45CD828D551D5EFD742F45F78D858D3F100E780CD` |

## Evidence boundaries

- Windows Application Error Event ID 1000 identified `combatbus_cross_dll_host.exe`, exception `0xC0000005`, faulting module `combatbus_test_provider.dll_unloaded`, offset `0x1AC0`.
- `dumpbin /exports` on that exact pre-fix Provider DLL reports `TestProvider_GetCounter` at RVA `0x1AC0`.
- `CrossDllHost.cpp` called `FreeLibrary(providerModule)`, loaded the Provider DLL again, re-resolved `getWRF`, `blockWRF`, `waitWRFEntered`, `releaseWRF`, and `resetProvider`, but retained the old `getCounter` pointer. The later Host-Shutdown fixture uses `getCounter`.
- The tests load Host/Provider libraries by the full paths passed from CTest. Build directories and binary outputs do not overlap. This reproduction occurred with compilation complete; concurrent test processes alone were sufficient.
- This investigation did not use a debugger, enable a global dump policy, or change OS-wide settings. Event metadata and the PE export table were sufficient to identify the stale address. No native game process was launched.

## Post-fix verification

- Fresh MSVC x64 Release build and serial CTest: `2/2 PASS`.
- Fresh Clang-cl 21.1.8 x64 Release build and serial CTest: `2/2 PASS`.
- Paired concurrent CTest repetition: 25 rounds; each round ran MSVC and Clang-cl tests simultaneously from their own completed, independent build roots. MSVC `25/25 PASS`; Clang-cl `25/25 PASS`; no binary hashes changed during the run.
- MSVC AddressSanitizer Debug: build and CTest `2/2 PASS`. The first ASan configure/build attempt failed because `/EHsc` was missing while warnings are errors; the retry added `/EHsc` and passed. The linker emitted LNK4300 that `/INCREMENTAL` was ignored for ASan metadata.
- Post-fix Host/DLL SHA-256:

| Build | Artifact | SHA-256 |
|---|---|---|
| MSVC Release | `combatbus_cross_dll_host.exe` | `FF9569B9028B52529E252A4E50B9DF7692065D9084E4ACBEFC24ADAEE8AC8E6A` |
| MSVC Release | `combatbus_iif_mock.dll` | `CB0B89FA0C07FF6448714AC78E1681551DFF930A591BE073369819B4FDBAB506` |
| MSVC Release | `combatbus_test_provider.dll` | `972B4ABDF446378F16F08DEC61B4FF690E682C77ED630E1314F54BA7734C5324` |
| Clang-cl Release | `combatbus_cross_dll_host.exe` | `D3BF8F5D629A9028CDF102A8F0CE5BD5972AC05E6F09B3FF68C42833B184B576` |
| Clang-cl Release | `combatbus_iif_mock.dll` | `8369CE94EB0A2D602C097CD96491405350C1DB95ABD31C372D1F4B609207256D` |
| Clang-cl Release | `combatbus_test_provider.dll` | `C48F811F1C6D68A568D4D122B0AEF626456F96B8E85DCEE461C279983506FE0E` |

ASan artifacts were built and tested from a third independent Debug root; they were not part of the Release concurrency pairs.

### Later unresolved Clang-cl failure

- After adding the shared exhaustive C/C++ layout assertions, the MSVC x64 Release build and tests passed `2/2`. The Clang-cl 21.1.8 build succeeded; the core fixture passed, but the cross-DLL CTest process segfaulted. CTest reported `SEGFAULT`; direct invocation of that exact executable with the exact two DLL paths returned `-1073741819` (`0xC0000005`).
- Windows Application Error Event ID 1000: process `combatbus_cross_dll_host.exe`; exception `0xC0000005`; faulting module `combatbus_cross_dll_host.exe`; offset `0x4DB0`. `dumpbin /headers` reports the same build's AddressOfEntryPoint as RVA `0x4DB0`. This is a different module/offset from the resolved `GetCounter` stale-export fault.
- WER created a local minidump outside the repository, SHA-256 `E1888216F36893060C3FE0ED052B0DCACD3E1B8CBD4E458058F2E3F591B2B6AE`. It was retained as local evidence and not copied into the public PR. No debugger was run and no process-wide dump or OS setting was changed. The available Event 1000 metadata and PE disassembly do not yield the failing call stack.
- The MSVC ASan Debug targets were rebuilt after the layout assertions and passed `2/2`. This does not clear the Clang-cl failure.
- The post-assert Clang-cl EXE/DLL hashes are recorded below. They identify the failing binary set and must not be represented as a passing build.

| Build | Artifact | SHA-256 |
|---|---|---|
| Clang-cl Release (unresolved failure) | `combatbus_cross_dll_host.exe` | `6488351F1E846A940B9BD9381F75036EBA9A96883BAE215C5EDD1BF9A04C183B` |
| Clang-cl Release (unresolved failure) | `combatbus_iif_mock.dll` | `BFD6C8C72ADDAF8550E1581CE22A5D1AD0F438A8614A9424CBEB368B3D16C506` |
| Clang-cl Release (unresolved failure) | `combatbus_test_provider.dll` | `A92BADF6281030DB90668487D405630F1D0DA0B494D394770D7F7F5D4B7E60BC` |

## Phase 1U offline follow-up

### Existing minidump

The exact local dump was found and its SHA-256 matched the recorded value above. It was parsed read-only with the Windows SDK `DbgHelp.dll` `MiniDumpReadDumpStream` API and a local stream reader. No debugger or live process was used, and the dump remains outside the repository.

| Field | Captured value |
|---|---|
| Exception code | `0xC0000005` (access violation) |
| Exception thread | `31388` |
| ExceptionAddress | `0x00007FF6C3FD4DB0` |
| ExceptionContext RIP / RSP | `0x00007FF6C3FD4DB0` / `0x00000047C1B9FF28` |
| Access kind / target | write (`ExceptionInformation[0] = 1`) to `0x00007FF6C3FD4DB0` |
| Runtime Host image base / RVA | `0x00007FF6C3FD0000` / `0x4DB0` |
| Modules in ModuleList | Host EXE, `ntdll.dll`, `kernel32.dll`, `KERNELBASE.dll` (4 entries) |
| Thread stack capture | one thread; stack range begins `0x00000047C1B9EA28`, size `0x15D8` |

The dump's ExceptionStream and AMD64 context agree on RIP and the exception address. The Host image's PE entry-point RVA is `0x4DB0`, but the exact matching on-disk instruction bytes there are `48 83 EC 28` (`sub rsp, 0x28`), followed by a relative call at RVA `0x4DB4`. The first instruction is not a memory store, while the captured access type says a write to the same address as RIP. Therefore the observed instruction and access target do not explain one another. The dump does not include the Host mock or Provider DLL in its module list; this is consistent with a failure before the fixture's Host load, but does not establish why the exception occurred. A raw stack scan contains OS and image addresses but does not produce a reliable unwound call stack. No source-level caller can be attributed.

The exact failed Phase 1T binary set remains available locally and matches the hashes already recorded in the table above. `llvm-readobj` confirmed the EXE's preferred image base `0x140000000`, entry RVA `0x4DB0`, and relocation directory; the dump shows it loaded at `0x00007FF6C3FD0000`. Its PE debug directory is absent (`DebugRVA/DebugSize = 0`), the COFF symbol table is empty, and no matching PDB exists in the saved build output. No mismatched/new PDB or rebuilt binary was used as exact-failure symbols. The exception address is therefore located by the exact EXE hash and RVA, but not symbolized to a source function.

**Root cause remains UNKNOWN.** The Phase 1T entrypoint RVA alone was not treated as evidence of entrypoint corruption. The old fault has not been reproduced with its exact binary set during Phase 1U, and the contradictory access instruction/target prevents a supported attribution to the DLL lifecycle or to the test harness.

### Missing-export guard and regression fixture

`CrossDllHost.cpp` now resolves each required export through a fail-fast helper that prints its exact missing name. It does not call a partially resolved export set. Both the initial provider load and the provider reload check every export before invoking any provider function. On reload failure it shuts down the mock Host, releases the loaded Provider module, releases the Host module, and exits the reload flow. The test-only Provider variant omits `TestProvider_GetCallbackShutdownStatus`; the negative fixture first runs the normal lifecycle, unloads the first Provider, then reloads that variant and verifies controlled rejection with that exact export name. The live output was:

```text
ERROR: missing required export 'TestProvider_GetCallbackShutdownStatus'
PASS: controlled reload rejection for missing export 'TestProvider_GetCallbackShutdownStatus'; no unresolved function pointer was called
```

The host's QueryInterface/Shutdown exports and test-hook exports are also checked individually before use. Explicit HMODULE path checks confirm that the Host and each initial/reloaded Provider handle refer to the exact requested DLL path. Review of every stored Provider export use found no call through the unloaded instance: the full required Provider table is rebound after reload, and a failed rebind exits before any Provider call.

### Phase 1U verification

Builds used distinct roots under the user's local temporary directory; each build completed before its tests began. CTest used absolute target paths. MSVC and Clang-cl serial suites each passed `3/3`, including the new negative fixture. Each compiler's three-test suite then passed 25 serial repetitions (`75/75` test executions); the recorded EXE and DLL hashes were identical before and after each repetition series. The seven mixed compiler combinations (MSVC/Clang-cl Host EXE × Host DLL × Provider DLL, excluding the all-MSVC baseline) passed `7/7` with explicit DLL path assertions. MSVC AddressSanitizer Debug passed `3/3`; the linker emitted the known LNK4300 warning that `/INCREMENTAL` is ignored for ASan metadata.

Release EXE and DLL SHA-256 values for this Phase 1U source:

| Toolchain | Artifact | SHA-256 |
|---|---|---|
| MSVC 19.44.35227 | Host EXE | `905DEC620C1C551C89D5474565AA35FBE97CE88AF3CBF1AF7AEF8058DB2F3B3E` |
| MSVC 19.44.35227 | Host mock DLL | `63603A4B3B6770A2E5B319639D5A1D2AE794D51925578B17EDAB8D1CDF9CE604` |
| MSVC 19.44.35227 | Full Provider DLL | `41C13C103EECA2924EE1A97B643EB0DBAEDB34E6C7D228935F637446A958636B` |
| MSVC 19.44.35227 | Missing-export Provider DLL | `3F4DB173BA5BA883CFA5D220B6F58FFE6F74563DA4E05743FAEE41FE81A6AC31` |
| Clang-cl 21.1.8 | Host EXE | `BFAA481B6C47FFFCACBC336DE015B64C254E54E20DCED1C2295D639748E9586B` |
| Clang-cl 21.1.8 | Host mock DLL | `73BBEDDDF8A8B0C08F0D589BB229CE0BFA8438F52C2FE87B6F9C365F400E9777` |
| Clang-cl 21.1.8 | Full Provider DLL | `2E1646D587488850A389127AC4394FFDBE5302C90E5A51F92B765170739204C6` |
| Clang-cl 21.1.8 | Missing-export Provider DLL | `51CAFAFA1112A5E02068560E968213BA4E62F64026EB41BF50A40A824923F402` |

The separate MSVC AddressSanitizer Debug run used these cross-DLL artifacts:

| Artifact | SHA-256 |
|---|---|
| Host EXE | `8D6ADC3DDA39CC02C438CC3EAAA6D17E9519E7C914F0310CCF01491EFB9D2755` |
| Host mock DLL | `7F2A5C0BE6E77A767FECF2914F5881B511BF1E2C7C6E583E82ED2F7E0EF80ED7` |
| Full Provider DLL | `A6CE23CE6BCE8883A7E3EC6F265C4E6B24D4E741B8AC2726BD81B06AAFB5B32B` |
| Missing-export Provider DLL | `10ADFC28AC02DB21BE3E63988B189D288CCFB74080C4E47443947EDE14E0FA28` |

Commands (source and build directories were the isolated clone and distinct local temporary roots named above):

```text
cmake -S research/combatbus-v3-offline -B %TEMP%\iif-phase1u-msvc-release-20261009 -G "Visual Studio 17 2022" -A x64
cmake --build %TEMP%\iif-phase1u-msvc-release-20261009 --config Release --parallel 4
ctest --test-dir %TEMP%\iif-phase1u-msvc-release-20261009 -C Release --output-on-failure -j1
ctest --test-dir %TEMP%\iif-phase1u-msvc-release-20261009 -C Release --output-on-failure --repeat until-fail:25 -j1

call VsDevCmd.bat -arch=x64 -host_arch=x64
cmake -S research/combatbus-v3-offline -B %TEMP%\iif-phase1u-clangcl-release-20261009 -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="C:\Program Files\LLVM\bin\clang-cl.exe" -DCMAKE_CXX_COMPILER="C:\Program Files\LLVM\bin\clang-cl.exe" -DCMAKE_MAKE_PROGRAM=nmake
cmake --build %TEMP%\iif-phase1u-clangcl-release-20261009
ctest --test-dir %TEMP%\iif-phase1u-clangcl-release-20261009 --output-on-failure -j1
ctest --test-dir %TEMP%\iif-phase1u-clangcl-release-20261009 --output-on-failure --repeat until-fail:25 -j1

cmake -S research/combatbus-v3-offline -B %TEMP%\iif-phase1u-msvc-asan-20261009 -G "Visual Studio 17 2022" -A x64 -DCMAKE_C_FLAGS=/fsanitize=address -DCMAKE_CXX_FLAGS=/fsanitize=address -DCMAKE_CXX_FLAGS_DEBUG=/EHsc -DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO -DCMAKE_SHARED_LINKER_FLAGS=/INCREMENTAL:NO
cmake --build %TEMP%\iif-phase1u-msvc-asan-20261009 --config Debug --parallel 4
ctest --test-dir %TEMP%\iif-phase1u-msvc-asan-20261009 -C Debug --output-on-failure -j1
```

The mixed matrix directly ran each compiled Host EXE with absolute Host DLL and Provider DLL paths from the MSVC/Clang-cl Release roots. The all-MSVC combination is included in the two ordinary same-toolchain runs; the remaining seven combinations passed. Each invocation exited `0`.

The historical Clang-cl failure is **NOT REPRODUCED** in the Phase 1U rebuilt test set, not resolved. The observed serial/repeat/mixed/ASan passes do not explain the old dump. ABI freeze remains not ready pending root-cause closure and independent review; production hooks remain not approved.

## Post-fix acceptance gate

Rebuild both compilers into fresh, disjoint directories; record fresh EXE/DLL SHA-256; run each CTest serially; then run the already-built Clang-cl and MSVC CTest processes concurrently in their own directories. Repeat the parallel cross-DLL run at least 25 times with deterministic output capture. Confirm that each Host process resolves Provider exports only from the currently loaded `HMODULE`, that all Provider callback entries are quiescent before `FreeLibrary`, and that no new Application Error event occurs for these Host executables. A pass after fixing the stale pointer does not establish native Hook safety or prove all prior failures shared this root cause.

## Phase 1V startup-only and stability follow-up

### Historical startup-boundary review

The exact old dump and failed Phase 1T EXE remain the evidence set described above. The old executable PE entry RVA is 0x4DB0 and the captured RIP equals ExceptionAddress there, but the exact matching instruction is sub rsp, 0x28, inconsistent with the reported write access to the same address. The dump lists the Host EXE and OS modules but not the simulated IIF Host DLL or Provider DLL. This is consistent with failure before those test DLLs loaded; it does not prove the process had not entered wmain or identify a cause. Its one captured thread stack does not permit a reliable unwind. Root cause remains UNKNOWN. No antivirus, compiler, injection, or loader attribution is supported. No mismatched PDB or new build symbols were used, and this review did not expand into broad reverse engineering.

### Current startup probes and markers

A minimal startup_probe.exe performs no DLL load or CombatBus call. Both MSVC and Clang-cl runs recorded probe.wmain.entered and probe.before_return, then exited normally. Successful full Host runs recorded, in order: host.wmain.entered; host.before_host_dll_load; host.host_dll_loaded; host.before_provider_dll_load; host.provider_dll_loaded; host.before_provider_reload; host.provider_reloaded; host.shutdown_drained; host.modules_unloaded; host.test_finished; host.before_normal_exit. These markers establish progress only for current runs and cannot locate the historical failure.

### Harness fail-fast review

CrossDllHost.cpp aborts on incomplete QueryInterface tables, wrong loaded module paths, missing required exports, failed Provider descriptor creation, unusable registration handles, failed unregister, failed/injected quiescence waits, and failed Host Shutdown. Provider exports are rebound and validated after reload before use. Failed Provider quiescence does not reach Provider FreeLibrary; failed Host Shutdown keeps modules loaded. The missing-export reload fixture reports the exact missing export and performs controlled cleanup. Barrier creation/entry failure paths release and join the callback before cleanup. The test does not unload an executing callback.

### Phase 1V builds and tests

Compilation used xmake in disjoint output directories under workspace scratch. CMake only configured seven CTest entries to run already-built xmake outputs by absolute path. All builds completed before tests ran.

- MSVC 19.44.35227 x64 Release: build PASS; serial CTest 7/7 PASS.
- Clang-cl 21.1.8 x64 Release using the MSVC linker: build PASS; serial CTest 7/7 PASS.
- MSVC x64 AddressSanitizer Debug: build PASS; serial CTest 7/7 PASS. The runtime directory was added only to the test process PATH. The initial missing-runtime test invocation was setup failure, not a pass.
- Paired parallel test: 25 rounds; each ran MSVC and Clang-cl CTest from disjoint completed build/test roots. Both passed 25/25; all exit codes were zero and hashes were unchanged.
- Mixed Host EXE × Host DLL × Provider DLL: all eight compiler combinations passed, with absolute DLL paths and module-path validation.
- Startup probes and full Host phase markers passed for both Release compilers.

A first MSVC ASan xmake build failed with C1041 because parallel cl.exe processes wrote the same PDB; rebuilding with /FS passed. Earlier CMake build attempts emitted Visual Studio generator path warnings and were superseded by xmake. These setup failures are recorded and are not counted as passes.

Representative artifact SHA-256 values follow. All test EXEs/DLLs were hashed before and after the 25 paired runs; none changed.

| Configuration | Artifact | SHA-256 |
|---|---|---|
| MSVC Release | Host EXE | 04608E7FF381F5F5852DFDB14A849A5F06A663BB301447AA4351BDE5AD5DBF2A |
| MSVC Release | Mock Host DLL | AB834F72F0863864894636DFCE5FD76CAF846AE2D8AB6E131344BC528C540290 |
| MSVC Release | Provider DLL | 522102C518D624B7AF3A593BC96438D191F9B1AEB1C17F0F896DC3F0122B78FC |
| MSVC Release | Missing-export Provider DLL | 1EB452A59770746691C270FE1B1918BE13B65C23E087B9CA1380D1AFA5FD30B1 |
| MSVC Release | Fixture EXE | 937328F9C0935E17F053CC398EFC12ABE259D27FCAF72C9E929EDA58EC63E3EA |
| MSVC Release | Startup probe | BB1DA472185FE4C171A2DBA784FB8DDBA58BFBA70B2A0267F5B5F1AFFAA14B93 |
| Clang-cl Release | Host EXE | 81215B420350D4CC7E62D6EEA733D29D7AE61F0DF863009E52AF2312CC13AF7B |
| Clang-cl Release | Mock Host DLL | DE7AF76110B9E756BF251C167E7D7911BD98335363C1BE35A31775AB06554380 |
| Clang-cl Release | Provider DLL | 90FC66BF5527585051D4E0E91BA69CB71209D6761E7FCA81E10AE1C5137E75C6 |
| Clang-cl Release | Missing-export Provider DLL | 5FEF24598875110141254F2AC6A4E2B91608D00D2A16F093889F3696D79D0D6F |
| Clang-cl Release | Fixture EXE | A73E26555D5F8AAC35CEA3CCA4C13DA908FAEF9C4C54CFCB24CA0B679159222B |
| Clang-cl Release | Startup probe | 5CF69ADBA5F059F0421A9BAFE8F911AFDC6CAFAABE79700A688D3769915662DA |
| MSVC ASan Debug | Host EXE | 5667341C37C0745B80A04C2BABA5F54E02BCCC9FE945043209AB24EADF3958A4 |
| MSVC ASan Debug | Mock Host DLL | D984A3C4AE4E95143F7B1ACB35ABC63E690389BA8FBB103DE6B1FD7E303C451E |
| MSVC ASan Debug | Provider DLL | 9DA7B889D4B170A1BA5B1D2B9BCDC86C719BB0941FD7435AFB417385BF2D6A1D |
| MSVC ASan Debug | Missing-export Provider DLL | E77BBB9F1EBA1ED2BC9DAE5C70455FD6E3EA40869156558127A155C718B11333 |
| MSVC ASan Debug | Fixture EXE | E375E05EAD15FAEE088E0552C21A76D941714523323368F1ABAE4B9242266218 |
| MSVC ASan Debug | Startup probe | 21C5E33BBC9A6FDE31C3F67FF3DF0F42DB96903EA3B7071E5332D36005FA772A |

These are local fixture results, not GitHub Actions or game-runtime validation. The historical Clang-cl exception is NOT REPRODUCED, not resolved. Current fixture stability does not prove every ABI, loader, or production-hook condition.
