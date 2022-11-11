project(trweb)

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
  assets/css/transmission-app.scss
)

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
  assets/img/type.svg
)

set(NODE_PATH "${CMAKE_CURRENT_BINARY_DIR}/node_modules")

add_custom_command(
  OUTPUT node_modules.timestamp
  COMMAND "${YARN_EXECUTABLE}"
    --non-interactive
    --frozen-lockfile
    --check-files
    --cwd "${CMAKE_CURRENT_SOURCE_DIR}"
    --modules-folder "${NODE_PATH}"
    install
  COMMAND "${CMAKE_COMMAND}" -E touch "${NODE_PATH}.timestamp"
  DEPENDS
    package.json
    yarn.lock
)

add_custom_command(
  OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/transmission-app.js"
    "${CMAKE_CURRENT_BINARY_DIR}/transmission-app.js.LICENSE.txt"
    "${CMAKE_CURRENT_BINARY_DIR}/transmission-app.js.map"
  COMMAND "${CMAKE_COMMAND}" -E env "NODE_PATH=${NODE_PATH}" "${YARN_EXECUTABLE}"
    --non-interactive
    --cwd "${CMAKE_CURRENT_SOURCE_DIR}"
    --modules-folder "${NODE_PATH}"
    webpack
    --resolve-modules "${NODE_PATH}"
    --resolve-loader-modules "${NODE_PATH}"
    build
    --output-path "${CMAKE_CURRENT_BINARY_DIR}"
  DEPENDS
    "${NODE_PATH}.timestamp"
    webpack.config.js
    ${TRWEB_IMGS}
    ${TRWEB_SRCS}
)

add_custom_target(${TR_NAME}-web ALL
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/transmission-app.js"
  SOURCES
    package.json
    webpack.config.js
    yarn.lock
    ${TRWEB_IMGS}
    ${TRWEB_SRCS}
)

set_property(DIRECTORY PROPERTY ADDITIONAL_CLEAN_FILES "${NODE_PATH}")

function(tr_install_web DST_DIR)
  install(
    DIRECTORY public_html
    DESTINATION ${DST_DIR})
  install(
    FILES
      "${CMAKE_CURRENT_BINARY_DIR}/transmission-app.js"
      "${CMAKE_CURRENT_BINARY_DIR}/transmission-app.js.LICENSE.txt"
      "${CMAKE_CURRENT_BINARY_DIR}/transmission-app.js.map"
    DESTINATION ${DST_DIR}/public_html)
endfunction()

tr_install_web(${CMAKE_INSTALL_DATAROOTDIR}/${TR_NAME})
