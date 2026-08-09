cmake_minimum_required(VERSION 3.16)
project(xtemplate LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-3.0)

set(GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/icons.gresource.xml")
set(GENERATED_C   "${CMAKE_CURRENT_SOURCE_DIR}/icons_resource.c")

add_custom_command(
  OUTPUT "${GENERATED_C}"
  COMMAND glib-compile-resources
          --target "${GENERATED_C}"
          --generate-source "${GRESOURCE_XML}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS "${GRESOURCE_XML}"
  VERBATIM
)

add_custom_target(icons_resources DEPENDS "${GENERATED_C}")

add_executable(xtemplate
  src/main.cpp
  src/xtemplate.cpp
  src/utils.cpp
  src/nodes.cpp
  src/block_canvas.cpp
  "${GENERATED_C}"
)

add_dependencies(xtemplate icons_resources)

target_include_directories(xtemplate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_options(xtemplate PRIVATE -Wall -Wextra -g)
target_link_options(xtemplate PRIVATE ${GTKMM_LDFLAGS})
target_include_directories(xtemplate PRIVATE ${GTKMM_INCLUDE_DIRS})
target_link_libraries(xtemplate PRIVATE ${GTKMM_LIBRARIES})

add_custom_command(
  TARGET xtemplate
  POST_BUILD
  COMMAND xtemplate
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
