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
pkg_check_modules(CRYPTO REQUIRED libcrypto>=3.0)
find_library(TIRED_CRYPTO_${TIRED_DEPENDENCY_MODE}_LIBRARY NAMES crypto
    HINTS ${CRYPTO_LIBRARY_DIRS} REQUIRED)
find_library(TIRED_JSON_C_${TIRED_DEPENDENCY_MODE}_LIBRARY NAMES json-c
    HINTS ${JSON_C_LIBRARY_DIRS} REQUIRED)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_tired_saved_suffixes})
add_library(tired_json_c UNKNOWN IMPORTED)
set_target_properties(tired_json_c PROPERTIES
    IMPORTED_LOCATION "${TIRED_JSON_C_${TIRED_DEPENDENCY_MODE}_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${JSON_C_INCLUDE_DIRS}"
    INTERFACE_COMPILE_OPTIONS "${JSON_C_CFLAGS_OTHER}")
find_package(Threads REQUIRED)
add_library(tired_crypto UNKNOWN IMPORTED)
set_target_properties(tired_crypto PROPERTIES
    IMPORTED_LOCATION "${TIRED_CRYPTO_${TIRED_DEPENDENCY_MODE}_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CRYPTO_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS};Threads::Threads")

pkg_check_modules(SYSTEMD REQUIRED libsystemd>=249)
add_library(tired_systemd UNKNOWN IMPORTED)
if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT")
    if(NOT DEFINED TIRED_SYSTEMD_CACHE)
        if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
            set(_systemd_cache "$ENV{XDG_CACHE_HOME}/tired/systemd-249.11-0ubuntu3.22")
        else()
            set(_systemd_cache "$ENV{HOME}/.cache/tired/systemd-249.11-0ubuntu3.22")
        endif()
        set(TIRED_SYSTEMD_CACHE "${_systemd_cache}" CACHE PATH "External prepared libsystemd source/build cache")
    endif()
    find_library(TIRED_SYSTEMD_DIRECT_LIBRARY NAMES systemd
        HINTS "${TIRED_SYSTEMD_CACHE}/build-${CMAKE_SYSTEM_PROCESSOR}" ${SYSTEMD_LIBRARY_DIRS})
    if(NOT TIRED_SYSTEMD_DIRECT_LIBRARY)
        message(FATAL_ERROR "Static libsystemd is missing. Run cmake -P cmake/PrepareSystemd.cmake with a populated external cache; see docs/systemd-dependency.md. No shared fallback is permitted.")
    endif()
    set(_systemd_static_deps)
    foreach(_dependency IN ITEMS cap gcrypt gpg-error lzma lz4 zstd)
        string(MAKE_C_IDENTIFIER "${_dependency}" _id)
        find_library(TIRED_SYSTEMD_${_id}_ARCHIVE NAMES "${_dependency}" REQUIRED)
        list(APPEND _systemd_static_deps "${TIRED_SYSTEMD_${_id}_ARCHIVE}")
    endforeach()
    set_target_properties(tired_systemd PROPERTIES
        IMPORTED_LOCATION "${TIRED_SYSTEMD_DIRECT_LIBRARY}"
        INTERFACE_LINK_LIBRARIES "${_systemd_static_deps};${CMAKE_DL_LIBS};Threads::Threads;rt")
else()
    set(CMAKE_FIND_LIBRARY_SUFFIXES .so)
    find_library(TIRED_SYSTEMD_DISTRIBUTION_LIBRARY NAMES systemd HINTS ${SYSTEMD_LIBRARY_DIRS} REQUIRED)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_tired_saved_suffixes})
    set_target_properties(tired_systemd PROPERTIES IMPORTED_LOCATION "${TIRED_SYSTEMD_DISTRIBUTION_LIBRARY}")
endif()
set_target_properties(tired_systemd PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SYSTEMD_INCLUDE_DIRS}"
    INTERFACE_COMPILE_OPTIONS "${SYSTEMD_CFLAGS_OTHER}")

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
