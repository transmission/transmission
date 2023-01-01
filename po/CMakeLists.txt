set(LINGUAS
    an
    ar
    ast
    az
    be
    bg
    bn
    br
    bs
    ca
    ca@valencia
    ceb
    ckb
    cs
    cy
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
    fr_CA
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
    jbo
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
    nb
    nl
    nn
    oc
    pl
    pt
    pt_BR
    pt_PT
    ro
    ru
    si
    sk
    sl
    sq
    sr
    sv
    ta
    te
    th
    tr
    ug
    uk
    ur
    uz
    vi
    zh_CN
    zh_HK
    zh_TW)

set(GETTEXT_PACKAGE ${TR_NAME}-gtk)

if(ENABLE_NLS)
    set(ENABLED_LINGUAS ${LINGUAS})
else()
    set(ENABLED_LINGUAS)
endif()

set(MO_FILES)
foreach(LANG ${ENABLED_LINGUAS})
    set(msgfmt_INPUT_FILE ${LANG}.po)
    set(msgfmt_OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${GETTEXT_PACKAGE}-${LANG}.mo)

    add_custom_command(
        OUTPUT ${msgfmt_OUTPUT_FILE}
        COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --output-file=${msgfmt_OUTPUT_FILE} ${msgfmt_INPUT_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${msgfmt_INPUT_FILE})

    list(APPEND MO_FILES ${msgfmt_OUTPUT_FILE})

    install(
        FILES ${msgfmt_OUTPUT_FILE}
        DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${LANG}/LC_MESSAGES/
        RENAME ${GETTEXT_PACKAGE}.mo)
endforeach()

if(MO_FILES)
    add_custom_target(${GETTEXT_PACKAGE}-po
        ALL
        DEPENDS ${MO_FILES})
endif()
