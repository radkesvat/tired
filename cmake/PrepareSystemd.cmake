# Explicit dependency preparation only. Normal project configuration never fetches.
cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED TIRED_SYSTEMD_CACHE)
    if(DEFINED ENV{XDG_CACHE_HOME} AND NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(TIRED_SYSTEMD_CACHE "$ENV{XDG_CACHE_HOME}/tired/systemd-252.39-1~deb12u2")
    else()
        set(TIRED_SYSTEMD_CACHE "$ENV{HOME}/.cache/tired/systemd-252.39-1~deb12u2")
    endif()
endif()
if(NOT IS_ABSOLUTE "${TIRED_SYSTEMD_CACHE}")
    message(FATAL_ERROR "TIRED_SYSTEMD_CACHE must be an absolute external cache path.")
endif()
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/.." _project)
cmake_path(IS_PREFIX _project "${TIRED_SYSTEMD_CACHE}" NORMALIZE _inside)
if(_inside)
    message(FATAL_ERROR "Dependency caches must remain outside the project.")
endif()
find_program(_uname uname REQUIRED)
execute_process(COMMAND "${_uname}" -m OUTPUT_VARIABLE _arch OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT _arch MATCHES "^(x86_64|aarch64)$")
    message(FATAL_ERROR "Static dependency preparation supports native x86_64/aarch64 only.")
endif()
find_program(_meson meson REQUIRED)
find_program(_ninja ninja REQUIRED)
find_program(_dpkg_source dpkg-source REQUIRED)
find_program(_clang clang REQUIRED)
find_program(_false false REQUIRED)
file(MAKE_DIRECTORY "${TIRED_SYSTEMD_CACHE}")
set(_files
    systemd_252.39-1~deb12u2.dsc
    systemd_252.39.orig.tar.gz
    systemd_252.39-1~deb12u2.debian.tar.xz)
set(_hashes
    f2f952ae61fd40f1ef3ee48c8721e23eaaec5396ef5f1b0c2e138d78fade9c6e
    08a54a6c4d4cf969fc025eaa8922a55d6bc458100c242510b56c96e7d72af1c5
    27c548c678593cbe82e1701fc480bfe56e99b8f8750cbc09e720b6aa4fcbffaf)
foreach(_index RANGE 0 2)
    list(GET _files ${_index} _file)
    list(GET _hashes ${_index} _expected)
    set(_path "${TIRED_SYSTEMD_CACHE}/${_file}")
    if(NOT EXISTS "${_path}")
        if(NOT TIRED_ALLOW_DOWNLOAD)
            message(FATAL_ERROR "Missing ${_path}. Supply pinned archives or explicitly use -DTIRED_ALLOW_DOWNLOAD=ON.")
        endif()
        file(DOWNLOAD "https://deb.debian.org/debian/pool/main/s/systemd/${_file}"
            "${_path}" EXPECTED_HASH "SHA256=${_expected}" TLS_VERIFY ON)
    endif()
    file(SHA256 "${_path}" _actual)
    if(NOT _actual STREQUAL _expected)
        message(FATAL_ERROR "Cached source hash mismatch: ${_path}")
    endif()
endforeach()
set(_source "${TIRED_SYSTEMD_CACHE}/source")
if(NOT EXISTS "${_source}")
    execute_process(COMMAND "${_dpkg_source}" --no-check -x
        "${TIRED_SYSTEMD_CACHE}/systemd_252.39-1~deb12u2.dsc" "${_source}"
        COMMAND_ERROR_IS_FATAL ANY)
endif()
set(_build "${TIRED_SYSTEMD_CACHE}/build-${_arch}-reader")
set(_reconfigure)
if(EXISTS "${_build}/build.ninja")
    set(_reconfigure --reconfigure)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "CC=${_clang}" "CXX=${_false}"
    "${_meson}" setup ${_reconfigure} "${_build}" "${_source}"
    --wrap-mode=nodownload --buildtype=release -Dc_link_args=-fuse-ld=lld
    -Dstatic-libsystemd=true -Dtests=false -Dinstall-tests=false -Dman=false -Dhtml=false
    -Dtranslations=false -Dmode=release -Dgnu-efi=false
    -Dgcrypt=false -Dopenssl=false -Dgnutls=false -Dcryptolib=auto -Dxz=true
    -Dlz4=true -Dzstd=true -Dselinux=false -Dapparmor=false -Daudit=true -Dseccomp=false
    -Dtime-epoch=0 -Dversion-tag=252.39-1~deb12u2
    COMMAND_ERROR_IS_FATAL ANY)
# The explicit version tag generates version.h at configure time for this archive target.
execute_process(COMMAND "${_ninja}" -C "${_build}" -j4 libsystemd.a COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Prepared ${_build}/libsystemd.a; no system files were installed.")
