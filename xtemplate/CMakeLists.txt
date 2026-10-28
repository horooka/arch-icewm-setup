cmake_minimum_required(VERSION 3.16)
project(xtemplate LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The rendering engine and CLI have no GTK/glib dependency. The GUI is
# optional so lean CLI builds do not pull in gtkmm.
option(XTEMPLATE_BUILD_GUI "Build the GTKmm3 GUI" ON)

add_library(xtemplate_core STATIC
  src/nodes.cpp
  src/cli_meta.cpp
)
target_include_directories(xtemplate_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(xtemplate_core PRIVATE -Wall -Wextra -g)

if (XTEMPLATE_BUILD_GUI)
  # Standalone lean CLI alongside the GUI binary.
  add_executable(xtemplate-cli
    src/main_cli.cpp
    src/cli.cpp
  )
  target_link_libraries(xtemplate-cli PRIVATE xtemplate_core)
  target_compile_options(xtemplate-cli PRIVATE -Wall -Wextra -g)

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
    src/cli.cpp
    "${GENERATED_C}"
  )
  add_dependencies(xtemplate icons_resources)

  target_include_directories(xtemplate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_options(xtemplate PRIVATE -Wall -Wextra -g)
  target_link_options(xtemplate PRIVATE ${GTKMM_LDFLAGS})
  target_include_directories(xtemplate PRIVATE ${GTKMM_INCLUDE_DIRS})
  target_link_libraries(xtemplate PRIVATE xtemplate_core ${GTKMM_LIBRARIES})
else()
  # Lean build: the installed name is simply `xtemplate`.
  add_executable(xtemplate
    src/main_cli.cpp
    src/cli.cpp
  )
  target_link_libraries(xtemplate PRIVATE xtemplate_core)
  target_compile_options(xtemplate PRIVATE -Wall -Wextra -g)
endif()
