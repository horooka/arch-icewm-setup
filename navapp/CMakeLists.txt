cmake_minimum_required(VERSION 3.16)
project(navapp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(navapp src/main.cpp src/navdict.cpp)

find_package(X11 REQUIRED)

target_link_libraries(navapp PRIVATE ${X11_LIBRARIES})
target_include_directories(navapp PRIVATE ${X11_INCLUDE_DIR} include)
target_compile_options(navapp PRIVATE -DUI_IMPLEMENTATION -DUI_LINUX -g)

add_custom_command(
  TARGET navapp
  POST_BUILD
  COMMAND navapp
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
