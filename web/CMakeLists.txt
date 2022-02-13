project(trweb)

set(TRWEB_SRCS
  src/about-dialog.js
  src/action-manager.js
  src/alert-dialog.js
  src/context-menu.js
  src/file-row.js
  src/formatter.js
  src/inspector.js
  src/index.js
  src/index.html
  src/labels-dialog.js
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
  src/assets/css/index.scss
  src/assets/css/mixins.scss
  src/assets/css/variables.scss
)

set(TRWEB_ASSETS
  src/assets/img/analytics.svg
  src/assets/img/application-x-executable.png
  src/assets/img/audio-x-generic.png
  src/assets/img/blue-turtle.png
  src/assets/img/checkered-flag.svg
  src/assets/img/chevron-down.svg
  src/assets/img/chevron-up.svg
  src/assets/img/diagram-3-fill.svg
  src/assets/img/favicon.ico
  src/assets/img/files.svg
  src/assets/img/folder.png
  src/assets/img/font-x-generic.png
  src/assets/img/gear-fill.svg
  src/assets/img/horizontal-rule.svg
  src/assets/img/image-x-generic.png
  src/assets/img/lock-fill.svg
  src/assets/img/logo.png
  src/assets/img/magnet.svg
  src/assets/img/package-x-generic.png
  src/assets/img/pause-circle-active.svg
  src/assets/img/pause-circle-idle.svg
  src/assets/img/play-circle-active.svg
  src/assets/img/play-circle-idle.svg
  src/assets/img/router.svg
  src/assets/img/search.svg
  src/assets/img/team.svg
  src/assets/img/text-x-generic.png
  src/assets/img/three-dots-vertical.svg
  src/assets/img/toolbar-close.png
  src/assets/img/toolbar-folder.png
  src/assets/img/toolbar-info.png
  src/assets/img/toolbar-pause.png
  src/assets/img/toolbar-start.png
  src/assets/img/turtle.png
  src/assets/img/up-and-down-arrows.svg
  src/assets/img/video-x-generic.png
)

add_custom_target(
  trweb ALL
  COMMENT "Building Web"
  COMMAND yarn install WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND yarn webpack --config "${CMAKE_CURRENT_SOURCE_DIR}/webpack.config.js" --context "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/public_html" "${CMAKE_CURRENT_BINARY_DIR}/public_html"
  BYPRODUCTS
    public_html/favicon.ico
    public_html/index.html
  DEPENDS ${TRWEB_ASSETS}
  SOURCES ${TRWEB_SRCS}
)
