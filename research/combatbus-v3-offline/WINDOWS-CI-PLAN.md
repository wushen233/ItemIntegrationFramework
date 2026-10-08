# Windows GitHub Actions CI for the CombatBus V3 Offline Prototype

The active workflow is .github/workflows/combatbus-v3-offline.yml. It runs on pull requests targeting main and pushes to the dedicated review branch, only when this workflow or research/combatbus-v3-offline changes. The workflow has read-only contents permission, a pinned actions/checkout v4.2.2 commit, independent MSVC and Clang-cl jobs, and no binary artifact upload.

Each Windows Server 2022 job uses the existing CMake project and builds the full x64 Release target set before running the complete CTest suite serially. The C source ABI smoke test and C++20 fixtures compile in both jobs. CTest covers the single-process fixture, Host/IIF mock/Provider lifecycle, controlled missing-export rejection, and startup probe. The verification step checks the expected marker progression, JUnit's seven tests with zero failures, and prints SHA-256 for each EXE/DLL. Native command failures propagate as job failures; the matrix uses fail-fast false only so one failed compiler job does not cancel evidence from the other.

The workflow records CMake/CTest, MSVC, linker, and Clang-cl versions in Actions logs. It does not upload EXE, DLL, PDB, minidump, event-log, build-cache, or game files. No user-specific build or source path is embedded in the workflow. GitHub's public Actions logs are the evidence; retention artifacts are not used.

This CI validates only the offline ABI and mock lifecycle on the two runner toolchains. It does not prove Fallout 4 native callsite semantics, production hook safety, runtime threading, or game behavior. Historical Phase 1T Clang-cl access violation remains UNKNOWN / NOT REPRODUCED. Production ABI is not frozen and Production Hook remains NOT APPROVED.

The workflow uses the Visual Studio 2022 CMake generator. Its Clang-cl job requires the Visual Studio LLVM/Clang toolset available on the Windows 2022 hosted image. The workflow checks compiler selection from CMakeCache and fails if the Clang-cl job accidentally configures another compiler.
