set(TRWEB_SRCS
    src/about-dialog.js
    src/action-manager.js
    src/alert-dialog.js
    src/context-menu.js
    src/file-row.js
    src/formatter.js
    src/inspector.js
    src/main.js
    src/move-dialog.js
    src/notifications.js
    src/open-dialog.js
    src/overflow-menu.js
    src/prefs-dialog.js
    src/prefs.js
    src/remote.js
    src/remove-dialog.js
    src/rename-dialog.js
    src/shortcuts-dialog.js
    src/statistics-dialog.js
    src/torrent.js
    src/torrent-row.js
    src/transmission.js
    src/utils.js
    assets/css/transmission-app.scss)

set(TRWEB_IMGS
    assets/img/box.svg
    assets/img/film.svg
    assets/img/modern-tortoise-blue.png
    assets/img/modern-tortoise.png
    assets/img/chevron-down.svg
    assets/img/chevron-up.svg
    assets/img/horizontal-rule.svg
    assets/img/lock-fill.svg
    assets/img/logo.png
    assets/img/image.svg
    assets/img/magnet.svg
    assets/img/music.svg
    assets/img/file-text.svg
    assets/img/package.svg
    assets/img/folder.svg
    assets/img/pause-circle-active.svg
    assets/img/pause-circle-idle.svg
    assets/img/play-circle-active.svg
    assets/img/play-circle-idle.svg
    assets/img/router.svg
    assets/img/type.svg)

add_custom_target(trweb ALL
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/package.json" "${CMAKE_CURRENT_BINARY_DIR}/package.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/yarn.lock" "${CMAKE_CURRENT_BINARY_DIR}/yarn.lock"
    COMMAND yarn install
    COMMAND yarn webpack --config "${CMAKE_CURRENT_SOURCE_DIR}/webpack.config.js" --context "${CMAKE_CURRENT_SOURCE_DIR}"
    BYPRODUCTS
        public_html/transmission-app.js
        public_html/transmission-app.js.LICENSE.txt
    DEPENDS ${TRWEB_IMGS}
    SOURCES ${TRWEB_SRCS})
