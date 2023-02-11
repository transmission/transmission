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

$global:VsInstallPrefix = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio' 2019 Community
$global:VsVersion = ((& (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio' Installer vswhere) -Property catalog_productSemanticVersion -Path $VsInstallPrefix) -Split '[+]')[0]
$global:VcVarsScript = Join-Path $VsInstallPrefix VC Auxiliary Build vcvarsall.bat
