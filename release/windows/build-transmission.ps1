#!/usr/bin/env pwsh

function global:Build-Transmission([string] $PrefixDir, [string] $Arch, [string] $DepsPrefixDir, [string] $SourceDir, [string] $ArtifactsDir) {
    $BuildDir = Join-Path $SourceDir .build

    $env:PATH = @(
        (Join-Path $DepsPrefixDir bin)
        $env:PATH
    ) -join [System.IO.Path]::PathSeparator

    $ConfigOptions = @(
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo'
        "-DCMAKE_INSTALL_PREFIX=${PrefixDir}"
        "-DCMAKE_PREFIX_PATH=${DepsPrefixDir}"
        "-DTR_THIRD_PARTY_DIR:PATH=${PrefixDir}"
        "-DTR_QT_DIR:PATH=${PrefixDir}"
    )

    Invoke-CMakeBuildAndInstall $SourceDir $BuildDir $ConfigOptions

    $DebugSymbolsDir = Join-Path $BuildDir dbg
    New-Item -Path $DebugSymbolsDir -ItemType Directory -ErrorAction Ignore | Out-Null

    foreach ($x in @('remote', 'create', 'edit', 'show', 'daemon', 'qt')) {
        # Copy-Item -Path (Join-Path $PrefixDir bin "transmission-${x}.exe") -Destination (Join-Path $PrefixDir bin)
        Get-ChildItem -Path $BuildDir -Filter "transmission-${x}.pdb" -Recurse | `
            Select-Object -First 1 | `
            ForEach-Object { Copy-Item -Path $_.FullName -Destination $DebugSymbolsDir }
    }

    $OpenSslLibSuffix = if ($Arch -eq 'x86') { '' } else { '-x64' }
    foreach ($x in @('libcurl', "libcrypto-1_1${OpenSslLibSuffix}", "libssl-1_1${OpenSslLibSuffix}", 'zlib', 'dbus-1')) {
        if ($DepsPrefixDir -ne $PrefixDir) {
            Copy-Item -Path (Join-Path $DepsPrefixDir bin "${x}.dll") -Destination (Join-Path $PrefixDir bin)
        }
        Copy-Item -Path (Join-Path $DepsPrefixDir bin "${x}.pdb") -Destination $DebugSymbolsDir
    }

    foreach ($x in @('Core', 'DBus', 'Gui', 'Network', 'Widgets', 'WinExtras')) {
        if ($DepsPrefixDir -ne $PrefixDir) {
            Copy-Item -Path (Join-Path $DepsPrefixDir bin "Qt5${x}.dll") -Destination (Join-Path $PrefixDir bin)
        }
        Copy-Item -Path (Join-Path $DepsPrefixDir bin "Qt5${x}.pdb") -Destination $DebugSymbolsDir
    }

    if ($DepsPrefixDir -ne $PrefixDir) {
        New-Item -Path (Join-Path $PrefixDir plugins platforms) -ItemType Directory -ErrorAction Ignore | Out-Null
        Copy-Item -Path (Join-Path $DepsPrefixDir plugins platforms qwindows.dll) -Destination (Join-Path $PrefixDir plugins platforms)
    }
    Copy-Item -Path (Join-Path $DepsPrefixDir plugins platforms qwindows.pdb) -Destination $DebugSymbolsDir

    if ($DepsPrefixDir -ne $PrefixDir) {
        New-Item -Path (Join-Path $PrefixDir plugins styles) -ItemType Directory -ErrorAction Ignore | Out-Null
        Copy-Item -Path (Join-Path $DepsPrefixDir plugins styles qwindowsvistastyle.dll) -Destination (Join-Path $PrefixDir plugins styles)
    }
    Copy-Item -Path (Join-Path $DepsPrefixDir plugins styles qwindowsvistastyle.pdb) -Destination $DebugSymbolsDir

    if ($DepsPrefixDir -ne $PrefixDir) {
        Copy-Item -Path (Join-Path $DepsPrefixDir translations) -Destination $PrefixDir -Recurse
    }

    Invoke-VcEnvCommand cmake --build $BuildDir --target pack-msi

    New-Item -Path $ArtifactsDir -ItemType Directory -ErrorAction Ignore | Out-Null
    $MsiPackage = (Get-ChildItem (Join-Path $BuildDir dist msi 'transmission-*.msi'))[0]
    Move-Item -Path $MsiPackage.FullName -Destination $ArtifactsDir
    Invoke-NativeCommand cmake -E chdir $DebugSymbolsDir 7z a -y (Join-Path $ArtifactsDir "$($MsiPackage.BaseName)-pdb.zip")
}

function global:Test-Transmission([string] $DepsPrefixDir, [string] $SourceDir) {
    $BuildDir = Join-Path $SourceDir .build

    $env:PATH = @(
        (Join-Path $DepsPrefixDir bin)
        $env:PATH
    ) -join [System.IO.Path]::PathSeparator

    try {
        Invoke-VcEnvCommand cmake -E chdir $BuildDir ctest -T Test --output-on-failure
    } finally {
        Publish-CTestResults (Join-Path $BuildDir Testing '*' Test.xml)
    }
}
