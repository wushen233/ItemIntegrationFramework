param(
	[Parameter(Mandatory = $true)]
	[string]$RepositoryRoot
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$mainPath = Join-Path $root 'src/main.cpp'
$apiPath = Join-Path $root 'src/IIF_API.h'
$v3Path = Join-Path $root 'src/CombatBusV3'
$main = Get-Content -LiteralPath $mainPath -Raw
$api = Get-Content -LiteralPath $apiPath -Raw
$v3Sources = Get-ChildItem -LiteralPath $v3Path -File -Recurse |
	Where-Object { $_.Extension -in '.cpp', '.h' } |
	ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }
$v3Text = $v3Sources -join "`n"

if ($main -notmatch 'IIF::CombatBus::Install\(\);') {
	throw 'Legacy CombatBus installation call was removed or renamed.'
}
foreach ($token in @('RegisterDamageModifier', 'RegisterArmorModifier', 'HandleEntryPoint_Hook',
		'HasPerkEntries_Hook', 'MH_EnableHook(MH_ALL_HOOKS)')) {
	if ($main -notmatch [regex]::Escape($token)) { throw "Legacy behavior token missing: $token" }
}
foreach ($token in @('RegisterCPPCard', 'RegisterDamageModifier', 'RegisterArmorModifier',
		'kMessage_ExchangeInterface')) {
	if ($api -notmatch [regex]::Escape($token)) { throw "Legacy IIF API token missing: $token" }
}
foreach ($token in @('IIF_CombatBus_QueryInterface', 'dispatch_outgoing', 'dispatch_incoming',
		'DispatchOutgoing', 'DispatchIncoming')) {
	if ($main -match [regex]::Escape($token)) { throw "V3 path is connected to main.cpp: $token" }
}
foreach ($pattern in @('MH_CreateHook', 'MH_EnableHook', 'write_vfunc', 'F4SEPlugin_',
		'RE::Actor', 'REL::Relocation', 'InstallNativeAdapter')) {
	if ($v3Text -match $pattern) { throw "V3 production source contains a native hook or dispatch callsite: $pattern" }
}

Write-Output 'PASS: V3 has no native hook/dispatch callsite; legacy path and IIF API remain present.'
