cmake_minimum_required(VERSION 3.22)

project(tired VERSION 0.1.0 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "tired supports Linux x86-64 and ARM64 only.")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(_tired_arch x64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(_tired_arch arm64)
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(DEFINED TIRED_TARGET_ARCH AND NOT TIRED_TARGET_ARCH STREQUAL _tired_arch)
    message(FATAL_ERROR "Preset requires ${TIRED_TARGET_ARCH}, but the toolchain targets ${_tired_arch}.")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "tired requires Clang. Use a linux-clang preset for your architecture.")
endif()
if(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$" OR CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "Use a single-configuration Debug or Release build (see presets).")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static project/dependency libraries; the compiler still links glibc dynamically.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libraries as static archives" FORCE)
set(TIRED_DEPENDENCY_MODE "DIRECT" CACHE STRING "Dependency linkage: DIRECT or DISTRIBUTION")
set_property(CACHE TIRED_DEPENDENCY_MODE PROPERTY STRINGS DIRECT DISTRIBUTION)
if(TIRED_DEPENDENCY_MODE STREQUAL "DIRECT")
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
elseif(NOT TIRED_DEPENDENCY_MODE STREQUAL "DISTRIBUTION")
    message(FATAL_ERROR "TIRED_DEPENDENCY_MODE must be DIRECT or DISTRIBUTION.")
endif()

get_filename_component(_clang_real_path "${CMAKE_C_COMPILER}" REALPATH)
get_filename_component(_clang_bin_dir "${_clang_real_path}" DIRECTORY)
find_program(TIRED_LLD NAMES ld.lld HINTS "${_clang_bin_dir}" REQUIRED)
find_program(TIRED_READELF NAMES readelf llvm-readelf HINTS "${_clang_bin_dir}" REQUIRED)
# Applies to dependency executables and CMake's LTO probe as well.
string(APPEND CMAKE_EXE_LINKER_FLAGS " -fuse-ld=${TIRED_LLD} -static-libgcc")

include(CheckCSourceCompiles)
check_c_source_compiles("
    #include <features.h>
    #if !defined(__GLIBC__) || (!defined(__x86_64__) && !defined(__aarch64__))
    #error tired requires x86-64 or ARM64 glibc
    #endif
    int main(void) { return 0; }
" TIRED_HAS_SUPPORTED_GLIBC)
if(NOT TIRED_HAS_SUPPORTED_GLIBC)
    message(FATAL_ERROR "tired requires a Linux x86-64 or ARM64 glibc toolchain.")
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_error LANGUAGES C)
    if(NOT _ipo_supported)
        message(FATAL_ERROR "Release requires working Clang LTO: ${_ipo_error}")
    endif()
endif()

include(cmake/Dependencies.cmake)
include(GNUInstallDirs)
include(CTest)

option(TIRED_WARNINGS_AS_ERRORS "Treat first-party compiler warnings as errors" ON)
add_executable(tired src/main.c)
add_library(tired_build_options INTERFACE)
target_compile_options(tired_build_options INTERFACE
    -Wall -Wextra -Wpedantic -Wformat=2 -Wformat-security
    -Wstrict-prototypes -Wmissing-prototypes
    "$<$<BOOL:${TIRED_WARNINGS_AS_ERRORS}>:-Werror>"
)
add_library(tired_core STATIC src/util/value.c src/model/name.c src/model/model.c src/model/limit.c src/capture/capture.c)
target_compile_definitions(tired_core PRIVATE _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
target_include_directories(tired_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(tired_core PRIVATE tired_build_options)
target_link_libraries(tired PRIVATE tired_core tired_build_options)

add_custom_command(TARGET tired POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DBINARY=$<TARGET_FILE:tired>"
        "-DREADELF=${TIRED_READELF}"
        "-DDEPENDENCY_MODE=${TIRED_DEPENDENCY_MODE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckLinkage.cmake"
    VERBATIM
)

if(BUILD_TESTING)
    add_executable(tired_model_test tests/unit/model_test.c)
    target_link_libraries(tired_model_test PRIVATE tired_core tired_build_options)
    add_test(NAME tired.model COMMAND tired_model_test)
    set_tests_properties(tired.model PROPERTIES LABELS unit)
    add_executable(tired_capture_test tests/unit/capture_test.c)
    target_compile_definitions(tired_capture_test PRIVATE _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
    target_link_libraries(tired_capture_test PRIVATE tired_core tired_build_options)
    add_test(NAME tired.capture COMMAND tired_capture_test)
    set_tests_properties(tired.capture PROPERTIES LABELS unit)
    add_executable(tired_name_test tests/unit/name_test.c)
    target_link_libraries(tired_name_test PRIVATE tired_core tired_build_options)
    add_test(NAME tired.name COMMAND tired_name_test)
    set_tests_properties(tired.name PROPERTIES LABELS unit)
    add_executable(tired_value_test tests/unit/value_test.c)
    target_link_libraries(tired_value_test PRIVATE tired_core tired_build_options)
    add_test(NAME tired.value COMMAND tired_value_test)
    set_tests_properties(tired.value PROPERTIES LABELS unit)
    add_test(NAME tired.smoke COMMAND tired)
    set_tests_properties(tired.smoke PROPERTIES
        LABELS smoke
        PASS_REGULAR_EXPRESSION "tired: build scaffold; application not implemented yet."
    )
endif()

install(TARGETS tired RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Development archive only; product/deb packaging belongs to later implementation.
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_FILE_NAME "tired-${PROJECT_VERSION}-linux-clang-${_tired_arch}-${CMAKE_BUILD_TYPE}")
include(CPack)
