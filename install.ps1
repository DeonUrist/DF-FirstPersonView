<#
.SYNOPSIS
    Installs the First Person View mod into Dwarf Fortress.

.DESCRIPTION
    The mod has two parts:

      * the script mod (info.txt, scripts_modinstalled), which goes into the
        Dwarf Fortress "mods" folder and provides the hotkey and settings
      * the fpview plugin (fpview.plug.dll), which does the drawing and goes
        into <DF>\hack\plugins. It has to be built for the exact DFHack
        version in use: run plugin\build.ps1 first.

    With -Link the script mod is linked to this repository instead of copied,
    so edits made here are picked up without reinstalling.

.PARAMETER ModsDir
    The Dwarf Fortress mods folder. Defaults to the per-user data folder the
    Steam version uses. For a portable install pass "<DF folder>\mods".

.PARAMETER DFPath
    The Dwarf Fortress install folder (where hack\plugins is).

.EXAMPLE
    .\plugin\build.ps1
    .\install.ps1
#>
param(
    [string]$ModsDir = (Join-Path $env:APPDATA 'Bay 12 Games\Dwarf Fortress\mods'),
    [string]$DFPath = 'C:\Soft\Steam\steamapps\common\Dwarf Fortress',
    [switch]$Link,
    [switch]$ResetMedia
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$target = Join-Path $ModsDir 'first_person_view'

if (-not (Test-Path $ModsDir)) {
    throw "Mods folder not found: $ModsDir (pass -ModsDir to point at your install)"
}

# --- script mod ---
# Art in the installed media folder may have been edited: it is kept, and only
# files that are missing there are added (-ResetMedia puts the default art back).
$media = Join-Path $target 'media'
$keep = $null
if ((Test-Path $target) -and -not (Get-Item $target -Force).LinkType -and (Test-Path $media) -and -not $ResetMedia) {
    $keep = Join-Path $ModsDir '_first_person_view_media_keep'
    if (Test-Path $keep) { Remove-Item $keep -Recurse -Force }
    Move-Item $media $keep
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
    Copy-Item (Join-Path $source 'media') $target -Recurse
    if ($keep) {
        # the player's files win over the defaults
        Copy-Item (Join-Path $keep '*') $media -Recurse -Force
        Remove-Item $keep -Recurse -Force
        Write-Host "Kept the art already in $media"
    }
    Write-Host "Installed script mod to $target"
}

# --- plugin ---
$dll = Get-ChildItem (Join-Path $source '_refs\dfhack\build') -Recurse -Filter 'fpview.plug.dll' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
$plugins = Join-Path $DFPath 'hack\plugins'
if (-not $dll) {
    Write-Warning 'fpview.plug.dll has not been built yet. Run plugin\build.ps1, then run this script again.'
} elseif (-not (Test-Path $plugins)) {
    Write-Warning "DFHack plugin folder not found: $plugins (pass -DFPath)"
} else {
    $dest = Join-Path $plugins 'fpview.plug.dll'
    try {
        Copy-Item $dll.FullName $dest -Force
        Write-Host "Installed plugin to $dest"
    } catch {
        Write-Warning "Could not replace $dest. If the game is running, run 'unload fpview' in the DFHack console (or close the game) and try again."
    }
}
Write-Host 'Restart Dwarf Fortress for DFHack to pick everything up.'
