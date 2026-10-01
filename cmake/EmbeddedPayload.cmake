# Identity is independent of checkout/install paths and timestamps. Changing an
# input regenerates the identifier even when the project version stays the same.
file(GLOB_RECURSE _identity_inputs CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src/*.c" "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/profiles/*.json" "${PROJECT_SOURCE_DIR}/cmake/*.cmake")
list(APPEND _identity_inputs CMakeLists.txt)
list(SORT _identity_inputs)
set(_identity "${PROJECT_VERSION};${CMAKE_SYSTEM_PROCESSOR};${TIRED_DEPENDENCY_MODE};${CMAKE_BUILD_TYPE};${CMAKE_C_COMPILER_VERSION};${CMAKE_C_FLAGS};${CMAKE_C_FLAGS_DEBUG};${CMAKE_C_FLAGS_RELEASE};${TIRED_SANITIZERS}")
foreach(_input IN LISTS _identity_inputs)
    file(SHA256 "${PROJECT_SOURCE_DIR}/${_input}" _hash)
    string(APPEND _identity "\n${_input}:${_hash}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${_input}")
endforeach()
string(SHA256 TIRED_BUILD_ID "${_identity}")
configure_file("${PROJECT_SOURCE_DIR}/cmake/build_identity.c.in"
    "${PROJECT_BINARY_DIR}/generated/build_identity.c" @ONLY)
target_sources(tired_core PRIVATE "${PROJECT_BINARY_DIR}/generated/build_identity.c")

if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT")
    target_compile_definitions(tired_core PRIVATE TIRED_EMBEDDED_PROFILES=1)
    target_compile_definitions(tired PRIVATE TIRED_PORTABLE=1)
    add_executable(tired_embed tools/embed.c src/util/sha256.c src/util/memory.c)
    target_include_directories(tired_embed PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(tired_embed PRIVATE tired_nettle tired_build_options)
    file(GLOB _profiles CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/profiles/*.json")
    list(SORT _profiles)
    add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/generated/profiles.c"
        COMMAND tired_embed "${PROJECT_BINARY_DIR}/generated/profiles.c" tired_embedded_profiles ${_profiles}
        DEPENDS tired_embed ${_profiles} VERBATIM)
    target_sources(tired_core PRIVATE "${PROJECT_BINARY_DIR}/generated/profiles.c")
    add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/generated/helper.c"
        COMMAND tired_embed "${PROJECT_BINARY_DIR}/generated/helper.c" tired_embedded_helper "$<TARGET_FILE:tired-helper>"
        DEPENDS tired_embed tired-helper VERBATIM)
    add_library(tired_portable STATIC src/portable/setup.c src/portable/cache.c
        "${PROJECT_BINARY_DIR}/generated/helper.c")
    target_include_directories(tired_portable PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(tired_portable PRIVATE tired_core tired_build_options)
    target_link_libraries(tired PRIVATE tired_portable)
endif()
