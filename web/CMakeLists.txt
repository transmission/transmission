project(trweb)

file(GLOB trweb_srcs "src/*.js" "style/*.scss")
file(GLOB trweb_imgs "style/images/*.png")

add_custom_target(
  trweb ALL
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/package.json" "${CMAKE_CURRENT_BINARY_DIR}/package.json"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/yarn.lock" "${CMAKE_CURRENT_BINARY_DIR}/yarn.lock"
  COMMAND yarn install
  COMMAND yarn webpack --config "${CMAKE_CURRENT_SOURCE_DIR}/webpack.config.js" --context "${CMAKE_CURRENT_SOURCE_DIR}"
  BYPRODUCTS
    public_html/transmission-app.js
    public_html/transmission-app.js.LICENSE.txt
  DEPENDS ${trweb_imgs}
  SOURCES ${trweb_srcs}
)
