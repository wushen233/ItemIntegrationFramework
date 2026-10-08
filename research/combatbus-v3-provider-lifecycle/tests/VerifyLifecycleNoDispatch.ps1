param(
	[Parameter(Mandatory = $true)][string]$RepositoryRoot,
	[Parameter(Mandatory = $true)][string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$smoke = Get-Content -LiteralPath (Join-Path $project 'src/ProviderLifecycleSmoke.cpp') -Raw
$scenario = Get-Content -LiteralPath (Join-Path $project 'src/ProviderLifecycleScenario.cpp') -Raw
$hostTest = Get-Content -LiteralPath (Join-Path $project 'tests/OfflineLifecycleHost.cpp') -Raw
$allSource = $smoke + "`n" + $scenario + "`n" + $hostTest

foreach ($required in @(
	'F4SE::MessagingInterface::kPostLoad',
	'GetModuleHandleW',
	'IIF_CombatBus_QueryInterface',
	'init\.hook\s*=\s*false',
	'init\.trampoline\s*=\s*false',
	'RunProviderLifecycleScenario',
	'IIF_CB_STATUS_DUPLICATE',
	'IIF_CB_STATUS_NOT_FOUND',
	'Phase 2D-A PASS')) {
	if ($allSource -notmatch $required) { throw "Required lifecycle-safety source marker missing: $required" }
}

foreach ($forbidden in @(
	'\.dispatch_outgoing\s*\(',
	'\.dispatch_incoming\s*\(',
	'\bFreeLibrary\s*\(',
	'\bDllMain\b',
	'\bMH_CreateHook\b',
	'\bMH_EnableHook\b',
	'\bDetourAttach\b',
	'\bwrite_vfunc\b',
	'RE::Actor',
	'RE::TESObjectWEAP')) {
	if ($allSource -match $forbidden) { throw "Forbidden Dispatch/Hook/unload/game-object code found: $forbidden" }
}

$main = Get-Content -LiteralPath (Join-Path $root 'src/main.cpp') -Raw
if ($main -match 'IIF_CombatBus_QueryInterface|DispatchOutgoing|DispatchIncoming') {
	throw 'Production main.cpp unexpectedly contains a V3 dispatch adapter/callsite.'
}

Write-Output 'PASS: lifecycle probe contains no Dispatch call, native Hook, FreeLibrary, or game-object access.'
