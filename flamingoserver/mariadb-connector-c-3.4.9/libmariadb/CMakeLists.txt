INCLUDE_DIRECTORIES(${CC_SOURCE_DIR}/include
                    ${CC_SOURCE_DIR}/libmariadb)

add_compile_definitions(HAVE_COMPRESS)
add_compile_definitions(LIBMARIADB)
add_compile_definitions(THREAD)

IF(APPLE)
  add_compile_definitions(_XOPEN_SOURCE)
ENDIF()

INCLUDE(${CC_SOURCE_DIR}/cmake/sign.cmake)

SET(MARIADB_LIB_SYMBOLS
 mariadb_cancel
 mariadb_connection
 mariadb_convert_string
 ma_pvio_register_callback
 mariadb_get_charset_by_name
 mariadb_stmt_execute_direct
 mariadb_get_charset_by_nr
 mariadb_get_info
 mariadb_get_infov
 mysql_get_timeout_value
 mysql_get_timeout_value_ms
 mysql_optionsv
 mysql_ps_fetch_functions
 mariadb_reconnect
 mysql_stmt_warning_count
 mariadb_stmt_fetch_fields
 mariadb_rpl_open
 mariadb_rpl_close
 mariadb_rpl_fetch
 mariadb_rpl_optionsv
 mariadb_rpl_get_optionsv
 mariadb_rpl_init_ex
 mariadb_free_rpl_event
 mariadb_field_attr
)

SET(MARIADB_LIB_3_3_5_SYMBOLS
 mariadb_rpl_extract_rows
 mariadb_rpl_error
 mariadb_rpl_errno)
IF(WITH_SSL)
  SET(MARIADB_LIB_SYMBOLS ${MARIADB_LIB_SYMBOLS} mariadb_deinitialize_ssl)
ENDIF()

SET(MYSQL_LIB_SYMBOLS
 mysql_affected_rows
 mysql_autocommit
 mysql_change_user
 mysql_character_set_name
 mysql_client_find_plugin
 mysql_client_register_plugin
 mysql_close
 mysql_commit
 mysql_data_seek
 mysql_debug
 mysql_dump_debug_info
 mysql_embedded
 mysql_eof
 mysql_errno
 mysql_error
 mysql_escape_string
 mysql_fetch_field
 mysql_fetch_field_direct
 mysql_fetch_fields
 mysql_fetch_lengths
 mysql_fetch_row
 mysql_field_count
 mysql_field_seek
 mysql_field_tell
 mysql_free_result
 mysql_get_character_set_info
 mysql_get_charset_by_name
 mysql_get_charset_by_nr
 mysql_get_client_info
 mysql_get_client_version
 mysql_get_host_info
 mysql_get_option
 mysql_get_optionv
 mysql_get_parameters
 mysql_get_proto_info
 mysql_get_server_info
 mysql_get_server_name
 mysql_get_server_version
 mysql_get_socket
 mysql_get_ssl_cipher
 mysql_hex_string
 mysql_info
 mysql_init
 mysql_insert_id
 mysql_kill
 mysql_list_dbs
 mysql_list_fields
 mysql_list_processes
 mysql_list_tables
 mysql_load_plugin
 mysql_load_plugin_v
 mysql_more_results
 mysql_net_field_length
 mysql_net_read_packet
 mysql_next_result
 mysql_num_fields
 mysql_num_rows
 mysql_options
 mysql_options4
 mysql_ping
 mysql_query
 mysql_read_query_result
 mysql_real_connect
 mysql_real_escape_string
 mysql_real_query
 mysql_refresh
 mysql_reset_connection
 mysql_rollback
 mysql_row_seek
 mysql_row_tell
 mysql_select_db
 mysql_send_query
 mysql_server_end
 mysql_server_init
 mysql_session_track_get_next
 mysql_session_track_get_first
 mysql_set_character_set
 mysql_set_local_infile_default
 mysql_set_local_infile_handler
 mysql_set_server_option
 mysql_shutdown
 mysql_sqlstate
 mysql_ssl_set
 mysql_stat
 mysql_stmt_affected_rows
 mysql_stmt_attr_get
 mysql_stmt_attr_set
 mysql_stmt_bind_param
 mysql_stmt_bind_result
 mysql_stmt_close
 mysql_stmt_data_seek
 mysql_stmt_errno
 mysql_stmt_error
 mysql_stmt_execute
 mysql_stmt_fetch
 mysql_stmt_fetch_column
 mysql_stmt_field_count
 mysql_stmt_free_result
 mysql_stmt_init
 mysql_stmt_insert_id
 mysql_stmt_more_results
 mysql_stmt_next_result
 mysql_stmt_num_rows
 mysql_stmt_param_count
 mysql_stmt_param_metadata
 mysql_stmt_prepare
 mysql_stmt_reset
 mysql_stmt_result_metadata
 mysql_stmt_row_seek
 mysql_stmt_row_tell
 mysql_stmt_send_long_data
 mysql_stmt_sqlstate
 mysql_stmt_store_result
 mysql_store_result
 mysql_thread_end
 mysql_thread_id
 mysql_thread_init
 mysql_thread_safe
 mysql_use_result
 mysql_warning_count)

