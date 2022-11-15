#!/usr/bin/env pwsh

$global:CurlVersion = '7.86.0'

$global:CurlDeps = @(
    'OpenSsl'
    'Zlib'
)

function global:Build-Curl([string] $PrefixDir, [string] $Arch, [string] $DepsPrefixDir) {
    $Filename = "curl-${CurlVersion}.tar.gz"
    $Url = "https://curl.haxx.se/download/${Filename}"

    $SourceDir = Invoke-DownloadAndUnpack $Url $Filename
    $BuildDir = Join-Path $SourceDir .build

    $ConfigOptions = @(
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo'
        "-DCMAKE_INSTALL_PREFIX=${PrefixDir}"
        "-DCMAKE_PREFIX_PATH=${DepsPrefixDir}"
        '-DBUILD_CURL_EXE=OFF'
        '-DBUILD_TESTING=OFF'
        '-DCURL_DISABLE_DICT=ON'
        '-DCURL_DISABLE_FTP=ON'
        '-DCURL_DISABLE_GOPHER=ON'
        '-DCURL_DISABLE_IMAP=ON'
        '-DCURL_DISABLE_LDAP=ON'
        '-DCURL_DISABLE_LDAPS=ON'
        '-DCURL_DISABLE_MQTT=ON'
        '-DCURL_DISABLE_POP3=ON'
        '-DCURL_DISABLE_RTSP=ON'
        '-DCURL_DISABLE_SMB=ON'
        '-DCURL_DISABLE_SMTP=ON'
        '-DCURL_DISABLE_TELNET=ON'
        '-DCURL_DISABLE_TFTP=ON'
        '-DCURL_USE_LIBSSH=OFF'
        '-DCURL_USE_LIBSSH2=OFF'
        '-DCURL_USE_OPENSSL=ON'
        '-DCURL_WINDOWS_SSPI=OFF'
        '-DENABLE_MANUAL=OFF'
    )

    Invoke-CMakeBuildAndInstall $SourceDir $BuildDir $ConfigOptions
    Invoke-NativeCommand cmake -E remove_directory (Join-Path $PrefixDir lib cmake CURL) # until we support it
    Copy-Item -Path (Join-Path $BuildDir lib libcurl.pdb) -Destination (Join-Path $PrefixDir bin)
}
