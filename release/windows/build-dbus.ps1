#!/usr/bin/env pwsh

$global:DBusVersion = '1.12.16'

$global:DBusDeps = @(
    'Expat'
)

function global:Build-DBus([string] $PrefixDir, [string] $Arch, [string] $DepsPrefixDir) {
    $Filename = "dbus-${DBusVersion}.tar.gz"
    $Url = "https://dbus.freedesktop.org/releases/dbus/${Filename}"

    $SourceDir = Invoke-DownloadAndUnpack $Url $Filename
    $BuildDir = Join-Path $SourceDir .build

    $ConfigOptions = @(
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo'
        "-DCMAKE_INSTALL_PREFIX=${PrefixDir}"
        "-DCMAKE_PREFIX_PATH=${DepsPrefixDir}"
        '-DDBUS_BUILD_TESTS=OFF'
    )

    # Patch to remove "-3" (or whatever) revision suffix part from DLL name since Qt doesn't seem to support that and we don't really need it
    Edit-TextFile (Join-Path $SourceDir cmake modules MacrosAutotools.cmake) '^.*_LIBRARY_REVISION.*' ''

    Invoke-CMakeBuildAndInstall (Join-Path $SourceDir cmake) $BuildDir $ConfigOptions
    Copy-Item -Path (Join-Path $BuildDir bin dbus-1.pdb) -Destination (Join-Path $PrefixDir bin)
}