IF(ZLIB_FOUND AND WITH_EXTERNAL_ZLIB)
  INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIR})
ELSE()
  SET(ZLIB_SOURCES
    ${CC_SOURCE_DIR}/external/zlib/adler32.c
    ${CC_SOURCE_DIR}/external/zlib/compress.c
    ${CC_SOURCE_DIR}/external/zlib/crc32.c
    ${CC_SOURCE_DIR}/external/zlib/deflate.c
    ${CC_SOURCE_DIR}/external/zlib/gzclose.c
    ${CC_SOURCE_DIR}/external/zlib/gzlib.c
    ${CC_SOURCE_DIR}/external/zlib/gzread.c
    ${CC_SOURCE_DIR}/external/zlib/gzwrite.c
    ${CC_SOURCE_DIR}/external/zlib/infback.c
    ${CC_SOURCE_DIR}/external/zlib/inffast.c
    ${CC_SOURCE_DIR}/external/zlib/inflate.c
    ${CC_SOURCE_DIR}/external/zlib/inftrees.c
    ${CC_SOURCE_DIR}/external/zlib/trees.c
    ${CC_SOURCE_DIR}/external/zlib/uncompr.c
    ${CC_SOURCE_DIR}/external/zlib/zutil.c
  )
  INCLUDE_DIRECTORIES(${CC_SOURCE_DIR}/external/zlib ${CC_BINARY_DIR}/external/zlib)
ENDIF()


SET(MARIADB_DYNCOL_SYMBOLS
 mariadb_dyncol_check
 mariadb_dyncol_column_cmp_named
 mariadb_dyncol_column_count
 mariadb_dyncol_create_many_named
 mariadb_dyncol_create_many_num
 mariadb_dyncol_exists_named
 mariadb_dyncol_exists_num
 mariadb_dyncol_free
 mariadb_dyncol_get_named
 mariadb_dyncol_get_num
 mariadb_dyncol_has_names
 mariadb_dyncol_json
 mariadb_dyncol_list_named
 mariadb_dyncol_list_num
 mariadb_dyncol_unpack
 mariadb_dyncol_update_many_named
 mariadb_dyncol_update_many_num
 mariadb_dyncol_val_double
 mariadb_dyncol_val_long
 mariadb_dyncol_val_str)

SET(MARIADB_NONBLOCK_SYMBOLS
 mysql_autocommit_cont
 mysql_autocommit_start
 mysql_change_user_cont
 mysql_change_user_start
 mysql_close_cont
 mysql_close_start
 mysql_commit_cont
 mysql_commit_start
 mysql_dump_debug_info_cont
 mysql_dump_debug_info_start
 mysql_fetch_row_cont
 mysql_fetch_row_start
 mysql_free_result_cont
 mysql_free_result_start
 mysql_kill_cont
 mysql_kill_start
 mysql_list_fields_cont
 mysql_list_fields_start
 mysql_next_result_cont
 mysql_next_result_start
 mysql_ping_cont
 mysql_ping_start
 mysql_reset_connection_start
 mysql_reset_connection_cont
 mysql_query_cont
 mysql_query_start
 mysql_read_query_result_cont
 mysql_read_query_result_start
 mysql_real_connect_cont
 mysql_real_connect_start
 mysql_real_query_cont
 mysql_real_query_start
 mysql_refresh_cont
 mysql_refresh_start
 mysql_rollback_cont
 mysql_rollback_start
 mysql_select_db_cont
 mysql_select_db_start
 mysql_send_query_cont
 mysql_send_query_start
 mysql_set_character_set_cont
 mysql_set_character_set_start
 mysql_set_server_option_cont
 mysql_set_server_option_start
 mysql_shutdown_cont
 mysql_shutdown_start
 mysql_stat_cont
 mysql_stat_start
 mysql_stmt_close_cont
 mysql_stmt_close_start
 mysql_stmt_execute_cont
 mysql_stmt_execute_start
 mysql_stmt_fetch_cont
 mysql_stmt_fetch_start
 mysql_stmt_free_result_cont
 mysql_stmt_free_result_start
 mysql_stmt_next_result_cont
 mysql_stmt_next_result_start
 mysql_stmt_prepare_cont
 mysql_stmt_prepare_start
 mysql_stmt_reset_cont
 mysql_stmt_reset_start
 mysql_stmt_send_long_data_cont
 mysql_stmt_send_long_data_start
 mysql_stmt_store_result_cont
 mysql_stmt_store_result_start
 mysql_store_result_cont
 mysql_store_result_start
)

