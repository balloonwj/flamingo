-- ============================================================
--  FlamingoIM 本地 SQLite 模式的建表脚本
--  balloonwj@qq.com   2026-07-31
--
--  本文件由 tools/gen_table_sqlite.py 从 table.sql 自动生成，请勿手工编辑。
--  改了 table.sql 之后重跑该脚本即可：python tools/gen_table_sqlite.py
--
--  chatserver 在 dbtype=sqlite 时启动会执行本脚本建表；语句全部是
--  IF NOT EXISTS 形式，重复执行安全。缺列的自动补齐由 CSqliteManager
--  用 PRAGMA table_info 比对后 ALTER TABLE ADD COLUMN 完成，不在本文件里。
-- ============================================================

CREATE TABLE IF NOT EXISTS t_user
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_user_id INTEGER NOT NULL,    -- 用户ID
    f_username VARCHAR(64) COLLATE NOCASE NOT NULL,    -- 用户名
    f_nickname VARCHAR(64) NOT NULL,    -- 用户昵称
    f_password VARCHAR(64) DEFAULT NULL,    -- 登录密码：v2存 MD5(SHA256(明文)+f_salt)
    f_salt VARCHAR(32) DEFAULT NULL,    -- 密码盐值，注册时生成（v2登录口令存储用）
    f_facetype INTEGER DEFAULT 0,    -- 用户头像类型
    f_customface VARCHAR(32) DEFAULT NULL,    -- 自定义头像名
    f_customfacefmt VARCHAR(6) DEFAULT NULL,    -- 自定义头像格式
    f_gender INTEGER DEFAULT 0,    -- 性别
    f_birthday INTEGER DEFAULT 19900101,    -- 生日
    f_signature VARCHAR(256) DEFAULT NULL,    -- 个性签名
    f_address VARCHAR(256) DEFAULT NULL,    -- 地址
    f_phonenumber VARCHAR(64) DEFAULT NULL,    -- 电话
    f_mail VARCHAR(256) COLLATE NOCASE DEFAULT NULL,    -- 邮箱
    f_register_time DATETIME NOT NULL,    -- 注册时间
    f_owner_id INTEGER DEFAULT 0,    -- 群账号群主userid
    f_remark VARCHAR(64) DEFAULT NULL,    -- 备注
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_teaminfo BLOB DEFAULT NULL,    -- 好友分组信息
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 所属企业ID，关联 t_enterprise.f_enterprise_id
    f_role INTEGER NOT NULL DEFAULT 0,    -- 企业内角色：0=普通成员 1=部门负责人 2=企业管理员
    f_employee_no VARCHAR(32) DEFAULT NULL,    -- 工号
    f_join_date DATETIME DEFAULT NULL,    -- 入职日期
    f_landline VARCHAR(64) DEFAULT NULL,    -- 座机
    f_workstation VARCHAR(256) DEFAULT NULL,    -- 工位地址
    f_status INTEGER NOT NULL DEFAULT 0,    -- 账号状态：0=启用 1=停用
    f_usertype INTEGER NOT NULL DEFAULT 0    -- 用户类型：0=普通用户 1=服务号用户
);

CREATE UNIQUE INDEX IF NOT EXISTS t_user_uk_primary ON t_user(f_user_id);    -- 原 MySQL 主键降级而来，唯一性约束不变
CREATE UNIQUE INDEX IF NOT EXISTS t_user_uk_f_username ON t_user(f_username);
CREATE UNIQUE INDEX IF NOT EXISTS t_user_uk_ent_employee_no ON t_user(f_enterprise_id, f_employee_no);
CREATE INDEX IF NOT EXISTS t_user_idx_enterprise ON t_user(f_enterprise_id);
CREATE INDEX IF NOT EXISTS t_user_f_user_id ON t_user(f_user_id);

CREATE TRIGGER IF NOT EXISTS trg_t_user_f_update_time AFTER UPDATE ON t_user
FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time
BEGIN
    UPDATE t_user SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;
END;

