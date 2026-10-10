/* Copyright Abandoned 1996, 1999, 2001 MySQL AB
   This file is public domain and comes with NO WARRANTY of any kind */

/* Version numbers for protocol & mysqld */

#ifndef _mariadb_version_h_
#define _mariadb_version_h_

#ifdef _CUSTOMCONFIG_
#include <custom_conf.h>
#else
#define PROTOCOL_VERSION		@PROTOCOL_VERSION@
#define MARIADB_CLIENT_VERSION_STR	"@MARIADB_CLIENT_VERSION@"
#define MARIADB_BASE_VERSION		"@MARIADB_BASE_VERSION@"
#define MARIADB_VERSION_ID		@MARIADB_VERSION_ID@
#define MARIADB_PORT	        	@MARIADB_PORT@
#define MARIADB_UNIX_ADDR               "@MARIADB_UNIX_ADDR@"
#ifndef MYSQL_UNIX_ADDR
#define MYSQL_UNIX_ADDR MARIADB_UNIX_ADDR
#endif
#ifndef MYSQL_PORT
#define MYSQL_PORT MARIADB_PORT
#endif

#define MYSQL_CONFIG_NAME               "my"
#define MYSQL_VERSION_ID                @MARIADB_VERSION_ID@
#define MYSQL_SERVER_VERSION            "@MARIADB_CLIENT_VERSION@-MariaDB"

#define MARIADB_PACKAGE_VERSION "@CPACK_PACKAGE_VERSION@"
#define MARIADB_PACKAGE_VERSION_ID @MARIADB_PACKAGE_VERSION_ID@
#define MARIADB_SYSTEM_TYPE "@CMAKE_SYSTEM_NAME@"
#define MARIADB_MACHINE_TYPE "@CMAKE_SYSTEM_PROCESSOR@"
#define MARIADB_PLUGINDIR "@CMAKE_INSTALL_PREFIX@/@INSTALL_PLUGINDIR@"

/* mysqld compile time options */
#ifndef MYSQL_CHARSET
#define MYSQL_CHARSET			"@default_charset@"
#endif
#endif

/* Source information */
#define CC_SOURCE_REVISION "@CC_SOURCE_REVISION@"

#endif /* _mariadb_version_h_ */
