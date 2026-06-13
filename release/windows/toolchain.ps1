#!/usr/bin/env pwsh

$global:CompilerFlags = @(
    '/FS'
)

$global:LinkerFlags = @(
    '/LTCG'
    '/INCREMENTAL:NO'
    '/OPT:REF'
    '/DEBUG'
    '/PDBALTPATH:%_PDB%'
)

$VsWherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio' Installer vswhere.exe
if (-not (Test-Path $VsWherePath)) {
    $VsWherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio' Installer vswhere
}
if (-not (Test-Path $VsWherePath)) {
    throw 'vswhere was not found. Expected it under "Program Files (x86)\\Microsoft Visual Studio\\Installer".'
}

$VsInstance = & $VsWherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
if ($VsInstance -is [Array]) {
    $VsInstance = $VsInstance[0]
}

if (-not $VsInstance -or -not $VsInstance.installationPath) {
    throw 'No Visual Studio instance with C++ x86/x64 tools was found by vswhere.'
}

$global:VsInstallPrefix = $VsInstance.installationPath
$global:VsVersion = ($VsInstance.catalog.productSemanticVersion -split '[+]')[0]
if (-not $global:VsVersion) {
    throw 'Unable to determine Visual Studio semantic version from vswhere output.'
}

$global:VcVarsScript = Join-Path $VsInstallPrefix VC Auxiliary Build vcvarsall.bat
if (-not (Test-Path $global:VcVarsScript)) {
    throw "vcvarsall.bat was not found at expected path: $global:VcVarsScript"
}