CREATE TABLE IF NOT EXISTS t_chatmsg
(
    f_msgid INTEGER PRIMARY KEY AUTOINCREMENT,    -- 服务器消息ID(全局自增,单调递增)
    f_clientmsgid VARCHAR(64) NOT NULL DEFAULT '',    -- 客户端消息ID(UUID,去重+回执回填)
    f_ctype INTEGER NOT NULL DEFAULT 1,    -- 会话类型:1单聊 2群聊(本阶段恒为1)
    f_from INTEGER NOT NULL,    -- 发送方userid
    f_to INTEGER NOT NULL,    -- 接收方:单聊为对端userid,群聊为群id
    f_msgtype INTEGER NOT NULL DEFAULT 1,    -- 内容类型:1文本 2图片 3文件 4语音 …17系统
    f_content BLOB,    -- 内容JSON(content/mentions/mentionall/quote 打包)
    f_recalled INTEGER NOT NULL DEFAULT 0,    -- 是否已撤回:0否 1是
    f_create_time INTEGER NOT NULL DEFAULT 0    -- 消息时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_chatmsg_idx_from_to ON t_chatmsg(f_from, f_to);
CREATE INDEX IF NOT EXISTS t_chatmsg_idx_to_from ON t_chatmsg(f_to, f_from);
CREATE INDEX IF NOT EXISTS t_chatmsg_idx_client ON t_chatmsg(f_clientmsgid);

CREATE TABLE IF NOT EXISTS t_department
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_dept_id INTEGER NOT NULL,    -- 部门业务ID（协议中引用部门统一用此ID，非自增f_id）
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 所属企业ID
    f_dept_name VARCHAR(64) NOT NULL,    -- 部门名称
    f_parent_dept_id INTEGER NOT NULL DEFAULT 0,    -- 父部门ID，0表示根部门（公司本身/顶级部门）
    f_dept_path VARCHAR(255) NOT NULL DEFAULT '',    -- 物化路径，自根到本节点(含本节点)，形如 /1001/1005/ ，用于子树查询
    f_dept_level INTEGER NOT NULL DEFAULT 1,    -- 部门层级，根部门为1，每向下一级+1
    f_dept_order INTEGER NOT NULL DEFAULT 0,    -- 同级部门排序权重，值小者靠前
    f_leader_user_id INTEGER NOT NULL DEFAULT 0,    -- 部门负责人userid，0表示暂未指定
    f_dept_desc VARCHAR(256) DEFAULT NULL,    -- 部门描述/职能说明
    f_status INTEGER NOT NULL DEFAULT 0,    -- 部门状态：0=正常，1=停用（停用不物理删除，保留历史）
    f_create_time DATETIME NOT NULL,    -- 创建时间
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_remark VARCHAR(64) DEFAULT NULL    -- 备注
);

CREATE UNIQUE INDEX IF NOT EXISTS t_department_uniq_dept_id ON t_department(f_dept_id);
CREATE INDEX IF NOT EXISTS t_department_idx_parent ON t_department(f_parent_dept_id);
CREATE INDEX IF NOT EXISTS t_department_idx_path ON t_department(f_dept_path);
CREATE INDEX IF NOT EXISTS t_department_idx_leader ON t_department(f_leader_user_id);
CREATE INDEX IF NOT EXISTS t_department_idx_enterprise ON t_department(f_enterprise_id);

CREATE TRIGGER IF NOT EXISTS trg_t_department_f_update_time AFTER UPDATE ON t_department
FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time
BEGIN
    UPDATE t_department SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;
END;

CREATE TABLE IF NOT EXISTS t_department_member
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_dept_id INTEGER NOT NULL,    -- 部门ID，关联 t_department.f_dept_id
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 所属企业ID（便于按租户隔离查询）
    f_user_id INTEGER NOT NULL,    -- 用户ID，关联 t_user.f_user_id
    f_position VARCHAR(64) DEFAULT NULL,    -- 该成员在本部门的职位/职务，如高级工程师、组长
    f_is_primary INTEGER NOT NULL DEFAULT 1,    -- 是否主部门：1=主部门，0=兼属部门；每个用户至多一条为1
    f_member_order INTEGER NOT NULL DEFAULT 0,    -- 成员在本部门内的排序权重，值小者靠前
    f_status INTEGER NOT NULL DEFAULT 0,    -- 成员状态：0=在职，1=已移出/离职（保留历史，不物理删除）
    f_join_time DATETIME NOT NULL,    -- 加入部门时间
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_remark VARCHAR(64) DEFAULT NULL    -- 备注
);

CREATE UNIQUE INDEX IF NOT EXISTS t_department_member_uniq_dept_user ON t_department_member(f_dept_id, f_user_id);
CREATE INDEX IF NOT EXISTS t_department_member_idx_user ON t_department_member(f_user_id);
CREATE INDEX IF NOT EXISTS t_department_member_idx_dept ON t_department_member(f_dept_id);

