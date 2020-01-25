#~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=
# Manage the PCRE dependency
#
# (a) Try to locate the system's installed pcre library.
# (b) If system they aren't available, build pcre as an external project.
#~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=

add_library(regexp_lib INTERFACE)

include (FindPCRE)
if (NOT PCRE_FOUND AND PKG_CONFIG_FOUND)
    ## simh only needs the 8-bit PCRE library
    pkg_check_modules(PCRE IMPORTED_TARGET libpng2-8)
endif (NOT PCRE_FOUND AND PKG_CONFIG_FOUND)

if (PCRE_FOUND)
    target_compile_definitions(regexp_lib INTERFACE PCRE_STATIC HAVE_PCRE_H)
    target_include_directories(regexp_lib INTERFACE ${PCRE_INCLUDE_DIRS})
    target_link_libraries(regexp_lib INTERFACE ${PCRE_LIBRARY})

    set(PCRE_PKG_STATUS "installed pcre")
else ()
    set(PCRE_DEPS)
    if (NOT ZLIB_FOUND)
        list(APPEND PCRE_DEPS zlib-dep)
    endif (NOT ZLIB_FOUND)

    message(STATUS "CMAKE_C_COMPILER=$(CMAKE_C_COMPILER)")
    message(STATUS "CMAKE_C_COMPILER=$(CMAKE_CXX_COMPILER)")

    ExternalProject_Add(pcre-ext
        URL https://ftp.pcre.org/pub/pcre/pcre-8.43.zip
        CMAKE_ARGS 
            -DCMAKE_INSTALL_PREFIX=${SIMH_DEP_TOPDIR}
            -DCMAKE_PREFIX_PATH=${SIMH_PREFIX_PATH_LIST}
            -DCMAKE_INCLUDE_PATH=${SIMH_INCLUDE_PATH_LIST}
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        DEPENDS
            ${PCRE_DEPS}
    )

    list(APPEND SIMH_BUILD_DEPS pcre)
    list(APPEND SIMH_DEP_TARGETS pcre-ext)
    message(STATUS "Building PCRE from https://ftp.pcre.org/pub/pcre/pcre-8.43.zip")
    set(PCRE_PKG_STATUS "pcre dependent build")
endif ()
