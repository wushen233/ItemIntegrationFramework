# Proposed Windows CI for CombatBus V3 Offline Prototype

Status: proposal only. No workflow has been added and no GitHub CI result is claimed.

## Scope and matrix

Run only research/combatbus-v3-offline on windows-latest. Do not build an F4SE plugin, access Fallout 4 binaries, install hooks, or publish game artifacts.

| Job | Toolchain | Required checks |
|---|---|---|
| msvc-release | Visual Studio x64 MSVC | C11/C++20 ABI layout, single-process fixtures, cross-DLL Host/Provider lifecycle, missing-export negative case, startup probe |
| clangcl-release | Visual Studio developer environment plus Clang-cl x64 | Same checks, with separate output paths |

Each job uses an independent build and test directory. Build every executable and DLL before CTest. Run CTest serially with output-on-failure. Print compiler versions and SHA-256 for the Host executable, mock Host DLL, both Provider DLLs, fixture executable, and startup probe. The shared header layout assertions are compiled from both C11 and C++20 translation units. Cross-DLL tests use absolute paths and verify loaded module paths.

## Failure evidence

On failure, retain CTest output and exit code, compiler version/configuration, the artifact hash manifest, and startup marker text. Keep failing EXE/DLL artifacts as a private, short-lived workflow artifact, for example seven days. Do not upload minidumps, PDBs, local event logs, user paths, Fallout 4 binaries, or unrestricted build trees. Preserve a failing binary set even if a later run passes.

## Limits and approval

CI can validate the candidate ABI and mock lifecycle on the tested Windows toolchains. It cannot prove Fallout 4 native call-site semantics, hook safety, game-thread behavior, or WRF/CSF/PAS behavior. Creating a workflow under .github/workflows requires separate authorization and is outside Phase 1V.