CREATE TRIGGER IF NOT EXISTS trg_t_department_member_f_update_time AFTER UPDATE ON t_department_member
FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time
BEGIN
    UPDATE t_department_member SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;
END;

CREATE TABLE IF NOT EXISTS t_enterprise
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_enterprise_id INTEGER NOT NULL,    -- 企业业务ID
    f_name VARCHAR(128) COLLATE NOCASE NOT NULL,    -- 企业全称
    f_short_name VARCHAR(64) DEFAULT NULL,    -- 企业简称
    f_industry VARCHAR(64) DEFAULT NULL,    -- 所属行业
    f_region VARCHAR(64) DEFAULT NULL,    -- 所在地区
    f_plan INTEGER NOT NULL DEFAULT 0,    -- 套餐：0=基础版 1=专业版 2=企业版
    f_status INTEGER NOT NULL DEFAULT 0,    -- 状态：0=待审核 1=活跃 2=已停用
    f_root_dept_id INTEGER NOT NULL DEFAULT 0,    -- 公司根部门ID，关联 t_department.f_dept_id；代表"公司本身"这一层，员工可直接挂其下；0表示尚未建立（仅存量数据迁移前会出现）
    f_admin_name VARCHAR(64) DEFAULT NULL,    -- 主管理员姓名
    f_admin_email VARCHAR(128) DEFAULT NULL,    -- 主管理员邮箱
    f_logo_color VARCHAR(16) DEFAULT NULL,    -- 企业Logo底色，如 #ff5a44
    f_open_date DATETIME DEFAULT NULL,    -- 开通日期
    f_license_expire_time DATETIME DEFAULT NULL,    -- 企业版授权到期时间
    f_create_time DATETIME NOT NULL,    -- 创建时间
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_remark VARCHAR(64) DEFAULT NULL    -- 备注
);

CREATE UNIQUE INDEX IF NOT EXISTS t_enterprise_uniq_enterprise_id ON t_enterprise(f_enterprise_id);
CREATE UNIQUE INDEX IF NOT EXISTS t_enterprise_uniq_enterprise_name ON t_enterprise(f_name);

CREATE TRIGGER IF NOT EXISTS trg_t_enterprise_f_update_time AFTER UPDATE ON t_enterprise
FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time
BEGIN
    UPDATE t_enterprise SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;
END;

CREATE TABLE IF NOT EXISTS t_admin
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_admin_id INTEGER NOT NULL,    -- 管理员业务ID
    f_email VARCHAR(128) COLLATE NOCASE NOT NULL,    -- 登录邮箱（即登录账号）
    f_password VARCHAR(64) NOT NULL,    -- 登录密码：MD5(明文密码 + f_salt) 的十六进制串
    f_salt VARCHAR(32) NOT NULL,    -- 每个管理员独立的密码盐值
    f_name VARCHAR(64) NOT NULL,    -- 管理员姓名
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 管辖范围：0=平台超级管理员，非0=仅限该企业
    f_status INTEGER NOT NULL DEFAULT 0,    -- 状态：0=正常 1=停用
    f_last_login_time DATETIME DEFAULT NULL,    -- 末次登录时间
    f_last_login_ip VARCHAR(64) DEFAULT NULL,    -- 末次登录IP
    f_create_time DATETIME NOT NULL,    -- 创建时间
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_remark VARCHAR(64) DEFAULT NULL    -- 备注
);

CREATE UNIQUE INDEX IF NOT EXISTS t_admin_uniq_admin_id ON t_admin(f_admin_id);
CREATE UNIQUE INDEX IF NOT EXISTS t_admin_uniq_email ON t_admin(f_email);

CREATE TRIGGER IF NOT EXISTS trg_t_admin_f_update_time AFTER UPDATE ON t_admin
FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time
BEGIN
    UPDATE t_admin SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;
END;

CREATE TABLE IF NOT EXISTS t_admin_session
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_token VARCHAR(64) NOT NULL,    -- 会话令牌，登录时下发
    f_admin_id INTEGER NOT NULL,    -- 所属管理员ID
    f_login_ip VARCHAR(64) DEFAULT NULL,    -- 登录IP
    f_login_time DATETIME NOT NULL,    -- 登录时间
    f_expire_time DATETIME NOT NULL,    -- 令牌过期时间
    f_create_time DATETIME NOT NULL    -- 创建时间
);

