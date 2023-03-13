#!/usr/bin/env pwsh

$global:Qt5Version = '5.15.8'

$global:Qt5Deps = @(
    'DBus'
    'OpenSsl'
    'Zlib'
)

function global:Build-Qt5([string] $PrefixDir, [string] $Arch, [string] $DepsPrefixDir) {
    $Filename = "qt-everywhere-opensource-src-${Qt5Version}.zip" # tar.xz has some names truncated (e.g. .../double-conversion.h -> .../double-conv)
    $Url = "http://qt.mirror.constant.com/archive/qt/$($Qt5Version -replace '\.\d+$', '')/${Qt5Version}/single/${Filename}"

    $ArchiveBase = "qt-everywhere-src-${Qt5Version}"
    $UnpackFlags = @(
        (Join-Path $ArchiveBase qtactiveqt '*')
        (Join-Path $ArchiveBase qtbase '*')
        (Join-Path $ArchiveBase qtsvg '*')
        (Join-Path $ArchiveBase qttools '*')
        (Join-Path $ArchiveBase qttranslations '*')
        (Join-Path $ArchiveBase qtwinextras '*')
        (Join-Path $ArchiveBase .gitmodules)
        (Join-Path $ArchiveBase configure.bat)
        (Join-Path $ArchiveBase configure.json)
        (Join-Path $ArchiveBase qt.pro)
    )

    $SourceDir = Invoke-DownloadAndUnpack $Url $Filename $UnpackFlags $ArchiveBase
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
        '-no-direct2d'
        '-no-freetype'
        '-no-harfbuzz'
        '-no-sql-db2'
        '-no-sql-ibase'
        '-no-sql-mysql'
        '-no-sql-oci'
        '-no-sql-odbc'
        '-no-sql-psql'
        '-no-sql-sqlite'
        '-no-sql-sqlite2'
        '-no-sql-tds'
        '-nomake'; 'examples'
        '-nomake'; 'tests'
        '-nomake'; 'tools'
        '-I'; (Join-Path $DepsPrefixDir include)
        '-L'; (Join-Path $DepsPrefixDir lib)
    )

    if ($env:LDFLAGS) {
        # Patch to add our linker flags, mainly /PDBALTPATH
        Edit-TextFile (Join-Path $SourceDir qtbase mkspecs win32-msvc qmake.conf) '(^QMAKE_CXXFLAGS\b.*)' "`$1`nQMAKE_LFLAGS += ${env:LDFLAGS}"
    }

    # No need in GUI tools
    Edit-TextFile (Join-Path $SourceDir qttools src src.pro) 'qtHaveModule[(]gui[)]' 'qtHaveModule(hughey)'
    Edit-TextFile (Join-Path $SourceDir qttools src src.pro) 'qtHaveModule[(]widgets[)]' 'qtHaveModule(digits)'
    Edit-TextFile (Join-Path $SourceDir qttools src linguist linguist.pro) 'qtHaveModule[(]widgets[)]' 'qtHaveModule(digits)'

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
    Invoke-VcEnvCommand jom
    Invoke-VcEnvCommand jom install
    Pop-Location

    # install target doesn't copy PDBs for release DLLs
    Get-Childitem -Path (Join-Path $BuildDir qtbase lib) | `
        ForEach-Object { if ($_ -is [System.IO.DirectoryInfo] -or $_.Name -like '*.pdb') { Copy-Item -Path $_.FullName -Destination (Join-Path $PrefixDir lib) -Filter '*.pdb' -Recurse -Force } }
    Get-Childitem -Path (Join-Path $BuildDir qtbase plugins) | `
        ForEach-Object { if ($_ -is [System.IO.DirectoryInfo] -or $_.Name -like '*.pdb') { Copy-Item -Path $_.FullName -Destination (Join-Path $PrefixDir plugins) -Filter '*.pdb' -Recurse -Force } }
}
