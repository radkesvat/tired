# Keep source archives and distribution packages in their configured layout.
if(CPACK_TIRED_DEPENDENCY_MODE STREQUAL "DIRECT" AND CPACK_INSTALL_CMAKE_PROJECTS)
    set(CPACK_SET_DESTDIR OFF)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr/local)
endif()