CREATE UNIQUE INDEX IF NOT EXISTS t_admin_session_uniq_token ON t_admin_session(f_token);
CREATE INDEX IF NOT EXISTS t_admin_session_idx_admin ON t_admin_session(f_admin_id);

CREATE TABLE IF NOT EXISTS t_admin_audit_log
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_actor_type INTEGER NOT NULL DEFAULT 0,    -- 操作者类型：0=管理员 1=系统
    f_actor_id INTEGER NOT NULL DEFAULT 0,    -- 操作者管理员ID，系统操作为0
    f_actor_name VARCHAR(64) DEFAULT NULL,    -- 操作者姓名（冗余，便于日志直读）
    f_ip VARCHAR(64) DEFAULT NULL,    -- 操作来源IP
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 关联企业ID，平台级操作为0
    f_action VARCHAR(64) NOT NULL,    -- 操作类型，如 dept.create / user.disable / login.success
    f_target VARCHAR(256) DEFAULT NULL,    -- 操作目标的文字描述
    f_severity INTEGER NOT NULL DEFAULT 0,    -- 级别：0=info 1=warn 2=danger
    f_detail VARCHAR(512) DEFAULT NULL,    -- 操作详情
    f_create_time DATETIME NOT NULL    -- 操作发生时间
);

CREATE INDEX IF NOT EXISTS t_admin_audit_log_idx_enterprise ON t_admin_audit_log(f_enterprise_id);
CREATE INDEX IF NOT EXISTS t_admin_audit_log_idx_action ON t_admin_audit_log(f_action);
CREATE INDEX IF NOT EXISTS t_admin_audit_log_idx_create_time ON t_admin_audit_log(f_create_time);

CREATE TABLE IF NOT EXISTS t_user_permission
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_user_id INTEGER NOT NULL,    -- 用户ID
    f_perm_key VARCHAR(64) NOT NULL,    -- 权限项键名，如 msg.external、data.export_org
    f_granted INTEGER NOT NULL DEFAULT 0,    -- 是否授予：0=禁止 1=允许（覆盖角色默认值）
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_remark VARCHAR(64) DEFAULT NULL    -- 备注
);

CREATE UNIQUE INDEX IF NOT EXISTS t_user_permission_uniq_user_perm ON t_user_permission(f_user_id, f_perm_key);

CREATE TRIGGER IF NOT EXISTS trg_t_user_permission_f_update_time AFTER UPDATE ON t_user_permission
FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time
BEGIN
    UPDATE t_user_permission SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;
END;

CREATE TABLE IF NOT EXISTS t_user_activity_log
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_user_id INTEGER NOT NULL,    -- 用户ID
    f_action VARCHAR(64) NOT NULL,    -- 操作类型，如 file.upload / msg.recall / login.success
    f_detail VARCHAR(512) DEFAULT NULL,    -- 操作详情
    f_severity INTEGER NOT NULL DEFAULT 0,    -- 级别：0=info 1=warn 2=danger
    f_create_time DATETIME NOT NULL    -- 操作发生时间
);

CREATE INDEX IF NOT EXISTS t_user_activity_log_idx_user ON t_user_activity_log(f_user_id);
CREATE INDEX IF NOT EXISTS t_user_activity_log_idx_create_time ON t_user_activity_log(f_create_time);

CREATE TABLE IF NOT EXISTS t_announcement
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_announcement_id INTEGER NOT NULL,    -- 公告业务ID
    f_title VARCHAR(256) NOT NULL,    -- 公告标题
    f_body TEXT DEFAULT NULL,    -- 公告正文
    f_severity INTEGER NOT NULL DEFAULT 0,    -- 级别：0=普通 1=提醒 2=警告
    f_sender_admin_id INTEGER NOT NULL DEFAULT 0,    -- 发布者管理员ID
    f_sender_name VARCHAR(64) DEFAULT NULL,    -- 发布者名称（冗余，如平台运营、安全团队）
    f_audience_type INTEGER NOT NULL DEFAULT 0,    -- 受众类型：0=全部企业 1=指定企业 2=指定部门 3=指定员工 4=平台内部
    f_pinned INTEGER NOT NULL DEFAULT 0,    -- 是否置顶：0=否 1=是
    f_scheduled INTEGER NOT NULL DEFAULT 0,    -- 是否定时发布：0=否 1=是
    f_scheduled_time DATETIME DEFAULT NULL,    -- 定时发布时间（f_scheduled=1时有效）
    f_sent_time DATETIME DEFAULT NULL,    -- 实际发布时间
    f_status INTEGER NOT NULL DEFAULT 0,    -- 状态：0=草稿 1=已发布 2=已撤回
    f_create_time DATETIME NOT NULL,    -- 创建时间
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_remark VARCHAR(64) DEFAULT NULL    -- 备注
);

