project(trweb)

file(GLOB_RECURSE TRWEB_ASSETS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/img/*.png
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/img/*.svg
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/img/favicon.ico
)

file(GLOB_RECURSE TRWEB_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.js
  ${CMAKE_CURRENT_SOURCE_DIR}/src/index.html
  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/css/*.scss
)

find_program(NODE node)
find_program(NODE_COREPACK corepack)

add_custom_target(
  trweb ALL
  COMMENT "Building Web"
  COMMAND node --version
  COMMAND corepack enable
  COMMAND yarn set version stable
  COMMAND yarn install WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMAND yarn webpack --config "${CMAKE_CURRENT_SOURCE_DIR}/webpack.config.js" --context "${CMAKE_CURRENT_SOURCE_DIR}"
  BYPRODUCTS
    public_html/d9466499ee75557f5395.ico
    public_html/favicon.ico
    public_html/index.html
    public_html/assets/
    public_html/js/
  DEPENDS ${TRWEB_ASSETS} ${NODE} ${NODE_COREPACK}
  SOURCES ${TRWEB_SRCS}
)
