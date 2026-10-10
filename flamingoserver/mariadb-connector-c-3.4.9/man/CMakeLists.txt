SET(CC_MAN_PAGES_3
mariadb_cancel.3
mariadb_connection.3
mariadb_dyncol_check.3
mariadb_dyncol_column_cmp_named.3
mariadb_dyncol_column_count.3
mariadb_dyncol_create_many_named.3
mariadb_dyncol_create_many_num.3
mariadb_dyncol_exists_named.3
mariadb_dyncol_exists_num.3
mariadb_dyncol_free.3
mariadb_dyncol_list_named.3
mariadb_dyncol_list_num.3
mariadb_dyncol_unpack.3
mariadb_dyncol_update_many_named.3
mariadb_dyncol_update_many_num.3
mariadb_get_infov.3
mariadb_reconnect.3
mariadb_rpl_close.3
mariadb_rpl_fetch.3
mariadb_rpl_get_optionsv.3
mariadb_rpl_open.3
mariadb_rpl_optionsv.3
mariadb_stmt_execute_direct.3
mariadb_stmt_fetch_fields.3
mysql_affected_rows.3
mysql_autocommit.3
mysql_change_user.3
mysql_close.3
mysql_commit.3
mysql_data_seek.3
mysql_errno.3
mysql_error.3
mysql_fetch_field.3
mysql_fetch_field_direct.3
mysql_fetch_fields.3
mysql_fetch_lengths.3
mysql_fetch_row.3
mysql_field_count.3
mysql_field_seek.3
mysql_field_tell.3
mysql_free_result.3
mysql_get_character_set_info.3
mysql_get_client_info.3
mysql_get_client_version.3
mysql_get_host_info.3
mysql_get_proto_info.3
mysql_get_server_info.3
mysql_get_server_version.3
mysql_get_socket.3
mysql_get_ssl_cipher.3
mysql_hex_string.3
mysql_info.3
mysql_init.3
mysql_kill.3
mysql_more_results.3
mysql_next_result.3
mysql_num_fields.3
mysql_num_rows.3
mysql_options.3
mysql_options4.3
mysql_optionsv.3
mysql_ping.3
mysql_query.3
mysql_read_query_result.3
mysql_real_connect.3
mysql_real_escape_string.3
mysql_real_query.3
mysql_refresh.3
mysql_reset_connection.3
mysql_rollback.3
mysql_row_seek.3
mysql_row_tell.3
mysql_select_db.3
mysql_send_query.3
mysql_server_end.3
mysql_server_init.3
mysql_session_track_get_first.3
mysql_session_track_get_next.3
mysql_set_character_set.3
mysql_set_server_option.3
mysql_shutdown.3
mysql_sqlstate.3
mysql_ssl_set.3
mysql_stat.3
mysql_stmt_affected_rows.3
mysql_stmt_attr_get.3
mysql_stmt_attr_set.3
mysql_stmt_bind_param.3
mysql_stmt_bind_result.3
mysql_stmt_close.3
mysql_stmt_data_seek.3
mysql_stmt_errno.3
mysql_stmt_error.3
mysql_stmt_execute.3
mysql_stmt_fetch.3
mysql_stmt_fetch_column.3
mysql_stmt_field_count.3
mysql_stmt_free_result.3
mysql_stmt_init.3
mysql_stmt_insert_id.3
mysql_stmt_more_results.3
mysql_stmt_next_result.3
mysql_stmt_num_rows.3
mysql_stmt_param_count.3
mysql_stmt_param_metadata.3
mysql_stmt_prepare.3
mysql_stmt_reset.3
mysql_stmt_result_metadata.3
mysql_stmt_row_seek.3
mysql_stmt_row_tell.3
mysql_stmt_send_long_data.3
mysql_stmt_sqlstate.3
mysql_stmt_store_result.3
mysql_stmt_warning_count.3
mysql_store_result.3
mysql_thread_end.3
mysql_thread_id.3
mysql_thread_init.3
mysql_use_result.3
mysql_warning_count.3)

IF(WITH_DOCS)
set(Python3_FIND_STRATEGY VERSION)
find_package (Python3 REQUIRED)
ADD_CUSTOM_COMMAND(OUTPUT ${CC_MAN_PAGES_3}
                   COMMAND python3 ${CC_SOURCE_DIR}/scripts/create_man
                                   --docs=${CC_SOURCE_DIR}/docs --funcs=${CC_BINARY_DIR}/manpages.list
                                   --version=${CPACK_PACKAGE_VERSION} --out=${CC_BINARY_DIR}/man3
                   DEPENDS ${CC_BINARY_DIR}/manpages.list)
ADD_CUSTOM_TARGET(docs ALL DEPENDS ${CC_MAN_PAGES_3})
INSTALL(DIRECTORY ${CC_BINARY_DIR}/man3
        DESTINATION ${INSTALL_MANDIR}
        COMPONENT ManPagesDevelopment
        FILES_MATCHING PATTERN "*.3")
ELSE()
INSTALL(FILES ${CC_MAN_PAGES_3} DESTINATION ${INSTALL_MANDIR}/man3 COMPONENT ManPagesDevelopment)
ENDIF()

