project(trmac)

include_directories(${CMAKE_SOURCE_DIR})

if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC ON)
else()
    add_definitions(-fobjc-arc)
endif()

find_program(IBTOOL_EXECUTABLE ibtool REQUIRED)
find_program(TIFFUTIL_EXECUTABLE tiffutil REQUIRED)
find_program(INSTALL_NAME_TOOL_EXECUTABLE install_name_tool REQUIRED)

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
endmacro()

function(tr_tiff_from_pngs OFILE)
    get_filename_component(ODIR "${OFILE}" DIRECTORY)
    add_custom_command(
        OUTPUT ${OFILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ODIR}
        COMMAND ${TIFFUTIL_EXECUTABLE} -cathidpicheck ${ARGN} -out ${OFILE}
        DEPENDS ${ARGN}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        VERBATIM
    )
endfunction()

set(${PROJECT_NAME}_SOURCES
    AboutWindowController.m
    AddMagnetWindowController.m
    AddWindowController.m
    BadgeView.m
    Badger.m
    BlocklistDownloader.m
    BlocklistDownloaderViewController.m
    BlocklistScheduler.m
    BonjourController.m
    ButtonToolbarItem.m
    ColorTextField.m
    Controller.m
    CreatorWindowController.m
    DragOverlayView.m
    DragOverlayWindow.m
    ExpandedPathToIconTransformer.m
    ExpandedPathToPathTransformer.m
    FileListNode.m
    FileNameCell.m
    FileOutlineController.m
    FileOutlineView.m
    FilePriorityCell.m
    FileRenameSheetController.m
    FilterBarController.m
    FilterBarView.m
    FilterButton.m
    GlobalOptionsPopoverViewController.m
    GroupToolbarItem.m
    GroupsController.m
    GroupsPrefsController.m
    InfoActivityViewController.m
    InfoFileViewController.m
    InfoGeneralViewController.m
    InfoOptionsViewController.m
    InfoPeersViewController.m
    InfoTabButtonBack.m
    InfoTabButtonCell.m
    InfoTextField.m
    InfoTrackersViewController.m
    InfoWindowController.m
    main.m
    MessageWindowController.m
    NSApplicationAdditions.m
    NSImageAdditions.m
    NSMutableArrayAdditions.m
    NSStringAdditions.m
    PeerProgressIndicatorCell.m
    PeerTableView.m
    PiecesView.m
    PortChecker.m
    PredicateEditorRowTemplateAny.m
    PrefsController.m
    PrefsWindow.m
    ProgressGradients.m
    ShareToolbarItem.m
    ShareTorrentFileHelper.m
    StatsWindowController.m
    StatusBarController.m
    StatusBarView.m
    ToolbarSegmentedCell.m
    Torrent.m
    TorrentCell.m
    TorrentGroup.m
    TorrentTableView.m
    TrackerCell.m
    TrackerNode.m
    TrackerTableView.m
    URLSheetWindowController.m
    WebSeedTableView.m
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
    InfoTabButtonBack.h
    InfoTabButtonCell.h
    InfoTextField.h
    InfoTrackersViewController.h
    InfoViewController.h
    InfoWindowController.h
    MessageWindowController.h
    NSApplicationAdditions.h
    NSImageAdditions.h
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

set(${PROJECT_NAME}_RESOURCES
    Defaults.plist
    Credits.rtf
    sparkle_dsa_pub.pem
    ../COPYING
    Images/Transmission.icns
    Images/TransmissionDocument.icns
)

set_source_files_properties(${${PROJECT_NAME}_RESOURCES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
)

set(${PROJECT_NAME}_HIDPI_IMAGES
    ActionHover
    ActionOn
    Bandwidth
    CleanupTemplate
    CompleteCheck
    CreateLarge
    DownArrowGroupTemplate
    DownArrowTemplate
    FavIcon
    Globe
    Groups
    GroupsNoneTemplate
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
    PurpleDotFlat
    RedDotFlat
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
    Transfers
    TurtleTemplate
    UpArrowGroupTemplate
    UpArrowTemplate
    YellowDotFlat
    YingYangGroupTemplate
    YingYangTemplate
)

foreach(IMG ${${PROJECT_NAME}_HIDPI_IMAGES})
    set(IMG_DIR "Images/Images.xcassets/${IMG}.imageset")
    list(APPEND ${PROJECT_NAME}_RESOURCES ${IMG_DIR}/${IMG}.png ${IMG_DIR}/${IMG}@2x.png)
    if(COMBINE_HIDPI_ARTWORK)
        list(APPEND ${PROJECT_NAME}_RESOURCES ${PROJECT_BINARY_DIR}/Images/${IMG}.tiff)
        tr_tiff_from_pngs(${PROJECT_BINARY_DIR}/Images/${IMG}.tiff ${IMG_DIR}/${IMG}.png ${IMG_DIR}/${IMG}@2x.png)
        set_source_files_properties(${PROJECT_BINARY_DIR}/Images/${IMG}.tiff PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
        )
    else()
        set_source_files_properties(${IMG_DIR}/${IMG}.png ${IMG_DIR}/${IMG}@2x.png PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
        )
    endif()
endforeach()

set(${PROJECT_NAME}_LODPI_IMAGES
    DownloadBadge
    QuitBadge
    UploadBadge
)

foreach(IMG ${${PROJECT_NAME}_LODPI_IMAGES})
    set(IMG_DIR "Images/Images.xcassets/${IMG}.imageset")
    list(APPEND ${PROJECT_NAME}_RESOURCES ${IMG_DIR}/${IMG}.png)
    set_source_files_properties(${IMG_DIR}/${IMG}.png PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
endforeach()

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

set(${PROJECT_NAME}_LINGUAS_XIB_FILES)
set(${PROJECT_NAME}_LINGUAS_NIB_FILES)
foreach(LANG ${${PROJECT_NAME}_ENABLED_LINGUAS})
    set(${PROJECT_NAME}_${LANG}_NIB_FILES)
    foreach(F ${${PROJECT_NAME}_LANG_XIB_FILES})
        set(F_XIB "${LANG}.lproj/${F}")
        get_filename_component(F_BASE "${F}" NAME_WE)
        set(F_NIB "${PROJECT_BINARY_DIR}/${LANG}.lproj/${F_BASE}.nib")
        tr_wrap_xib("${F_XIB}" "${F_NIB}")
        list(APPEND ${PROJECT_NAME}_LINGUAS_XIB_FILES "${F_XIB}")
        list(APPEND ${PROJECT_NAME}_LINGUAS_NIB_FILES "${F_NIB}")
        list(APPEND ${PROJECT_NAME}_${LANG}_NIB_FILES "${F_NIB}")
    endforeach()

    set(${PROJECT_NAME}_${LANG}_STRINGS_FILES)
    foreach(F ${${PROJECT_NAME}_LANG_STRINGS_FILES})
        list(APPEND ${PROJECT_NAME}_${LANG}_STRINGS_FILES ${LANG}.lproj/${F})
    endforeach()
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
    VDKQueue/VDKQueue.m
    VDKQueue/VDKQueue.h
)

foreach(LANG ${${PROJECT_NAME}_ENABLED_LINGUAS})
    list(APPEND ${PROJECT_NAME}_RESOURCES ${${PROJECT_NAME}_${LANG}_NIB_FILES} ${${PROJECT_NAME}_${LANG}_STRINGS_FILES})
    set_source_files_properties(${${PROJECT_NAME}_${LANG}_NIB_FILES} ${${PROJECT_NAME}_${LANG}_STRINGS_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources/${LANG}.lproj)
endforeach()

add_executable(${TR_NAME}-mac MACOSX_BUNDLE
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_XIB_FILES}
    ${${PROJECT_NAME}_NIB_FILES}
    ${${PROJECT_NAME}_LINGUAS_XIB_FILES}
    ${${PROJECT_NAME}_LINGUAS_NIB_FILES}
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
)

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
