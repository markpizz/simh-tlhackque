# Locate the PCAP library
#
# This module defines:
#
# ::
#
#   PCAP_INCLUDE_DIRS, where to find the headers
#   PCAP_FOUND, if false, do not try to link against
#
# Tweaks:
# 1. PCAP_PATH: A list of directories in which to search
# 2. PCAP_DIR: An environment variable to the directory where you've unpacked or installed PCAP.
#
# "scooter me fecit"

find_path(PCAP_INCLUDE_DIR pcap.h
    HINTS
      ENV PCAP_DIR
      # path suffixes to search inside ENV{PCAP_DIR}
      include/pcap include/PCAP include
    PATHS
      ${PCAP_PATH}
    )

if (PCAP_INCLUDE_DIR)
    set(PCAP_FOUND true)
endif ()


set(PCAP_INCLUDE_DIRS ${PCAP_INCLUDE_DIR})
set(PCAP_INCLUDE_DIR)
