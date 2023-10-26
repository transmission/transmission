#!/usr/bin/env pwsh

function global:Build-Transmission(
    [string] $PrefixDir,
    [string] $Arch,
    [string] $DepsPrefixDir,
    [string] $SourceDir,
    [string] $ArtifactsDir,
    [string] $UseQtVersion,
    [boolean] $PackDebugSyms
) {
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
        '-DRUN_CLANG_TIDY=OFF'
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
    foreach ($x in @('libcurl', "libcrypto-3${OpenSslLibSuffix}", "libssl-3${OpenSslLibSuffix}", 'zlib', 'dbus-1')) {
        if ($DepsPrefixDir -ne $PrefixDir) {
            Copy-Item -Path (Join-Path $DepsPrefixDir bin "${x}.dll") -Destination (Join-Path $PrefixDir bin)
        }
        Copy-Item -Path (Join-Path $DepsPrefixDir bin "${x}.pdb") -Destination $DebugSymbolsDir
    }

    $QtModules = @('Core', 'DBus', 'Gui', 'Network', 'Svg', 'Widgets')
    if ($UseQtVersion -eq '5') {
        $QtModules += @('WinExtras')
    }

    foreach ($x in $QtModules) {
        if ($DepsPrefixDir -ne $PrefixDir) {
            Copy-Item -Path (Join-Path $DepsPrefixDir bin "Qt${UseQtVersion}${x}.dll") -Destination (Join-Path $PrefixDir bin)
        }
        Copy-Item -Path (Join-Path $DepsPrefixDir bin "Qt${UseQtVersion}${x}.pdb") -Destination $DebugSymbolsDir
    }

    foreach ($x in @('gif', 'ico', 'jpeg', 'svg')) {
        if ($DepsPrefixDir -ne $PrefixDir) {
            New-Item -Path (Join-Path $PrefixDir plugins imageformats) -ItemType Directory -ErrorAction Ignore | Out-Null
            Copy-Item -Path (Join-Path $DepsPrefixDir plugins imageformats "q${x}.dll") -Destination (Join-Path $PrefixDir plugins imageformats)
        }
        Copy-Item -Path (Join-Path $DepsPrefixDir plugins imageformats "q${x}.pdb") -Destination $DebugSymbolsDir
    }

    if ($UseQtVersion -eq '6') {
        foreach ($x in @('openssl')) {
            if ($DepsPrefixDir -ne $PrefixDir) {
                New-Item -Path (Join-Path $PrefixDir plugins tls) -ItemType Directory -ErrorAction Ignore | Out-Null
                Copy-Item -Path (Join-Path $DepsPrefixDir plugins tls "q${x}backend.dll") -Destination (Join-Path $PrefixDir plugins tls)
            }
            Copy-Item -Path (Join-Path $DepsPrefixDir plugins tls "q${x}backend.pdb") -Destination $DebugSymbolsDir
        }
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

    if ($PackDebugSyms) {
        Invoke-NativeCommand cmake -E chdir $DebugSymbolsDir 7z a -y (Join-Path $ArtifactsDir "$($MsiPackage.BaseName)-pdb.7z")
    }
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
