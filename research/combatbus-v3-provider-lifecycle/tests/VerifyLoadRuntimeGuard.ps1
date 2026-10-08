param(
	[Parameter(Mandatory = $true)]
	[string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$source = Get-Content -LiteralPath (Join-Path $project 'src/ProviderLifecycleSmoke.cpp') -Raw

$loadPrefix = [regex]::Match(
	$source,
	'(?s)F4SEPlugin_Load\s*\(\s*const\s+F4SE::LoadInterface\s*\*\s*f4se\s*\)\s*\{(?<beforeInit>.*?)F4SE::Init\s*\(\s*f4se')
if (-not $loadPrefix.Success) {
	throw 'F4SEPlugin_Load must contain the runtime guard before F4SE::Init(f4se).'
}

$expectedPrefix = '^\s*if\s*\(\s*!f4se\s*\|\|\s*f4se->RuntimeVersion\(\)\s*!=\s*F4SE::RUNTIME_1_10_163\s*\)\s*\{\s*return\s+false\s*;\s*\}'
if ($loadPrefix.Groups['beforeInit'].Value -notmatch $expectedPrefix) {
	throw 'F4SEPlugin_Load must reject null or non-OG runtime before any initialization or callback setup.'
}
if ($loadPrefix.Groups['beforeInit'].Value -match 'RegisterListener|GetModuleHandle|IIF_CombatBus_QueryInterface|RunProviderLifecycleScenario') {
	throw 'F4SEPlugin_Load performs host interaction or registers callbacks before its runtime guard.'
}

if ($source -notmatch '(?s)F4SEPlugin_Query\s*\([^)]*\)\s*\{\s*if\s*\(\s*!f4se\s*\|\|\s*!info\s*\|\|\s*f4se->IsEditor\(\)\s*\|\|\s*f4se->RuntimeVersion\(\)\s*!=\s*F4SE::RUNTIME_1_10_163') {
	throw 'The existing OG-only F4SEPlugin_Query guard was removed or weakened.'
}

foreach ($required in @(
	'IIF_CombatBus_QueryInterface',
	'RunProviderLifecycleScenario\(api')) {
	if ($source -notmatch $required) {
		throw "Existing QueryInterface/lifecycle probe marker missing: $required"
	}
}

Write-Output 'PASS: F4SEPlugin_Load rejects non-OG runtime before F4SE initialization; OG continues to initialization.'
