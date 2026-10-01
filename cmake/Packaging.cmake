set(CPACK_PACKAGE_NAME tired)
set(CPACK_PACKAGE_VENDOR radkesvat)
set(CPACK_PACKAGE_CONTACT "radkesvat <asedmosa66@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Persistent systemd service creation and management")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/radkesvat/tired")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_SET_DESTDIR ON)
set(CPACK_TIRED_DEPENDENCY_MODE "${TIRED_DEPENDENCY_MODE}")
set(CPACK_PROJECT_CONFIG_FILE "${PROJECT_SOURCE_DIR}/cmake/PackageLayout.cmake")
set(CPACK_PACKAGE_RELOCATABLE OFF)
if(_tired_arch STREQUAL "x64")
    set(_package_arch amd64)
else()
    set(_package_arch arm64)
endif()
configure_file("${PROJECT_SOURCE_DIR}/cmake/release.txt.in"
    "${PROJECT_BINARY_DIR}/release.txt" @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/release.txt" DESTINATION "${CMAKE_INSTALL_DATADIR}/tired")
configure_file("${PROJECT_SOURCE_DIR}/snap/local/meta.yaml.in" "${PROJECT_BINARY_DIR}/snap.yaml" @ONLY)
include("${PROJECT_SOURCE_DIR}/cmake/DependencyNotices.cmake")
if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT" AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(TIRED_RELINK_ARCHIVES "${TIRED_JSON_C_DIRECT_LIBRARY};${TIRED_NETTLE_DIRECT_LIBRARY};${TIRED_SYSTEMD_DIRECT_LIBRARY};${_systemd_static_deps};${TIRED_NCURSESW_ARCHIVE};${TIRED_TINFO_ARCHIVE}")
    set(TIRED_RELINK_LIBRARIES "")
    foreach(_archive IN LISTS TIRED_RELINK_ARCHIVES)
        get_filename_component(_name "${_archive}" NAME)
        list(APPEND TIRED_RELINK_LIBRARIES "${_name}")
    endforeach()
    file(WRITE "${PROJECT_BINARY_DIR}/relink-libraries.cmake" "set(TIRED_RELINK_ARCHIVES \"${TIRED_RELINK_ARCHIVES}\")\n")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/RelinkProject.cmake.in"
        "${PROJECT_BINARY_DIR}/relink-project.cmake" @ONLY)
    add_custom_target(relink-bundle
        COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIRECTORY=${PROJECT_BINARY_DIR}"
            "-DSOURCE_DIRECTORY=${PROJECT_SOURCE_DIR}" "-DVERSION=${PROJECT_VERSION}"
            "-DARCHITECTURE=${_package_arch}" -P "${PROJECT_SOURCE_DIR}/cmake/RelinkBundle.cmake"
        DEPENDS tired tired-helper VERBATIM)
    add_custom_target(snap-package
        COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIRECTORY=${PROJECT_BINARY_DIR}"
            "-DVERSION=${PROJECT_VERSION}" "-DARCHITECTURE=${_package_arch}"
            -P "${PROJECT_SOURCE_DIR}/cmake/SnapPackage.cmake"
        DEPENDS tired tired-helper VERBATIM)
endif()
install(FILES "${PROJECT_SOURCE_DIR}/CONTRIBUTING.md" "${PROJECT_SOURCE_DIR}/SECURITY.md"
    "${PROJECT_SOURCE_DIR}/CHANGELOG.md" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/wiki/" DESTINATION "${CMAKE_INSTALL_DOCDIR}/wiki")
if(TIRED_DEPENDENCY_MODE STREQUAL "DISTRIBUTION")
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_package_arch}")
    set(CPACK_DEBIAN_PACKAGE_SECTION admin)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "radkesvat <asedmosa66@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd (>= 249), sudo")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "dbus-user-session, libpam-systemd")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_FILE_NAME "tired-${PROJECT_VERSION}-linux-${_package_arch}")
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "tired-${PROJECT_VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/;/install/;/[.]git/;/[.]aws/;/[.]git/info/;"
    "/AGENTS[.]md$;/[^/]*[pP][lL][aA][nN][.]md$;/implementation-(progress|questions)[.]md$;"
    "/review_[0-9]+[.]md$;/packaging/;"
    "~$;[.]swp$;/controller-test-[^/]+/;"
)
include(CPack)
