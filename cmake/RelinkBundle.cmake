cmake_minimum_required(VERSION 3.22)
set(_root "${BUILD_DIRECTORY}/tired-${VERSION}-relink-${ARCHITECTURE}")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}/objects" "${_root}/libraries" "${_root}/licenses")
file(COPY_FILE "${BUILD_DIRECTORY}/CMakeFiles/tired.dir/src/main.c.o" "${_root}/objects/tired.o")
file(COPY_FILE "${BUILD_DIRECTORY}/CMakeFiles/tired-helper.dir/src/helper_main.c.o"
    "${_root}/objects/tired-helper.o")
file(COPY_FILE "${BUILD_DIRECTORY}/libtired_core.a" "${_root}/libraries/libtired_core.a")
include("${BUILD_DIRECTORY}/relink-libraries.cmake")
foreach(_archive IN LISTS TIRED_RELINK_ARCHIVES)
    file(COPY "${_archive}" DESTINATION "${_root}/libraries")
endforeach()
file(COPY "${BUILD_DIRECTORY}/dependency-notices/" DESTINATION "${_root}/licenses")
file(COPY_FILE "${BUILD_DIRECTORY}/relink-project.cmake" "${_root}/CMakeLists.txt")
file(COPY_FILE "${BUILD_DIRECTORY}/dependency-inventory.txt" "${_root}/build-info.txt")
file(COPY_FILE "${SOURCE_DIRECTORY}/LICENSE" "${_root}/LICENSE")
file(WRITE "${_root}/README.md" "# Relinking tired ${VERSION} (${ARCHITECTURE})\n\nUse the matching Clang/LLD versions listed in build-info.txt on this architecture.\nRelease objects contain LTO bitcode. Replace libraries/ archives with ABI-compatible\nmodified library builds, preserving their names, then run:\n\n    cmake -S . -B relink -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release\n    cmake --build relink\n    ./relink/tired --version\n    readelf -d relink/tired relink/tired-helper\n\nBoth executables must retain only glibc components as shared dependencies. Do not\nexecute or install the privileged helper merely to validate linking. Full upstream\ntired source, corresponding dependency sources and preparation instructions are\ncompanion release assets. Full dependency license notices are in licenses/.\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf
    "${BUILD_DIRECTORY}/tired-${VERSION}-relink-${ARCHITECTURE}.tar.gz"
    "tired-${VERSION}-relink-${ARCHITECTURE}"
    WORKING_DIRECTORY "${BUILD_DIRECTORY}" RESULT_VARIABLE _packed)
if(NOT _packed EQUAL 0)
    message(FATAL_ERROR "Cannot create relinking material.")
endif()
