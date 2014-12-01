project(trpo)

set(${PROJECT_NAME}_LINGUAS
    an
    ar
    ast
    az
    be
    be@latin
    bg
    bn
    bo
    br
    bs
    ca
    ca@valencia
    ceb
    ckb
    cs
    da
    de
    el
    en_AU
    en_CA
    en_GB
    eo
    es
    et
    eu
    fa
    fi
    fil
    fo
    fr
    ga
    gl
    gv
    he
    hi
    hr
    hu
    hy
    ia
    id
    is
    it
    ja
    ka
    kk
    ko
    ku
    ky
    li
    lt
    lv
    mk
    ml
    mr
    ms
    mt
    my
    nb
    nds
    nl
    nn
    oc
    pa
    pl
    pt
    pt_BR
    ro
    ru
    si
    sk
    sl
    sq
    sr
    sv
    sw
    ta_LK
    te
    th
    tl
    tr
    ur
    ug
    uk
    uz
    vi
    zh_CN
    zh_TW
)

set(GETTEXT_PACKAGE ${TR_NAME}-gtk)

if(ENABLE_NLS)
    set(${PROJECT_NAME}_ENABLED_LINGUAS ${${PROJECT_NAME}_LINGUAS})
else()
    set(${PROJECT_NAME}_ENABLED_LINGUAS)
endif()

set(${PROJECT_NAME}_MO_FILES)
foreach(LANG ${${PROJECT_NAME}_ENABLED_LINGUAS})
    set(msgfmt_INPUT_FILE ${LANG}.po)
    set(msgfmt_OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${GETTEXT_PACKAGE}-${LANG}.mo)

    add_custom_command(
        OUTPUT
            ${msgfmt_OUTPUT_FILE}
        COMMAND
            ${GETTEXT_MSGFMT_EXECUTABLE}
            --output-file=${msgfmt_OUTPUT_FILE}
            ${msgfmt_INPUT_FILE}
        WORKING_DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS
            ${msgfmt_INPUT_FILE}
    )

    list(APPEND ${PROJECT_NAME}_MO_FILES ${msgfmt_OUTPUT_FILE})

    install(FILES ${msgfmt_OUTPUT_FILE} DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${LANG}/LC_MESSAGES/ RENAME ${GETTEXT_PACKAGE}.mo)
endforeach()

if(${PROJECT_NAME}_MO_FILES)
    add_custom_target(${GETTEXT_PACKAGE}-po ALL DEPENDS ${${PROJECT_NAME}_MO_FILES})
endif()