# handle static plugins
SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES} ${LIBMARIADB_PLUGIN_SOURCES})
SET(SYSTEM_LIBS ${SYSTEM_LIBS} ${LIBMARIADB_PLUGIN_LIBS} ${ZSTD_LIBRARY} ${ZLIB_LIBRARY})
MESSAGE(STATUS "SYSTEM_LIBS: ${SYSTEM_LIBS}")
INCLUDE_DIRECTORIES(${LIBMARIADB_PLUGIN_INCLUDES})
ADD_DEFINITIONS(${LIBMARIADB_PLUGIN_DEFS})
FOREACH(plugin ${PLUGINS_STATIC})
  SET(EXTERNAL_PLUGINS "${EXTERNAL_PLUGINS} extern struct st_mysql_client_plugin_${PLUGIN_${plugin}_TYPE} ${plugin}_client_plugin;\n")
  SET(BUILTIN_PLUGINS "${BUILTIN_PLUGINS}   (struct st_mysql_client_plugin *)&${plugin}_client_plugin,\n")
ENDFOREACH()
CONFIGURE_FILE(${CC_SOURCE_DIR}/libmariadb/ma_client_plugin.c.in
               ${CC_BINARY_DIR}/libmariadb/ma_client_plugin.c)

SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES}
${CC_SOURCE_DIR}/plugins/auth/my_auth.c
ma_array.c
ma_charset.c
ma_decimal.c
ma_hashtbl.c
ma_net.c
mariadb_charset.c
ma_time.c
ma_default.c
ma_errmsg.c
mariadb_lib.c
ma_list.c
ma_pvio.c
ma_tls.c
ma_alloc.c
ma_compress.c
ma_init.c
ma_password.c
ma_ll2str.c
mariadb_stmt.c
ma_loaddata.c
ma_stmt_codec.c
ma_string.c
ma_dtoa.c
mariadb_rpl.c
${CC_BINARY_DIR}/libmariadb/ma_client_plugin.c
ma_io.c
${SSL_SOURCES}
)

IF(WIN32)
  add_compile_definitions(SIZEOF_CHARP=${CMAKE_SIZEOF_VOID_P})
  INCLUDE_DIRECTORIES(${CC_SOURCE_DIR}/win-iconv)
  SET(LIBMARIADB_SOURCES
    ${LIBMARIADB_SOURCES}
    ${CC_SOURCE_DIR}/win-iconv/win_iconv.c
    win32_errmsg.c
    win32_errmsg.h)
    IF(WITH_SSL STREQUAL "SCHANNEL")
      SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES}
          secure/win_crypt.c)
    ENDIF()
ELSE()
  IF(ICONV_INCLUDE_DIR)
     INCLUDE_DIRECTORIES(BEFORE ${ICONV_INCLUDE_DIR}) 
  ENDIF()
  IF(NOT CMAKE_SYSTEM_NAME MATCHES AIX)
    add_compile_definitions(LIBICONV_PLUG)
  ENDIF()
  IF(APPLE)
    add_compile_options(-Wno-deprecated-declarations)
  ENDIF()
ENDIF()

IF(ZLIB_FOUND AND WITH_EXTERNAL_ZLIB)
  INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIR})
