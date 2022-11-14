#!/usr/bin/env pwsh

$global:ExpatVersion = '2.5.0'

$global:ExpatDeps = @()

function global:Build-Expat([string] $PrefixDir, [string] $Arch, [string] $DepsPrefixDir) {
    $Filename = "expat-${ExpatVersion}.tar.bz2"
    $Url = "https://github.com/libexpat/libexpat/releases/download/R_$($ExpatVersion.replace(".", "_"))/${Filename}"

    $SourceDir = Invoke-DownloadAndUnpack $Url $Filename
    $BuildDir = Join-Path $SourceDir .build

    $ConfigOptions = @(
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo'
        "-DCMAKE_INSTALL_PREFIX=${PrefixDir}"
        "-DCMAKE_PREFIX_PATH=${DepsPrefixDir}"
        '-DEXPAT_BUILD_EXAMPLES=OFF'
        '-DEXPAT_BUILD_TESTS=OFF'
        '-DEXPAT_BUILD_TOOLS=OFF'
        '-DEXPAT_SHARED_LIBS=ON'
    )

    Invoke-CMakeBuildAndInstall $SourceDir $BuildDir $ConfigOptions
    Copy-Item -Path (Join-Path $BuildDir libexpat.pdb) -Destination (Join-Path $PrefixDir bin)
}
