param(
	[Parameter(Mandatory = $true)]
	[string]$ProjectRoot
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$source = Get-Content -LiteralPath (Join-Path $root 'src/RuntimeSmoke.cpp') -Raw
$xmake = Get-Content -LiteralPath (Join-Path $root 'xmake.lua') -Raw

$guardPattern = '(?s)#if defined\(IIF_CB_RUNTIME_SMOKE_OUTGOING_IDENTITY\)(.*?)#else(.*?)#endif'
$guardMatches = [regex]::Matches($source, $guardPattern)
if ($guardMatches.Count -ne 1) {
	throw 'Expected exactly one complete opt-in compile guard for synthetic Outgoing code.'
}
$guardMatch = [regex]::Match($source, $guardPattern)
if (-not $guardMatch.Success) {
	throw 'Synthetic Outgoing code is not protected by the opt-in compile guard.'
}
$outgoingBlock = $guardMatch.Groups[1].Value
$defaultBlock = $guardMatch.Groups[2].Value

$dispatchCalls = [regex]::Matches($source, 'api\.dispatch_outgoing\s*\(')
if ($dispatchCalls.Count -ne 1 -or $outgoingBlock -notmatch 'api\.dispatch_outgoing\s*\(') {
	throw 'Outgoing Dispatch call is missing or appears outside the opt-in block.'
}
if ($defaultBlock -match 'api\.dispatch_outgoing\s*\(|outgoingSentinel') {
	throw 'The default query-only branch contains synthetic Outgoing dispatch or pointers.'
}
foreach ($required in @('outgoing.attacker = &outgoingSentinel', 'outgoing.weapon = &outgoingSentinel')) {
	if ($outgoingBlock -notmatch [regex]::Escape($required)) {
		throw "Opt-in Outgoing sentinel assignment missing from guarded block: $required"
	}
}
if ($xmake -notmatch '(?s)option\("outgoing_identity_smoke"\).*?set_default\(false\)') {
	throw 'The optional Outgoing xmake option must default to false.'
}
if ($xmake -notmatch '(?s)if has_config\("outgoing_identity_smoke"\).*?add_defines\("IIF_CB_RUNTIME_SMOKE_OUTGOING_IDENTITY"\).*?set_basename\("IIFCombatBusRuntimeSmoke_OutgoingOptIn"\)') {
	throw 'The opt-in must define the guarded code and use a distinct DLL basename.'
}
if ($source -match 'confidence\s*=\s*IIF_CB_CONFIDENCE_VERIFIED_ADAPTER_CALLSITE') {
	throw 'The diagnostic plugin must not forge VerifiedAdapterCallsite confidence.'
}

Write-Output 'PASS: default build is query-only; synthetic Outgoing Dispatch exists only behind the opt-in compile flag.'