CREATE UNIQUE INDEX IF NOT EXISTS t_announcement_uniq_announcement_id ON t_announcement(f_announcement_id);

CREATE TRIGGER IF NOT EXISTS trg_t_announcement_f_update_time AFTER UPDATE ON t_announcement
FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time
BEGIN
    UPDATE t_announcement SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;
END;

CREATE TABLE IF NOT EXISTS t_announcement_target
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_announcement_id INTEGER NOT NULL,    -- 公告ID
    f_target_type INTEGER NOT NULL,    -- 目标类型：1=企业 2=部门 3=员工
    f_target_id INTEGER NOT NULL,    -- 目标ID（企业ID/部门ID/用户ID）
    f_create_time DATETIME NOT NULL    -- 创建时间
);

CREATE INDEX IF NOT EXISTS t_announcement_target_idx_announcement ON t_announcement_target(f_announcement_id);

CREATE TABLE IF NOT EXISTS t_announcement_read
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_announcement_id INTEGER NOT NULL,    -- 公告ID
    f_user_id INTEGER NOT NULL,    -- 已读用户ID
    f_read_time DATETIME NOT NULL    -- 已读时间
);

CREATE UNIQUE INDEX IF NOT EXISTS t_announcement_read_uniq_announcement_user ON t_announcement_read(f_announcement_id, f_user_id);
CREATE INDEX IF NOT EXISTS t_announcement_read_idx_announcement ON t_announcement_read(f_announcement_id);

CREATE TABLE IF NOT EXISTS t_login_record
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_user_id INTEGER NOT NULL,    -- 登录的用户ID
    f_login_time DATETIME NOT NULL,    -- 登录时间
    f_device VARCHAR(128) DEFAULT NULL,    -- 设备描述
    f_device_kind INTEGER NOT NULL DEFAULT 0,    -- 设备类型：1=PC 2=Android 3=iOS 4=Web 5=Mac
    f_os VARCHAR(64) DEFAULT NULL,    -- 操作系统
    f_ip VARCHAR(64) DEFAULT NULL,    -- 登录IP
    f_location VARCHAR(128) DEFAULT NULL,    -- 登录地理位置
    f_result INTEGER NOT NULL DEFAULT 0,    -- 结果：0=成功 1=失败
    f_fail_reason VARCHAR(128) DEFAULT NULL,    -- 失败原因（f_result=1时）
    f_create_time DATETIME NOT NULL    -- 创建时间
);

CREATE INDEX IF NOT EXISTS t_login_record_idx_user ON t_login_record(f_user_id);
CREATE INDEX IF NOT EXISTS t_login_record_idx_login_time ON t_login_record(f_login_time);

CREATE TABLE IF NOT EXISTS t_service_account
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_service_id INTEGER NOT NULL,    -- 服务号业务ID
    f_enterprise_id INTEGER NOT NULL,    -- 所属企业ID
    f_name VARCHAR(64) NOT NULL,    -- 服务号名称
    f_kind INTEGER NOT NULL DEFAULT 0,    -- 类型：0=通知型（单向推送）1=助手型（员工可回复）
    f_description VARCHAR(256) DEFAULT NULL,    -- 服务号描述
    f_avatar_color VARCHAR(16) DEFAULT NULL,    -- 头像底色
    f_avatar_glyph VARCHAR(16) DEFAULT NULL,    -- 头像字形/图标
    f_owner_admin_id INTEGER NOT NULL DEFAULT 0,    -- 所有者管理员ID
    f_allow_unsubscribe INTEGER NOT NULL DEFAULT 1,    -- 是否允许员工取消订阅：0=否 1=是
    f_status INTEGER NOT NULL DEFAULT 0,    -- 状态：0=正常 1=停用
    f_create_time DATETIME NOT NULL,    -- 创建时间
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_remark VARCHAR(64) DEFAULT NULL    -- 备注
);

CREATE UNIQUE INDEX IF NOT EXISTS t_service_account_uniq_service_id ON t_service_account(f_service_id);
CREATE INDEX IF NOT EXISTS t_service_account_idx_enterprise ON t_service_account(f_enterprise_id);