ELSE()
  SET(ZLIB_SOURCES
    ${CC_SOURCE_DIR}/external/zlib/adler32.c
    ${CC_SOURCE_DIR}/external/zlib/compress.c
    ${CC_SOURCE_DIR}/external/zlib/crc32.c
    ${CC_SOURCE_DIR}/external/zlib/deflate.c
    ${CC_SOURCE_DIR}/external/zlib/gzclose.c
    ${CC_SOURCE_DIR}/external/zlib/gzlib.c
    ${CC_SOURCE_DIR}/external/zlib/gzread.c
    ${CC_SOURCE_DIR}/external/zlib/gzwrite.c
    ${CC_SOURCE_DIR}/external/zlib/infback.c
    ${CC_SOURCE_DIR}/external/zlib/inffast.c
    ${CC_SOURCE_DIR}/external/zlib/inflate.c
    ${CC_SOURCE_DIR}/external/zlib/inftrees.c
    ${CC_SOURCE_DIR}/external/zlib/trees.c
    ${CC_SOURCE_DIR}/external/zlib/uncompr.c
    ${CC_SOURCE_DIR}/external/zlib/zutil.c
  )
  IF(MSVC)
    SET_SOURCE_FILES_PROPERTIES(${ZLIB_SOURCES} PROPERTIES COMPILE_FLAGS -W0)
  ENDIF()
  SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES} ${ZLIB_SOURCES})
  INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/external/zlib)
ENDIF()

IF(WITH_DYNCOL)
  MESSAGE1(WITH_DYNCOL "Dynamic column API support: ON")
  SET(MARIADB_LIB_SYMBOLS ${MARIADB_LIB_SYMBOLS} ${MARIADB_DYNCOL_SYMBOLS})
  SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES} mariadb_dyncol.c)
ENDIF()

SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES} mariadb_async.c ma_context.c)
IF(WITH_BOOST_CONTEXT)
  SET(LIBMARIADB_SOURCES ${LIBMARIADB_SOURCES} ma_boost_context.cc)
ENDIF()
SET(MARIADB_LIB_SYMBOLS ${MARIADB_LIB_SYMBOLS} ${MARIADB_NONBLOCK_SYMBOLS})

INCLUDE(${CC_SOURCE_DIR}/cmake/export.cmake)
IF(NOT WIN32)
  CREATE_EXPORT_FILE(WRITE mariadbclient.def
                   "libmysqlclient_18"
                   "${MYSQL_LIB_SYMBOLS}"
                   "libmariadbclient_18")
  CREATE_EXPORT_FILE(APPEND mariadbclient.def
                   "libmariadb_3"
                   "${MARIADB_LIB_SYMBOLS}"
                   "")
  CREATE_EXPORT_FILE(APPEND mariadbclient.def
                   "libmariadb_3_3_5"
                   "${MARIADB_LIB_3_3_5_SYMBOLS}"
                   "")
ELSE()
  CREATE_EXPORT_FILE(WRITE mariadbclient.def
                   "libmariadb_3"
                   "${MARIADB_LIB_SYMBOLS};${MYSQL_LIB_SYMBOLS};${MARIADB_LIB_3_3_5_SYMBOLS}"
                   "")
ENDIF()


