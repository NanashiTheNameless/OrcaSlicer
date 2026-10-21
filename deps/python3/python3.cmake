
include(ProcessorCount)
ProcessorCount(NPROC)

set(_python_version "3.14.9")
string(REGEX REPLACE "^([0-9]+\\.[0-9]+)\\..*" "\\1" _python_version_short "${_python_version}")
set(_python_url "https://www.python.org/ftp/python/${_python_version}/Python-${_python_version}.tar.xz")
set(_python_sha256 "e02d9ac21cebda6e46435369744a92c65cc7cd8669697694122cf97ffacd1e53")


if(WIN32)
    # Windows ships the official, Authenticode-signed CPython runtime from nuget.org
    # instead of compiling it from source. The nuget `python` / `pythonarm64`
    # packages lay out exactly what the embedded interpreter and the pybind11 bridge
    # need (python.exe, pythonNNN.dll, a loose Lib/ stdlib, DLLs/, plus include/ and
    # libs/ for linking), so staging is a plain copy of their tools/ directory into
    # the dep prefix's libpython/. The signed binaries also build download reputation
    # with SmartScreen, unlike a locally compiled python.exe.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_python_nuget_package pythonarm64)
        set(_python_nuget_sha256 979ba7a2f09d18e7bc17e47b15264e49189fe73227cd772b3f856c29663f019a)
    else()
        set(_python_nuget_package python)
        set(_python_nuget_sha256 7cd9d07aec975212344dc6f7e45019378cf6775dd564181e269010a1eddaaec7)
    endif()

    ExternalProject_Add(dep_python3
        URL "https://api.nuget.org/v3-flatcontainer/${_python_nuget_package}/${_python_version}/${_python_nuget_package}.${_python_version}.nupkg"
        URL_HASH SHA256=${_python_nuget_sha256}
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/python3
        # Rename the .nupkg to .zip so ExternalProject extracts it.
        DOWNLOAD_NAME ${_python_nuget_package}.${_python_version}.zip
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/tools ${DESTDIR}/libpython
    )

    # The nuget runtime is self-contained (its _ssl/_hashlib embed their own OpenSSL),
    # so unlike the source build it needs no dep_OpenSSL / dep_ZLIB wiring. Nothing
    # below this point applies to Windows.
    return()
endif()

