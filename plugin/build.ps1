<#
.SYNOPSIS
    Builds the fpview DFHack plugin and (optionally) copies it into the game.

.DESCRIPTION
    DFHack plugins have to be compiled inside a DFHack source tree of exactly
    the version that is installed. This script expects:

      _refs\dfhack        DFHack source at the matching tag, with submodules
                          git clone --depth 1 --branch <tag> --recurse-submodules
                              --shallow-submodules https://github.com/DFHack/dfhack
      _refs\tools\perl    portable Strawberry Perl (needs XML::LibXML, XML::LibXSLT)
      _refs\tools\pycmake CMake installed with: pip install --target <dir> cmake

    plus Ninja on PATH (pip install ninja) and a Visual Studio that has the
    MSVC x64 compiler and a Windows SDK. It links this folder into the DFHack
    tree as an "external" plugin, configures once, and builds only the fpview
    target (which pulls in the DFHack core library).

    DFHack 53.17's build script refuses compilers newer than VS 2022 and
    treats warnings as errors. Both are relaxed in the local source tree; the
    MSVC 14.x toolsets are binary compatible with each other.

.PARAMETER Install
    Copy the built plugin to <DF>\hack\plugins afterwards.
#>
param(
    [string]$DFPath = 'C:\Soft\Steam\steamapps\common\Dwarf Fortress',
    [switch]$Install,
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$dfhack = Join-Path $repo '_refs\dfhack'
$tools = Join-Path $repo '_refs\tools'
$build = Join-Path $dfhack 'build\win64\fpview'
$cmake = Join-Path $tools 'pycmake\cmake\data\bin\cmake.exe'
$perl = Join-Path $tools 'perl'

foreach ($p in $dfhack, $cmake, (Join-Path $perl 'perl\bin\perl.exe')) {
    if (-not (Test-Path $p)) { throw "Missing: $p (see the notes at the top of this script)" }
}
$env:PATH = "$perl\c\bin;$perl\perl\site\bin;$perl\perl\bin;" + $env:PATH

# load the MSVC x64 command-line environment (cl, link, Windows SDK) into this session
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products * -prerelease -property installationPath
    $devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path $devcmd)) { throw "VsDevCmd.bat not found under $vs" }
    cmd /c "`"$devcmd`" -arch=amd64 -host_arch=amd64 -no_logo >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        # Visual Studio without the C++ desktop workload can still carry a
        # complete compiler (e.g. via the Python workload); wire it up by hand.
        $msvc = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName 'bin\Hostx64\x64\cl.exe') } |
            Sort-Object Name -Descending | Select-Object -First 1
        $kit = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
        $sdk = Get-ChildItem (Join-Path $kit 'Lib') -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName 'um\x64\kernel32.lib') } |
            Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
        if (-not $msvc -or -not $sdk) { throw 'No usable MSVC x64 toolset or Windows SDK found' }
        $m = $msvc.FullName
        $env:PATH = "$m\bin\Hostx64\x64;$kit\bin\$sdk\x64;" + $env:PATH
        $env:INCLUDE = "$m\include;$kit\Include\$sdk\ucrt;$kit\Include\$sdk\um;$kit\Include\$sdk\shared;$kit\Include\$sdk\winrt"
        $env:LIB = "$m\lib\x64;$kit\Lib\$sdk\ucrt\x64;$kit\Lib\$sdk\um\x64"
    }
}

# relax the compiler gate and warnings-as-errors in the local DFHack tree
$top = Join-Path $dfhack 'CMakeLists.txt'
$text = Get-Content $top -Raw
$patched = $text -replace 'set\(MSVC_MAX_VER 19\d\d\)', 'set(MSVC_MAX_VER 1959)' `
                 -replace '(?m)^(\s*)add_compile_options\("/WX"\)', '$1# add_compile_options("/WX")  # relaxed for local plugin builds'
if ($patched -ne $text) { Set-Content $top $patched -NoNewline }

# Strawberry Perl 5.42 with XML::LibXSLT crashes while tearing down at exit,
# after the generator has written everything. Exit without global destruction.
$codegen = Join-Path $dfhack 'library\xml\codegen.pl'
$text = Get-Content $codegen -Raw
if ($text -notmatch 'POSIX::_exit') {
    $text = $text -replace '(?m)^exit \$errorlevel;\s*$', "use POSIX ();`nclose(STDOUT);`nPOSIX::_exit(`$errorlevel);`n"
    Set-Content $codegen $text -NoNewline
}

# link this folder in as an external plugin
$external = Join-Path $dfhack 'plugins\external'
$link = Join-Path $external 'fpview'
if (-not (Test-Path $link)) {
    New-Item -ItemType Junction -Path $link -Target $PSScriptRoot | Out-Null
}
$extList = Join-Path $external 'CMakeLists.txt'
if (-not (Test-Path $extList) -or -not (Select-String -Path $extList -SimpleMatch 'add_subdirectory(fpview)' -Quiet)) {
    Add-Content $extList "`nadd_subdirectory(fpview)`n"
}

if ($Reconfigure -or -not (Test-Path (Join-Path $build 'build.ninja'))) {
    & $cmake -S $dfhack -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
        "-DCMAKE_INSTALL_PREFIX=$DFPath" `
        -DDFHACK_BUILD_ARCH=64 `
        -DBUILD_SUPPORTED=0 -DBUILD_DEV_PLUGINS=0 -DBUILD_STONESENSE=0 `
        -DBUILD_DOCS=0 -DBUILD_TESTS=0
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
}

& $cmake --build $build --target fpview
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

$dll = Get-ChildItem $build -Recurse -Filter 'fpview.plug.dll' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $dll) { throw 'fpview.plug.dll was not produced' }
Write-Host "Built $($dll.FullName)"

if ($Install) {
    $dest = Join-Path $DFPath 'hack\plugins\fpview.plug.dll'
    Copy-Item $dll.FullName $dest -Force
    Write-Host "Installed $dest"
}
