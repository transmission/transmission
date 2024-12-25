if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH x64)
else()
    set(ARCH x86)
endif()

set(VERSION "${TR_USER_AGENT_PREFIX}")
set(VERSION_MSI "${TR_VERSION_MAJOR}.${TR_VERSION_MINOR}.${TR_VERSION_PATCH}")
set(VERSION_FULL "${TR_USER_AGENT_PREFIX} (${TR_VCS_REVISION})")

if(MSVC_TOOLSET_VERSION MATCHES "^([0-9]+)([0-9])$")
    set(MSVC_TOOLSET_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(MSVC_TOOLSET_VERSION_MINOR ${CMAKE_MATCH_2})
endif()

if(MSVC_TOOLSET_VERSION EQUAL 143)
    set(MSVS_VERSION 17)
elseif(MSVC_TOOLSET_VERSION EQUAL 142)
    set(MSVS_VERSION 16)
elseif(MSVC_TOOLSET_VERSION EQUAL 141)
    set(MSVS_VERSION 15)
else()
    message(FATAL_ERROR "Unsupported MSVC_TOOLSET_VERSION value: '${MSVC_TOOLSET_VERSION}'")
endif()

set(MSI_FILENAME_VERSION "${TR_SEMVER}")
if(NOT TR_STABLE_RELEASE AND NOT "${TR_VCS_REVISION}" STREQUAL "")
    string(APPEND MSI_FILENAME_VERSION "+r${TR_VCS_REVISION}")
endif()
if(NOT Qt_VERSION_MAJOR EQUAL 6)
    string(APPEND MSI_FILENAME_VERSION "-qt${Qt_VERSION_MAJOR}")
endif()

if(NOT TR_THIRD_PARTY_DIR)
    set(TR_THIRD_PARTY_DIR "$<TARGET_FILE_DIR:OpenSSL::SSL>/..")
endif()

if(NOT TR_QT_DIR)
    set(TR_QT_DIR "$<TARGET_FILE_DIR:Qt${Qt_VERSION_MAJOR}::Core>/..")
endif()

set(TR_OPENSSL_ARCH)
if(ARCH STREQUAL "x64")
    set(TR_OPENSSL_ARCH "-x64")
endif()

if(OPENSSL_VERSION MATCHES "^3[.]")
    set(TR_OPENSSL_CRYPTO_NAME "libcrypto-3${TR_OPENSSL_ARCH}.dll")
    set(TR_OPENSSL_SSL_NAME "libssl-3${TR_OPENSSL_ARCH}.dll")
elseif(OPENSSL_VERSION MATCHES "^1[.]1[.]")
    set(TR_OPENSSL_CRYPTO_NAME "libcrypto-1_1${TR_OPENSSL_ARCH}.dll")
    set(TR_OPENSSL_SSL_NAME "libssl-1_1${TR_OPENSSL_ARCH}.dll")
else()
    set(TR_OPENSSL_CRYPTO_NAME "libeay32.dll")
    set(TR_OPENSSL_SSL_NAME "ssleay32.dll")
endif()

configure_file(TransmissionConfig.wxi.in TransmissionConfig.wxi)

set(WIX_OUTPUT_BASE "${CMAKE_CURRENT_BINARY_DIR}/transmission-${MSI_FILENAME_VERSION}-${ARCH}")
add_custom_target(pack-msi
    COMMAND wix build
        -arch ${ARCH}
        -define "SrcDir=${CMAKE_INSTALL_PREFIX}"
        -define "ThirdPartyDir=${TR_THIRD_PARTY_DIR}"
        -define "QtDir=${TR_QT_DIR}"
        -define "QtMajorVer=${Qt_VERSION_MAJOR}"
        -define "LicenseFile=${CMAKE_CURRENT_SOURCE_DIR}/GPLv2.rtf"
        -define "IconFile=${SOURCE_ICONS_DIR}/transmission.ico"
        -define "WebSrcDir=${CMAKE_INSTALL_PREFIX}/share/transmission/public_html"
        -define "TrQmSrcDir=${CMAKE_INSTALL_PREFIX}/share/transmission/translations"
        -define "QtQmSrcDir=${TR_QT_DIR}/translations"
        -define "OpenSslCryptoName=${TR_OPENSSL_CRYPTO_NAME}"
        -define "OpenSslSslName=${TR_OPENSSL_SSL_NAME}"
        -includepath "${CMAKE_CURRENT_BINARY_DIR}"
        -out "${WIX_OUTPUT_BASE}.msi"
        -ext WixToolset.UI.wixext
        -ext WixToolset.Util.wixext
        "${CMAKE_CURRENT_SOURCE_DIR}/components/CliTools.wxs"
        "${CMAKE_CURRENT_SOURCE_DIR}/components/CommonLibs.wxs"
        "${CMAKE_CURRENT_SOURCE_DIR}/components/Daemon.wxs"
        "${CMAKE_CURRENT_SOURCE_DIR}/components/QtClient.wxs"
        "${CMAKE_CURRENT_SOURCE_DIR}/components/WebUi.wxs"
        "${CMAKE_CURRENT_SOURCE_DIR}/Transmission.wxs"
    SOURCES
        components/CliTools.wxs
        components/CommonLibs.wxs
        components/Daemon.wxs
        components/QtClient.wxs
        components/WebUi.wxs
        GPLv2.rtf
        Transmission.wxs
        TransmissionConfig.wxi.in
        "${CMAKE_CURRENT_BINARY_DIR}/TransmissionConfig.wxi")

add_custom_target(pack-exe
    COMMAND wix build
        -arch ${ARCH}
        -define "VcRedistFile=$ENV{VCINSTALLDIR}Redist/MSVC/v${MSVC_TOOLSET_VERSION}/vc_redist.${ARCH}.exe"
        -define "MsiFile=${WIX_OUTPUT_BASE}.msi"
        -define "SplashScreenFile=${CMAKE_CURRENT_SOURCE_DIR}/SplashScreen.bmp"
        -includepath "${CMAKE_CURRENT_BINARY_DIR}"
        -out "${WIX_OUTPUT_BASE}.exe"
        -ext WixToolset.BootstrapperApplications.wixext
        "${CMAKE_CURRENT_SOURCE_DIR}/Bundle.wxs"
    SOURCES
        Bundle.wxs
        TransmissionConfig.wxi.in
        "${CMAKE_CURRENT_BINARY_DIR}/TransmissionConfig.wxi")
