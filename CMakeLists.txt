cmake_minimum_required(VERSION 3.20)

project(breeze VERSION 0.1.0 LANGUAGES CXX)

option(BREEZE_BUILD_EXAMPLES "Build breeze examples" ON)
option(BREEZE_BUILD_CLI "Build breeze CLI" ON)
option(BREEZE_BUILD_TESTS "Build breeze tests" OFF)

add_library(breeze)
add_library(breeze::breeze ALIAS breeze)

target_compile_features(breeze PUBLIC cxx_std_20)

target_include_directories(breeze
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

file(GLOB_RECURSE BREEZE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

list(FILTER BREEZE_SOURCES EXCLUDE REGEX "/src/commands/.*\\.cpp$")

target_sources(breeze PRIVATE ${BREEZE_SOURCES})

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS breeze
  EXPORT breezeTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT breezeTargets
  FILE breezeTargets.cmake
  NAMESPACE breeze::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/breeze
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/breeze-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/breeze-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/breeze
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/breeze-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/breeze-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/breeze-config-version.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/breeze
)

if(BREEZE_BUILD_EXAMPLES)
  add_executable(breeze_example examples/basic.cpp)
  target_link_libraries(breeze_example PRIVATE breeze::breeze)
endif()

add_executable(breeze_app main.cpp)
target_link_libraries(breeze_app PRIVATE breeze::breeze)

if(BREEZE_BUILD_CLI)
  add_executable(breeze_cli src/commands/cli.cpp)
endif()

if(BREEZE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
