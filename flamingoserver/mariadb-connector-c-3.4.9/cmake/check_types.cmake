#
#  Copyright (C) 2013-2016 MariaDB Corporation AB
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the COPYING-CMAKE-SCRIPTS file.
#
# This file is included by CMakeLists.txt and
# checks for type sizes.
# You will find the appropriate defines in 
# include/my_config.h.in
INCLUDE (CheckTypeSize)

SET(CMAKE_EXTRA_INCLUDE_FILES signal.h)

CHECK_TYPE_SIZE("char *" SIZEOF_CHARP)
CHECK_TYPE_SIZE(int SIZEOF_INT)
CHECK_TYPE_SIZE(long SIZEOF_LONG)
CHECK_TYPE_SIZE("long long" SIZEOF_LONG_LONG)
SET(CMAKE_EXTRA_INCLUDE_FILES stdio.h)
CHECK_TYPE_SIZE(size_t SIZEOF_SIZE_T)
SET(CMAKE_EXTRA_INCLUDE_FILES sys/types.h)
CHECK_TYPE_SIZE(uint SIZEOF_UINT)
CHECK_TYPE_SIZE(ushort SIZEOF_USHORT)
CHECK_TYPE_SIZE(ulong SIZEOF_ULONG)
CHECK_TYPE_SIZE(int8 SIZEOF_INT8)
CHECK_TYPE_SIZE(uint8 SIZEOF_UINT8)
CHECK_TYPE_SIZE(int16 SIZEOF_INT16)
CHECK_TYPE_SIZE(uint16 SIZEOF_UINT16)
CHECK_TYPE_SIZE(int32 SIZEOF_INT32)
CHECK_TYPE_SIZE(uint32 SIZEOF_UINT32)
CHECK_TYPE_SIZE(int64 SIZEOF_INT64)
CHECK_TYPE_SIZE(uint64 SIZEOF_UINT64)
CHECK_TYPE_SIZE(socklen_t SIZEOF_SOCKLEN_T)

#
# Compile testing
#
INCLUDE (CheckCSourceCompiles)

#
# SOCKET_SIZE 
#
IF(WIN32)
  SET(SOCKET_SIZE_TYPE int)
ELSE()
  FOREACH(CHECK_TYPE "socklen_t" "size_t" "int")
    IF (NOT SOCKET_SIZE_TYPE)
      CHECK_C_SOURCE_COMPILES("
        #include <sys/socket.h>
        int main(int argc, char **argv)
        {
          getsockname(0, 0, (${CHECK_TYPE} *)0);
          return 0;
        }"
        SOCKET_SIZE_FOUND_${CHECK_TYPE})
      IF(SOCKET_SIZE_FOUND_${CHECK_TYPE})
        SET(SOCKET_SIZE_TYPE ${CHECK_TYPE})
      ENDIF()
    ENDIF()
  ENDFOREACH()
ENDIF()
