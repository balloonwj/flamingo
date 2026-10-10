# - Find Nettle
#
#  NETTLE_INCLUDE_DIR    - where to find <nettle/sha.h>, etc.
#  NETTLE_LIBRARIES      - List of libraries when using libnettle.
#  NETTLE_FOUND          - True if libnettle found.

IF (NETTLE_INCLUDE_DIR)
  SET(NETTLE_FIND_QUIETLY TRUE)
ENDIF()

FIND_PATH(NETTLE_INCLUDE_DIR nettle/md5.h nettle/ripemd160.h nettle/sha.h)
FIND_LIBRARY(NETTLE_LIBRARY NAMES nettle libnettle)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(NETTLE DEFAULT_MSG NETTLE_LIBRARY NETTLE_INCLUDE_DIR)

IF(NETTLE_FOUND)
  SET(NETTLE_LIBRARIES ${NETTLE_LIBRARY})
ENDIF()
