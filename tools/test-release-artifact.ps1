[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$Installer
)

$ErrorActionPreference = 'Stop'

$path = (Resolve-Path -LiteralPath $Installer).Path
$sidecar = $path + '.sha256'
if(-not (Test-Path -LiteralPath $sidecar -PathType Leaf)) {
    throw "Missing SHA256 sidecar: $sidecar"
}

$line = (Get-Content -LiteralPath $sidecar -Raw).Trim()
$name = [IO.Path]::GetFileName($path)
$pattern = '^([0-9A-Fa-f]{64})\s+' + [regex]::Escape($name) + '$'
$match = [regex]::Match($line, $pattern)
if(-not $match.Success) {
    throw "Invalid sidecar format; expected '<64 hex>  $name'"
}

$actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
$expected = $match.Groups[1].Value.ToUpperInvariant()
if($actual -ne $expected) {
    throw "SHA256 mismatch for $name (expected $expected, got $actual)"
}

$size = (Get-Item -LiteralPath $path).Length
if($size -lt 1024) {
    throw "Installer is unexpectedly small: $size bytes"
}

Write-Output "PASS: release installer artifact, sidecar, and SHA256 ($name, $size bytes)"
