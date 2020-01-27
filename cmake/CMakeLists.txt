# Setup the correct source directory since all cmake stuff is 
# in the cmake subdirectory
get_filename_component(CMAKE_SOURCE_DIR ""
                       REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}/..")
string(LENGTH "${CMAKE_SOURCE_DIR}/BIN/" CMAKE_BUILD_PATH_BASE_LENGTH)
string(SUBSTRING ${CMAKE_BINARY_DIR} 0 ${CMAKE_BUILD_PATH_BASE_LENGTH} CMAKE_BINARY_DIR_BASE)
if (NOT (${CMAKE_BINARY_DIR_BASE} STREQUAL "${CMAKE_SOURCE_DIR}/BIN/"))
  message("")
  message("*** Do NOT build or generate CMake artifacts in the SIMH source directory! ***")
  message("")
  message("Create a subdirectory in the BIN directory and build in that subdirectory, e.g.:")
  message("")
  message("  $ mkdir -p BIN/cmake-build")
  message("  $ cd BIN/cmake-build")
  message("  $ cmake -G \"your generator here\" ../../cmake")
  message("")
  message(FATAL_ERROR "Preventing in-tree source build.")
  unset(CMAKE_BINARY_DIR CACHE)
  unset(CMAKE_SOURCE_DIR CACHE)
endif ()

## Note: pthreads4w requires 3.14 or better.
cmake_minimum_required(VERSION 3.10)

set(SIMH_VERSION "4.0.0")
project(simh VERSION "${SIMH_VERSION}" LANGUAGES C)

# SIMH_DEP_TOPDIR: This is the top-level directory where dependencies live:
set(SIMH_DEP_TOPDIR ${CMAKE_BINARY_DIR}/build-stage)
message(STATUS "SIMH_DEP_TOPDIR is ${SIMH_DEP_TOPDIR}")

# Places to look for CMake modules/includes
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Places where CMake should look for dependent package configuration fragments and artifacts:
set(SIMH_PREFIX_PATH_LIST ${SIMH_DEP_TOPDIR})
list(APPEND CMAKE_PREFIX_PATH ${SIMH_PREFIX_PATH_LIST})

set(SIMH_INCLUDE_PATH_LIST ${SIMH_DEP_TOPDIR})
list(APPEND CMAKE_INCLUDE_PATH ${SIMH_INCLUDE_PATH_LIST})

include(CheckCSourceCompiles)
include(CheckCCompilerFlag)
include(CheckIncludeFile)
include(CheckSymbolExists)
include (ExternalProject)

## pkg-config is our fallback when find_package() can't find a package.
find_package(PkgConfig)

##-- Options --##
option(WITH_NETWORK
       "Enable (=1)/disable (=0) simulator networking support. (def: enabled)"
       TRUE)
option(WITH_VIDEO
       "Enable (=1)/disable (=0) simulator display and graphics support (def: enabled)"
       TRUE)
option(PANDA_LIGHTS
       "Enable (=1)/disable (=0) KA-10/KI-11 simulator's Panda display. (def: disabled)"
       FALSE)
option(DONT_USE_ROMS
       "Enable (=1)/disable (=0) building support ROMs. (def: disabled)"
       FALSE)
option(ENABLE_CPPCHECK
       "Enable (=1)/disable (=0) 'cppcheck' static code analysis. (def: disabled.)"
       FALSE)

## Provide a default CMAKE_BUILD_TYPE if CMAKE_CONFIGURATION_TYPES is empty or not defined.
if (NOT CMAKE_CONFIGURATION_TYPES)
    if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
	message(STATUS "CMAKE_BUILD_TYPE defaulted to ${CMAKE_BUILD_TYPE}")
    else (NOT CMAKE_BUILD_TYPE)
	message(STATUS "CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")
    endif (NOT CMAKE_BUILD_TYPE)
else ()
    message(STATUS "Available build configurations: ${CMAKE_CONFIGURATION_TYPES}")
endif ()

# For 64-bit builds (and this is especially true for MSVC), set the library
# architecture.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # For the WinPcap and Npcap SDKs, the Lib subdirectory of the top-level
    # directory contains 32-bit libraries; the 64-bit libraries are in the
    # Lib/x64 directory.
    #
    # The only way to *FORCE* CMake to look in the Lib/x64 directory
    # without searching in the Lib directory first appears to be to set
    # CMAKE_LIBRARY_ARCHITECTURE to "x64".
    #
    if (MSVC OR MINGW)
        set(CMAKE_C_LIBRARY_ARCHITECTURE "x64")
        set(CMAKE_LIBRARY_ARCHITECTURE "x64")
    else (EXISTS /usr/lib/x86_64-linux-gnu)
        # Linux-ism
        set(CMAKE_C_LIBRARY_ARCHITECTURE "x86_64-linux-gnu")
        set(CMAKE_LIBRARY_ARCHITECTURE "x86_64-linux-gnu")
    endif ()
