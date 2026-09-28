include_guard(GLOBAL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSON_C REQUIRED json-c>=0.15)
if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT")
    set(_tired_json_suffixes .a)
else()
    set(_tired_json_suffixes .so)
endif()
set(_tired_saved_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_tired_json_suffixes})
find_library(TIRED_JSON_C_${TIRED_DEPENDENCY_MODE}_LIBRARY NAMES json-c
    HINTS ${JSON_C_LIBRARY_DIRS} REQUIRED)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_tired_saved_suffixes})
add_library(tired_json_c UNKNOWN IMPORTED)
set_target_properties(tired_json_c PROPERTIES
    IMPORTED_LOCATION "${TIRED_JSON_C_${TIRED_DEPENDENCY_MODE}_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${JSON_C_INCLUDE_DIRS}"
    INTERFACE_COMPILE_OPTIONS "${JSON_C_CFLAGS_OTHER}")

if(TIRED_DEPENDENCY_MODE STREQUAL "DISTRIBUTION")
    # Distribution builds resolve installed libraries only and never load CPM.
    return()
endif()

if(NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
    if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(CPM_SOURCE_CACHE "$ENV{XDG_CACHE_HOME}/tired/cpm" CACHE PATH "External dependency source cache")
    elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(CPM_SOURCE_CACHE "$ENV{HOME}/.cache/tired/cpm" CACHE PATH "External dependency source cache")
    else()
        message(FATAL_ERROR "Set CPM_SOURCE_CACHE to an external writable source cache.")
    endif()
endif()

# Vendored build helper remains available without a configure-time download.
include("${CMAKE_CURRENT_LIST_DIR}/vendor/CPM.cmake")

# Add future dependencies here using CPMAddPackage with immutable GIT_TAG commits
# or URL + URL_HASH. Supply their static-library options explicitly, and preserve
# an offline path using prepopulated CPM_SOURCE_CACHE or CPM_<name>_SOURCE.
# Do not fetch the planned application dependencies until implementation needs them.
