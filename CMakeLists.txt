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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
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

# install(EXPORT breezeTargets
#   FILE breezeTargets.cmake
#   NAMESPACE breeze::
#   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/breeze
# )
# Fixes the CMP0135 warning for 2025 standards
cmake_policy(SET CMP0135 NEW)

include(FetchContent)

FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3  # or a specific version you want
)

FetchContent_MakeAvailable(nlohmann_json)

# Link using the target name provided by the library
target_link_libraries(breeze PUBLIC nlohmann_json::nlohmann_json)

# Find OpenSSL to provide SHA1 implementation
find_package(OpenSSL REQUIRED)
if(OpenSSL_FOUND)
  message(STATUS "Found OpenSSL: ${OpenSSL_VERSION}")
  target_link_libraries(breeze PUBLIC OpenSSL::Crypto)
endif()

#install(EXPORT breezeTargets
#   FILE breezeTargets.cmake
#   NAMESPACE breeze::
#   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/breeze
#)

if(BREEZE_BUILD_EXAMPLES)
  add_executable(breeze_example examples/basic.cpp)
  target_link_libraries(breeze_example PRIVATE breeze::breeze)
endif()

add_executable(breeze_app main.cpp)
target_link_libraries(breeze_app PRIVATE breeze::breeze)

if(BREEZE_BUILD_CLI)
  add_executable(breeze_cli src/commands/cli.cpp)
  target_link_libraries(breeze_cli PRIVATE breeze::breeze)
endif()

if(BREEZE_BUILD_TESTS)
  enable_testing()
  add_executable(view_test tests/view_test.cpp)
  target_link_libraries(view_test PRIVATE breeze::breeze)
  add_test(NAME view_test COMMAND view_test)
  add_subdirectory(tests)
endif()

# Generate an aggregated routes header at configure-time so we can include a single header
# that pulls in all route headers. This scans routes/ for .hpp and creates generated/routes_all.hpp.
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

file(GLOB_RECURSE ROUTE_HEADERS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/routes"
  "${CMAKE_CURRENT_SOURCE_DIR}/routes/*.hpp"
)

# Require that route headers exist. If none found, stop configuration for a clearer error.
if(ROUTE_HEADERS STREQUAL "")
  message(FATAL_ERROR "No route headers found in ${CMAKE_CURRENT_SOURCE_DIR}/routes. Please add route .hpp files (e.g., routes/web.hpp).")
endif()

set(ROUTES_ALL_PATH "${GENERATED_DIR}/routes_all.hpp")
file(WRITE "${ROUTES_ALL_PATH}" "#pragma once\n\n// Auto-generated routes aggregator. Do not edit.\n\n#include <breeze/breeze.hpp>\n\n")
foreach(r ${ROUTE_HEADERS})
  file(APPEND "${ROUTES_ALL_PATH}" "#include \"../routes/${r}\"\n")
endforeach()

# Define a single register_routes function that delegates to each route file's register_routes
file(APPEND "${ROUTES_ALL_PATH}" "\ninline void register_routes(breeze::core::Application& app) {\n")
foreach(r ${ROUTE_HEADERS})
  get_filename_component(_name_we ${r} NAME_WE)
  # Expect each header provides routes_<name> inline namespace with register_routes
  file(APPEND "${ROUTES_ALL_PATH}" "    routes_${_name_we}::register_routes(app);\n")
endforeach()
file(APPEND "${ROUTES_ALL_PATH}" "}\n")

# Add generated include dir so the provider can `#include <routes_all.hpp>`
target_include_directories(breeze PUBLIC $<BUILD_INTERFACE:${GENERATED_DIR}>)