CREATE TRIGGER IF NOT EXISTS trg_t_service_account_f_update_time AFTER UPDATE ON t_service_account
FOR EACH ROW WHEN NEW.f_update_time = OLD.f_update_time
BEGIN
    UPDATE t_service_account SET f_update_time = datetime('now','localtime') WHERE rowid = NEW.rowid;
END;

CREATE TABLE IF NOT EXISTS t_service_account_binding
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_service_id INTEGER NOT NULL,    -- 服务号ID
    f_bind_type INTEGER NOT NULL,    -- 绑定类型：0=本企业全员 1=部门 2=员工
    f_bind_target_id INTEGER NOT NULL DEFAULT 0,    -- 绑定目标ID（部门ID或用户ID；全员时为0）
    f_create_time DATETIME NOT NULL    -- 创建时间
);

CREATE INDEX IF NOT EXISTS t_service_account_binding_idx_service ON t_service_account_binding(f_service_id);

CREATE TABLE IF NOT EXISTS t_contact_remark
(
    f_userid INTEGER NOT NULL,    -- 备注名归属用户userid
    f_target_userid INTEGER NOT NULL,    -- 被备注的联系人userid
    f_remark VARCHAR(32) NOT NULL DEFAULT '',    -- 备注名
    f_update_time INTEGER NOT NULL DEFAULT 0,    -- 最近一次修改时间(Unix毫秒)
    PRIMARY KEY (f_userid, f_target_userid)
);

CREATE INDEX IF NOT EXISTS t_contact_remark_idx_user ON t_contact_remark(f_userid);

CREATE TABLE IF NOT EXISTS t_blacklist
(
    f_userid INTEGER NOT NULL,    -- 拉黑发起方userid
    f_blocked_userid INTEGER NOT NULL,    -- 被拉黑方userid
    f_create_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 拉黑时间
    PRIMARY KEY (f_userid, f_blocked_userid)
);

CREATE TABLE IF NOT EXISTS t_conversation
(
    f_userid INTEGER NOT NULL,    -- 会话归属用户userid
    f_ctype INTEGER NOT NULL DEFAULT 1,    -- 会话类型:1单聊 2群聊
    f_cid INTEGER NOT NULL,    -- 会话ID:单聊为对端userid,群聊为群id
    f_pinned INTEGER NOT NULL DEFAULT 0,    -- 是否置顶:0否 1是
    f_mute INTEGER NOT NULL DEFAULT 0,    -- 是否免打扰:0否 1是
    f_readmsgid INTEGER NOT NULL DEFAULT 0,    -- 已读到的最大消息msgid
    f_deleted INTEGER NOT NULL DEFAULT 0,    -- 会话是否从列表删除:0否 1是
    f_deleted_msgid INTEGER NOT NULL DEFAULT 0,    -- 删除会话时该会话的最后消息msgid锚点:只有最后消息msgid<=本值时才隐藏,来了更新的消息会话即重现
    f_update_time INTEGER NOT NULL DEFAULT 0,    -- 本行更新时间,Unix毫秒
    PRIMARY KEY (f_userid, f_ctype, f_cid)
);

CREATE TABLE IF NOT EXISTS t_message_deleted
(
    f_userid INTEGER NOT NULL,    -- 执行删除的用户userid
    f_msgid INTEGER NOT NULL,    -- 被该用户删除的消息msgid
    PRIMARY KEY (f_userid, f_msgid)
);

CREATE TABLE IF NOT EXISTS t_file_ack
(
    f_msgid INTEGER NOT NULL,    -- 文件消息的msgid
    f_state INTEGER NOT NULL DEFAULT 0,    -- 收取状态:1正在接收 2已接收 3已拒绝
    f_update_time INTEGER NOT NULL DEFAULT 0,    -- 状态更新时间,Unix毫秒
    PRIMARY KEY (f_msgid)
);

CREATE TABLE IF NOT EXISTS t_message_pin
(
    f_msgid INTEGER NOT NULL,    -- 被Pin的消息msgid
    f_operator INTEGER NOT NULL DEFAULT 0,    -- 执行Pin的用户userid,取消Pin的权限判断依据
    f_pin_time INTEGER NOT NULL DEFAULT 0,    -- Pin时间,Unix毫秒;重复Pin不刷新本列
    PRIMARY KEY (f_msgid)
);

