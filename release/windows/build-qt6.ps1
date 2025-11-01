#!/usr/bin/env pwsh

$global:Qt6Version = '6.10.0'

$global:Qt6Deps = @(
    'DBus'
    'OpenSsl'
    'Zlib'
)

function global:Build-Qt6([string] $PrefixDir, [string] $Arch, [string] $DepsPrefixDir) {
    $Filename = "qt-everywhere-src-${Qt6Version}.zip" # tar.xz has some names truncated (e.g. .../double-conversion.h -> .../double-conv)
    $Url = "https://qt.mirror.constant.com/archive/qt/$($Qt6Version -replace '\.\d+$', '')/${Qt6Version}/single/${Filename}"

    switch ($Arch) {
        'x64'   { $QtPlatform = 'win32-msvc' }
        'arm64' { $QtPlatform = 'win32-arm64-msvc' }
        default { $QtPlatform = 'win32-msvc' }
    }

    $ArchiveBase = "qt-everywhere-src-${Qt6Version}"
    $UnpackFlags = @(
        (Join-Path $ArchiveBase qtactiveqt '*')
        (Join-Path $ArchiveBase qtbase '*')
        (Join-Path $ArchiveBase qtsvg '*')
        (Join-Path $ArchiveBase qttools '*')
        (Join-Path $ArchiveBase qttranslations '*')
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
        '-platform'; $QtPlatform
        '-opensource'
        '-confirm-license'
        '-prefix'; $PrefixDir
        '-disable-deprecated-up-to'; '0x060000'
        '-release'
        '-force-debug-info'
        '-unity-build'
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
        '-no-feature-assistant'
        '-no-feature-brotli'
        '-no-feature-clang'
        '-no-feature-commandlinkbutton'
        '-no-feature-concurrent'
        '-no-feature-cpp-winrt'
        '-no-feature-datawidgetmapper'
        '-no-feature-designer'
        '-no-feature-dial'
        '-no-feature-direct2d'
        '-no-feature-directwrite3'
        '-no-feature-distancefieldgenerator'
        '-no-feature-dockwidget'
        '-no-feature-emojisegmenter'
        '-no-feature-fontcombobox'
        '-no-feature-fontdialog'
        '-no-feature-freetype'
        '-no-feature-gestures'
        '-no-feature-harfbuzz'
        '-no-feature-keysequenceedit'
        '-no-feature-lcdnumber'
        '-no-feature-listwidget'
        '-no-feature-mdiarea'
        '-no-feature-networkdiskcache'
        '-no-feature-networklistmanager'
        '-no-feature-opengl'
        '-no-feature-pdf'
        '-no-feature-pixeltool'
        '-no-feature-printsupport'
        '-no-feature-qdbus'
        '-no-feature-qtattributionsscanner'
        '-no-feature-qtdiag'
        '-no-feature-qtgui-threadpool'
        '-no-feature-qtplugininfo'
        '-no-feature-raster-64bit'
        '-no-feature-schannel'
        '-no-feature-scroller'
        '-no-feature-sharedmemory'
        '-no-feature-splashscreen'
        '-no-feature-sql'
        '-no-feature-sqlmodel'
        '-no-feature-syntaxhighlighter'
        '-no-feature-systemsemaphore'
        '-no-feature-tablewidget'
        '-no-feature-testlib'
        '-no-feature-textmarkdownreader'
        '-no-feature-textmarkdownwriter'
        '-no-feature-textodfwriter'
        '-no-feature-toolbox'
        # '-no-feature-treewidget'
        '-no-feature-tuiotouch'
        '-no-feature-undocommand'
        '-no-feature-vkgen'
        '-no-feature-vulkan'
        '-no-feature-whatsthis'
        '-no-feature-windeployqt'
        '-no-feature-wizard'
        '-no-feature-zstd'
        '-nomake'; 'examples'
        '-nomake'; 'tests'
        '-I'; (Join-Path $DepsPrefixDir include).Replace('\', '/')
        '-L'; (Join-Path $DepsPrefixDir lib).Replace('\', '/')
        '--'
        "-DCMAKE_PREFIX_PATH=${DepsPrefixDir}"
    )

    if ($env:LDFLAGS) {
        # Patch to add our linker flags, mainly /PDBALTPATH
        Edit-TextFile (Join-Path $SourceDir qtbase mkspecs $QtPlatform qmake.conf) '(^QMAKE_CXXFLAGS\b.*)' "`$1`nQMAKE_LFLAGS += ${env:LDFLAGS}"
    }

    # No need in GUI and some other tools
    Edit-TextFile (Join-Path $SourceDir qttools src CMakeLists.txt) 'TARGET Qt::Widgets' 'QT_FEATURE_designer'
    Edit-TextFile (Join-Path $SourceDir qttools src CMakeLists.txt) 'add_subdirectory[(]qdoc[)]' ''
    Edit-TextFile (Join-Path $SourceDir qttools src linguist CMakeLists.txt) 'add_subdirectory[(](linguist|lprodump)[)]' ''

    # No need in 'testcon' QtAx tool
    Edit-TextFile (Join-Path $SourceDir qtactiveqt CMakeLists.txt) 'OR NOT TARGET Qt::PrintSupport' ''
    Edit-TextFile (Join-Path $SourceDir qtactiveqt CMakeLists.txt) 'PrintSupport' ''
    Edit-TextFile (Join-Path $SourceDir qtactiveqt tools CMakeLists.txt) 'add_subdirectory[(]testcon[)]' ''

    # Fix build (including because of disabled features)
    Edit-TextFile (Join-Path $SourceDir qtbase src gui text windows qwindowsfontdatabasebase_p.h) 'unique_ptr<QCustomFontFileLoader>' 'unique_ptr<int>'
    Edit-TextFile (Join-Path $SourceDir qtactiveqt src activeqt container qaxwidget.cpp) '.*<(qdockwidget|qwhatsthis)[.]h>|QWhatsThis::[a-zA-Z]+[(][)]' ''
    Edit-TextFile (Join-Path $SourceDir qtactiveqt src activeqt control qaxserverbase.cpp) '.*<qwhatsthis[.]h>|QWhatsThis::[a-zA-Z]+[(][)]' ''

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
    Invoke-VcEnvCommand cmake --build .
    Invoke-VcEnvCommand cmake --install .
    Pop-Location

    # install target doesn't copy PDBs for release DLLs
    Get-Childitem -Path (Join-Path $BuildDir qtbase lib) | `
        ForEach-Object { if ($_ -is [System.IO.DirectoryInfo] -or $_.Name -like '*.pdb') { Copy-Item -Path $_.FullName -Destination (Join-Path $PrefixDir lib) -Filter '*.pdb' -Recurse -Force } }
    Get-Childitem -Path (Join-Path $BuildDir qtbase plugins) | `
        ForEach-Object { if ($_ -is [System.IO.DirectoryInfo] -or $_.Name -like '*.pdb') { Copy-Item -Path $_.FullName -Destination (Join-Path $PrefixDir plugins) -Filter '*.pdb' -Recurse -Force } }
}
