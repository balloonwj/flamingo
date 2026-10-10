#
#  Copyright (C) 2013-2016 MariaDB Corporation AB
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the COPYING-CMAKE-SCRIPTS file.
#

# This file is included by CMakeLists.txt and
# checks for various functions.
# You will find the appropriate defines in 
# include/my_config.h.in

INCLUDE(CheckFunctionExists)

CHECK_FUNCTION_EXISTS (alloca HAVE_ALLOCA)
CHECK_FUNCTION_EXISTS (dlerror HAVE_DLERROR)
CHECK_FUNCTION_EXISTS (dlopen HAVE_DLOPEN)
CHECK_FUNCTION_EXISTS (fcntl HAVE_FCNTL)
CHECK_FUNCTION_EXISTS (memcpy HAVE_MEMCPY)
CHECK_FUNCTION_EXISTS (nl_langinfo HAVE_NL_LANGINFO)
CHECK_FUNCTION_EXISTS (setlocale HAVE_SETLOCALE)
CHECK_FUNCTION_EXISTS (poll HAVE_POLL)
CHECK_FUNCTION_EXISTS (getpwuid HAVE_GETPWUID)

IF(HAVE_FILE_UCONTEXT_H)
  CHECK_FUNCTION_EXISTS (makecontext HAVE_UCONTEXT_H)
ENDIF()

CHECK_FUNCTION_EXISTS (cuserid HAVE_CUSERID)