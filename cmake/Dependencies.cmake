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
pkg_check_modules(NETTLE REQUIRED nettle>=3.7)
find_library(TIRED_NETTLE_${TIRED_DEPENDENCY_MODE}_LIBRARY NAMES nettle
    HINTS ${NETTLE_LIBRARY_DIRS} REQUIRED)
find_library(TIRED_JSON_C_${TIRED_DEPENDENCY_MODE}_LIBRARY NAMES json-c
    HINTS ${JSON_C_LIBRARY_DIRS} REQUIRED)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_tired_saved_suffixes})
add_library(tired_json_c UNKNOWN IMPORTED)
set_target_properties(tired_json_c PROPERTIES
    IMPORTED_LOCATION "${TIRED_JSON_C_${TIRED_DEPENDENCY_MODE}_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${JSON_C_INCLUDE_DIRS}"
    INTERFACE_COMPILE_OPTIONS "${JSON_C_CFLAGS_OTHER}")
find_package(Threads REQUIRED)
add_library(tired_nettle UNKNOWN IMPORTED)
set_target_properties(tired_nettle PROPERTIES
    IMPORTED_LOCATION "${TIRED_NETTLE_${TIRED_DEPENDENCY_MODE}_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${NETTLE_INCLUDE_DIRS}"
    INTERFACE_COMPILE_OPTIONS "${NETTLE_CFLAGS_OTHER}")

pkg_check_modules(SYSTEMD REQUIRED libsystemd>=249)
pkg_check_modules(NCURSESW REQUIRED ncursesw>=6.2)
add_library(tired_curses INTERFACE)
if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT")
    find_library(TIRED_NCURSESW_ARCHIVE NAMES ncursesw REQUIRED)
    find_library(TIRED_TINFO_ARCHIVE NAMES tinfo REQUIRED)
    target_link_libraries(tired_curses INTERFACE "${TIRED_NCURSESW_ARCHIVE}" "${TIRED_TINFO_ARCHIVE}" "${CMAKE_DL_LIBS}")
else()
    target_link_libraries(tired_curses INTERFACE ${NCURSESW_LINK_LIBRARIES})
endif()
target_include_directories(tired_curses INTERFACE ${NCURSESW_INCLUDE_DIRS})
add_library(tired_systemd UNKNOWN IMPORTED)
if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT")
    if(NOT DEFINED TIRED_SYSTEMD_CACHE)
        if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
            set(_systemd_cache "$ENV{XDG_CACHE_HOME}/tired/systemd-252.39-1~deb12u2")
        else()
            set(_systemd_cache "$ENV{HOME}/.cache/tired/systemd-252.39-1~deb12u2")
        endif()
        set(TIRED_SYSTEMD_CACHE "${_systemd_cache}" CACHE PATH "External prepared libsystemd source/build cache")
    endif()
    set(_journal_header "${TIRED_SYSTEMD_CACHE}/source/src/libsystemd/sd-journal/journal-def.h")
    if(NOT EXISTS "${_journal_header}")
        message(FATAL_ERROR "Prepare the pinned static client first; it must support compact journals. See docs/systemd-dependency.md.")
    endif()
    file(READ "${_journal_header}" _journal_format)
    if(NOT _journal_format MATCHES "HEADER_INCOMPATIBLE_COMPACT")
        message(FATAL_ERROR "This old static client cannot read compact journals. Prepare the current pin and select its TIRED_SYSTEMD_CACHE; manager API baseline remains 249.")
    endif()
    set(_reader_build "${TIRED_SYSTEMD_CACHE}/build-${CMAKE_SYSTEM_PROCESSOR}-reader")
    if(NOT EXISTS "${_reader_build}/config.h")
        message(FATAL_ERROR "Prepare the journal reader without optional cryptographic backends first: cmake -P cmake/PrepareSystemd.cmake.")
    endif()
    file(READ "${_reader_build}/config.h" _reader_config)
    foreach(_feature IN ITEMS GCRYPT OPENSSL GNUTLS)
        if(NOT _reader_config MATCHES "#define HAVE_${_feature} 0([\r\n]|$)")
            message(FATAL_ERROR "The direct journal reader must disable ${_feature}; rerun cmake/PrepareSystemd.cmake.")
        endif()
    endforeach()
    unset(_tired_prepared_systemd CACHE)
    find_library(_tired_prepared_systemd NAMES systemd
        PATHS "${_reader_build}" NO_DEFAULT_PATH)
    set(TIRED_SYSTEMD_DIRECT_LIBRARY "${_tired_prepared_systemd}")
    if(NOT TIRED_SYSTEMD_DIRECT_LIBRARY)
        message(FATAL_ERROR "Static libsystemd is missing. Run cmake -P cmake/PrepareSystemd.cmake with a populated external cache; see docs/systemd-dependency.md. No shared fallback is permitted.")
    endif()
    set(_systemd_static_deps)
    foreach(_dependency IN ITEMS cap lzma lz4 zstd)
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
