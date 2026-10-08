param(
	[Parameter(Mandatory = $true)]
	[string]$RepositoryRoot
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$iifXmakePath = Join-Path $root 'xmake.lua'
$diagnosticRoot = Join-Path $root 'research/combatbus-v3-provider-lifecycle'
$diagnosticXmakePath = Join-Path $diagnosticRoot 'xmake.lua'
$iifTemplatePath = Join-Path $root 'res/commonlibf4-plugin.cpp.in'
$diagnosticTemplatePath = Join-Path $diagnosticRoot 'res/commonlibf4-plugin.cpp.in'

foreach ($path in @($iifXmakePath, $diagnosticXmakePath, $iifTemplatePath, $diagnosticTemplatePath)) {
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Required F4SE version template input is missing: $path"
	}
}

$iifXmake = Get-Content -LiteralPath $iifXmakePath -Raw
$diagnosticXmake = Get-Content -LiteralPath $diagnosticXmakePath -Raw
$iifTemplate = Get-Content -LiteralPath $iifTemplatePath -Raw
$diagnosticTemplate = Get-Content -LiteralPath $diagnosticTemplatePath -Raw

if ($iifXmake -notmatch 'plugin_template\s*=\s*path\.join\(os\.scriptdir\(\),\s*"res/commonlibf4-plugin\.cpp\.in"\)') {
	throw 'The ItemIntegrationFramework target is not wired to its project-owned F4SE version template.'
}
if ($diagnosticXmake -notmatch 'plugin_template\s*=\s*path\.join\(os\.scriptdir\(\),\s*"res/commonlibf4-plugin\.cpp\.in"\)') {
	throw 'The Provider lifecycle target is not wired to its project-owned F4SE version template.'
}

function Get-CompatibleRuntimeDeclaration([string]$Text, [string]$Label) {
	$matches = [regex]::Matches($Text, '(?s)v\.CompatibleVersions\s*\(\s*\{(?<items>.*?)\}\s*\)')
	if ($matches.Count -ne 1) {
		throw "$Label must contain exactly one CompatibleVersions declaration; found $($matches.Count)."
	}
	$items = [regex]::Matches($matches[0].Groups['items'].Value, 'F4SE::(RUNTIME_[A-Z0-9_]+)')
	if ($items.Count -eq 0) {
		throw "$Label has no explicit F4SE runtime entries."
	}
	return @($items | ForEach-Object { $_.Groups[1].Value })
}

$iifRuntimes = @(Get-CompatibleRuntimeDeclaration $iifTemplate 'IIF template')
$diagnosticRuntimes = @(Get-CompatibleRuntimeDeclaration $diagnosticTemplate 'Provider lifecycle template')
$expectedIif = @('RUNTIME_1_10_163', 'RUNTIME_1_11_240')
$expectedDiagnostic = @('RUNTIME_1_10_163')

if (($iifRuntimes -join ',') -cne ($expectedIif -join ',')) {
	throw "IIF compatibility list mismatch. Expected [$($expectedIif -join ', ')], got [$($iifRuntimes -join ', ')]"
}
if (($diagnosticRuntimes -join ',') -cne ($expectedDiagnostic -join ',')) {
	throw "Diagnostic compatibility list mismatch. Expected [$($expectedDiagnostic -join ', ')], got [$($diagnosticRuntimes -join ', ')]"
}
if ($iifTemplate -match 'F4SE::RUNTIME_LATEST' -or $diagnosticTemplate -match 'F4SE::RUNTIME_LATEST') {
	throw 'A project template reverted to the CommonLibF4 RUNTIME_LATEST default.'
}

Write-Output 'F4SE version template static verification PASS'
Write-Output "IIF CompatibleVersions: $($iifRuntimes -join ', ')"
Write-Output "Provider lifecycle CompatibleVersions: $($diagnosticRuntimes -join ', ')"
