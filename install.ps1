<#
.SYNOPSIS
    Installs the First Person View mod into Dwarf Fortress.

.DESCRIPTION
    Copies the mod (info.txt, README.md and scripts_modinstalled) into the
    Dwarf Fortress "mods" folder, where both the game and DFHack look for mods.

    With -Link a directory junction to this repository is created instead, so
    edits made here are picked up by the game without reinstalling.

.PARAMETER ModsDir
    The Dwarf Fortress mods folder. Defaults to the per-user data folder the
    Steam version uses. For a portable install pass "<DF folder>\mods".

.EXAMPLE
    .\install.ps1
.EXAMPLE
    .\install.ps1 -Link
#>
param(
    [string]$ModsDir = (Join-Path $env:APPDATA 'Bay 12 Games\Dwarf Fortress\mods'),
    [switch]$Link
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$target = Join-Path $ModsDir 'first_person_view'

if (-not (Test-Path $ModsDir)) {
    throw "Mods folder not found: $ModsDir (pass -ModsDir to point at your install)"
}

if (Test-Path $target) {
    $item = Get-Item $target -Force
    if ($item.LinkType) {
        # a junction: remove the link only, never the repository behind it
        $item.Delete()
    } else {
        Remove-Item $target -Recurse -Force
    }
}

if ($Link) {
    New-Item -ItemType Junction -Path $target -Target $source | Out-Null
    Write-Host "Linked $target -> $source"
} else {
    New-Item -ItemType Directory -Path $target | Out-Null
    Copy-Item (Join-Path $source 'info.txt') $target
    Copy-Item (Join-Path $source 'README.md') $target -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $source 'scripts_modinstalled') $target -Recurse
    Write-Host "Installed to $target"
}
Write-Host 'Restart Dwarf Fortress (or load a world) for DFHack to pick the mod up.'
