# Local unsigned source preparation; this recipe never signs or uploads.
cmake_minimum_required(VERSION 3.22)
foreach(_required IN ITEMS UPSTREAM_SOURCE_ARCHIVE OUTPUT_DIRECTORY VERSION)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Set ${_required} for Ubuntu source-package preparation.")
    endif()
endforeach()
if(NOT VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$" OR
   NOT IS_ABSOLUTE "${UPSTREAM_SOURCE_ARCHIVE}" OR
   NOT IS_ABSOLUTE "${OUTPUT_DIRECTORY}" OR
   NOT EXISTS "${UPSTREAM_SOURCE_ARCHIVE}")
    message(FATAL_ERROR "Use an existing absolute source archive, absolute output and release version.")
endif()
find_program(_dpkg_buildpackage dpkg-buildpackage REQUIRED)
foreach(_series IN ITEMS jammy noble)
    set(_destination "${OUTPUT_DIRECTORY}/${_series}")
    if(EXISTS "${_destination}")
        message(FATAL_ERROR "Choose a fresh output directory; ${_destination} already exists.")
    endif()
    file(MAKE_DIRECTORY "${_destination}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${UPSTREAM_SOURCE_ARCHIVE}"
        WORKING_DIRECTORY "${_destination}" COMMAND_ERROR_IS_FATAL ANY)
    set(_source "${_destination}/tired-${VERSION}")
    if(NOT EXISTS "${_source}/debian/control")
        message(FATAL_ERROR "Archive must contain tired-${VERSION} and its maintained Debian recipe.")
    endif()
    file(COPY_FILE "${UPSTREAM_SOURCE_ARCHIVE}"
        "${_destination}/tired_${VERSION}.orig.tar.gz")
    file(READ "${_source}/debian/changelog" _changelog)
    string(FIND "${_changelog}" "tired (${VERSION}-" _header)
    if(NOT _header EQUAL 0)
        message(FATAL_ERROR "Archive changelog does not describe release ${VERSION}.")
    endif()
    string(REGEX REPLACE "^tired [(][^)]*[)] [^;]*;"
        "tired (${VERSION}-1~${_series}1) ${_series};" _series_changelog "${_changelog}")
    file(WRITE "${_source}/debian/changelog" "${_series_changelog}")
    # A fresh extracted source tree needs no debhelper cleanup. Source-only mode
    # follows --no-pre-clean explicitly because that flag otherwise implies binary.
    execute_process(COMMAND "${_dpkg_buildpackage}" --no-pre-clean -S -d -us -uc -sa
        WORKING_DIRECTORY "${_source}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
message(STATUS "Prepared unsigned jammy/noble source uploads in ${OUTPUT_DIRECTORY}; nothing was uploaded.")
