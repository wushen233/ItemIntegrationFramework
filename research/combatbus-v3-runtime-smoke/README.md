# IIF CombatBus V3 Runtime Smoke Probe

This is a temporary, test-only F4SE plugin for Fallout 4 OG 1.10.163. It is separate from the production IIF target and is not deployed by the build.

At F4SE `kPostLoad`, it looks up the already-loaded `ItemIntegrationFramework.dll`, resolves `IIF_CombatBus_QueryInterface`, checks V3 success plus unsupported-version and structure-size rejection, and records an Outgoing no-provider identity probe using opaque local sentinels. It never registers a Provider. It logs through CommonLibF4 as `IIFCombatBusRuntimeSmoke.log`.

The Incoming check intentionally passes `CONFIDENCE_UNKNOWN` and verifies fail-closed rejection with the value unchanged. A successful Incoming identity dispatch requires `VERIFIED_ADAPTER_CALLSITE`; this smoke plugin has no native adapter and must not manufacture that evidence. Incoming identity remains covered by offline Registry fixtures, not by this in-game probe.

Before running the Outgoing identity probe, use a test profile with no experimental V3 Provider plugins loaded. The probe requires an empty V3 Outgoing Registry. Existing legacy IIF consumers can remain enabled if they do not register V3 Providers. If that precondition is uncertain, do not run the probe; query/version checks alone are safe.

The plugin has no hooks, threads, timers, game tasks, or unload callbacks. It registers one F4SE message listener, runs once at `kPostLoad`, logs, and remains resident for process lifetime like the F4SE plugin host. It uses the already-required CommonLibF4 build dependency and adds no runtime library dependency beyond F4SE/CommonLib conventions.

Build from this directory with the selected CommonLibF4 profile. Outputs remain in this research checkout's ignored `build/` and `.xmake/` directories. The resulting DLL is a review artifact only; do not stage or deploy it until the test plan and binary are reviewed.
