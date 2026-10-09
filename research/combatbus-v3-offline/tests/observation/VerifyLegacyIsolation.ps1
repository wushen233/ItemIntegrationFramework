param(
	[Parameter(Mandatory = $true)][string]$RepositoryRoot,
	[Parameter(Mandatory = $true)][string]$ContractPath
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$contract = Get-Content -LiteralPath $ContractPath -Raw | ConvertFrom-Json
$mainPath = Join-Path $root 'src/main.cpp'
$apiPath = Join-Path $root 'src/IIF_API.h'
$main = Get-Content -LiteralPath $mainPath -Raw
$api = Get-Content -LiteralPath $apiPath -Raw

function Get-Sha256([string]$Path) {
	$sha = [System.Security.Cryptography.SHA256]::Create()
	$stream = [System.IO.File]::OpenRead($Path)
	try {
		return ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
	}
	finally {
		$stream.Dispose()
		$sha.Dispose()
	}
}

$mainHash = Get-Sha256 $mainPath
$apiHash = Get-Sha256 $apiPath
if ($mainHash -ne $contract.productionBaseline.'main.cpp.sha256') {
	throw "Production source main.cpp differs from the pinned review baseline: $mainHash"
}
if ($apiHash -ne $contract.productionBaseline.'IIF_API.h.sha256') {
	throw "IIF_API.h differs from the pinned review baseline: $apiHash"
}

foreach ($anchor in @(
	'IIF::CombatBus::Install();',
	'HandleEntryPoint_Hook',
	'HasPerkEntries_Hook',
	'write_vfunc(0x10B',
	'MH_CreateHook',
	'MH_EnableHook(MH_ALL_HOOKS)',
	'g_damageModifiers',
	'g_armorModifiers'
)) {
	if ($main -notmatch [regex]::Escape($anchor)) { throw "Expected legacy source anchor missing: $anchor" }
	$normalizedExclusions = @($contract.excludedLegacyBehavior | ForEach-Object { $_.Trim().TrimEnd(';') })
	if ($normalizedExclusions -notcontains $anchor.TrimEnd(';')) { throw "Observation contract does not exclude: $anchor" }
}

foreach ($anchor in @('RegisterCPPCard', 'RegisterDamageModifier', 'RegisterArmorModifier', 'kMessage_ExchangeInterface')) {
	if ($api -notmatch [regex]::Escape($anchor)) { throw "Legacy IIF interface token missing: $anchor" }
}
if ($contract.legacyApiTestMode.IIF_InterfaceLayoutChanged -ne $false -or
	$contract.legacyApiTestMode.registerCallbackSignatureReturnsStatus -ne $false) {
	throw 'The test-only observation contract must not alter the public IIF API layout or callback signature.'
}
if (-not $contract.moduleIdentityRules.productionAndObservationDllsMutuallyExclusive -or
	-not $contract.moduleIdentityRules.loadedModulePathMustMatchManifest -or
	-not $contract.moduleIdentityRules.sha256Required) {
	throw 'Module exclusion and identity rules are incomplete.'
}

Write-Output 'PASS: production source remains pinned; observation build exclusions, API boundary, and module identity rules are explicit.'
