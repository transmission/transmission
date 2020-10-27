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
  style/transmission-app.scss
)

set(TRWEB_IMGS
  style/images/analytics.svg
  style/images/application-x-executable.png
  style/images/audio-x-generic.png
  style/images/blue-turtle.png
  style/images/checkered-flag.svg
  style/images/chevron-down.svg
  style/images/chevron-up.svg
  style/images/diagram-3-fill.svg
  style/images/files.svg
  style/images/folder.png
  style/images/font-x-generic.png
  style/images/gear-fill.svg
  style/images/horizontal-rule.svg
  style/images/image-x-generic.png
  style/images/lock-fill.svg
  style/images/logo.png
  style/images/package-x-generic.png
  style/images/pause-circle-active.svg
  style/images/pause-circle-idle.svg
  style/images/play-circle-active.svg
  style/images/play-circle-idle.svg
  style/images/router.svg
  style/images/search.svg
  style/images/team.svg
  style/images/text-x-generic.png
  style/images/three-dots-vertical.svg
  style/images/toolbar-close.png
  style/images/toolbar-folder.png
  style/images/toolbar-info.png
  style/images/toolbar-pause.png
  style/images/toolbar-start.png
  style/images/turtle.png
  style/images/up-and-down-arrows.svg
  style/images/video-x-generic.png
)

add_custom_target(
  trweb ALL
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/package.json" "${CMAKE_CURRENT_BINARY_DIR}/package.json"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/yarn.lock" "${CMAKE_CURRENT_BINARY_DIR}/yarn.lock"
  COMMAND yarn install
  COMMAND yarn webpack --config "${CMAKE_CURRENT_SOURCE_DIR}/webpack.config.js" --context "${CMAKE_CURRENT_SOURCE_DIR}"
  BYPRODUCTS
    public_html/transmission-app.js
    public_html/transmission-app.js.LICENSE.txt
  DEPENDS ${TRWEB_IMGS}
  SOURCES ${TRWEB_SRCS}
)