CREATE TABLE IF NOT EXISTS t_favorite
(
    f_userid INTEGER NOT NULL,    -- 收藏归属用户userid
    f_ctype INTEGER NOT NULL,    -- 收藏对象类型:1联系人 2群组
    f_cid INTEGER NOT NULL,    -- 收藏对象ID:联系人为对端userid,群组为群id
    f_order INTEGER NOT NULL DEFAULT 0,    -- 手动排序权重,值小者靠前;0表示未手动排序(本期恒为0,留给日后拖拽排序)
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 收藏时间,Unix毫秒;列表按本列倒序,最近收藏的排最前
    PRIMARY KEY (f_userid, f_ctype, f_cid)
);

CREATE INDEX IF NOT EXISTS t_favorite_idx_favorite_user ON t_favorite(f_userid);

CREATE TABLE IF NOT EXISTS t_group
(
    f_groupid INTEGER NOT NULL,    -- 群ID(高位id空间,>=GROUPID_BOUBDARY)
    f_name VARCHAR(64) NOT NULL,    -- 群名称
    f_facetype INTEGER NOT NULL DEFAULT 0,    -- 群头像类型
    f_customface VARCHAR(256) NOT NULL DEFAULT '',    -- 群自定义头像文件标识
    f_announcement VARCHAR(1024) NOT NULL DEFAULT '',    -- 群公告
    f_ownerid INTEGER NOT NULL,    -- 群主userid
    f_join_mode INTEGER NOT NULL DEFAULT 1,    -- 加群方式:0自由 1需审批 2禁止
    f_mute_all INTEGER NOT NULL DEFAULT 0,    -- 全员禁言:0否 1是
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 创建时间(Unix毫秒)
    f_intro VARCHAR(512) NOT NULL DEFAULT '',    -- 群简介
    f_at_all_scope INTEGER NOT NULL DEFAULT 0,    -- 谁能@全体:0仅群主和管理员 1所有人
    f_new_see_history INTEGER NOT NULL DEFAULT 1,    -- 新成员可看入群前历史消息:0否 1是
    f_member_invite INTEGER NOT NULL DEFAULT 0,    -- 普通成员邀请好友免审批:0否(转待审批) 1是(直接入群)
    f_welcome_on INTEGER NOT NULL DEFAULT 0,    -- 入群欢迎语开关:0关 1开
    f_welcome_text VARCHAR(256) NOT NULL DEFAULT '',    -- 入群欢迎语正文
    f_sensitive_on INTEGER NOT NULL DEFAULT 0,    -- 消息敏感词过滤开关:0关 1开
    f_announce_id INTEGER NOT NULL DEFAULT 0,    -- 最新已发布公告的f_id(t_group_announcement);0=本群还没有公告
    f_announce_title VARCHAR(128) NOT NULL DEFAULT '',    -- 最新已发布公告的标题(冗余快照,群聊窗头部摘要行直接读它)
    f_announce_time INTEGER NOT NULL DEFAULT 0,    -- 最新已发布公告的发布时间(Unix毫秒)
    PRIMARY KEY (f_groupid)
);

CREATE TABLE IF NOT EXISTS t_group_announcement
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 公告ID
    f_groupid INTEGER NOT NULL,    -- 所属群ID
    f_title VARCHAR(128) NOT NULL DEFAULT '',    -- 公告标题(必填)
    f_content VARCHAR(2048) NOT NULL DEFAULT '',    -- 公告正文
    f_severity INTEGER NOT NULL DEFAULT 0,    -- 优先级:0通知 1重要 2紧急
    f_publisher INTEGER NOT NULL DEFAULT 0,    -- 发布人userid
    f_publish_time INTEGER NOT NULL DEFAULT 0    -- 发布时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_group_announcement_idx_group_time ON t_group_announcement(f_groupid, f_publish_time);

CREATE TABLE IF NOT EXISTS t_group_member
(
    f_groupid INTEGER NOT NULL,    -- 群ID
    f_userid INTEGER NOT NULL,    -- 成员userid
    f_role INTEGER NOT NULL DEFAULT 0,    -- 角色:0成员 1管理员 2群主
    f_card VARCHAR(64) NOT NULL DEFAULT '',    -- 群名片;空则用账号昵称
    f_muted INTEGER NOT NULL DEFAULT 0,    -- 是否被禁言:0否 1是
    f_join_time INTEGER NOT NULL DEFAULT 0,    -- 入群时间(Unix毫秒)
    f_mute_until INTEGER NOT NULL DEFAULT 0,    -- 禁言到期时间(Unix毫秒);f_muted=1且本列为0表示永久禁言
    f_mute_reason VARCHAR(128) NOT NULL DEFAULT '',    -- 禁言原因
    PRIMARY KEY (f_groupid, f_userid)
);

