#!/usr/bin/env pwsh

$global:ZlibVersion = '1.2.13'

$global:ZlibDeps = @()

function global:Build-Zlib([string] $PrefixDir, [string] $Arch, [string] $DepsPrefixDir) {
    $Filename = "zlib-${ZlibVersion}.tar.gz"
    $Url = "https://zlib.net/fossils/${Filename}"

    $SourceDir = Invoke-DownloadAndUnpack $Url $Filename
    $BuildDir = Join-Path $SourceDir .build

    $ConfigOptions = @(
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo'
        "-DCMAKE_INSTALL_PREFIX=${PrefixDir}"
        "-DCMAKE_PREFIX_PATH=${DepsPrefixDir}"
    )

    Invoke-CMakeBuildAndInstall $SourceDir $BuildDir $ConfigOptions
    Copy-Item -Path (Join-Path $BuildDir zlib.pdb) -Destination (Join-Path $PrefixDir bin)
}
