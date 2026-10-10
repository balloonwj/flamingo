/**
 *  SQLite 库结构数据，SqliteSchema.cpp
 *  balloonwj@qq.com   2026-07-31
 *
 *  本文件由 tools/gen_table_sqlite.py 从 table_mysql.sql 自动生成，请勿手工编辑。
 *  改了 table_mysql.sql 之后重跑该脚本：python tools/gen_table_sqlite.py
 *
 *  文件内容与同时生成的 table_sqlite.sql 一致，只是把语句嵌进了程序，
 *  这样 chatserver 建表时不依赖运行期能否读到那个 .sql 文件。
 *  非 ASCII 字节用八进制转义，原因见生成脚本里 render_c_string 的注释。
 */
#include "SqliteSchema.h"

namespace sqliteapi {

const char* const SCHEMA_TABLE_STATEMENTS[] =
{
    "CREATE TABLE IF NOT EXISTS t_user\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_user_id INTEGER NOT NULL,\n"
        "    f_username VARCHAR(64) COLLATE NOCASE NOT NULL,\n"
        "    f_nickname VARCHAR(64) NOT NULL,\n"
        "    f_password VARCHAR(64) NOT NULL,\n"
        "    f_salt VARCHAR(32) DEFAULT NULL,\n"
        "    f_facetype INTEGER DEFAULT 0,\n"
        "    f_customface VARCHAR(32) DEFAULT NULL,\n"
        "    f_customfacefmt VARCHAR(6) DEFAULT NULL,\n"
        "    f_gender INTEGER DEFAULT 0,\n"
        "    f_birthday INTEGER DEFAULT 19900101,\n"
        "    f_signature VARCHAR(256) DEFAULT NULL,\n"
        "    f_address VARCHAR(256) DEFAULT NULL,\n"
        "    f_phonenumber VARCHAR(64) DEFAULT NULL,\n"
        "    f_mail VARCHAR(256) COLLATE NOCASE DEFAULT NULL,\n"
        "    f_register_time DATETIME NOT NULL,\n"
        "    f_owner_id INTEGER DEFAULT 0,\n"
        "    f_remark VARCHAR(64) DEFAULT NULL,\n"
        "    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    f_teaminfo BLOB DEFAULT NULL,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_role INTEGER NOT NULL DEFAULT 0,\n"
        "    f_employee_no VARCHAR(32) DEFAULT NULL,\n"
        "    f_join_date DATETIME DEFAULT NULL,\n"
        "    f_landline VARCHAR(64) DEFAULT NULL,\n"
        "    f_post VARCHAR(64) DEFAULT NULL,\n"
        "    f_phone_visible INTEGER NOT NULL DEFAULT 0,\n"
        "    f_landline_visible INTEGER NOT NULL DEFAULT 1,\n"
        "    f_workstation VARCHAR(256) DEFAULT NULL,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_usertype INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_chatmsg\n"
        "(\n"
        "    f_msgid INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_clientmsgid VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_ctype INTEGER NOT NULL DEFAULT 1,\n"
        "    f_from INTEGER NOT NULL,\n"
        "    f_to INTEGER NOT NULL,\n"
        "    f_msgtype INTEGER NOT NULL DEFAULT 1,\n"
        "    f_content BLOB,\n"
        "    f_recalled INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_audience INTEGER NOT NULL DEFAULT 0,\n"
        "    f_recalled_by INTEGER NOT NULL DEFAULT 0,\n"
        "    f_search_text TEXT,\n"
        "    f_rev INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_department\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_dept_id INTEGER NOT NULL,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_dept_name VARCHAR(64) NOT NULL,\n"
        "    f_parent_dept_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_dept_path VARCHAR(255) NOT NULL DEFAULT '',\n"
        "    f_dept_level INTEGER NOT NULL DEFAULT 1,\n"
        "    f_dept_order INTEGER NOT NULL DEFAULT 0,\n"
        "    f_leader_user_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_dept_desc VARCHAR(256) DEFAULT NULL,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time DATETIME NOT NULL,\n"
        "    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    f_remark VARCHAR(64) DEFAULT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_department_member\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_dept_id INTEGER NOT NULL,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_user_id INTEGER NOT NULL,\n"
        "    f_position VARCHAR(64) DEFAULT NULL,\n"
        "    f_is_primary INTEGER NOT NULL DEFAULT 1,\n"
        "    f_member_order INTEGER NOT NULL DEFAULT 0,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_join_time DATETIME NOT NULL,\n"
        "    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    f_remark VARCHAR(64) DEFAULT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_enterprise\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_enterprise_id INTEGER NOT NULL,\n"
        "    f_name VARCHAR(128) COLLATE NOCASE NOT NULL,\n"
        "    f_short_name VARCHAR(64) DEFAULT NULL,\n"
        "    f_industry VARCHAR(64) DEFAULT NULL,\n"
        "    f_region VARCHAR(64) DEFAULT NULL,\n"
        "    f_plan INTEGER NOT NULL DEFAULT 0,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_root_dept_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_admin_name VARCHAR(64) DEFAULT NULL,\n"
        "    f_admin_email VARCHAR(128) DEFAULT NULL,\n"
        "    f_logo_color VARCHAR(16) DEFAULT NULL,\n"
        "    f_open_date DATETIME DEFAULT NULL,\n"
        "    f_license_expire_time DATETIME DEFAULT NULL,\n"
        "    f_create_time DATETIME NOT NULL,\n"
        "    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    f_remark VARCHAR(64) DEFAULT NULL,\n"
        "    f_sensitive_on INTEGER NOT NULL DEFAULT 1,\n"
        "    f_account_prefix VARCHAR(16) DEFAULT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_admin\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_admin_id INTEGER NOT NULL,\n"
        "    f_email VARCHAR(128) COLLATE NOCASE NOT NULL,\n"
        "    f_password VARCHAR(64) NOT NULL,\n"
        "    f_salt VARCHAR(32) NOT NULL,\n"
        "    f_name VARCHAR(64) NOT NULL,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_last_login_time DATETIME DEFAULT NULL,\n"
        "    f_last_login_ip VARCHAR(64) DEFAULT NULL,\n"
        "    f_create_time DATETIME NOT NULL,\n"
        "    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    f_remark VARCHAR(64) DEFAULT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_admin_session\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_token VARCHAR(64) NOT NULL,\n"
        "    f_admin_id INTEGER NOT NULL,\n"
        "    f_login_ip VARCHAR(64) DEFAULT NULL,\n"
        "    f_login_time DATETIME NOT NULL,\n"
        "    f_expire_time DATETIME NOT NULL,\n"
        "    f_create_time DATETIME NOT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_admin_audit_log\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_actor_type INTEGER NOT NULL DEFAULT 0,\n"
        "    f_actor_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_actor_name VARCHAR(64) DEFAULT NULL,\n"
        "    f_ip VARCHAR(64) DEFAULT NULL,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_action VARCHAR(64) NOT NULL,\n"
        "    f_target VARCHAR(256) DEFAULT NULL,\n"
        "    f_severity INTEGER NOT NULL DEFAULT 0,\n"
        "    f_detail VARCHAR(512) DEFAULT NULL,\n"
        "    f_create_time DATETIME NOT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_user_permission\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_user_id INTEGER NOT NULL,\n"
        "    f_perm_key VARCHAR(64) NOT NULL,\n"
        "    f_granted INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    f_remark VARCHAR(64) DEFAULT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_user_activity_log\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_user_id INTEGER NOT NULL,\n"
        "    f_action VARCHAR(64) NOT NULL,\n"
        "    f_detail VARCHAR(512) DEFAULT NULL,\n"
        "    f_severity INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time DATETIME NOT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_announcement\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_announcement_id INTEGER NOT NULL,\n"
        "    f_title VARCHAR(256) NOT NULL,\n"
        "    f_body TEXT DEFAULT NULL,\n"
        "    f_severity INTEGER NOT NULL DEFAULT 0,\n"
        "    f_sender_admin_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_sender_name VARCHAR(64) DEFAULT NULL,\n"
        "    f_audience_type INTEGER NOT NULL DEFAULT 0,\n"
        "    f_pinned INTEGER NOT NULL DEFAULT 0,\n"
        "    f_scheduled INTEGER NOT NULL DEFAULT 0,\n"
        "    f_scheduled_time DATETIME DEFAULT NULL,\n"
        "    f_sent_time DATETIME DEFAULT NULL,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time DATETIME NOT NULL,\n"
        "    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    f_remark VARCHAR(64) DEFAULT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_announcement_target\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_announcement_id INTEGER NOT NULL,\n"
        "    f_target_type INTEGER NOT NULL,\n"
        "    f_target_id INTEGER NOT NULL,\n"
        "    f_create_time DATETIME NOT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_announcement_read\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_announcement_id INTEGER NOT NULL,\n"
        "    f_user_id INTEGER NOT NULL,\n"
        "    f_read_time DATETIME NOT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_login_record\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_user_id INTEGER NOT NULL,\n"
        "    f_login_time DATETIME NOT NULL,\n"
        "    f_device VARCHAR(128) DEFAULT NULL,\n"
        "    f_device_kind INTEGER NOT NULL DEFAULT 0,\n"
        "    f_os VARCHAR(64) DEFAULT NULL,\n"
        "    f_ip VARCHAR(64) DEFAULT NULL,\n"
        "    f_location VARCHAR(128) DEFAULT NULL,\n"
        "    f_result INTEGER NOT NULL DEFAULT 0,\n"
        "    f_fail_reason VARCHAR(128) DEFAULT NULL,\n"
        "    f_create_time DATETIME NOT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_service_account\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_service_id INTEGER NOT NULL,\n"
        "    f_enterprise_id INTEGER NOT NULL,\n"
        "    f_name VARCHAR(64) NOT NULL,\n"
        "    f_kind INTEGER NOT NULL DEFAULT 0,\n"
        "    f_description VARCHAR(256) DEFAULT NULL,\n"
        "    f_avatar_color VARCHAR(16) DEFAULT NULL,\n"
        "    f_avatar_glyph VARCHAR(16) DEFAULT NULL,\n"
        "    f_owner_admin_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_allow_unsubscribe INTEGER NOT NULL DEFAULT 1,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time DATETIME NOT NULL,\n"
        "    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    f_remark VARCHAR(64) DEFAULT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_service_account_binding\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_service_id INTEGER NOT NULL,\n"
        "    f_bind_type INTEGER NOT NULL,\n"
        "    f_bind_target_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time DATETIME NOT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_contact_remark\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_target_userid INTEGER NOT NULL,\n"
        "    f_remark VARCHAR(32) NOT NULL DEFAULT '',\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_userid, f_target_userid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_contact_tag\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_target_userid INTEGER NOT NULL,\n"
        "    f_tag VARCHAR(16) COLLATE NOCASE NOT NULL DEFAULT '',\n"
        "    f_order INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_userid, f_target_userid, f_tag)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_blacklist\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_blocked_userid INTEGER NOT NULL,\n"
        "    f_create_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),\n"
        "    PRIMARY KEY (f_userid, f_blocked_userid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_conversation\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_ctype INTEGER NOT NULL DEFAULT 1,\n"
        "    f_cid INTEGER NOT NULL,\n"
        "    f_pinned INTEGER NOT NULL DEFAULT 0,\n"
        "    f_notify_mode INTEGER NOT NULL DEFAULT 0,\n"
        "    f_readmsgid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_deleted INTEGER NOT NULL DEFAULT 0,\n"
        "    f_deleted_msgid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_cleared_msgid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_unread_mark INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_userid, f_ctype, f_cid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_message_deleted\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_msgid INTEGER NOT NULL,\n"
        "    PRIMARY KEY (f_userid, f_msgid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_message_deleted_range\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_begin_time INTEGER NOT NULL,\n"
        "    f_end_time INTEGER NOT NULL,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_userid, f_begin_time)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_file_ack\n"
        "(\n"
        "    f_msgid INTEGER NOT NULL,\n"
        "    f_state INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_msgid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_message_pin\n"
        "(\n"
        "    f_msgid INTEGER NOT NULL,\n"
        "    f_operator INTEGER NOT NULL DEFAULT 0,\n"
        "    f_pin_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_msgid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_favorite\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_ctype INTEGER NOT NULL,\n"
        "    f_cid INTEGER NOT NULL,\n"
        "    f_order INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_userid, f_ctype, f_cid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group\n"
        "(\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_name VARCHAR(64) NOT NULL,\n"
        "    f_facetype INTEGER NOT NULL DEFAULT 0,\n"
        "    f_customface VARCHAR(256) NOT NULL DEFAULT '',\n"
        "    f_announcement VARCHAR(1024) NOT NULL DEFAULT '',\n"
        "    f_ownerid INTEGER NOT NULL,\n"
        "    f_join_mode INTEGER NOT NULL DEFAULT 1,\n"
        "    f_mute_all INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_intro VARCHAR(512) NOT NULL DEFAULT '',\n"
        "    f_at_all_scope INTEGER NOT NULL DEFAULT 0,\n"
        "    f_new_see_history INTEGER NOT NULL DEFAULT 1,\n"
        "    f_member_invite INTEGER NOT NULL DEFAULT 0,\n"
        "    f_welcome_on INTEGER NOT NULL DEFAULT 0,\n"
        "    f_welcome_text VARCHAR(256) NOT NULL DEFAULT '',\n"
        "    f_announce_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_announce_title VARCHAR(128) NOT NULL DEFAULT '',\n"
        "    f_announce_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_name_custom INTEGER NOT NULL DEFAULT 1,\n"
        "    f_must_read_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_group_type INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_groupid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_announcement\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_title VARCHAR(128) NOT NULL DEFAULT '',\n"
        "    f_content VARCHAR(2048) NOT NULL DEFAULT '',\n"
        "    f_severity INTEGER NOT NULL DEFAULT 0,\n"
        "    f_publisher INTEGER NOT NULL DEFAULT 0,\n"
        "    f_publish_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_member\n"
        "(\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_role INTEGER NOT NULL DEFAULT 0,\n"
        "    f_card VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_muted INTEGER NOT NULL DEFAULT 0,\n"
        "    f_join_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_mute_until INTEGER NOT NULL DEFAULT 0,\n"
        "    f_mute_reason VARCHAR(128) NOT NULL DEFAULT '',\n"
        "    f_history_from INTEGER NOT NULL DEFAULT 0,\n"
        "    f_must_read INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_groupid, f_userid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_join_request\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_verify_msg VARCHAR(256) DEFAULT NULL,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_handle_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_handler INTEGER NOT NULL DEFAULT 0,\n"
        "    f_source INTEGER NOT NULL DEFAULT 1,\n"
        "    f_recommender INTEGER NOT NULL DEFAULT 0,\n"
        "    f_history_limit INTEGER NOT NULL DEFAULT -1,\n"
        "    f_invite_id INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_invite\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_inviter INTEGER NOT NULL,\n"
        "    f_invitee INTEGER NOT NULL,\n"
        "    f_history_limit INTEGER NOT NULL DEFAULT -1,\n"
        "    f_state INTEGER NOT NULL DEFAULT 0,\n"
        "    f_msgid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_expire_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_handle_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_sensitive_word\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_enterprise_id INTEGER NOT NULL,\n"
        "    f_word VARCHAR(32) NOT NULL,\n"
        "    f_word_norm VARCHAR(32) NOT NULL,\n"
        "    f_category INTEGER NOT NULL DEFAULT 6,\n"
        "    f_remark VARCHAR(64) DEFAULT NULL,\n"
        "    f_creator_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_creator_name VARCHAR(64) DEFAULT NULL,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_sensitive_hit_log\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_enterprise_id INTEGER NOT NULL,\n"
        "    f_user_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_user_name VARCHAR(64) DEFAULT NULL,\n"
        "    f_source INTEGER NOT NULL,\n"
        "    f_ctype INTEGER NOT NULL DEFAULT 0,\n"
        "    f_cid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_target_name VARCHAR(64) DEFAULT NULL,\n"
        "    f_word_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_word VARCHAR(32) NOT NULL,\n"
        "    f_category INTEGER NOT NULL DEFAULT 6,\n"
        "    f_snippet VARCHAR(128) DEFAULT NULL,\n"
        "    f_create_time INTEGER NOT NULL\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_card_interaction\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_msgid INTEGER NOT NULL,\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_data VARCHAR(1024) NOT NULL DEFAULT '',\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_card_state\n"
        "(\n"
        "    f_msgid INTEGER NOT NULL,\n"
        "    f_closed INTEGER NOT NULL DEFAULT 0,\n"
        "    f_closer INTEGER NOT NULL DEFAULT 0,\n"
        "    f_close_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_msgid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_file\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_md5 CHAR(32) NOT NULL DEFAULT '',\n"
        "    f_name VARCHAR(256) NOT NULL DEFAULT '',\n"
        "    f_ext VARCHAR(32) NOT NULL DEFAULT '',\n"
        "    f_size INTEGER NOT NULL DEFAULT 0,\n"
        "    f_scene INTEGER NOT NULL DEFAULT 0,\n"
        "    f_uploaderid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_createtime INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_file\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_fileid INTEGER NOT NULL,\n"
        "    f_filetype INTEGER NOT NULL DEFAULT 6,\n"
        "    f_uploaderid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_uploadtime INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_client_version\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_platform INTEGER NOT NULL DEFAULT 1,\n"
        "    f_version VARCHAR(32) NOT NULL DEFAULT '',\n"
        "    f_min_version VARCHAR(32) NOT NULL DEFAULT '',\n"
        "    f_release_note VARCHAR(2048) NOT NULL DEFAULT '',\n"
        "    f_package_md5 VARCHAR(32) NOT NULL DEFAULT '',\n"
        "    f_package_size INTEGER NOT NULL DEFAULT 0,\n"
        "    f_package_name VARCHAR(255) NOT NULL DEFAULT '',\n"
        "    f_forced INTEGER NOT NULL DEFAULT 0,\n"
        "    f_enabled INTEGER NOT NULL DEFAULT 0,\n"
        "    f_publish_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_creator_adminid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_message_audience\n"
        "(\n"
        "    f_msgid INTEGER NOT NULL,\n"
        "    f_user_id INTEGER NOT NULL,\n"
        "    PRIMARY KEY (f_msgid, f_user_id)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_file_share\n"
        "(\n"
        "    f_shareid INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_client_share_id VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_owner_id INTEGER NOT NULL,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_device_id VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_ctype INTEGER NOT NULL DEFAULT 1,\n"
        "    f_cid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_kind INTEGER NOT NULL DEFAULT 2,\n"
        "    f_name VARCHAR(260) NOT NULL DEFAULT '',\n"
        "    f_display_path VARCHAR(1024) NOT NULL DEFAULT '',\n"
        "    f_include_hidden INTEGER NOT NULL DEFAULT 0,\n"
        "    f_state INTEGER NOT NULL DEFAULT 1,\n"
        "    f_cancel_reason INTEGER NOT NULL DEFAULT 0,\n"
        "    f_canceled_by INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_cancel_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_file_share_member\n"
        "(\n"
        "    f_shareid INTEGER NOT NULL,\n"
        "    f_user_id INTEGER NOT NULL,\n"
        "    f_perms INTEGER NOT NULL DEFAULT 0,\n"
        "    f_add_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_shareid, f_user_id)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_file_share_audit\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_shareid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_owner_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_actor_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_action INTEGER NOT NULL DEFAULT 0,\n"
        "    f_source INTEGER NOT NULL DEFAULT 1,\n"
        "    f_path VARCHAR(1024) NOT NULL DEFAULT '',\n"
        "    f_to_path VARCHAR(1024) NOT NULL DEFAULT '',\n"
        "    f_size INTEGER NOT NULL DEFAULT 0,\n"
        "    f_result INTEGER NOT NULL DEFAULT 0,\n"
        "    f_access_id VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_system_setting\n"
        "(\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_key VARCHAR(64) NOT NULL,\n"
        "    f_value VARCHAR(1024) NOT NULL DEFAULT '',\n"
        "    f_updated_by INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_enterprise_id, f_key)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_memo\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_ctype INTEGER NOT NULL DEFAULT 1,\n"
        "    f_owner INTEGER NOT NULL,\n"
        "    f_creator INTEGER NOT NULL DEFAULT 0,\n"
        "    f_editor INTEGER NOT NULL DEFAULT 0,\n"
        "    f_title VARCHAR(256) NOT NULL DEFAULT '',\n"
        "    f_note VARCHAR(1024) NOT NULL DEFAULT '',\n"
        "    f_start INTEGER NOT NULL,\n"
        "    f_advance INTEGER NOT NULL DEFAULT 0,\n"
        "    f_repeat INTEGER NOT NULL DEFAULT 0,\n"
        "    f_weekdays INTEGER NOT NULL DEFAULT 0,\n"
        "    f_count INTEGER NOT NULL DEFAULT 1,\n"
        "    f_style INTEGER NOT NULL DEFAULT 1,\n"
        "    f_target_all INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_memo_target\n"
        "(\n"
        "    f_memoid INTEGER NOT NULL,\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    PRIMARY KEY (f_memoid, f_userid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_follow\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_target_userid INTEGER NOT NULL,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_userid, f_groupid, f_target_userid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_qa\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_question VARCHAR(128) NOT NULL DEFAULT '',\n"
        "    f_answer VARCHAR(2048) NOT NULL DEFAULT '',\n"
        "    f_editor INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_enterprise_qa\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_enterprise_id INTEGER NOT NULL,\n"
        "    f_question VARCHAR(128) NOT NULL DEFAULT '',\n"
        "    f_answer VARCHAR(2048) NOT NULL DEFAULT '',\n"
        "    f_scope INTEGER NOT NULL DEFAULT 0,\n"
        "    f_creator_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_creator_name VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_enterprise_qa_group\n"
        "(\n"
        "    f_qa_id INTEGER NOT NULL,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    PRIMARY KEY (f_qa_id, f_groupid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_link\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_code VARCHAR(32) NOT NULL,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_creator INTEGER NOT NULL,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_expire_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_revoked INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_cloud_record\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_owner INTEGER NOT NULL,\n"
        "    f_source INTEGER NOT NULL DEFAULT 1,\n"
        "    f_roomid VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_ctype INTEGER NOT NULL DEFAULT 0,\n"
        "    f_cid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_title VARCHAR(256) NOT NULL DEFAULT '',\n"
        "    f_start_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_end_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_duration_ms INTEGER NOT NULL DEFAULT 0,\n"
        "    f_frec_bytes INTEGER NOT NULL DEFAULT 0,\n"
        "    f_bytes INTEGER NOT NULL DEFAULT 0,\n"
        "    f_audio_only INTEGER NOT NULL DEFAULT 0,\n"
        "    f_status INTEGER NOT NULL DEFAULT 1,\n"
        "    f_frec_file VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_file VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_expire_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_warned INTEGER NOT NULL DEFAULT 0,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_cloud_record_share\n"
        "(\n"
        "    f_recordid INTEGER NOT NULL,\n"
        "    f_ctype INTEGER NOT NULL,\n"
        "    f_cid INTEGER NOT NULL,\n"
        "    f_sender INTEGER NOT NULL DEFAULT 0,\n"
        "    f_send_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_recordid, f_ctype, f_cid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_thread_post\n"
        "(\n"
        "    f_postid INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_author INTEGER NOT NULL,\n"
        "    f_client_postid VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_content BLOB,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_edit_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_active_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_comment_count INTEGER NOT NULL DEFAULT 0,\n"
        "    f_pin_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_cardmsgid INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_thread_addendum\n"
        "(\n"
        "    f_addid INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_postid INTEGER NOT NULL,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_author INTEGER NOT NULL,\n"
        "    f_content BLOB,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_cardmsgid INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_thread_comment\n"
        "(\n"
        "    f_commentid INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_postid INTEGER NOT NULL,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_author INTEGER NOT NULL,\n"
        "    f_parentid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_reply_to INTEGER NOT NULL DEFAULT 0,\n"
        "    f_client_commentid VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_content BLOB,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_deleted INTEGER NOT NULL DEFAULT 0,\n"
        "    f_reply_count INTEGER NOT NULL DEFAULT 0,\n"
        "    f_cardmsgid INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_thread_reaction\n"
        "(\n"
        "    f_target_type INTEGER NOT NULL,\n"
        "    f_target_id INTEGER NOT NULL,\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_reaction INTEGER NOT NULL,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_target_type, f_target_id, f_userid, f_reaction)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_bot\n"
        "(\n"
        "    f_bot_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_user_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_enterprise_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_owner_type INTEGER NOT NULL DEFAULT 1,\n"
        "    f_owner_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_name VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_desc VARCHAR(512) NOT NULL DEFAULT '',\n"
        "    f_glyph VARCHAR(16) NOT NULL DEFAULT '',\n"
        "    f_color VARCHAR(16) NOT NULL DEFAULT '',\n"
        "    f_scope INTEGER NOT NULL DEFAULT 0,\n"
        "    f_allow_group_off INTEGER NOT NULL DEFAULT 0,\n"
        "    f_status INTEGER NOT NULL DEFAULT 0,\n"
        "    f_source INTEGER NOT NULL DEFAULT 0,\n"
        "    f_creator_type INTEGER NOT NULL DEFAULT 1,\n"
        "    f_creator_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_creator_name VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_webhook_token VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_secret VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_last_fire_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_bot_rule\n"
        "(\n"
        "    f_rule_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_bot_id INTEGER NOT NULL,\n"
        "    f_seq INTEGER NOT NULL DEFAULT 0,\n"
        "    f_name VARCHAR(64) NOT NULL DEFAULT '',\n"
        "    f_enabled INTEGER NOT NULL DEFAULT 1,\n"
        "    f_trigger_type INTEGER NOT NULL DEFAULT 1,\n"
        "    f_trigger BLOB,\n"
        "    f_action_type INTEGER NOT NULL DEFAULT 1,\n"
        "    f_action BLOB,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_bot_group\n"
        "(\n"
        "    f_bot_id INTEGER NOT NULL,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    PRIMARY KEY (f_bot_id, f_groupid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_bot_group_off\n"
        "(\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_bot_id INTEGER NOT NULL,\n"
        "    f_operator_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_groupid, f_bot_id)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_bot_domain\n"
        "(\n"
        "    f_enterprise_id INTEGER NOT NULL,\n"
        "    f_domain VARCHAR(128) NOT NULL,\n"
        "    PRIMARY KEY (f_enterprise_id, f_domain)\n"
        ");\n",
};

const int SCHEMA_TABLE_STATEMENT_COUNT = 60;

const char* const SCHEMA_POST_STATEMENTS[] =
{
    "CREATE UNIQUE INDEX IF NOT EXISTS t_user_uk_primary ON t_user(f_user_id);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_user_uk_f_username ON t_user(f_username);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_user_uk_ent_employee_no ON t_user(f_enterprise_id, f_employee_no);\n",
    "CREATE INDEX IF NOT EXISTS t_user_idx_enterprise ON t_user(f_enterprise_id);\n",
    "CREATE INDEX IF NOT EXISTS t_user_f_user_id ON t_user(f_user_id);\n",
    "CREATE TRIGGER IF NOT EXISTS trg_t_user_f_update_time AFTER UPDATE ON t_user\n"
        "FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time\n"
        "BEGIN\n"
        "    UPDATE t_user SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
        "END;\n",
    "CREATE INDEX IF NOT EXISTS t_chatmsg_idx_from_to ON t_chatmsg(f_from, f_to);\n",
    "CREATE INDEX IF NOT EXISTS t_chatmsg_idx_to_from ON t_chatmsg(f_to, f_from);\n",
    "CREATE INDEX IF NOT EXISTS t_chatmsg_idx_client ON t_chatmsg(f_clientmsgid);\n",
    "CREATE INDEX IF NOT EXISTS t_chatmsg_idx_to_msgid ON t_chatmsg(f_to, f_msgid);\n",
    "CREATE INDEX IF NOT EXISTS t_chatmsg_idx_rev ON t_chatmsg(f_rev);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_department_uniq_dept_id ON t_department(f_dept_id);\n",
    "CREATE INDEX IF NOT EXISTS t_department_idx_parent ON t_department(f_parent_dept_id);\n",
    "CREATE INDEX IF NOT EXISTS t_department_idx_path ON t_department(f_dept_path);\n",
    "CREATE INDEX IF NOT EXISTS t_department_idx_leader ON t_department(f_leader_user_id);\n",
    "CREATE INDEX IF NOT EXISTS t_department_idx_enterprise ON t_department(f_enterprise_id);\n",
    "CREATE TRIGGER IF NOT EXISTS trg_t_department_f_update_time AFTER UPDATE ON t_department\n"
        "FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time\n"
        "BEGIN\n"
        "    UPDATE t_department SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
        "END;\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_department_member_uniq_dept_user ON t_department_member(f_dept_id, f_user_id);\n",
    "CREATE INDEX IF NOT EXISTS t_department_member_idx_user ON t_department_member(f_user_id);\n",
    "CREATE INDEX IF NOT EXISTS t_department_member_idx_dept ON t_department_member(f_dept_id);\n",
    "CREATE TRIGGER IF NOT EXISTS trg_t_department_member_f_update_time AFTER UPDATE ON t_department_member\n"
        "FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time\n"
        "BEGIN\n"
        "    UPDATE t_department_member SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
        "END;\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_enterprise_uniq_enterprise_id ON t_enterprise(f_enterprise_id);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_enterprise_uniq_enterprise_name ON t_enterprise(f_name);\n",
    "CREATE TRIGGER IF NOT EXISTS trg_t_enterprise_f_update_time AFTER UPDATE ON t_enterprise\n"
        "FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time\n"
        "BEGIN\n"
        "    UPDATE t_enterprise SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
        "END;\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_admin_uniq_admin_id ON t_admin(f_admin_id);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_admin_uniq_email ON t_admin(f_email);\n",
    "CREATE TRIGGER IF NOT EXISTS trg_t_admin_f_update_time AFTER UPDATE ON t_admin\n"
        "FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time\n"
        "BEGIN\n"
        "    UPDATE t_admin SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
        "END;\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_admin_session_uniq_token ON t_admin_session(f_token);\n",
    "CREATE INDEX IF NOT EXISTS t_admin_session_idx_admin ON t_admin_session(f_admin_id);\n",
    "CREATE INDEX IF NOT EXISTS t_admin_audit_log_idx_enterprise ON t_admin_audit_log(f_enterprise_id);\n",
    "CREATE INDEX IF NOT EXISTS t_admin_audit_log_idx_action ON t_admin_audit_log(f_action);\n",
    "CREATE INDEX IF NOT EXISTS t_admin_audit_log_idx_create_time ON t_admin_audit_log(f_create_time);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_user_permission_uniq_user_perm ON t_user_permission(f_user_id, f_perm_key);\n",
    "CREATE TRIGGER IF NOT EXISTS trg_t_user_permission_f_update_time AFTER UPDATE ON t_user_permission\n"
        "FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time\n"
        "BEGIN\n"
        "    UPDATE t_user_permission SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
        "END;\n",
    "CREATE INDEX IF NOT EXISTS t_user_activity_log_idx_user ON t_user_activity_log(f_user_id);\n",
    "CREATE INDEX IF NOT EXISTS t_user_activity_log_idx_create_time ON t_user_activity_log(f_create_time);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_announcement_uniq_announcement_id ON t_announcement(f_announcement_id);\n",
    "CREATE TRIGGER IF NOT EXISTS trg_t_announcement_f_update_time AFTER UPDATE ON t_announcement\n"
        "FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time\n"
        "BEGIN\n"
        "    UPDATE t_announcement SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
        "END;\n",
    "CREATE INDEX IF NOT EXISTS t_announcement_target_idx_announcement ON t_announcement_target(f_announcement_id);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_announcement_read_uniq_announcement_user ON t_announcement_read(f_announcement_id, f_user_id);\n",
    "CREATE INDEX IF NOT EXISTS t_announcement_read_idx_announcement ON t_announcement_read(f_announcement_id);\n",
    "CREATE INDEX IF NOT EXISTS t_login_record_idx_user ON t_login_record(f_user_id);\n",
    "CREATE INDEX IF NOT EXISTS t_login_record_idx_login_time ON t_login_record(f_login_time);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_service_account_uniq_service_id ON t_service_account(f_service_id);\n",
    "CREATE INDEX IF NOT EXISTS t_service_account_idx_enterprise ON t_service_account(f_enterprise_id);\n",
    "CREATE TRIGGER IF NOT EXISTS trg_t_service_account_f_update_time AFTER UPDATE ON t_service_account\n"
        "FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time\n"
        "BEGIN\n"
        "    UPDATE t_service_account SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
        "END;\n",
    "CREATE INDEX IF NOT EXISTS t_service_account_binding_idx_service ON t_service_account_binding(f_service_id);\n",
    "CREATE INDEX IF NOT EXISTS t_contact_remark_idx_user ON t_contact_remark(f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_contact_tag_idx_user ON t_contact_tag(f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_favorite_idx_favorite_user ON t_favorite(f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_announcement_idx_group_time ON t_group_announcement(f_groupid, f_publish_time);\n",
    "CREATE INDEX IF NOT EXISTS t_group_member_idx_user ON t_group_member(f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_join_request_idx_group_status ON t_group_join_request(f_groupid, f_status);\n",
    "CREATE INDEX IF NOT EXISTS t_group_invite_idx_invitee_group ON t_group_invite(f_invitee, f_groupid);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_sensitive_word_uk_ent_norm ON t_sensitive_word(f_enterprise_id, f_word_norm);\n",
    "CREATE INDEX IF NOT EXISTS t_sensitive_word_idx_enterprise ON t_sensitive_word(f_enterprise_id);\n",
    "CREATE INDEX IF NOT EXISTS t_sensitive_hit_log_idx_ent_time ON t_sensitive_hit_log(f_enterprise_id, f_create_time);\n",
    "CREATE INDEX IF NOT EXISTS t_sensitive_hit_log_idx_create_time ON t_sensitive_hit_log(f_create_time);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_card_interaction_uniq_msg_user ON t_card_interaction(f_msgid, f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_card_interaction_idx_msgid ON t_card_interaction(f_msgid);\n",
    "CREATE INDEX IF NOT EXISTS t_file_idx_md5 ON t_file(f_md5);\n",
    "CREATE INDEX IF NOT EXISTS t_file_idx_uploader ON t_file(f_uploaderid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_file_idx_group ON t_group_file(f_groupid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_file_idx_fileid ON t_group_file(f_fileid);\n",
    "CREATE INDEX IF NOT EXISTS t_client_version_idx_platform_enabled ON t_client_version(f_platform, f_enabled);\n",
    "CREATE INDEX IF NOT EXISTS t_message_audience_idx_user ON t_message_audience(f_user_id);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_file_share_uk_owner_client ON t_file_share(f_owner_id, f_client_share_id);\n",
    "CREATE INDEX IF NOT EXISTS t_file_share_idx_owner ON t_file_share(f_owner_id, f_state);\n",
    "CREATE INDEX IF NOT EXISTS t_file_share_idx_conv ON t_file_share(f_ctype, f_cid, f_state);\n",
    "CREATE INDEX IF NOT EXISTS t_file_share_idx_enterprise ON t_file_share(f_enterprise_id, f_state);\n",
    "CREATE INDEX IF NOT EXISTS t_file_share_member_idx_user ON t_file_share_member(f_user_id);\n",
    "CREATE INDEX IF NOT EXISTS t_file_share_audit_idx_enterprise_time ON t_file_share_audit(f_enterprise_id, f_create_time);\n",
    "CREATE INDEX IF NOT EXISTS t_file_share_audit_idx_actor_time ON t_file_share_audit(f_actor_id, f_create_time);\n",
    "CREATE INDEX IF NOT EXISTS t_file_share_audit_idx_share ON t_file_share_audit(f_shareid);\n",
    "CREATE INDEX IF NOT EXISTS t_memo_idx_owner ON t_memo(f_ctype, f_owner);\n",
    "CREATE INDEX IF NOT EXISTS t_memo_target_idx_user ON t_memo_target(f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_qa_idx_group ON t_group_qa(f_groupid);\n",
    "CREATE INDEX IF NOT EXISTS t_enterprise_qa_idx_enterprise ON t_enterprise_qa(f_enterprise_id);\n",
    "CREATE INDEX IF NOT EXISTS t_enterprise_qa_group_idx_group ON t_enterprise_qa_group(f_groupid);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_group_link_uniq_code ON t_group_link(f_code);\n",
    "CREATE INDEX IF NOT EXISTS t_group_link_idx_group ON t_group_link(f_groupid);\n",
    "CREATE INDEX IF NOT EXISTS t_cloud_record_idx_owner ON t_cloud_record(f_owner, f_create_time);\n",
    "CREATE INDEX IF NOT EXISTS t_cloud_record_idx_enterprise ON t_cloud_record(f_enterprise_id);\n",
    "CREATE INDEX IF NOT EXISTS t_cloud_record_idx_expire ON t_cloud_record(f_expire_time);\n",
    "CREATE INDEX IF NOT EXISTS t_cloud_record_idx_room ON t_cloud_record(f_roomid);\n",
    "CREATE INDEX IF NOT EXISTS t_cloud_record_share_idx_conv ON t_cloud_record_share(f_ctype, f_cid);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_post_idx_group_active ON t_thread_post(f_groupid, f_active_time);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_post_idx_group_post ON t_thread_post(f_groupid, f_postid);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_post_idx_author_client ON t_thread_post(f_author, f_client_postid);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_addendum_idx_post ON t_thread_addendum(f_postid, f_addid);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_addendum_idx_group ON t_thread_addendum(f_groupid);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_comment_idx_post ON t_thread_comment(f_postid, f_parentid, f_commentid);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_comment_idx_parent ON t_thread_comment(f_parentid, f_commentid);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_comment_idx_group ON t_thread_comment(f_groupid);\n",
    "CREATE INDEX IF NOT EXISTS t_thread_reaction_idx_group ON t_thread_reaction(f_groupid);\n",
    "CREATE INDEX IF NOT EXISTS t_bot_idx_enterprise ON t_bot(f_enterprise_id);\n",
    "CREATE INDEX IF NOT EXISTS t_bot_idx_owner ON t_bot(f_owner_type, f_owner_id);\n",
    "CREATE INDEX IF NOT EXISTS t_bot_idx_webhook ON t_bot(f_webhook_token);\n",
    "CREATE INDEX IF NOT EXISTS t_bot_rule_idx_bot ON t_bot_rule(f_bot_id, f_seq);\n",
    "CREATE INDEX IF NOT EXISTS t_bot_group_idx_group ON t_bot_group(f_groupid);\n",
};

const int SCHEMA_POST_STATEMENT_COUNT = 100;

const char* const SCHEMA_TABLES[] =
{
    "t_user",
    "t_chatmsg",
    "t_department",
    "t_department_member",
    "t_enterprise",
    "t_admin",
    "t_admin_session",
    "t_admin_audit_log",
    "t_user_permission",
    "t_user_activity_log",
    "t_announcement",
    "t_announcement_target",
    "t_announcement_read",
    "t_login_record",
    "t_service_account",
    "t_service_account_binding",
    "t_contact_remark",
    "t_contact_tag",
    "t_blacklist",
    "t_conversation",
    "t_message_deleted",
    "t_message_deleted_range",
    "t_file_ack",
    "t_message_pin",
    "t_favorite",
    "t_group",
    "t_group_announcement",
    "t_group_member",
    "t_group_join_request",
    "t_group_invite",
    "t_sensitive_word",
    "t_sensitive_hit_log",
    "t_card_interaction",
    "t_card_state",
    "t_file",
    "t_group_file",
    "t_client_version",
    "t_message_audience",
    "t_file_share",
    "t_file_share_member",
    "t_file_share_audit",
    "t_system_setting",
    "t_memo",
    "t_memo_target",
    "t_group_follow",
    "t_group_qa",
    "t_enterprise_qa",
    "t_enterprise_qa_group",
    "t_group_link",
    "t_cloud_record",
    "t_cloud_record_share",
    "t_thread_post",
    "t_thread_addendum",
    "t_thread_comment",
    "t_thread_reaction",
    "t_bot",
    "t_bot_rule",
    "t_bot_group",
    "t_bot_group_off",
    "t_bot_domain",
};

const int SCHEMA_TABLE_COUNT = 60;

} // namespace sqliteapi