CREATE INDEX IF NOT EXISTS t_group_member_idx_user ON t_group_member(f_userid);

CREATE TABLE IF NOT EXISTS t_group_join_request
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 申请ID
    f_groupid INTEGER NOT NULL,    -- 目标群ID
    f_userid INTEGER NOT NULL,    -- 申请人userid
    f_verify_msg VARCHAR(256) DEFAULT NULL,    -- 验证留言
    f_status INTEGER NOT NULL DEFAULT 0,    -- 状态:0待处理 1已通过 2已拒绝
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 申请时间(Unix毫秒)
    f_handle_time INTEGER NOT NULL DEFAULT 0,    -- 处理时间(Unix毫秒,0未处理)
    f_handler INTEGER NOT NULL DEFAULT 0,    -- 处理人userid(0未处理)
    f_source INTEGER NOT NULL DEFAULT 1,    -- 申请来源:0好友邀请 1群搜索 2扫码加群
    f_recommender INTEGER NOT NULL DEFAULT 0    -- 推荐人userid(仅好友邀请时非0)
);

CREATE INDEX IF NOT EXISTS t_group_join_request_idx_group_status ON t_group_join_request(f_groupid, f_status);

CREATE TABLE IF NOT EXISTS t_group_sensitive_word
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 词条ID
    f_groupid INTEGER NOT NULL,    -- 所属群ID
    f_word VARCHAR(64) NOT NULL,    -- 敏感词
    f_create_time INTEGER NOT NULL DEFAULT 0    -- 添加时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_group_sensitive_word_idx_group ON t_group_sensitive_word(f_groupid);

CREATE TABLE IF NOT EXISTS t_card_interaction
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_msgid INTEGER NOT NULL,    -- 卡片所在消息id
    f_userid INTEGER NOT NULL,    -- 互动用户userid
    f_data VARCHAR(1024) NOT NULL DEFAULT '',    -- 互动数据:投票为选项下标JSON数组/接龙为接龙文本/待办为勾选标记
    f_create_time INTEGER NOT NULL DEFAULT 0    -- 互动时间(Unix毫秒)
);

CREATE UNIQUE INDEX IF NOT EXISTS t_card_interaction_uniq_msg_user ON t_card_interaction(f_msgid, f_userid);
CREATE INDEX IF NOT EXISTS t_card_interaction_idx_msgid ON t_card_interaction(f_msgid);

CREATE TABLE IF NOT EXISTS t_file
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 文件记录ID,即对外fileid
    f_md5 CHAR(32) NOT NULL DEFAULT '',    -- 文件内容MD5(小写十六进制),秒传与磁盘文件名依据
    f_name VARCHAR(256) NOT NULL DEFAULT '',    -- 上传时的原始文件名
    f_ext VARCHAR(32) NOT NULL DEFAULT '',    -- 扩展名(不含点)
    f_size INTEGER NOT NULL DEFAULT 0,    -- 文件总字节数
    f_scene INTEGER NOT NULL DEFAULT 0,    -- 用途场景:1聊天文件 2群文件 3头像 4图片消息
    f_uploaderid INTEGER NOT NULL DEFAULT 0,    -- 上传者用户userid
    f_status INTEGER NOT NULL DEFAULT 0,    -- 状态:0待上传 1已完成
    f_createtime INTEGER NOT NULL DEFAULT 0    -- 记录创建时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_file_idx_md5 ON t_file(f_md5);
CREATE INDEX IF NOT EXISTS t_file_idx_uploader ON t_file(f_uploaderid);

CREATE TABLE IF NOT EXISTS t_group_file
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 群文件条目ID,即对外entryid
    f_groupid INTEGER NOT NULL,    -- 所属群ID
    f_fileid INTEGER NOT NULL,    -- 文件ID,引用t_file.f_id
    f_filetype INTEGER NOT NULL DEFAULT 6,    -- 文件类别:1文档 2图片 3视频 4音频 5压缩包 6其它
    f_uploaderid INTEGER NOT NULL DEFAULT 0,    -- 登记者(上传者)userid
    f_uploadtime INTEGER NOT NULL DEFAULT 0    -- 登记时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_group_file_idx_group ON t_group_file(f_groupid);
CREATE INDEX IF NOT EXISTS t_group_file_idx_fileid ON t_group_file(f_fileid);
