/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02111-1301, USA */

/* Error messages for clients */
/* error messages for the demon is in share/language/errmsg.sys */

#include <ma_global.h>
#include <ma_sys.h>
#include "errmsg.h"
#include <stdarg.h>

const char *SQLSTATE_UNKNOWN= "HY000";

const char *client_errors[]=
{
/* 2000 */  "Unknown error",
/* 2001 */  "Can't create UNIX socket (%d)",
/* 2002 */  "Can't connect to local server through socket '%-.64s' (%d)",
/* 2003 */  "Can't connect to server on '%-.64s' (%d)",
/* 2004 */  "Can't create TCP/IP socket (%d)",
/* 2005 */  "Unknown server host '%-.100s' (%d)",
/* 2006 */  "Server has gone away",
/* 2007 */  "Protocol mismatch. Server Version = %d Client Version = %d",
/* 2008 */  "Client run out of memory",
/* 2009 */  "Wrong host info",
/* 2010 */  "Localhost via UNIX socket",
/* 2011 */  "%-.64s via TCP/IP",
/* 2012 */  "Error in server handshake",
/* 2013 */  "Lost connection to server during query",
/* 2014 */  "Commands out of sync; you can't run this command now",
/* 2015 */  "%-.64s via named pipe",
/* 2016 */  "Can't wait for named pipe to host: %-.64s  pipe: %-.32s (%lu)",
/* 2017 */  "Can't open named pipe to host: %-.64s  pipe: %-.32s (%lu)",
/* 2018 */  "Can't set state of named pipe to host: %-.64s  pipe: %-.32s (%lu)",
/* 2019 */  "Can't initialize character set %-.64s (path: %-.64s)",
/* 2020 */  "Got packet bigger than 'max_allowed_packet'",
/* 2021 */  "",
/* 2022 */  "",
/* 2023 */  "",
/* 2024 */  "",
/* 2025 */  "",
/* 2026 */  "TLS/SSL error: %s",
/* 2027 */  "Received malformed packet",
/* 2028 */  "",
/* 2029 */  "",
/* 2030 */  "Statement is not prepared",
/* 2031 */  "No data supplied for parameters in prepared statement",
/* 2032 */  "Data truncated",
/* 2033 */  "",
/* 2034 */  "Invalid parameter number",
/* 2035 */  "Invalid buffer type: %d (parameter: %d)",
/* 2036 */  "Buffer type is not supported",
/* 2037 */  "Shared memory: %-.64s",
/* 2038 */  "Shared memory connection failed during %s. (%lu)",
/* 2039 */  "",
/* 2040 */  "",
/* 2041 */  "",
/* 2042 */  "",
/* 2043 */  "",
/* 2044 */  "",
/* 2045 */  "",
/* 2046 */  "",
/* 2047 */  "Wrong or unknown protocol",
/* 2048 */  "",
/* 2049 */  "Connection with old authentication protocol refused.",
/* 2050 */  "",
/* 2051 */  "",
/* 2052 */  "Prepared statement contains no metadata",
/* 2053 */  "",
/* 2054 */  "This feature is not implemented or disabled",
/* 2055 */  "Lost connection to server at '%s', system error: %d",
/* 2056 */  "Server closed statement due to a prior %s function call",
/* 2057 */  "The number of parameters in bound buffers differs from number of columns in resultset",
/* 2058 */  "Can't connect twice. Already connected",
/* 2059 */  "Plugin %s could not be loaded: %s",
/* 2060 */  "An attribute with same name already exists",
/* 2061 */  "Plugin doesn't support this function",
            ""
};

const char *mariadb_client_errors[] =
{
  /* 5000 */ "Creating an event failed (Errorcode: %d)",
  /* 5001 */ "Bind to local interface '-.%64s' failed (Errorcode: %d)",
  /* 5002 */ "Connection type doesn't support asynchronous IO operations",
  /* 5003 */ "Server doesn't support function '%s'",
  /* 5004 */ "File '%s' not found (Errcode: %d)",
  /* 5005 */ "Error reading file '%s' (Errcode: %d)",
  /* 5006 */ "Bulk operation without parameters is not supported",
  /* 5007 */ "Invalid statement handle",
  /* 5008 */ "Unsupported version %d. Supported versions are in the range %d - %d",
  /* 5009 */ "Invalid or missing parameter '%s'.",
  /* 5010 */ "Authentication plugin '%s' couldn't be found in restricted_auth plugin list.",
  /* 5011 */ "Parse error in connection string (offset %d)",
  /* 5012 */ "Error while loading plugin '%s'",
  /* 5013 */ "Read error: %s (%d)",
  /* 5014 */ "Write error: %s (%d)",
  /* 5015 */ "Error while uncompressing packet",
  /* 5016 */ "Error while retrieving parameter from callback function",
  /* 5017 */ "Error while uncompressing packet (File: %.*s start_pos=%ld).",
  /* 5018 */ "Checksum verification failed (File: %.*s start_pos=%ld). Reported checksum is %x, while calculated checksum is %x.",
  /* 5019 */ "Binary log protocol error (File: %.*s start_pos=%ld): Log format %d not supported.",
  /* 5020 */ "Binary log error (File: %.*s start_pos=%ld): Unknown event type (%d) with flag 'not_ignorable'.",
  /* 5021 */ "Binary log error (File: %.*s start_pos=%ld): %s.",
  /* 5022 */ "File '%s' is not a binary log file",
  /* 5023 */ "Semi sync request error: %s",
  /* 5024 */ "Invalid client flags (%lu) specified. Supported flags: %lu",
  /* 5025 */ "Statement has no result set",
  /* 5026 */ "Server returned an error packet without further information",
  ""
};

const char ** NEAR my_errmsg[MAXMAPS]={0,0,0,0};
char NEAR errbuff[NRERRBUFFS][ERRMSGSIZE];

void init_client_errs(void)
{
  my_errmsg[CLIENT_ERRMAP] = &client_errors[0];
}

