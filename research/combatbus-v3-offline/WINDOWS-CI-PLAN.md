# Windows GitHub Actions CI for the CombatBus V3 Offline Prototype

The active workflow is `.github/workflows/combatbus-v3-offline.yml`. It runs on pull requests targeting `main` only when this workflow or `research/combatbus-v3-offline/` changes. It has read-only contents permission, pins both `actions/checkout` and the xmake setup Action by commit, selects xmake 3.0.2, runs independent MSVC and Clang-cl jobs, and uploads no binary artifacts.

Each Windows Server 2022 job builds the full x64 Release target set with the repository's existing `xmake.lua`. MSVC uses the `msvc` toolchain; Clang-cl uses `clang-cl` with Visual Studio's linker. `CMakeLists.txt` is used only to configure the existing CTest manifest to run the xmake-built outputs; CMake does not compile the targets. The C source ABI smoke test and C++20 fixtures compile in both jobs. CTest covers the single-process fixture, Host/IIF mock/Provider lifecycle, controlled missing-export rejection, and startup probe. The verification step checks the expected marker progression, JUnit's seven tests with zero failures, and prints SHA-256 for each EXE/DLL. Native command failures propagate as job failures; the matrix uses fail-fast false only so one failed compiler job does not cancel evidence from the other.

The workflow records xmake, CMake/CTest, MSVC, linker, and Clang-cl versions in Actions logs. It does not upload EXE, DLL, PDB, minidump, event-log, build-cache, or game files. No user-specific build or source path is embedded in the workflow. GitHub's public Actions logs are the evidence; retention artifacts are not used.

This CI validates only the offline ABI and mock lifecycle on the two runner toolchains. It does not prove Fallout 4 native callsite semantics, production hook safety, runtime threading, or game behavior. Historical Phase 1T Clang-cl access violation remains UNKNOWN / NOT REPRODUCED. Production ABI is not frozen and Production Hook remains NOT APPROVED.

The workflow uses the pinned xmake setup Action and xmake 3.0.2. The Clang-cl job requires the Visual Studio LLVM/Clang toolset available on the Windows 2022 hosted image. It checks the selected compiler from xmake's target report and fails if Clang-cl was not selected.

GitHub Actions results are attached to the pull request's exact tested head. Earlier setup-only workflow failures are not test passes; each was corrected before accepting a CI result. Historical Phase 1T crash evidence remains unchanged and the root cause remains UNKNOWN / NOT REPRODUCED.
