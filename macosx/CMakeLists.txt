project(trmac)

if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 10.9)
    # 10.9 required for actool to produce Assets.car
    message(FATAL_ERROR "Targeting macOS versions before 10.9 is not supported, please adjust CMAKE_OSX_DEPLOYMENT_TARGET (${CMAKE_OSX_DEPLOYMENT_TARGET})")
endif()

include_directories(${CMAKE_SOURCE_DIR})

add_compile_options(
    -fcxx-modules
    -fmodules
    -fobjc-arc)

find_program(ACTOOL_EXECUTABLE actool REQUIRED)
find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
find_program(IBTOOL_EXECUTABLE ibtool REQUIRED)

macro(tr_wrap_xib IFILE OFILE)
    get_filename_component(twx_nib_dir "${OFILE}" DIRECTORY)
    add_custom_command(
        OUTPUT ${OFILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${twx_nib_dir}
        COMMAND ${IBTOOL_EXECUTABLE} --compile ${OFILE} ${IFILE}
        DEPENDS ${IFILE}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        VERBATIM
    )
    source_group("Generated Files" FILES ${OFILE})
endmacro()

set(${PROJECT_NAME}_SOURCES
    AboutWindowController.mm
    AddMagnetWindowController.mm
    AddWindowController.mm
    BadgeView.mm
    Badger.mm
    BlocklistDownloader.mm
    BlocklistDownloaderViewController.mm
    BlocklistScheduler.mm
    BonjourController.mm
    ButtonToolbarItem.mm
    ColorTextField.mm
    Controller.mm
    CreatorWindowController.mm
    DragOverlayView.mm
    DragOverlayWindow.mm
    ExpandedPathToIconTransformer.mm
    ExpandedPathToPathTransformer.mm
    FileListNode.mm
    FileNameCell.mm
    FileOutlineController.mm
    FileOutlineView.mm
    FilePriorityCell.mm
    FileRenameSheetController.mm
    FilterBarController.mm
    FilterBarView.mm
    FilterButton.mm
    GlobalOptionsPopoverViewController.mm
    GroupToolbarItem.mm
    GroupsController.mm
    GroupsPrefsController.mm
    InfoActivityViewController.mm
    InfoFileViewController.mm
    InfoGeneralViewController.mm
    InfoOptionsViewController.mm
    InfoPeersViewController.mm
    InfoTextField.mm
    InfoTrackersViewController.mm
    InfoWindowController.mm
    main.mm
    MessageWindowController.mm
    NSApplicationAdditions.mm
    NSDataAdditions.mm
    NSImageAdditions.mm
    NSKeyedUnarchiverAdditions.mm
    NSMutableArrayAdditions.mm
    NSStringAdditions.mm
    PeerProgressIndicatorCell.mm
    PeerTableView.mm
    PiecesView.mm
    PortChecker.mm
    PredicateEditorRowTemplateAny.mm
    PrefsController.mm
    PrefsWindow.mm
    ProgressGradients.mm
    ShareToolbarItem.mm
    ShareTorrentFileHelper.mm
    StatsWindowController.mm
    StatusBarController.mm
    StatusBarView.mm
    Toolbar.mm
    ToolbarSegmentedCell.mm
    Torrent.mm
    TorrentCell.mm
    TorrentGroup.mm
    TorrentTableView.mm
    TrackerCell.mm
    TrackerNode.mm
    TrackerTableView.mm
    URLSheetWindowController.mm
    WebSeedTableView.mm
)

set(${PROJECT_NAME}_HEADERS
    AboutWindowController.h
    AddMagnetWindowController.h
    AddWindowController.h
    BadgeView.h
    Badger.h
    BlocklistDownloader.h
    BlocklistDownloaderViewController.h
    BlocklistScheduler.h
    BonjourController.h
    ButtonToolbarItem.h
    ColorTextField.h
    Controller.h
    CreatorWindowController.h
    DragOverlayView.h
    DragOverlayWindow.h
    ExpandedPathToIconTransformer.h
    ExpandedPathToPathTransformer.h
    FileListNode.h
    FileNameCell.h
    FileOutlineController.h
    FileOutlineView.h
    FilePriorityCell.h
    FileRenameSheetController.h
    FilterBarController.h
    FilterBarView.h
    FilterButton.h
    GlobalOptionsPopoverViewController.h
    GroupToolbarItem.h
    GroupsController.h
    GroupsPrefsController.h
    InfoActivityViewController.h
    InfoFileViewController.h
    InfoGeneralViewController.h
    InfoOptionsViewController.h
    InfoPeersViewController.h
    InfoTextField.h
    InfoTrackersViewController.h
    InfoViewController.h
    InfoWindowController.h
    MessageWindowController.h
    NSApplicationAdditions.h
    NSDataAdditions.h
    NSImageAdditions.h
    NSKeyedUnarchiverAdditions.h
    NSMutableArrayAdditions.h
    NSStringAdditions.h
    PeerProgressIndicatorCell.h
    PeerTableView.h
    PiecesView.h
    PortChecker.h
    PredicateEditorRowTemplateAny.h
    PrefsController.h
    PrefsWindow.h
    ProgressGradients.h
    ShareToolbarItem.h
    ShareTorrentFileHelper.h
    StatsWindowController.h
    StatusBarController.h
    StatusBarView.h
    Toolbar.h
    ToolbarSegmentedCell.h
    Torrent.h
    TorrentCell.h
    TorrentGroup.h
    TorrentTableView.h
    TrackerCell.h
    TrackerNode.h
    TrackerTableView.h
    URLSheetWindowController.h
    WebSeedTableView.h
)

set(${PROJECT_NAME}_XIB_FILES
    AboutWindow.xib
    BlocklistStatusWindow.xib
    FileRenameSheetController.xib
    FilterBar.xib
    InfoFileView.xib
    InfoPeersView.xib
    InfoTrackersView.xib
    InfoWindow.xib
    MessageWindow.xib
    StatsWindow.xib
    StatusBar.xib
    URLSheetWindow.xib
)
source_group(Resources FILES ${${PROJECT_NAME}_XIB_FILES})

set(${PROJECT_NAME}_RESOURCES
    Defaults.plist
    Credits.rtf
    sparkle_dsa_pub.pem
    ../COPYING
    Images/TransmissionDocument.icns
)
source_group(Resources FILES ${${PROJECT_NAME}_RESOURCES})

set_source_files_properties(${${PROJECT_NAME}_RESOURCES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
)

set(${PROJECT_NAME}_IMAGESETS
    ActionHover
    Bandwidth
    CleanupTemplate
    CompleteCheck
    CreateLarge
    DownArrowGroupTemplate
    DownArrowTemplate
    DownloadBadge
    EllipsisTemplate
    FavIcon
    GearshapeTemplate
    Globe
    Groups
    InfoActivity
    InfoFiles
    InfoGeneral
    InfoOptions
    InfoPeers
    InfoTracker
    Lock
    Magnet
    PauseHover
    PauseOff
    PauseOn
    PinTemplate
    PriorityControlHigh
    PriorityControlLow
    PriorityControlNormal
    PriorityHighTemplate
    PriorityLowTemplate
    PriorityNormalTemplate
    Remote
    ResumeHover
    ResumeNoWaitHover
    ResumeNoWaitOff
    ResumeNoWaitOn
    ResumeOff
    ResumeOn
    RevealHover
    RevealOff
    RevealOn
    ToolbarCreateTemplate
    ToolbarFilterTemplate
    ToolbarInfoTemplate
    ToolbarOpenTemplate
    ToolbarOpenWebTemplate
    ToolbarPauseAllTemplate
    ToolbarPauseSelectedTemplate
    ToolbarRemoveTemplate
    ToolbarResumeAllTemplate
    ToolbarResumeSelectedTemplate
    TortoiseTemplate
    Transfers
    UpArrowGroupTemplate
    UpArrowTemplate
    UploadBadge
    YingYangGroupTemplate
    YingYangTemplate
)

set(APPICON_NAME AppIcon)

set(IMAGE_ASSETS_DIR Images/Images.xcassets)
set(APPICONSET_DIR "${IMAGE_ASSETS_DIR}/${APPICON_NAME}.appiconset")

set(IMAGE_ASSETS
    ${IMAGE_ASSETS_DIR}/Contents.json
    ${APPICONSET_DIR}/Contents.json)

foreach(IMG IN LISTS ${PROJECT_NAME}_IMAGESETS)
    set(IMG_DIR "${IMAGE_ASSETS_DIR}/${IMG}.imageset")
    list(APPEND IMAGE_ASSETS
        ${IMG_DIR}/${IMG}.png
        ${IMG_DIR}/${IMG}@2x.png
        ${IMG_DIR}/Contents.json)
endforeach()

foreach(SIZE IN ITEMS 16 32 128 256 512)
    list(APPEND IMAGE_ASSETS
        ${APPICONSET_DIR}/icon_${SIZE}x${SIZE}.png
        ${APPICONSET_DIR}/icon_${SIZE}x${SIZE}@2x.png)
endforeach()

set(COMPILED_IMAGE_ASSETS
    "${CMAKE_CURRENT_BINARY_DIR}/${APPICON_NAME}.icns"
    "${CMAKE_CURRENT_BINARY_DIR}/Assets.car")

add_custom_command(
    OUTPUT
        ${COMPILED_IMAGE_ASSETS}
        ${APPICON_NAME}.Info.plist
    COMMAND ${ACTOOL_EXECUTABLE}
        --compile "${CMAKE_CURRENT_BINARY_DIR}"
        --platform macosx
        --minimum-deployment-target ${CMAKE_OSX_DEPLOYMENT_TARGET}
        --app-icon ${APPICON_NAME}
        --output-partial-info-plist "${CMAKE_CURRENT_BINARY_DIR}/${APPICON_NAME}.Info.plist"
        ${IMAGE_ASSETS_DIR}
    DEPENDS ${IMAGE_ASSETS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
source_group("Generated Files" FILES ${COMPILED_IMAGE_ASSETS} ${APPICON_NAME}.Info.plist)

list(APPEND ${PROJECT_NAME}_RESOURCES ${COMPILED_IMAGE_ASSETS})

set_source_files_properties(${COMPILED_IMAGE_ASSETS} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

set(${PROJECT_NAME}_LINGUAS
    da
    de
    en
    es
    fr
    it
    nl
    pt_PT
    ru
    tr
)

if(ENABLE_NLS)
    set(${PROJECT_NAME}_ENABLED_LINGUAS ${${PROJECT_NAME}_LINGUAS})
else()
    set(${PROJECT_NAME}_ENABLED_LINGUAS en)
endif()

set(${PROJECT_NAME}_LANG_XIB_FILES
    AddMagnetWindow.xib
    AddWindow.xib
    Creator.xib
    GlobalOptionsPopover.xib
    GroupRules.xib
    InfoActivityView.xib
    InfoGeneralView.xib
    InfoOptionsView.xib
    MainMenu.xib
    PrefsWindow.xib
)

set(${PROJECT_NAME}_LANG_STRINGS_FILES
    InfoPlist.strings
    Localizable.strings
)

set(${PROJECT_NAME}_NIB_FILES)
foreach(F ${${PROJECT_NAME}_XIB_FILES})
    get_filename_component(F_BASE "${F}" NAME_WE)
    set(F_NIB "${PROJECT_BINARY_DIR}/${F_BASE}.nib")
    tr_wrap_xib("${F}" "${F_NIB}")
    list(APPEND ${PROJECT_NAME}_NIB_FILES "${F_NIB}")
endforeach()

set_source_files_properties(${${PROJECT_NAME}_NIB_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

set(${PROJECT_NAME}_BASE_XIB_FILES)
set(${PROJECT_NAME}_BASE_NIB_FILES)
foreach(F ${${PROJECT_NAME}_LANG_XIB_FILES})
    set(F_XIB "Base.lproj/${F}")
    get_filename_component(F_BASE "${F}" NAME_WE)
    set(F_NIB "${PROJECT_BINARY_DIR}/Base.lproj/${F_BASE}.nib")
    tr_wrap_xib("${F_XIB}" "${F_NIB}")
    list(APPEND ${PROJECT_NAME}_BASE_XIB_FILES "${F_XIB}")
    list(APPEND ${PROJECT_NAME}_BASE_NIB_FILES "${F_NIB}")
    source_group(Resources/Base.lproj FILES ${F_XIB})
endforeach()

foreach(LANG ${${PROJECT_NAME}_ENABLED_LINGUAS})
    set(${PROJECT_NAME}_${LANG}_STRINGS_FILES)
    if(NOT LANG STREQUAL "en")
        foreach(F ${${PROJECT_NAME}_LANG_XIB_FILES})
            get_filename_component(F_BASE "${F}" NAME_WE)
            list(APPEND ${PROJECT_NAME}_${LANG}_STRINGS_FILES ${LANG}.lproj/${F_BASE}.strings)
        endforeach()
    endif()
    foreach(F ${${PROJECT_NAME}_LANG_STRINGS_FILES})
        list(APPEND ${PROJECT_NAME}_${LANG}_STRINGS_FILES ${LANG}.lproj/${F})
    endforeach()
    source_group(Resources/${LANG}.lproj FILES ${${PROJECT_NAME}_${LANG}_STRINGS_FILES})
endforeach()

include_directories(
    ${CMAKE_SOURCE_DIR}/libtransmission
    ${CMAKE_BINARY_DIR}/libtransmission
    ${PROJECT_SOURCE_DIR}/VDKQueue
)

add_definitions(
    -Wno-unused-parameter
    "-Wno-#warnings"
)

add_library(vdkqueue STATIC
    VDKQueue/VDKQueue.mm
    VDKQueue/VDKQueue.h
)

set_property(TARGET vdkqueue PROPERTY FOLDER "third-party")

set(BASE_NIB_FILES_RESOURCES_DIR Resources)
if(NOT CMAKE_GENERATOR STREQUAL Xcode)
    string(APPEND BASE_NIB_FILES_RESOURCES_DIR /Base.lproj)
endif()
set_source_files_properties(${${PROJECT_NAME}_BASE_NIB_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION ${BASE_NIB_FILES_RESOURCES_DIR})

foreach(LANG ${${PROJECT_NAME}_ENABLED_LINGUAS})
    list(APPEND ${PROJECT_NAME}_RESOURCES ${${PROJECT_NAME}_${LANG}_STRINGS_FILES})
    set(STRINGS_FILES_RESOURCES_DIR Resources)
    if(NOT CMAKE_GENERATOR STREQUAL Xcode)
        string(APPEND STRINGS_FILES_RESOURCES_DIR /${LANG}.lproj)
    endif()
    set_source_files_properties(${${PROJECT_NAME}_${LANG}_STRINGS_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION ${STRINGS_FILES_RESOURCES_DIR})
endforeach()

add_executable(${TR_NAME}-mac MACOSX_BUNDLE
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_XIB_FILES}
    ${${PROJECT_NAME}_NIB_FILES}
    ${${PROJECT_NAME}_BASE_XIB_FILES}
    ${${PROJECT_NAME}_BASE_NIB_FILES}
    ${${PROJECT_NAME}_RESOURCES}
)

find_library(SPARKLE_FRAMEWORK Sparkle PATHS ${PROJECT_SOURCE_DIR} NO_DEFAULT_PATHS)

target_link_libraries(${TR_NAME}-mac
    ${TR_NAME}
    vdkqueue
    ${SPARKLE_FRAMEWORK}
    "-framework AppKit"
    "-framework Carbon"
    "-framework Foundation"
    "-framework IOKit"
    "-framework Quartz"
    "-framework Security"
    "-weak_framework UserNotifications"
)

if(NOT CMAKE_GENERATOR STREQUAL Xcode)
    add_custom_command(TARGET ${TR_NAME}-mac POST_BUILD
        COMMAND ${CODESIGN_EXECUTABLE} -s - $<TARGET_BUNDLE_DIR:${TR_NAME}-mac>)
endif()

set(MAC_BUNDLE_NAME Transmission)

set_target_properties(${TR_NAME}-mac PROPERTIES
    OUTPUT_NAME ${MAC_BUNDLE_NAME}
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.m0k.transmission"
    MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Info.plist.in
    INSTALL_RPATH "@executable_path;@executable_path/../Frameworks"
)

add_subdirectory(QuickLookPlugin)

install(TARGETS ${TR_NAME}-mac DESTINATION Applications)

install(DIRECTORY TransmissionHelp DESTINATION Applications/${MAC_BUNDLE_NAME}.app/Contents/Resources)
tr_install_web(Applications/${MAC_BUNDLE_NAME}.app/Contents/Resources)

install(CODE "
    list(APPEND CMAKE_MODULE_PATH \"${CMAKE_SOURCE_DIR}/cmake\")
    include(TrMacros)
    include(GetPrerequisites)
    tr_fixup_bundle_item(\"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Applications/${MAC_BUNDLE_NAME}.app\" \"Contents/MacOS/${MAC_BUNDLE_NAME}\" \"${PROJECT_SOURCE_DIR}\")
")
