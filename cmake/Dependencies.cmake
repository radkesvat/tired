include_guard(GLOBAL)

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

# Vendored CPM keeps the empty scaffold fully offline at configure/build time.
include("${CMAKE_CURRENT_LIST_DIR}/vendor/CPM.cmake")

# Add future dependencies here using CPMAddPackage with immutable GIT_TAG commits
# or URL + URL_HASH. Supply their static-library options explicitly, and preserve
# an offline path using prepopulated CPM_SOURCE_CACHE or CPM_<name>_SOURCE.
# Do not fetch the planned application dependencies until implementation needs them.
