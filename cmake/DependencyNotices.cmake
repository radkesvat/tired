set(_notice_directory "${PROJECT_BINARY_DIR}/dependency-notices")
file(REMOVE_RECURSE "${_notice_directory}")
file(MAKE_DIRECTORY "${_notice_directory}")
set(_inventory "tired=${PROJECT_VERSION}\narchitecture=${_package_arch}\nlinkage=${TIRED_DEPENDENCY_MODE}\ncompiler=${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}\ncmake=${CMAKE_VERSION}\njson-c=${JSON_C_VERSION}\nnettle=${NETTLE_VERSION}\nlibsystemd-api=${SYSTEMD_VERSION}\nncursesw=${NCURSESW_VERSION}\nglibc=dynamic\n")
if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT")
    string(APPEND _inventory "libsystemd-direct-source=systemd 252.39-1~deb12u2\n")
endif()
find_program(_dpkg_query dpkg-query)
find_program(_dpkg dpkg)
if(_dpkg)
    execute_process(COMMAND "${_dpkg}" --print-architecture OUTPUT_VARIABLE _native_arch
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
foreach(_package IN ITEMS libsystemd0 libncursesw6 libtinfo6 libjson-c5 nettle-dev
    libcap2 liblzma5 liblz4-1 libzstd1 libgcc-s1)
    if(_dpkg_query)
        execute_process(COMMAND "${_dpkg_query}" -W
            "-f=\${binary:Package} \${Version} source=\${source:Package} \${source:Version}\n"
            "${_package}:${_native_arch}" RESULT_VARIABLE _known OUTPUT_VARIABLE _version ERROR_QUIET)
        if(_known EQUAL 0)
            string(APPEND _inventory "${_version}")
        endif()
    endif()
    if(EXISTS "/usr/share/doc/${_package}/copyright")
        configure_file("/usr/share/doc/${_package}/copyright"
            "${_notice_directory}/${_package}.copyright" COPYONLY)
    endif()
endforeach()
if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT" AND EXISTS "${TIRED_SYSTEMD_CACHE}/source/debian/copyright")
    configure_file("${TIRED_SYSTEMD_CACHE}/source/debian/copyright"
        "${_notice_directory}/libsystemd-direct.copyright" COPYONLY)
endif()
execute_process(COMMAND "${CMAKE_C_COMPILER}" --print-libgcc-file-name
    RESULT_VARIABLE _support_known OUTPUT_VARIABLE _support_archive
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_support_known EQUAL 0 AND EXISTS "${_support_archive}")
    file(REAL_PATH "${_support_archive}" _support_archive)
    string(APPEND _inventory "compiler-support-archive=${_support_archive}\n")
    if(_dpkg_query)
        execute_process(COMMAND "${_dpkg_query}" -S "${_support_archive}"
            RESULT_VARIABLE _owner_known OUTPUT_VARIABLE _archive_owner ERROR_QUIET)
        if(_owner_known EQUAL 0)
            string(REGEX REPLACE ": /[^\n]+\n$" "" _archive_owner "${_archive_owner}")
            execute_process(COMMAND "${_dpkg_query}" -W
                "-f=\${binary:Package} \${Version} source=\${source:Package} \${source:Version}\n"
                "${_archive_owner}" OUTPUT_VARIABLE _support_version ERROR_QUIET)
            string(APPEND _inventory "compiler-support-package=${_support_version}")
            string(REGEX REPLACE ":[^:]+$" "" _support_package "${_archive_owner}")
            if(EXISTS "/usr/share/doc/${_support_package}/copyright")
                configure_file("/usr/share/doc/${_support_package}/copyright"
                    "${_notice_directory}/${_support_package}.copyright" COPYONLY)
            endif()
        endif()
    endif()
endif()
foreach(_license IN ITEMS LGPL-2.1 LGPL-3 GPL-2 GPL-3)
    if(EXISTS "/usr/share/common-licenses/${_license}")
        configure_file("/usr/share/common-licenses/${_license}"
            "${_notice_directory}/${_license}.txt" COPYONLY)
    endif()
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/cmake/vendor/CPM.LICENSE"
    "${_notice_directory}/CPM.LICENSE" COPYONLY)
file(WRITE "${PROJECT_BINARY_DIR}/dependency-inventory.txt" "${_inventory}")
install(FILES "${PROJECT_BINARY_DIR}/dependency-inventory.txt"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tired")
install(DIRECTORY "${_notice_directory}/" DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses")