# CREATE OBJECT LIBRARY
ADD_LIBRARY(mariadb_obj OBJECT ${LIBMARIADB_SOURCES})
IF(UNIX)
  SET_TARGET_PROPERTIES(mariadb_obj PROPERTIES COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
ENDIF()
SET (MARIADB_OBJECTS $<TARGET_OBJECTS:mariadb_obj>)

# Xcode doesn't support targets that have only object files,
# so let's add an empty file to keep Xcode happy
IF(CMAKE_GENERATOR MATCHES Xcode)
  FILE(WRITE ${CC_BINARY_DIR}/libmariadb/empty.c "")
  SET(EMPTY_FILE ${CC_BINARY_DIR}/libmariadb/empty.c)
ENDIF()

#* create file with list of functions */
FILE(WRITE ${CC_BINARY_DIR}/manpages.list "${MARIADB_LIB_SYMBOLS};${MYSQL_LIB_SYMBOLS}")

IF(WIN32)
  SET_VERSION_INFO("TARGET:libmariadb"
                   "FILE_TYPE:VFT_DLL"
                   "SOURCE_FILE:libmariadb/libmariadb.c"
                   "ORIGINAL_FILE_NAME:libmariadb.dll"
                   "FILE_DESCRIPTION:Dynamic lib for client/server communication")
ENDIF()

ADD_LIBRARY(mariadbclient STATIC  ${MARIADB_OBJECTS} ${EMPTY_FILE})
TARGET_LINK_LIBRARIES(mariadbclient ${SYSTEM_LIBS} ${CRYPT_LIBS})

IF(UNIX)
  ADD_LIBRARY(libmariadb SHARED ${libmariadb_RC} ${MARIADB_OBJECTS} ${EMPTY_FILE})
  SET_TARGET_PROPERTIES(libmariadb PROPERTIES COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
ELSE()
  ADD_LIBRARY(libmariadb SHARED ${libmariadb_RC} ${MARIADB_OBJECTS} mariadbclient.def)
  SET_TARGET_PROPERTIES(libmariadb PROPERTIES LINKER_LANGUAGE C)
ENDIF()

TARGET_LINK_LIBRARIES(libmariadb LINK_PRIVATE ${SYSTEM_LIBS} ${CRYPT_LIBS})

SIGN_TARGET(libmariadb)

IF(CMAKE_SYSTEM_NAME MATCHES "Linux" OR
   CMAKE_SYSTEM_NAME MATCHES "kFreeBSD" OR
   CMAKE_SYSTEM_NAME MATCHES "GNU")
  IF (NOT WITH_ASAN AND NOT WITH_TSAN AND NOT WITH_UBSAN AND NOT WITH_MSAN)
    TARGET_LINK_LIBRARIES (libmariadb LINK_PRIVATE "-Wl,--no-undefined")
  ENDIF()
  SET_TARGET_PROPERTIES(libmariadb PROPERTIES LINK_FLAGS "${CC_BINARY_DIR}/libmariadb/mariadbclient.def")
ENDIF()

SET_TARGET_PROPERTIES(mariadbclient PROPERTIES IMPORTED_INTERFACE_LINK_LIBRARIES "${SYSTEM_LIBS}")
SET_TARGET_PROPERTIES(libmariadb PROPERTIES IMPORTED_INTERFACE_LINK_LIBRARIES "${SYSTEM_LIBS}")

SET_TARGET_PROPERTIES(libmariadb PROPERTIES PREFIX "")

#
# Installation
#
INCLUDE(${CC_SOURCE_DIR}/cmake/symlink.cmake)

# There are still several projects which don't make use
# of the config program. To make sure these programs can
# use mariadb client library we provide libmysql symlinks

IF(WITH_MYSQLCOMPAT)
  create_symlink(libmysqlclient${CMAKE_SHARED_LIBRARY_SUFFIX} libmariadb ${INSTALL_LIBDIR})
  create_symlink(libmysqlclient_r${CMAKE_SHARED_LIBRARY_SUFFIX} libmariadb ${INSTALL_LIBDIR})
  IF(NOT CMAKE_SYSTEM_NAME MATCHES AIX)
    create_symlink(libmysqlclient${CMAKE_STATIC_LIBRARY_SUFFIX} mariadbclient ${INSTALL_LIBDIR})
    create_symlink(libmysqlclient_r${CMAKE_STATIC_LIBRARY_SUFFIX} mariadbclient ${INSTALL_LIBDIR})
  ENDIF()
ENDIF()

create_symlink(libmariadb${CMAKE_STATIC_LIBRARY_SUFFIX} mariadbclient ${INSTALL_LIBDIR})

SET_TARGET_PROPERTIES(libmariadb PROPERTIES VERSION
 ${CPACK_PACKAGE_VERSION_MAJOR}
 SOVERSION ${CPACK_PACKAGE_VERSION_MAJOR})

IF(NOT WIN32)
  SET_TARGET_PROPERTIES(mariadbclient PROPERTIES OUTPUT_NAME "${LIBMARIADB_STATIC_NAME}")
ENDIF()

INSTALL(TARGETS mariadbclient
          COMPONENT Development
          DESTINATION ${INSTALL_LIBDIR})
IF(WIN32)
INSTALL(TARGETS libmariadb
        COMPONENT SharedLibraries
        DESTINATION ${INSTALL_LIBDIR})
ELSE()
INSTALL(TARGETS libmariadb LIBRARY DESTINATION ${INSTALL_LIBDIR}
        COMPONENT SharedLibraries NAMELINK_COMPONENT Development)
ENDIF()

IF(MSVC)
   # On Windows, install PDB
   INSTALL(FILES $<TARGET_PDB_FILE:libmariadb> DESTINATION "${INSTALL_LIBDIR}"
           CONFIGURATIONS Debug RelWithDebInfo
           COMPONENT Development)
ENDIF()
