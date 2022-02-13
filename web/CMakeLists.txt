project(trweb)

file(GLOB_RECURSE TRWEB_ASSETS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/img/*.png
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/img/*.svg
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/img/favicon.ico
)

file(GLOB_RECURSE TRWEB_BYPRODUCTS
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/assets/css/main.*.css
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/assets/img/*.png
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/assets/img/*.svg
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/js/main.*.js
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/js/main.*.js.LICENSE.txt
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/js/main.*.js.map
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/js/runtime.*.js
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/js/runtime.*.js.map
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/js/vendors.*.js
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/js/vendors.*.js.map
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/*.ico
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/favicon.ico
  ${CMAKE_CURRENT_BINARY_DIR}/public_html/index.html
)

file(GLOB_RECURSE TRWEB_TMP_BUILD
  ${CMAKE_CURRENT_SOURCE_DIR}/public_html/assets/**/*
  ${CMAKE_CURRENT_SOURCE_DIR}/public_html/js/*
  ${CMAKE_CURRENT_SOURCE_DIR}/public_html/*
)

file(GLOB_RECURSE TRWEB_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.js
  ${CMAKE_CURRENT_SOURCE_DIR}/src/index.html
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/css/*.scss
)

add_custom_target(
  trweb ALL
  COMMENT "Building Web"
  COMMAND yarn install WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND yarn webpack --config "${CMAKE_CURRENT_SOURCE_DIR}/webpack.config.js" --context "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/public_html" "${CMAKE_CURRENT_BINARY_DIR}/public_html"
  COMMAND ${CMAKE_COMMAND} -E remove -f ${TRWEB_TMP_BUILD}
  BYPRODUCTS
    public_html/favicon.ico
    public_html/index.html
    public_html/assets/
    public_html/js/
  DEPENDS ${TRWEB_ASSETS}
  SOURCES ${TRWEB_SRCS}
)
