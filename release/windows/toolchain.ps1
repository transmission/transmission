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

foreach ($VsType in @('Enterprise', 'Professional', 'Community', 'BuildTools')) {
    $global:VsInstallPrefix = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio' 2022 $VsType
    if (Test-Path $global:VsInstallPrefix) {
        break
    }
    $global:VsInstallPrefix = Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio' 2022 $VsType
    if (Test-Path $global:VsInstallPrefix) {
        break
    }
}
$global:VsVersion = ((& (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio' Installer vswhere) -Property catalog_productSemanticVersion -Path $VsInstallPrefix) -Split '[+]')[0]
$global:VcVarsScript = Join-Path $VsInstallPrefix VC Auxiliary Build vcvarsall.bat
