#!/usr/bin/env pwsh

$global:Qt6Version = '6.4.0'

$global:Qt6Deps = @(
    'DBus'
    'OpenSsl'
    'Zlib'
)

function global:Build-Qt6([string] $PrefixDir, [string] $Arch, [string] $DepsPrefixDir) {
    $Filename = "qt-everywhere-src-${Qt6Version}.zip" # tar.xz has some names truncated (e.g. .../double-conversion.h -> .../double-conv)
    $Url = "http://qt.mirror.constant.com/archive/qt/$($Qt6Version -replace '\.\d+$', '')/${Qt6Version}/single/${Filename}"

    $ArchiveBase = "qt-everywhere-src-${Qt6Version}"
    $UnpackFlags = @(
        (Join-Path $ArchiveBase qtactiveqt '*')
        (Join-Path $ArchiveBase qtbase '*')
        (Join-Path $ArchiveBase qtsvg '*')
        (Join-Path $ArchiveBase qttools '*')
        (Join-Path $ArchiveBase qttranslations '*')
        (Join-Path $ArchiveBase qtwinextras '*')
        (Join-Path $ArchiveBase .gitmodules)
        (Join-Path $ArchiveBase cmake)
        (Join-Path $ArchiveBase CMakeLists.txt)
        (Join-Path $ArchiveBase configure.bat)
        (Join-Path $ArchiveBase configure.json)
        (Join-Path $ArchiveBase qt.pro)
    )

    $SourceDir = Invoke-DownloadAndUnpack $Url $Filename $UnpackFlags
    $BuildDir = Join-Path $SourceDir .build

    $ConfigOptions = @(
        '-platform'; 'win32-msvc'
        '-mp'
        # '-ltcg' # error C1002 on VS 2019 16.5.4
        '-opensource'
        '-confirm-license'
        '-prefix'; $PrefixDir
        '-release'
        '-force-debug-info'
        '-dbus'
        '-ssl'
        '-openssl'
        '-system-zlib'
        '-qt-pcre'
        '-qt-libpng'
        '-qt-libjpeg'
        '-no-opengl'
        '-no-freetype'
        '-no-harfbuzz'
        '-no-feature-androiddeployqt'
        '-no-feature-assistant' # No need in GUI tools
        '-no-feature-clang'
        '-no-feature-clangcpp'
        '-no-feature-designer' # No need in GUI tools
        '-no-feature-schannel'
        '-no-feature-sql'
        '-no-feature-testlib'
        '-nomake'; 'examples'
        '-nomake'; 'tests'
        '-I'; (Join-Path $DepsPrefixDir include).Replace('\', '/')
        '-L'; (Join-Path $DepsPrefixDir lib).Replace('\', '/')
    )

    if ($env:LDFLAGS) {
        # Patch to add our linker flags, mainly /PDBALTPATH
        Edit-TextFile (Join-Path $SourceDir qtbase mkspecs win32-msvc qmake.conf) '(^QMAKE_CXXFLAGS\b.*)' "`$1`nQMAKE_LFLAGS += ${env:LDFLAGS}"
    }

    # No need in GUI tools
    Edit-TextFile (Join-Path $SourceDir qttools src linguist CMakeLists.txt) 'add_subdirectory[(]linguist[)]' ''

    Invoke-NativeCommand cmake -E remove_directory $BuildDir
    $env:PATH = @(
        (Join-Path $PrefixDir bin)
        (Join-Path $DepsPrefixDir bin)
        (Join-Path $BuildDir qtbase lib)
        $env:PATH
    ) -join [System.IO.Path]::PathSeparator

    New-Item -Path $BuildDir -ItemType Directory -ErrorAction Ignore | Out-Null
    Push-Location -Path $BuildDir
    Invoke-VcEnvCommand (Join-Path $SourceDir configure) @ConfigOptions
    Invoke-VcEnvCommand cmake --build . --parallel
    Invoke-VcEnvCommand cmake --install .
    Pop-Location

    # install target doesn't copy PDBs for release DLLs
    Get-Childitem -Path (Join-Path $BuildDir qtbase lib) | `
        ForEach-Object { if ($_ -is [System.IO.DirectoryInfo] -or $_.Name -like '*.pdb') { Copy-Item -Path $_.FullName -Destination (Join-Path $PrefixDir lib) -Filter '*.pdb' -Recurse -Force } }
    Get-Childitem -Path (Join-Path $BuildDir qtbase plugins) | `
        ForEach-Object { if ($_ -is [System.IO.DirectoryInfo] -or $_.Name -like '*.pdb') { Copy-Item -Path $_.FullName -Destination (Join-Path $PrefixDir plugins) -Filter '*.pdb' -Recurse -Force } }
}