endif()

## Additional command line arguments for dependencies. Need this because Powershell will report
## an error if anything is sent to stderr and $ErrorDefaultAction is set to "Stop".
set(DEP_CMAKE_ARGS "-Wno-dev" "--no-warn-unused-cli")

## build-stage directory hierarchy for dependency installs:
message(STATUS "Creating build-stage directory hierarchy (include, lib, bin)")
execute_process(
    COMMAND ${CMAKE_COMMAND} -E make_directory build-stage build-stage/include build-stage/lib build-stage/bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

## Default install location is ${CMAKE_SOURCE_DIR}/BIN if not otherwise set
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/" CACHE PATH "${CMAKE_SOURCE_DIR} default install prefix" FORCE)
  set(CMAKE_INSTALL_BINDIR BIN CACHE PATH "BIN default binary install directory" FORCE)

  if (WIN32 AND MSVC)
    set(CMAKE_INSTALL_BINDIR "BIN/Win32/$<CONFIG>" CACHE PATH "BIN/Win32/<config> default binary install directory" FORCE)
  endif ()
endif()

# Make sure we can include and link from the top level source...
include_directories(${CMAKE_SOURCE_DIR} ${SIMH_DEP_TOPDIR}/include)
link_directories(${SIMH_DEP_TOPDIR}/lib)

# A bunch of Windows customizations
if (WIN32)
    list(APPEND CMAKE_LIBRARY_PATH ${SIMH_DEP_TOPDIR}/lib)
    list(APPEND CMAKE_INCLUDE_PATH ${SIMH_DEP_TOPDIR}/include)

    add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)

    if (MSVC_VERSION GREATER_EQUAL 1920)
	    add_compile_definitions(USE_ISO_C99_NAMES)
    endif ()
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # For some reason, the MSVC solution builds as 32-bit, but none of the
    # *nix platforms do.
    #
    # If 32-bit compiles have to be added back, uncomment the following 2 lines:
    #
    # add_compile_options("-m32")
    # set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
endif (WIN32)