if(APPLE)
    # macOS configuration
    if(CMAKE_OSX_ARCHITECTURES)
        set(_python_target_arch "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_python_target_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        set(_python_build_arch aarch64)
        set(_python_build_arch_flag "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        set(_python_build_arch x86_64)
        set(_python_build_arch_flag "x86_64")
    else()
        set(_python_build_arch "${CMAKE_SYSTEM_PROCESSOR}")
        set(_python_build_arch_flag "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(_python_target_arch MATCHES "ARM64|arm64|aarch64")
        set(_python_host_arch aarch64)
        set(_python_arch_flag "arm64")
    elseif(_python_target_arch MATCHES "x86_64|AMD64|amd64")
        set(_python_host_arch x86_64)
        set(_python_arch_flag "x86_64")
    else()
        message(FATAL_ERROR "Unsupported macOS Python target architecture: ${_python_target_arch}")
    endif()

    set(_python_arch_flags "-arch ${_python_arch_flag} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    # No -rpath: all other deps are static, so libpython has no shared
    # dependencies to find there. headerpad reserves load-command space for
    # the post-install -add_rpath below.
    set(_python_ldflags "${_python_arch_flags} -Wl,-headerpad_max_install_names")

    # The macOS 27 SDK declares pipe2() and dup3() as available from macOS 27, so
    # configure finds them and CPython 3.14 calls them without a runtime check.
    # Below a macOS 27 deployment target they are weak-linked and resolve to NULL
    # on older systems, where os.pipe() then segfaults -- in `make install`
    # (compileall, ensurepip) and in the shipped app alike. Every configure below
    # keeps the pipe()/dup2() fallbacks (python/cpython#153711).
    if(IS_CROSS_COMPILE)
        set(_python_build_tgt --build=${_python_build_arch}-apple-darwin --host=${_python_host_arch}-apple-darwin)
        set(_python_build_arch_flags "-arch ${_python_build_arch_flag} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        set(_python_build_ldflags "${_python_build_arch_flags} -Wl,-rpath,${DESTDIR}/lib")
        set(_python_build_python_dir "<SOURCE_DIR>/build-python-host")
        set(_python_build_python "${_python_build_python_dir}/python")
        set(_conf_cmd
            /bin/sh -c
            "rm -rf '${_python_build_python_dir}' && \
             mkdir -p '${_python_build_python_dir}' && \
             cd '${_python_build_python_dir}' && \
             env \
               CC='${CMAKE_C_COMPILER}' \
               CXX='${CMAKE_CXX_COMPILER}' \
               CFLAGS='${_python_build_arch_flags}' \
               CXXFLAGS='${_python_build_arch_flags}' \
               LDFLAGS='${_python_build_ldflags}' \
               MACOSX_DEPLOYMENT_TARGET='${CMAKE_OSX_DEPLOYMENT_TARGET}' \
               ../configure \
                 --prefix='${_python_build_python_dir}/install' \
                 --enable-shared \
                 --without-static-libpython \
                 --disable-test-modules \
                 --build=${_python_build_arch}-apple-darwin \
                 ac_cv_func_pipe2=no ac_cv_func_dup3=no && \
             make -j${NPROC} python && \
             cd '<SOURCE_DIR>' && \
             env \
               CC='${CMAKE_C_COMPILER}' \
               CXX='${CMAKE_CXX_COMPILER}' \
               CFLAGS='${_python_arch_flags}' \
               CXXFLAGS='${_python_arch_flags}' \
               LDFLAGS='${_python_ldflags}' \
               MACOSX_DEPLOYMENT_TARGET='${CMAKE_OSX_DEPLOYMENT_TARGET}' \
               ./configure \
                 --prefix='${DESTDIR}/libpython' \
                 --enable-shared \
                 --enable-optimizations \
                 --without-static-libpython \
                 --with-openssl='${DESTDIR}' \
                 --disable-test-modules \
                 ac_cv_func_pipe2=no ac_cv_func_dup3=no \
                 ${_python_build_tgt} \
                 --with-build-python='${_python_build_python}' \
                 py_cv_module__tkinter=n/a"
        )
    else()
        set(_python_build_tgt --build=${_python_host_arch}-apple-darwin)
        set(_conf_cmd
            env
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            "CFLAGS=${_python_arch_flags}"
            "CXXFLAGS=${_python_arch_flags}"
            "LDFLAGS=${_python_ldflags}"
            "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            ./configure
            --prefix=${DESTDIR}/libpython
            --enable-shared
            --enable-optimizations
            --without-static-libpython
            --with-openssl=${DESTDIR}
            --disable-test-modules
            ${_python_build_tgt}
            ac_cv_func_pipe2=no
            ac_cv_func_dup3=no
            # Tcl/Tk 9.0 (e.g. from Homebrew) is incompatible with CPython 3.14's
            # _tkinter; OrcaSlicer's embedded Python does not need tkinter anyway.
            py_cv_module__tkinter=n/a
        )
    endif()
    set(_build_cmd make -j${NPROC})

    # CPython stamps libpython with an absolute install name ($prefix/lib/...),
    # which every consumer inherits at link time and which only exists on the
    # build host. Normalize once here, before anything links against the dep:
    # give the dylib an @rpath id and teach the interpreter to find it relative
    # to itself. Consumers then just need an rpath entry (src/CMakeLists.txt).
    # install_name_tool invalidates code signatures, so re-sign ad-hoc; CI
    # re-signs the whole bundle with the real identity later.
    # ld collapses '//' in -install_name (but not in -rpath) strings, while
    # ${DESTDIR} ends with a slash -- collapse slashes so -change matches the
    # recorded install name.
    string(REGEX REPLACE "/+" "/" _python_prefix "${DESTDIR}/libpython")
    set(_python_dylib "${_python_prefix}/lib/libpython${_python_version_short}.dylib")
    set(_python_bin "${_python_prefix}/bin/python${_python_version_short}")
    set(_install_cmd make install
        COMMAND install_name_tool -id "@rpath/libpython${_python_version_short}.dylib" "${_python_dylib}"
        COMMAND install_name_tool -change "${_python_dylib}" "@rpath/libpython${_python_version_short}.dylib" "${_python_bin}"
        COMMAND install_name_tool -add_rpath "@loader_path/../lib" "${_python_bin}"
        COMMAND codesign --force --sign - "${_python_dylib}"
        COMMAND codesign --force --sign - "${_python_bin}"
    )
else()
    # Linux/Unix
    # Kept verbatim, no slash normalization (unlike the macOS branch's
    # collapsed copy): the LDFLAGS rpath below is recorded byte-for-byte in
    # the ELF, and the OLD_RPATH handed to relocate_linux.cmake must match it
    # exactly -- both derive from this one variable to make that structural.
    set(_python_prefix "${DESTDIR}/libpython")
    # The rpath points at libpython's real install dir, so the interpreter runs
    # in-tree pre-relocation -- and, critically, it reserves enough RUNPATH
    # bytes for the in-place $ORIGIN rewrite at install time (Flatpak's
    # DESTDIR is the short /app) -- see relocate_linux.cmake.
    set(_conf_cmd ./configure
        --prefix=${_python_prefix}
        --enable-shared
        --enable-optimizations
        --with-openssl=${DESTDIR}
        --without-static-libpython
        --disable-test-modules
        # Tcl/Tk 9.0 is incompatible with CPython 3.14's _tkinter; not needed here.
        py_cv_module__tkinter=n/a
        LDFLAGS=-Wl,-rpath,${_python_prefix}/lib
    )
    set(_build_cmd make -j${NPROC})
    set(_install_cmd make install
        COMMAND ${CMAKE_COMMAND}
            "-DPYTHON_BIN=${_python_prefix}/bin/python${_python_version_short}"
            "-DOLD_RPATH=${_python_prefix}/lib"
            -P "${CMAKE_CURRENT_LIST_DIR}/relocate_linux.cmake"
    )
endif()

ExternalProject_Add(dep_python3
    URL "${_python_url}"
    URL_HASH SHA256=${_python_sha256}
    DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/python3
    BUILD_IN_SOURCE ON
    CONFIGURE_COMMAND ${_conf_cmd}
    BUILD_COMMAND ${_build_cmd}
    INSTALL_COMMAND ${_install_cmd}
    DEPENDS ${_python_depends}
)

# Python depends on OpenSSL and ZLIB
if(TARGET dep_OpenSSL)
    add_dependencies(dep_python3 dep_OpenSSL)
endif()
if(TARGET dep_ZLIB)
    add_dependencies(dep_python3 dep_ZLIB)
endif()

if (NOT WIN32)
    # _ssl and _hashlib embed the static OpenSSL built in deps/ (Windows uses
    # CPython's prebuilt OpenSSL instead). Their Makefile rules depend only on
    # their own sources, not on the OpenSSL archives, so a rebuilt OpenSSL does
    # not make them relink and they keep the previous one. On an incremental tree,
    # drop the built modules and relink them against the current OpenSSL; a
    # fresh build is left alone (its PGO target builds them). "make" alone is a
    # no-op once PGO has run, so sharedmods is invoked explicitly.
    ExternalProject_Get_Property(dep_python3 SOURCE_DIR)
    file(GLOB _python_ssl_modules
        "${SOURCE_DIR}/Modules/_ssl*.so"
        "${SOURCE_DIR}/Modules/_hashlib*.so")
    if (_python_ssl_modules)
        ExternalProject_Add_Step(dep_python3 relink_ssl_extensions
            DEPENDEES configure
            DEPENDERS build
            COMMAND sh -c "rm -f '${SOURCE_DIR}'/Modules/_ssl*.so '${SOURCE_DIR}'/Modules/_hashlib*.so && make -j${NPROC} sharedmods"
            WORKING_DIRECTORY "${SOURCE_DIR}"
            COMMENT "CPython: relinking _ssl/_hashlib against the current OpenSSL"
            DEPENDS "${CMAKE_CURRENT_LIST_FILE}"
                    "${CMAKE_CURRENT_LIST_DIR}/../OpenSSL/OpenSSL.cmake"
        )
    endif ()
endif ()
