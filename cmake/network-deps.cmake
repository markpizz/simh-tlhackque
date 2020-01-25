#~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=
# Manage the networking dependencies
#
# (a) Try to locate the system's installed libraries.
# (b) Build source libraries, if not found.
#~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=~=

include (ExternalProject)

# pcap networking (slirp is handled in its own directory):
add_library(pcap INTERFACE)

if (WITH_NETWORK)

    include(CheckIncludeFiles)
    check_include_files (linux/if_tun.h HAVE_TAP_NETWORK)
    if (HAVE_TAP_NETWORK)
        target_compile_definitions(pcap INTERFACE HAVE_TAP_NETWORK)
    endif ()

    include (FindPCAP)

    if (PCAP_FOUND)
        target_compile_definitions(pcap INTERFACE USE_SHARED HAVE_PCAP_NETWORK)
        target_include_directories(pcap INTERFACE "${PCAP_INCLUDE_DIRS}")

        set(NETWORK_PKG_STATUS "installed PCAP")
    else (PCAP_FOUND)
        message(STATUS "Downloading libpcap-1.9 from https://github.com/the-tcpdump-group/libpcap.git")

        set(PCAP_ARCHIVE ${CMAKE_BINARY_DIR}/build-stage/libpcap-1.9.zip)
        file (DOWNLOAD https://github.com/the-tcpdump-group/libpcap/archive/libpcap-1.9.zip ${PCAP_ARCHIVE})

        set (PCAP_PATH "${CMAKE_BINARY_DIR}/build-stage/libpcap-libpcap-1.9")
        execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf ${PCAP_ARCHIVE} libpcap-libpcap-1.9
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/build-stage
        )
        include (FindPCAP)

        target_compile_definitions(pcap INTERFACE USE_SHARED HAVE_PCAP_NETWORK)
        target_include_directories(pcap INTERFACE "${PCAP_INCLUDE_DIRS}")

        set(NETWORK_PKG_STATUS "downloaded PCAP")

    endif (PCAP_FOUND)
else ()
    set(NETWORK_STATUS "networking disabled")
endif ()