# Tweak GNU's flags
set(EXTRA_CFLAGS "")
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    include(fpintrin)
    # Turn on warnings about strict overflow/potential overflows.
    ## LIST(APPEND EXTRA_CFLAGS "-Wall" "-fno-inline" "-fstrict-overflow" "-Wstrict-overflow=3")
    LIST(APPEND EXTRA_CFLAGS "-Wformat")

    # 'O3' optimization and strict overflow cause all kinds of simulator issues, especially inside
    # the VAX simulators. Reduce optimization and ensure strict overflow is turned off.
    string(REGEX REPLACE "-O3" "-O2" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    string(REGEX REPLACE "-fstrict-overflow[ \t\r\n]*" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    string(APPEND CMAKE_C_FLAGS_RELEASE " -fno-strict-overflow")

    string(REGEX REPLACE "-O3" "-O2" CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")
    string(REGEX REPLACE "-fstrict-overflow[ \t\r\n]*" "" CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")
    string(APPEND CMAKE_C_FLAGS_MINSIZEREL " -fno-strict-overflow")
endif (CMAKE_C_COMPILER_ID STREQUAL "GNU")

# Find packages, arrange for dependency download/compile/install:
#
# SIMH_BUILD_DEPS is the list of the dependecies' names, for pretty-printing.
# SIMH_DEP_TARGETS is the list of dependency targets' names that we'll actually build.

set(SIMH_BUILD_DEPS)
set(SIMH_DEP_TARGETS)

set(THREADING_PKG_STATUS "unknown")
set(ZLIB_PKG_STATUS "unknown")
set(PCRE_PKG_STATUS "unknown")
set(VIDEO_PKG_STATUS "unknown")
set(NETWORK_PKG_STATUS "unknown")

include(pthreads-dep)
include(os-features)
include(zlib-dep)
include(pcre-dep)

if (WITH_VIDEO)
    include(video-deps)
    set(BUILD_WITH_VIDEO TRUE)
else (WITH_VIDEO)
    set(VIDEO_PKG_STATUS "skipped")
    set(BUILD_WITH_VIDEO FALSE)
endif (WITH_VIDEO)

if (WITH_NETWORK)
    include(network-deps)
    set(BUILD_WITH_NETWORK TRUE)
else (WITH_NETWORK)
    set(NETWORK_PKG_STATUS "skipped")
    set(BUILD_WITH_NETWORK FALSE)
endif (WITH_NETWORK)

get_target_property(OS_FEATURE_DEFS os_features INTERFACE_COMPILE_DEFINITIONS)
string(REPLACE ";" ", " OS_FEATURE_DEFS "${OS_FEATURE_DEFS}")
if (NOT OS_FEATURE_DEFS)
  set(OS_FEATURE_DEFS "no operating system-specific features")
endif (NOT OS_FEATURE_DEFS)
get_target_property(OS_FEATURE_LIBS os_features INTERFACE_LINK_LIBRARIES)
string(REPLACE ";" ", " OS_FEATURE_LIBS "${OS_FEATURE_LIBS}")
if (NOT OS_FEATURE_LIBS)
  set(OS_FEATURE_LIBS "no operating system-specific libraries")
endif (NOT OS_FEATURE_LIBS)

set(CPPCHECK_STATUS "disabled.")

find_program(cppcheck_cmd NAMES cppcheck)
if (cppcheck_cmd)
    list(APPEND cppcheck_cmd
         "--language=c"
         "--enable=warning,style,performance,portability,information,missingInclude"
         "--suppress=missingIncludeSystem"
         "--inline-suppr"
         "--std=c99"
         "--force")
    set(CPPCHECK_STATUS "enabled.")
    if (WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            list(APPEND cppcheck_cmd
                 "--platform=win64")
            set(CPPCHECK_STATUS "enabled, Win64 platform.")
        else ()
            list(APPEND cppcheck_cmd
                 "--platform=win32A")
            set(CPPCHECK_STATUS "enabled, Win32 ASCII platform.")
        endif ()
    endif ()
else (cppcheck_cmd)
    set(CPPCHECK_STATUS "'cppcheck' not installed.")
endif ()

message(STATUS "")
message(STATUS "Libraries and features:")
message(STATUS "  Thread support .................. ${THREADING_PKG_STATUS}")
message(STATUS "  zlib ............................ ${ZLIB_PKG_STATUS}")
message(STATUS "  Perl-Compatible RegExps.......... ${PCRE_PKG_STATUS}")
message(STATUS "  PNG, Freetype, SDL2, SDL2_ttf ... ${VIDEO_PKG_STATUS}")
message(STATUS "  Packet Capture (PCAP) ........... ${NETWORK_PKG_STATUS}")
message(STATUS "  OS Feature definitions .......... ${OS_FEATURE_DEFS}")
message(STATUS "  OS Feature libraries ............ ${OS_FEATURE_LIBS}")
message(STATUS "")
message(STATUS "Build options:")
message(STATUS "  Video/graphics support ....... ${BUILD_WITH_VIDEO}")
message(STATUS "  Networking ................... ${BUILD_WITH_NETWORK}")
if (DONT_SET_ROMS)
    set(ROM_STATUS "No")
else ()
    set(ROM_STATUS "Yes")
endif ()
message(STATUS "  Build internal ROMS .......... ${ROM_STATUS}")
message(STATUS "  'cppcheck' ................... ${CPPCHECK_STATUS}")
message(STATUS "")

unset(ROM_STATUS)

if (NOT DEFINED DO_DEPENDENCY_BUILD OR SIMH_BUILD_DEPS)
    if (DEFINED DO_DEPENDENCY_BUILD AND NOT DO_DEPENDENCY_BUILD AND SIMH_BUILD_DEPS)
	message(FATAL_ERROR "Dependency libraries did not build successfully!!??")
    endif()

    if (SIMH_BUILD_DEPS)
	message(STATUS "Building dependency libraries as a superbuild")
	set(DO_DEPENDENCY_BUILD ON CACHE BOOL "Superbuild flag" FORCE)
    else ()
	set(DO_DEPENDENCY_BUILD OFF CACHE BOOL "Superbuild flag" FORCE)
    endif ()
else ()
    set(DO_DEPENDENCY_BUILD ${DO_DEPENDENCY_BUILD} CACHE BOOL "Superbuild flag" FORCE)
endif ()

if (NOT DO_DEPENDENCY_BUILD)

    message(STATUS "Generating CMake simulator build rules from simh makefile...")
    set(SIMHD "${CMAKE_SOURCE_DIR}")
    try_run(
        RUN_RESULT COMPILE_RESULT
        "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/cmake/makefile2cmake.c"
        COMPILE_DEFINITIONS "-DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}"
        COMPILE_DEFINITIONS "-DCMAKE_SOURCE_DIR=${SIMHD}"
        COMPILE_OUTPUT_VARIABLE MAKEFILE2CMAKE_BROKEN_COMPILE
        RUN_OUTPUT_VARIABLE MAKEFILE2CMAKE_BROKEN_RUN)
    if (NOT DEFINED RUN_RESULT OR RUN_RESULT)
        message(STATUS "Compile or Execution failed:")
        message(STATUS "compile_result=${COMPILE_RESULT} - ${MAKEFILE2CMAKE_BROKEN_COMPILE}")
        message(STATUS "run_results: ${MAKEFILE2CMAKE_BROKEN_RUN}")
    endif ()

    message(STATUS "Adding simulators")
    include (add_simulator)

    ## The actual simulators:

    ## And here's where the actual work gets done:
    set(SLIRP_SOURCES
		"${CMAKE_SOURCE_DIR}/slirp/arp_table.c"
		"${CMAKE_SOURCE_DIR}/slirp/bootp.c"
		"${CMAKE_SOURCE_DIR}/slirp/bootp.h"
		"${CMAKE_SOURCE_DIR}/slirp/cksum.c"
		"${CMAKE_SOURCE_DIR}/slirp/dnssearch.c"
		"${CMAKE_SOURCE_DIR}/slirp/if.c"
		"${CMAKE_SOURCE_DIR}/slirp/ip_icmp.c"
		"${CMAKE_SOURCE_DIR}/slirp/ip_input.c"
		"${CMAKE_SOURCE_DIR}/slirp/ip_output.c"
		"${CMAKE_SOURCE_DIR}/slirp/mbuf.c"
		"${CMAKE_SOURCE_DIR}/slirp/misc.c"
		"${CMAKE_SOURCE_DIR}/slirp/sbuf.c"
		"${CMAKE_SOURCE_DIR}/slirp/slirp.c"
		"${CMAKE_SOURCE_DIR}/slirp/socket.c"
		"${CMAKE_SOURCE_DIR}/slirp/tcp_input.c"
		"${CMAKE_SOURCE_DIR}/slirp/tcp_output.c"
		"${CMAKE_SOURCE_DIR}/slirp/tcp_subr.c"
		"${CMAKE_SOURCE_DIR}/slirp/tcp_timer.c"
		"${CMAKE_SOURCE_DIR}/slirp/tftp.c"
		"${CMAKE_SOURCE_DIR}/slirp/udp.c"
		"${CMAKE_SOURCE_DIR}/slirp_glue/glib_qemu_stubs.c"
		"${CMAKE_SOURCE_DIR}/slirp_glue/sim_slirp.c")

    add_library(slirp STATIC "${SLIRP_SOURCES}")
    target_compile_definitions(slirp
		  PRIVATE HAVE_SLIRP_NETWORK USE_SIMH_SLIRP_DEBUG _WINSOCK_DEPRECATED_NO_WARNINGS
		  INTERFACE HAVE_SLIRP_NETWORK USE_SIMH_SLIRP_DEBUG)

    target_include_directories(slirp PUBLIC
		  "${CMAKE_SOURCE_DIR}/slirp"
		  "${CMAKE_SOURCE_DIR}/slirp_glue"
		  "${CMAKE_SOURCE_DIR}/slirp_glue/qemu")
    if (WIN32)
		target_include_directories(slirp PUBLIC "${CMAKE_SOURCE_DIR}/slirp_glue/qemu/win32/include")
		target_link_libraries(slirp Iphlpapi)
    endif ()
    
    # consume the configuration details for all simulators
    include(${CMAKE_BINARY_DIR}/simh_makefile.cmake)

    # Install the DLLs
    if (WIN32 AND EXISTS ${CMAKE_BINARY_DIR}/build-stage/bin)
	file(GLOB SIMH_DLLS ${CMAKE_BINARY_DIR}/build-stage/bin/*.dll)

	install(
	    FILES ${SIMH_DLLS}
	    DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}
	)
    endif (WIN32 AND EXISTS ${CMAKE_BINARY_DIR}/build-stage/bin)
else (NOT DO_DEPENDENCY_BUILD)
    message(STATUS "")
    message(STATUS "The following dependencies need to be built:")
    message(STATUS "")
    message(STATUS "  ${SIMH_BUILD_DEPS}")
    message(STATUS "")

    ExternalProject_Add (simh_superbuild
        DEPENDS
	    ${SIMH_DEP_TARGETS}
	SOURCE_DIR
	    ${CMAKE_SOURCE_DIR}/cmake
	CMAKE_ARGS
	    -DDO_DEPENDENCY_BUILD:BOOL=OFF
	    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
	    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
	    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
	INSTALL_COMMAND ""
	BINARY_DIR
	    ${CMAKE_CURRENT_BINARY_DIR}
	USES_TERMINAL_CONFIGURE TRUE
	USES_TERMINAL_BUILD     TRUE
    )

    ## Ensure that the next build rechecks the dependency
    ## libraries that were just built and finds them.
    unset(FREETYPE_FOUND CACHE)
    unset(PCAP_FOUND CACHE)
    unset(PCRE_FOUND CACHE)
    unset(PCRE2_FOUND CACHE)
    unset(PNG_FOUND CACHE)
    unset(PTW_FOUND CACHE)
    unset(SDL2_FOUND CACHE)
    unset(SDL2_ttf_FOUND CACHE)
    unset(ZLIB_FOUND CACHE)
endif (NOT DO_DEPENDENCY_BUILD)
