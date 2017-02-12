project(trmacql)

set(${PROJECT_NAME}_SOURCES
    ../NSApplicationAdditions.m
    ../NSStringAdditions.m
    GeneratePreviewForURL.m
    GenerateThumbnailForURL.m
    main.c
)

set(${PROJECT_NAME}_HEADERS
)

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

set(${PROJECT_NAME}_LANG_STRINGS_FILES
    Localizable.strings
)

set(${PROJECT_NAME}_RESOURCES
    style.css
)

set_source_files_properties(${${PROJECT_NAME}_RESOURCES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)

foreach(LANG ${${PROJECT_NAME}_ENABLED_LINGUAS})
    set(${PROJECT_NAME}_${LANG}_STRINGS_FILES)
    foreach(F ${${PROJECT_NAME}_LANG_STRINGS_FILES})
        list(APPEND ${PROJECT_NAME}_${LANG}_STRINGS_FILES ${LANG}.lproj/${F})
        list(APPEND ${PROJECT_NAME}_RESOURCES ${${PROJECT_NAME}_${LANG}_STRINGS_FILES})
        set_source_files_properties(${${PROJECT_NAME}_${LANG}_STRINGS_FILES} PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources/${LANG}.lproj)
    endforeach()
endforeach()

add_library(${TR_NAME}-mac-ql MODULE
    ${${PROJECT_NAME}_SOURCES}
    ${${PROJECT_NAME}_HEADERS}
    ${${PROJECT_NAME}_RESOURCES}
)

set(MAC_QL_BUNDLE_NAME QuickLookPlugin)

set_target_properties(${TR_NAME}-mac-ql PROPERTIES
    OUTPUT_NAME ${MAC_QL_BUNDLE_NAME}
    MACOSX_BUNDLE ON
    BUNDLE ON
    BUNDLE_EXTENSION qlgenerator
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/Info.plist.in"
    INSTALL_RPATH "@loader_path/../../../../../MacOS;@loader_path/../../../../../Frameworks"
)

target_include_directories(${TR_NAME}-mac-ql PRIVATE
    ..
)

target_link_libraries(${TR_NAME}-mac-ql
    ${TR_NAME}
    "-framework Foundation"
    "-framework CoreFoundation"
    "-framework CoreServices"
    "-framework AppKit"
    "-framework QuickLook"
)

install(TARGETS ${TR_NAME}-mac-ql DESTINATION Applications/${MAC_BUNDLE_NAME}.app/Contents/Library/QuickLook)

install(CODE "
    list(APPEND CMAKE_MODULE_PATH \"${CMAKE_SOURCE_DIR}/cmake\")
    include(TrMacros)
    include(GetPrerequisites)
    tr_fixup_bundle_item(\"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Applications/${MAC_BUNDLE_NAME}.app\" \"Contents/Library/QuickLook/${MAC_QL_BUNDLE_NAME}.qlgenerator/Contents/MacOS/${MAC_QL_BUNDLE_NAME}\" \"\")
")
