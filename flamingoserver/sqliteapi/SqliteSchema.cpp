/**
 *  SQLite 库结构数据，SqliteSchema.cpp
 *  balloonwj@qq.com   2026-07-31
 *
 *  本文件由 tools/gen_table_sqlite.py 从 table.sql 自动生成，请勿手工编辑。
 *  改了 table.sql 之后重跑该脚本：python tools/gen_table_sqlite.py
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
        "    f_password VARCHAR(64) DEFAULT NULL,\n"
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
        "    f_create_time INTEGER NOT NULL DEFAULT 0\n"
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
        "    f_remark VARCHAR(64) DEFAULT NULL\n"
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
        "    f_mute INTEGER NOT NULL DEFAULT 0,\n"
        "    f_readmsgid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_deleted INTEGER NOT NULL DEFAULT 0,\n"
        "    f_deleted_msgid INTEGER NOT NULL DEFAULT 0,\n"
        "    f_update_time INTEGER NOT NULL DEFAULT 0,\n"
        "    PRIMARY KEY (f_userid, f_ctype, f_cid)\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_message_deleted\n"
        "(\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_msgid INTEGER NOT NULL,\n"
        "    PRIMARY KEY (f_userid, f_msgid)\n"
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
        "    f_sensitive_on INTEGER NOT NULL DEFAULT 0,\n"
        "    f_announce_id INTEGER NOT NULL DEFAULT 0,\n"
        "    f_announce_title VARCHAR(128) NOT NULL DEFAULT '',\n"
        "    f_announce_time INTEGER NOT NULL DEFAULT 0,\n"
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
        "    f_recommender INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_group_sensitive_word\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_groupid INTEGER NOT NULL,\n"
        "    f_word VARCHAR(64) NOT NULL,\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0\n"
        ");\n",
    "CREATE TABLE IF NOT EXISTS t_card_interaction\n"
        "(\n"
        "    f_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "    f_msgid INTEGER NOT NULL,\n"
        "    f_userid INTEGER NOT NULL,\n"
        "    f_data VARCHAR(1024) NOT NULL DEFAULT '',\n"
        "    f_create_time INTEGER NOT NULL DEFAULT 0\n"
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
};

const int SCHEMA_TABLE_STATEMENT_COUNT = 31;

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
    "CREATE INDEX IF NOT EXISTS t_favorite_idx_favorite_user ON t_favorite(f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_announcement_idx_group_time ON t_group_announcement(f_groupid, f_publish_time);\n",
    "CREATE INDEX IF NOT EXISTS t_group_member_idx_user ON t_group_member(f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_join_request_idx_group_status ON t_group_join_request(f_groupid, f_status);\n",
    "CREATE INDEX IF NOT EXISTS t_group_sensitive_word_idx_group ON t_group_sensitive_word(f_groupid);\n",
    "CREATE UNIQUE INDEX IF NOT EXISTS t_card_interaction_uniq_msg_user ON t_card_interaction(f_msgid, f_userid);\n",
    "CREATE INDEX IF NOT EXISTS t_card_interaction_idx_msgid ON t_card_interaction(f_msgid);\n",
    "CREATE INDEX IF NOT EXISTS t_file_idx_md5 ON t_file(f_md5);\n",
    "CREATE INDEX IF NOT EXISTS t_file_idx_uploader ON t_file(f_uploaderid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_file_idx_group ON t_group_file(f_groupid);\n",
    "CREATE INDEX IF NOT EXISTS t_group_file_idx_fileid ON t_group_file(f_fileid);\n",
};

const int SCHEMA_POST_STATEMENT_COUNT = 57;

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
    "t_blacklist",
    "t_conversation",
    "t_message_deleted",
    "t_file_ack",
    "t_message_pin",
    "t_favorite",
    "t_group",
    "t_group_announcement",
    "t_group_member",
    "t_group_join_request",
    "t_group_sensitive_word",
    "t_card_interaction",
    "t_file",
    "t_group_file",
};

const int SCHEMA_TABLE_COUNT = 31;

const SchemaColumn SCHEMA_COLUMNS[] =
{
    { "t_user", "f_user_id", "INTEGER" },
    { "t_user", "f_username", "VARCHAR(64) COLLATE NOCASE" },
    { "t_user", "f_nickname", "VARCHAR(64)" },
    { "t_user", "f_password", "VARCHAR(64) DEFAULT NULL" },
    { "t_user", "f_salt", "VARCHAR(32) DEFAULT NULL" },
    { "t_user", "f_facetype", "INTEGER DEFAULT 0" },
    { "t_user", "f_customface", "VARCHAR(32) DEFAULT NULL" },
    { "t_user", "f_customfacefmt", "VARCHAR(6) DEFAULT NULL" },
    { "t_user", "f_gender", "INTEGER DEFAULT 0" },
    { "t_user", "f_birthday", "INTEGER DEFAULT 19900101" },
    { "t_user", "f_signature", "VARCHAR(256) DEFAULT NULL" },
    { "t_user", "f_address", "VARCHAR(256) DEFAULT NULL" },
    { "t_user", "f_phonenumber", "VARCHAR(64) DEFAULT NULL" },
    { "t_user", "f_mail", "VARCHAR(256) COLLATE NOCASE DEFAULT NULL" },
    { "t_user", "f_register_time", "DATETIME" },
    { "t_user", "f_owner_id", "INTEGER DEFAULT 0" },
    { "t_user", "f_remark", "VARCHAR(64) DEFAULT NULL" },
    { "t_user", "f_update_time", "TIMESTAMP" },
    { "t_user", "f_teaminfo", "BLOB DEFAULT NULL" },
    { "t_user", "f_enterprise_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_user", "f_role", "INTEGER NOT NULL DEFAULT 0" },
    { "t_user", "f_employee_no", "VARCHAR(32) DEFAULT NULL" },
    { "t_user", "f_join_date", "DATETIME DEFAULT NULL" },
    { "t_user", "f_landline", "VARCHAR(64) DEFAULT NULL" },
    { "t_user", "f_workstation", "VARCHAR(256) DEFAULT NULL" },
    { "t_user", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_user", "f_usertype", "INTEGER NOT NULL DEFAULT 0" },
    { "t_chatmsg", "f_clientmsgid", "VARCHAR(64) NOT NULL DEFAULT ''" },
    { "t_chatmsg", "f_ctype", "INTEGER NOT NULL DEFAULT 1" },
    { "t_chatmsg", "f_from", "INTEGER" },
    { "t_chatmsg", "f_to", "INTEGER" },
    { "t_chatmsg", "f_msgtype", "INTEGER NOT NULL DEFAULT 1" },
    { "t_chatmsg", "f_content", "BLOB" },
    { "t_chatmsg", "f_recalled", "INTEGER NOT NULL DEFAULT 0" },
    { "t_chatmsg", "f_create_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department", "f_dept_id", "INTEGER" },
    { "t_department", "f_enterprise_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department", "f_dept_name", "VARCHAR(64)" },
    { "t_department", "f_parent_dept_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department", "f_dept_path", "VARCHAR(255) NOT NULL DEFAULT ''" },
    { "t_department", "f_dept_level", "INTEGER NOT NULL DEFAULT 1" },
    { "t_department", "f_dept_order", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department", "f_leader_user_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department", "f_dept_desc", "VARCHAR(256) DEFAULT NULL" },
    { "t_department", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department", "f_create_time", "DATETIME" },
    { "t_department", "f_update_time", "TIMESTAMP" },
    { "t_department", "f_remark", "VARCHAR(64) DEFAULT NULL" },
    { "t_department_member", "f_dept_id", "INTEGER" },
    { "t_department_member", "f_enterprise_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department_member", "f_user_id", "INTEGER" },
    { "t_department_member", "f_position", "VARCHAR(64) DEFAULT NULL" },
    { "t_department_member", "f_is_primary", "INTEGER NOT NULL DEFAULT 1" },
    { "t_department_member", "f_member_order", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department_member", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_department_member", "f_join_time", "DATETIME" },
    { "t_department_member", "f_update_time", "TIMESTAMP" },
    { "t_department_member", "f_remark", "VARCHAR(64) DEFAULT NULL" },
    { "t_enterprise", "f_enterprise_id", "INTEGER" },
    { "t_enterprise", "f_name", "VARCHAR(128) COLLATE NOCASE" },
    { "t_enterprise", "f_short_name", "VARCHAR(64) DEFAULT NULL" },
    { "t_enterprise", "f_industry", "VARCHAR(64) DEFAULT NULL" },
    { "t_enterprise", "f_region", "VARCHAR(64) DEFAULT NULL" },
    { "t_enterprise", "f_plan", "INTEGER NOT NULL DEFAULT 0" },
    { "t_enterprise", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_enterprise", "f_root_dept_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_enterprise", "f_admin_name", "VARCHAR(64) DEFAULT NULL" },
    { "t_enterprise", "f_admin_email", "VARCHAR(128) DEFAULT NULL" },
    { "t_enterprise", "f_logo_color", "VARCHAR(16) DEFAULT NULL" },
    { "t_enterprise", "f_open_date", "DATETIME DEFAULT NULL" },
    { "t_enterprise", "f_license_expire_time", "DATETIME DEFAULT NULL" },
    { "t_enterprise", "f_create_time", "DATETIME" },
    { "t_enterprise", "f_update_time", "TIMESTAMP" },
    { "t_enterprise", "f_remark", "VARCHAR(64) DEFAULT NULL" },
    { "t_admin", "f_admin_id", "INTEGER" },
    { "t_admin", "f_email", "VARCHAR(128) COLLATE NOCASE" },
    { "t_admin", "f_password", "VARCHAR(64)" },
    { "t_admin", "f_salt", "VARCHAR(32)" },
    { "t_admin", "f_name", "VARCHAR(64)" },
    { "t_admin", "f_enterprise_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_admin", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_admin", "f_last_login_time", "DATETIME DEFAULT NULL" },
    { "t_admin", "f_last_login_ip", "VARCHAR(64) DEFAULT NULL" },
    { "t_admin", "f_create_time", "DATETIME" },
    { "t_admin", "f_update_time", "TIMESTAMP" },
    { "t_admin", "f_remark", "VARCHAR(64) DEFAULT NULL" },
    { "t_admin_session", "f_token", "VARCHAR(64)" },
    { "t_admin_session", "f_admin_id", "INTEGER" },
    { "t_admin_session", "f_login_ip", "VARCHAR(64) DEFAULT NULL" },
    { "t_admin_session", "f_login_time", "DATETIME" },
    { "t_admin_session", "f_expire_time", "DATETIME" },
    { "t_admin_session", "f_create_time", "DATETIME" },
    { "t_admin_audit_log", "f_actor_type", "INTEGER NOT NULL DEFAULT 0" },
    { "t_admin_audit_log", "f_actor_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_admin_audit_log", "f_actor_name", "VARCHAR(64) DEFAULT NULL" },
    { "t_admin_audit_log", "f_ip", "VARCHAR(64) DEFAULT NULL" },
    { "t_admin_audit_log", "f_enterprise_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_admin_audit_log", "f_action", "VARCHAR(64)" },
    { "t_admin_audit_log", "f_target", "VARCHAR(256) DEFAULT NULL" },
    { "t_admin_audit_log", "f_severity", "INTEGER NOT NULL DEFAULT 0" },
    { "t_admin_audit_log", "f_detail", "VARCHAR(512) DEFAULT NULL" },
    { "t_admin_audit_log", "f_create_time", "DATETIME" },
    { "t_user_permission", "f_user_id", "INTEGER" },
    { "t_user_permission", "f_perm_key", "VARCHAR(64)" },
    { "t_user_permission", "f_granted", "INTEGER NOT NULL DEFAULT 0" },
    { "t_user_permission", "f_update_time", "TIMESTAMP" },
    { "t_user_permission", "f_remark", "VARCHAR(64) DEFAULT NULL" },
    { "t_user_activity_log", "f_user_id", "INTEGER" },
    { "t_user_activity_log", "f_action", "VARCHAR(64)" },
    { "t_user_activity_log", "f_detail", "VARCHAR(512) DEFAULT NULL" },
    { "t_user_activity_log", "f_severity", "INTEGER NOT NULL DEFAULT 0" },
    { "t_user_activity_log", "f_create_time", "DATETIME" },
    { "t_announcement", "f_announcement_id", "INTEGER" },
    { "t_announcement", "f_title", "VARCHAR(256)" },
    { "t_announcement", "f_body", "TEXT DEFAULT NULL" },
    { "t_announcement", "f_severity", "INTEGER NOT NULL DEFAULT 0" },
    { "t_announcement", "f_sender_admin_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_announcement", "f_sender_name", "VARCHAR(64) DEFAULT NULL" },
    { "t_announcement", "f_audience_type", "INTEGER NOT NULL DEFAULT 0" },
    { "t_announcement", "f_pinned", "INTEGER NOT NULL DEFAULT 0" },
    { "t_announcement", "f_scheduled", "INTEGER NOT NULL DEFAULT 0" },
    { "t_announcement", "f_scheduled_time", "DATETIME DEFAULT NULL" },
    { "t_announcement", "f_sent_time", "DATETIME DEFAULT NULL" },
    { "t_announcement", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_announcement", "f_create_time", "DATETIME" },
    { "t_announcement", "f_update_time", "TIMESTAMP" },
    { "t_announcement", "f_remark", "VARCHAR(64) DEFAULT NULL" },
    { "t_announcement_target", "f_announcement_id", "INTEGER" },
    { "t_announcement_target", "f_target_type", "INTEGER" },
    { "t_announcement_target", "f_target_id", "INTEGER" },
    { "t_announcement_target", "f_create_time", "DATETIME" },
    { "t_announcement_read", "f_announcement_id", "INTEGER" },
    { "t_announcement_read", "f_user_id", "INTEGER" },
    { "t_announcement_read", "f_read_time", "DATETIME" },
    { "t_login_record", "f_user_id", "INTEGER" },
    { "t_login_record", "f_login_time", "DATETIME" },
    { "t_login_record", "f_device", "VARCHAR(128) DEFAULT NULL" },
    { "t_login_record", "f_device_kind", "INTEGER NOT NULL DEFAULT 0" },
    { "t_login_record", "f_os", "VARCHAR(64) DEFAULT NULL" },
    { "t_login_record", "f_ip", "VARCHAR(64) DEFAULT NULL" },
    { "t_login_record", "f_location", "VARCHAR(128) DEFAULT NULL" },
    { "t_login_record", "f_result", "INTEGER NOT NULL DEFAULT 0" },
    { "t_login_record", "f_fail_reason", "VARCHAR(128) DEFAULT NULL" },
    { "t_login_record", "f_create_time", "DATETIME" },
    { "t_service_account", "f_service_id", "INTEGER" },
    { "t_service_account", "f_enterprise_id", "INTEGER" },
    { "t_service_account", "f_name", "VARCHAR(64)" },
    { "t_service_account", "f_kind", "INTEGER NOT NULL DEFAULT 0" },
    { "t_service_account", "f_description", "VARCHAR(256) DEFAULT NULL" },
    { "t_service_account", "f_avatar_color", "VARCHAR(16) DEFAULT NULL" },
    { "t_service_account", "f_avatar_glyph", "VARCHAR(16) DEFAULT NULL" },
    { "t_service_account", "f_owner_admin_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_service_account", "f_allow_unsubscribe", "INTEGER NOT NULL DEFAULT 1" },
    { "t_service_account", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_service_account", "f_create_time", "DATETIME" },
    { "t_service_account", "f_update_time", "TIMESTAMP" },
    { "t_service_account", "f_remark", "VARCHAR(64) DEFAULT NULL" },
    { "t_service_account_binding", "f_service_id", "INTEGER" },
    { "t_service_account_binding", "f_bind_type", "INTEGER" },
    { "t_service_account_binding", "f_bind_target_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_service_account_binding", "f_create_time", "DATETIME" },
    { "t_contact_remark", "f_userid", "INTEGER" },
    { "t_contact_remark", "f_target_userid", "INTEGER" },
    { "t_contact_remark", "f_remark", "VARCHAR(32) NOT NULL DEFAULT ''" },
    { "t_contact_remark", "f_update_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_blacklist", "f_userid", "INTEGER" },
    { "t_blacklist", "f_blocked_userid", "INTEGER" },
    { "t_blacklist", "f_create_time", "TIMESTAMP" },
    { "t_conversation", "f_userid", "INTEGER" },
    { "t_conversation", "f_ctype", "INTEGER NOT NULL DEFAULT 1" },
    { "t_conversation", "f_cid", "INTEGER" },
    { "t_conversation", "f_pinned", "INTEGER NOT NULL DEFAULT 0" },
    { "t_conversation", "f_mute", "INTEGER NOT NULL DEFAULT 0" },
    { "t_conversation", "f_readmsgid", "INTEGER NOT NULL DEFAULT 0" },
    { "t_conversation", "f_deleted", "INTEGER NOT NULL DEFAULT 0" },
    { "t_conversation", "f_deleted_msgid", "INTEGER NOT NULL DEFAULT 0" },
    { "t_conversation", "f_update_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_message_deleted", "f_userid", "INTEGER" },
    { "t_message_deleted", "f_msgid", "INTEGER" },
    { "t_file_ack", "f_msgid", "INTEGER" },
    { "t_file_ack", "f_state", "INTEGER NOT NULL DEFAULT 0" },
    { "t_file_ack", "f_update_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_message_pin", "f_msgid", "INTEGER" },
    { "t_message_pin", "f_operator", "INTEGER NOT NULL DEFAULT 0" },
    { "t_message_pin", "f_pin_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_favorite", "f_userid", "INTEGER" },
    { "t_favorite", "f_ctype", "INTEGER" },
    { "t_favorite", "f_cid", "INTEGER" },
    { "t_favorite", "f_order", "INTEGER NOT NULL DEFAULT 0" },
    { "t_favorite", "f_create_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_groupid", "INTEGER" },
    { "t_group", "f_name", "VARCHAR(64)" },
    { "t_group", "f_facetype", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_customface", "VARCHAR(256) NOT NULL DEFAULT ''" },
    { "t_group", "f_announcement", "VARCHAR(1024) NOT NULL DEFAULT ''" },
    { "t_group", "f_ownerid", "INTEGER" },
    { "t_group", "f_join_mode", "INTEGER NOT NULL DEFAULT 1" },
    { "t_group", "f_mute_all", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_create_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_intro", "VARCHAR(512) NOT NULL DEFAULT ''" },
    { "t_group", "f_at_all_scope", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_new_see_history", "INTEGER NOT NULL DEFAULT 1" },
    { "t_group", "f_member_invite", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_welcome_on", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_welcome_text", "VARCHAR(256) NOT NULL DEFAULT ''" },
    { "t_group", "f_sensitive_on", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_announce_id", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group", "f_announce_title", "VARCHAR(128) NOT NULL DEFAULT ''" },
    { "t_group", "f_announce_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_announcement", "f_groupid", "INTEGER" },
    { "t_group_announcement", "f_title", "VARCHAR(128) NOT NULL DEFAULT ''" },
    { "t_group_announcement", "f_content", "VARCHAR(2048) NOT NULL DEFAULT ''" },
    { "t_group_announcement", "f_severity", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_announcement", "f_publisher", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_announcement", "f_publish_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_member", "f_groupid", "INTEGER" },
    { "t_group_member", "f_userid", "INTEGER" },
    { "t_group_member", "f_role", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_member", "f_card", "VARCHAR(64) NOT NULL DEFAULT ''" },
    { "t_group_member", "f_muted", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_member", "f_join_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_member", "f_mute_until", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_member", "f_mute_reason", "VARCHAR(128) NOT NULL DEFAULT ''" },
    { "t_group_join_request", "f_groupid", "INTEGER" },
    { "t_group_join_request", "f_userid", "INTEGER" },
    { "t_group_join_request", "f_verify_msg", "VARCHAR(256) DEFAULT NULL" },
    { "t_group_join_request", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_join_request", "f_create_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_join_request", "f_handle_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_join_request", "f_handler", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_join_request", "f_source", "INTEGER NOT NULL DEFAULT 1" },
    { "t_group_join_request", "f_recommender", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_sensitive_word", "f_groupid", "INTEGER" },
    { "t_group_sensitive_word", "f_word", "VARCHAR(64)" },
    { "t_group_sensitive_word", "f_create_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_card_interaction", "f_msgid", "INTEGER" },
    { "t_card_interaction", "f_userid", "INTEGER" },
    { "t_card_interaction", "f_data", "VARCHAR(1024) NOT NULL DEFAULT ''" },
    { "t_card_interaction", "f_create_time", "INTEGER NOT NULL DEFAULT 0" },
    { "t_file", "f_md5", "CHAR(32) NOT NULL DEFAULT ''" },
    { "t_file", "f_name", "VARCHAR(256) NOT NULL DEFAULT ''" },
    { "t_file", "f_ext", "VARCHAR(32) NOT NULL DEFAULT ''" },
    { "t_file", "f_size", "INTEGER NOT NULL DEFAULT 0" },
    { "t_file", "f_scene", "INTEGER NOT NULL DEFAULT 0" },
    { "t_file", "f_uploaderid", "INTEGER NOT NULL DEFAULT 0" },
    { "t_file", "f_status", "INTEGER NOT NULL DEFAULT 0" },
    { "t_file", "f_createtime", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_file", "f_groupid", "INTEGER" },
    { "t_group_file", "f_fileid", "INTEGER" },
    { "t_group_file", "f_filetype", "INTEGER NOT NULL DEFAULT 6" },
    { "t_group_file", "f_uploaderid", "INTEGER NOT NULL DEFAULT 0" },
    { "t_group_file", "f_uploadtime", "INTEGER NOT NULL DEFAULT 0" },
};

const int SCHEMA_COLUMN_COUNT = 252;

} // namespace sqliteapi
