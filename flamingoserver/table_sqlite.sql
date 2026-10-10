-- ============================================================
--  FlamingoIM 本地 SQLite 模式的建表脚本
--  balloonwj@qq.com   2026-07-31
--
--  本文件由 tools/gen_table_sqlite.py 从 table_mysql.sql 自动生成，请勿手工编辑。
--  改了 table_mysql.sql 之后重跑该脚本即可：python tools/gen_table_sqlite.py
--
--  chatserver 在 dbtype=sqlite 时启动会执行本脚本建表；语句全部是
--  IF NOT EXISTS 形式，重复执行安全；已有的表不做任何改动（不补列），
--  表结构有变化时，已有的库须手工修改或重建。
-- ============================================================

CREATE TABLE IF NOT EXISTS t_user
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_user_id INTEGER NOT NULL,    -- 用户ID
    f_username VARCHAR(64) COLLATE NOCASE NOT NULL,    -- 用户名
    f_nickname VARCHAR(64) NOT NULL,    -- 用户昵称
    f_password VARCHAR(64) NOT NULL,    -- 登录密码：v2存 MD5(SHA256(明文)+f_salt)
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
    f_post VARCHAR(64) DEFAULT NULL,    -- 岗位：本人填写的自由文本，与部门职位（t_department_member.f_position）无关
    f_phone_visible INTEGER NOT NULL DEFAULT 0,    -- 手机号是否对同事公开：0=仅本人可见 1=对同事可见
    f_landline_visible INTEGER NOT NULL DEFAULT 1,    -- 座机是否对同事公开：0=仅本人可见 1=对同事可见
    f_workstation VARCHAR(256) DEFAULT NULL,    -- 工位地址
    f_status INTEGER NOT NULL DEFAULT 0,    -- 账号状态：0=启用 1=停用
    f_usertype INTEGER NOT NULL DEFAULT 0    -- 用户类型：0=普通用户 1=服务号用户 2=机器人
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
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 消息时间(Unix毫秒)
    f_audience INTEGER NOT NULL DEFAULT 0,    -- 可见范围:0全员可见 1仅发送者与t_message_audience里登记的人可见(如只给部分群成员看的文件共享卡片)
    f_recalled_by INTEGER NOT NULL DEFAULT 0,    -- 撤回者userid:0表示未撤回,或撤回发生在2026-10-03之前没有记录(此时按发送方处理)。群主与管理员可撤回别人的群消息,所以不一定等于f_from
    f_search_text TEXT,    -- 搜索用纯文本(文字正文/文件名/卡片标题等,UTF-8,去掉四字节字符,最长4000字节);NULL表示尚未提取
    f_rev INTEGER NOT NULL DEFAULT 0    -- 改写序号:0表示从未被改写;每次改写内容或撤回时取全局递增的新值
);

CREATE INDEX IF NOT EXISTS t_chatmsg_idx_from_to ON t_chatmsg(f_from, f_to);
CREATE INDEX IF NOT EXISTS t_chatmsg_idx_to_from ON t_chatmsg(f_to, f_from);
CREATE INDEX IF NOT EXISTS t_chatmsg_idx_client ON t_chatmsg(f_clientmsgid);
CREATE INDEX IF NOT EXISTS t_chatmsg_idx_to_msgid ON t_chatmsg(f_to, f_msgid);
CREATE INDEX IF NOT EXISTS t_chatmsg_idx_rev ON t_chatmsg(f_rev);

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
    f_plan INTEGER NOT NULL DEFAULT 0,    -- 套餐：0=基础版 1=专业版 2=旗舰版
    f_status INTEGER NOT NULL DEFAULT 0,    -- 状态：0=待审核 1=活跃 2=已停用
    f_root_dept_id INTEGER NOT NULL DEFAULT 0,    -- 公司根部门ID，关联 t_department.f_dept_id；代表"公司本身"这一层，员工可直接挂其下；0表示尚未建立（仅存量数据迁移前会出现）
    f_admin_name VARCHAR(64) DEFAULT NULL,    -- 主管理员姓名
    f_admin_email VARCHAR(128) DEFAULT NULL,    -- 主管理员邮箱
    f_logo_color VARCHAR(16) DEFAULT NULL,    -- 企业Logo底色，如 #ff5a44
    f_open_date DATETIME DEFAULT NULL,    -- 开通日期
    f_license_expire_time DATETIME DEFAULT NULL,    -- 企业版授权到期时间
    f_create_time DATETIME NOT NULL,    -- 创建时间
    f_update_time TIMESTAMP NOT NULL DEFAULT (datetime('now','localtime')),    -- 更新时间
    f_remark VARCHAR(64) DEFAULT NULL,    -- 备注
    f_sensitive_on INTEGER NOT NULL DEFAULT 1,    -- 敏感词过滤总开关：0=暂停 1=启用（词库为空时开关不起作用）
    f_account_prefix VARCHAR(16) DEFAULT NULL    -- 新建账号的前缀：英文字母与点号、大写保存；为空时用 EMP
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
    f_kind INTEGER NOT NULL DEFAULT 0,    -- 类型：0=通知型（单向推送）1=助手型（员工可回复）2=集成型（对接外部系统）
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

CREATE TABLE IF NOT EXISTS t_contact_tag
(
    f_userid INTEGER NOT NULL,    -- 标签归属用户userid（谁贴的）
    f_target_userid INTEGER NOT NULL,    -- 被贴标签的联系人userid（贴给谁）
    f_tag VARCHAR(16) COLLATE NOCASE NOT NULL DEFAULT '',    -- 标签文本，去掉首尾空白后的内容
    f_order INTEGER NOT NULL DEFAULT 0,    -- 同一联系人下的显示顺序，值小者靠前，从0起算
    f_update_time INTEGER NOT NULL DEFAULT 0,    -- 最近一次整份覆盖的时间(Unix毫秒)
    PRIMARY KEY (f_userid, f_target_userid, f_tag)
);

CREATE INDEX IF NOT EXISTS t_contact_tag_idx_user ON t_contact_tag(f_userid);

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
    f_notify_mode INTEGER NOT NULL DEFAULT 0,    -- 消息提醒模式:0允许提醒 1接收不提醒 2收进消息盒子 3屏蔽(仅群聊) 4特别关注 5只提醒@我和关注的人(仅群聊);取代原f_mute(0/1含义不变),见docs/消息提醒模式与消息盒子方案.md
    f_readmsgid INTEGER NOT NULL DEFAULT 0,    -- 已读到的最大消息msgid
    f_deleted INTEGER NOT NULL DEFAULT 0,    -- 会话是否从列表删除:0否 1是
    f_deleted_msgid INTEGER NOT NULL DEFAULT 0,    -- 删除会话时该会话的最后消息msgid锚点:只有最后消息msgid<=本值时才隐藏,来了更新的消息会话即重现
    f_cleared_msgid INTEGER NOT NULL DEFAULT 0,    -- 本人清空该会话聊天记录时的消息msgid锚点:msgid<=本值的消息对本人隐藏,对方与其他成员不受影响;0表示没有清空过
    f_unread_mark INTEGER NOT NULL DEFAULT 0,    -- 本人是否手动标为未读:0否 1是;打开会话或已读上报时清零,见docs/会话标为未读方案.md
    f_update_time INTEGER NOT NULL DEFAULT 0,    -- 本行更新时间,Unix毫秒
    PRIMARY KEY (f_userid, f_ctype, f_cid)
);

CREATE TABLE IF NOT EXISTS t_message_deleted
(
    f_userid INTEGER NOT NULL,    -- 执行删除的用户userid
    f_msgid INTEGER NOT NULL,    -- 被该用户删除的消息msgid
    PRIMARY KEY (f_userid, f_msgid)
);

CREATE TABLE IF NOT EXISTS t_message_deleted_range
(
    f_userid INTEGER NOT NULL,    -- 执行清理的用户userid
    f_begin_time INTEGER NOT NULL,    -- 区间开始,Unix毫秒(含)
    f_end_time INTEGER NOT NULL,    -- 区间结束,Unix毫秒(含)
    f_update_time INTEGER NOT NULL DEFAULT 0,    -- 本行写入时间,Unix毫秒,供排查
    PRIMARY KEY (f_userid, f_begin_time)
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
    f_announce_id INTEGER NOT NULL DEFAULT 0,    -- 最新已发布公告的f_id(t_group_announcement);0=本群还没有公告
    f_announce_title VARCHAR(128) NOT NULL DEFAULT '',    -- 最新已发布公告的标题(冗余快照,群聊窗头部摘要行直接读它)
    f_announce_time INTEGER NOT NULL DEFAULT 0,    -- 最新已发布公告的发布时间(Unix毫秒)
    f_name_custom INTEGER NOT NULL DEFAULT 1,    -- 群名是否由用户设定:1是(固定不变) 0否(自动群名,成员或昵称变化时由服务端重算)
    f_must_read_id INTEGER NOT NULL DEFAULT 0,    -- 入群必读公告的f_id(t_group_announcement);0=本群没有入群必读(docs/群公告接入方案.md §14)
    f_group_type INTEGER NOT NULL DEFAULT 0,    -- 群类型:0普通群 1帖子群(取值见Protocol.h的group_type,docs/帖子群方案.md);建群时确定,之后不变
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
    f_history_from INTEGER NOT NULL DEFAULT 0,    -- 本人可见的最早一条群消息msgid;0不限(入群时按邀请人选择的同步条数算出,群主与管理员不受限)
    f_must_read INTEGER NOT NULL DEFAULT 0,    -- 入群必读:0不需要或已确认 1待确认(待确认时不能发言,docs/群公告接入方案.md §14)
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
    f_recommender INTEGER NOT NULL DEFAULT 0,    -- 推荐人userid(仅好友邀请时非0)
    f_history_limit INTEGER NOT NULL DEFAULT -1,    -- 通过后可看的入群前消息条数:-1全部 0不同步 N最近N条(由邀请转来的申请记邀请人的选择)
    f_invite_id INTEGER NOT NULL DEFAULT 0    -- 由入群邀请卡片转来时为t_group_invite.f_id,审批结果据此回写卡片;0表示不是
);

CREATE INDEX IF NOT EXISTS t_group_join_request_idx_group_status ON t_group_join_request(f_groupid, f_status);

CREATE TABLE IF NOT EXISTS t_group_invite
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 邀请ID
    f_groupid INTEGER NOT NULL,    -- 目标群ID
    f_inviter INTEGER NOT NULL,    -- 邀请人userid
    f_invitee INTEGER NOT NULL,    -- 被邀请人userid
    f_history_limit INTEGER NOT NULL DEFAULT -1,    -- 入群后可看的入群前消息条数:-1全部 0不同步 N最近N条
    f_state INTEGER NOT NULL DEFAULT 0,    -- 状态:0待加入 1已加入 2等待审批 3未通过审批
    f_msgid INTEGER NOT NULL DEFAULT 0,    -- 卡片消息msgid(邀请人与被邀请人的单聊)
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 发出时间(Unix毫秒)
    f_expire_time INTEGER NOT NULL DEFAULT 0,    -- 到期时间(Unix毫秒),超过后仍待加入的显示为已过期
    f_handle_time INTEGER NOT NULL DEFAULT 0    -- 状态最后变化的时间(Unix毫秒,0未变化)
);

CREATE INDEX IF NOT EXISTS t_group_invite_idx_invitee_group ON t_group_invite(f_invitee, f_groupid);

CREATE TABLE IF NOT EXISTS t_sensitive_word
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 词条ID
    f_enterprise_id INTEGER NOT NULL,    -- 所属企业ID
    f_word VARCHAR(32) NOT NULL,    -- 词条原文（去掉首尾空白，最多32个字符）
    f_word_norm VARCHAR(32) NOT NULL,    -- 归一化后的词条，用于判重与构建匹配器
    f_category INTEGER NOT NULL DEFAULT 6,    -- 分类：1违法违规 2色情低俗 3攻击辱骂 4商业机密 5广告引流 6其他
    f_remark VARCHAR(64) DEFAULT NULL,    -- 备注
    f_creator_id INTEGER NOT NULL DEFAULT 0,    -- 添加者管理员ID
    f_creator_name VARCHAR(64) DEFAULT NULL,    -- 添加者姓名（冗余，便于列表直读）
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 添加时间(Unix毫秒)
    f_update_time INTEGER NOT NULL DEFAULT 0    -- 最后修改时间(Unix毫秒)
);

CREATE UNIQUE INDEX IF NOT EXISTS t_sensitive_word_uk_ent_norm ON t_sensitive_word(f_enterprise_id, f_word_norm);
CREATE INDEX IF NOT EXISTS t_sensitive_word_idx_enterprise ON t_sensitive_word(f_enterprise_id);

CREATE TABLE IF NOT EXISTS t_sensitive_hit_log
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增ID
    f_enterprise_id INTEGER NOT NULL,    -- 提交者所属企业ID
    f_user_id INTEGER NOT NULL DEFAULT 0,    -- 提交者userid；自助注册时尚无账号，为0
    f_user_name VARCHAR(64) DEFAULT NULL,    -- 提交者账号或昵称快照
    f_source INTEGER NOT NULL,    -- 来源：见 chatserversrc/SensitiveWordDefs.h 的 sensitive_hit_source
    f_ctype INTEGER NOT NULL DEFAULT 0,    -- 会话类型：0与会话无关 1单聊 2群聊
    f_cid INTEGER NOT NULL DEFAULT 0,    -- 会话ID：单聊为对方userid，群聊为群ID
    f_target_name VARCHAR(64) DEFAULT NULL,    -- 会话名称快照（对方姓名或群名）
    f_word_id INTEGER NOT NULL DEFAULT 0,    -- 命中的词条ID（词条可能已被删除）
    f_word VARCHAR(32) NOT NULL,    -- 命中的词条原文快照
    f_category INTEGER NOT NULL DEFAULT 6,    -- 命中词条的分类快照
    f_snippet VARCHAR(128) DEFAULT NULL,    -- 内容摘要：命中位置前后各20个字符
    f_create_time INTEGER NOT NULL    -- 拦截时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_sensitive_hit_log_idx_ent_time ON t_sensitive_hit_log(f_enterprise_id, f_create_time);
CREATE INDEX IF NOT EXISTS t_sensitive_hit_log_idx_create_time ON t_sensitive_hit_log(f_create_time);

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

CREATE TABLE IF NOT EXISTS t_card_state
(
    f_msgid INTEGER NOT NULL,    -- 卡片所在消息id
    f_closed INTEGER NOT NULL DEFAULT 0,    -- 是否已被提前结束:1是
    f_closer INTEGER NOT NULL DEFAULT 0,    -- 结束操作者userid
    f_close_time INTEGER NOT NULL DEFAULT 0,    -- 结束时间(Unix毫秒)
    PRIMARY KEY (f_msgid)
);

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

CREATE TABLE IF NOT EXISTS t_client_version
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增主键
    f_platform INTEGER NOT NULL DEFAULT 1,    -- 客户端平台:1 Windows PC 2 Android(预留)
    f_version VARCHAR(32) NOT NULL DEFAULT '',    -- 本条记录发布的版本号,形如3.3.0
    f_min_version VARCHAR(32) NOT NULL DEFAULT '',    -- 允许登录的最低版本号,低于它的客户端被拒绝登录并强制升级
    f_release_note VARCHAR(2048) NOT NULL DEFAULT '',    -- 更新说明,展示在升级窗里
    f_package_md5 VARCHAR(32) NOT NULL DEFAULT '',    -- 升级包内容MD5(小写十六进制32位),fileserver在升级包目录里按它反查实际文件
    f_package_size INTEGER NOT NULL DEFAULT 0,    -- 升级包字节数
    f_package_name VARCHAR(255) NOT NULL DEFAULT '',    -- 升级包原始文件名,仅用于界面显示与本地落地命名
    f_forced INTEGER NOT NULL DEFAULT 0,    -- 是否强制升级:0否 1是。为1时客户端升级窗不给稍后出口、也不放行登录;与f_min_version相互独立,任一命中即按强制处理
    f_enabled INTEGER NOT NULL DEFAULT 0,    -- 是否已发布:0草稿不下发 1已发布
    f_publish_time INTEGER NOT NULL DEFAULT 0,    -- 发布时间(Unix毫秒)
    f_creator_adminid INTEGER NOT NULL DEFAULT 0,    -- 发布人的管理员ID,引用t_admin.f_admin_id;0表示非控制台创建
    f_create_time INTEGER NOT NULL DEFAULT 0    -- 记录创建时间(Unix毫秒),f_publish_time回滚时会被刷新而本列不变
);

CREATE INDEX IF NOT EXISTS t_client_version_idx_platform_enabled ON t_client_version(f_platform, f_enabled);

CREATE TABLE IF NOT EXISTS t_message_audience
(
    f_msgid INTEGER NOT NULL,    -- 消息ID,引用t_chatmsg.f_msgid
    f_user_id INTEGER NOT NULL,    -- 可以看到这条消息的用户userid
    PRIMARY KEY (f_msgid, f_user_id)
);

CREATE INDEX IF NOT EXISTS t_message_audience_idx_user ON t_message_audience(f_user_id);

CREATE TABLE IF NOT EXISTS t_file_share
(
    f_shareid INTEGER PRIMARY KEY AUTOINCREMENT,    -- 共享ID(全局自增,删除后不复用)
    f_client_share_id VARCHAR(64) NOT NULL DEFAULT '',    -- 客户端生成的UUID,与f_owner_id组成唯一键,发起共享请求重发时据此去重
    f_owner_id INTEGER NOT NULL,    -- 分享者userid
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 分享者所属企业ID(冗余一份,供后台按企业查询)
    f_device_id VARCHAR(64) NOT NULL DEFAULT '',    -- 发起共享时分享者的设备标识;共享只由这台电脑提供
    f_ctype INTEGER NOT NULL DEFAULT 1,    -- 所在会话类型:1单聊 2群聊
    f_cid INTEGER NOT NULL DEFAULT 0,    -- 所在会话:单聊为对端userid(分享者视角),群聊为群id
    f_kind INTEGER NOT NULL DEFAULT 2,    -- 共享类型:1磁盘 2文件夹 3单个文件,取值见Protocol.h的share_kind
    f_name VARCHAR(260) NOT NULL DEFAULT '',    -- 显示名:共享根的最后一级名字或磁盘卷标
    f_display_path VARCHAR(1024) NOT NULL DEFAULT '',    -- 分享者发起时的显示路径,仅供显示,客户端不据此提供文件
    f_include_hidden INTEGER NOT NULL DEFAULT 0,    -- 是否包含隐藏文件与系统文件:0否 1是
    f_state INTEGER NOT NULL DEFAULT 1,    -- 状态:1有效 2已取消
    f_cancel_reason INTEGER NOT NULL DEFAULT 0,    -- 取消原因,取值见Protocol.h的share_invalid_reason(1~5);未取消为0
    f_canceled_by INTEGER NOT NULL DEFAULT 0,    -- 取消操作人:后台强制取消时为管理员ID,其余为用户userid;未取消为0
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 发起时间(Unix毫秒)
    f_cancel_time INTEGER NOT NULL DEFAULT 0    -- 取消时间(Unix毫秒);未取消为0
);

CREATE UNIQUE INDEX IF NOT EXISTS t_file_share_uk_owner_client ON t_file_share(f_owner_id, f_client_share_id);
CREATE INDEX IF NOT EXISTS t_file_share_idx_owner ON t_file_share(f_owner_id, f_state);
CREATE INDEX IF NOT EXISTS t_file_share_idx_conv ON t_file_share(f_ctype, f_cid, f_state);
CREATE INDEX IF NOT EXISTS t_file_share_idx_enterprise ON t_file_share(f_enterprise_id, f_state);

CREATE TABLE IF NOT EXISTS t_file_share_member
(
    f_shareid INTEGER NOT NULL,    -- 共享ID,引用t_file_share.f_shareid
    f_user_id INTEGER NOT NULL,    -- 成员userid
    f_perms INTEGER NOT NULL DEFAULT 0,    -- 写权限位:1上传 2新建文件夹 4重命名 8删除 16移动,取值见Protocol.h的share_perm
    f_add_time INTEGER NOT NULL DEFAULT 0,    -- 加入时间(Unix毫秒)
    PRIMARY KEY (f_shareid, f_user_id)
);

CREATE INDEX IF NOT EXISTS t_file_share_member_idx_user ON t_file_share_member(f_user_id);

CREATE TABLE IF NOT EXISTS t_file_share_audit
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 自增主键
    f_shareid INTEGER NOT NULL DEFAULT 0,    -- 共享ID,引用t_file_share.f_shareid
    f_owner_id INTEGER NOT NULL DEFAULT 0,    -- 分享者userid(冗余一份,便于按分享者查询)
    f_actor_id INTEGER NOT NULL DEFAULT 0,    -- 操作人:访问者、分享者的userid,或后台管理员ID
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 分享者所属企业ID,后台按企业查询用
    f_action INTEGER NOT NULL DEFAULT 0,    -- 动作,取值见Protocol.h的share_audit_action
    f_source INTEGER NOT NULL DEFAULT 1,    -- 来源:1服务端记录 2分享者客户端上报,取值见share_audit_source
    f_path VARCHAR(1024) NOT NULL DEFAULT '',    -- 相对共享根的路径(UTF-8,/分隔);申请传输时为条目清单的摘要
    f_to_path VARCHAR(1024) NOT NULL DEFAULT '',    -- 重命名后的新名字或移动的目标目录;其余动作为空
    f_size INTEGER NOT NULL DEFAULT 0,    -- 涉及的字节数;没有字节数的动作为0
    f_result INTEGER NOT NULL DEFAULT 0,    -- 结果:0成功,否则为错误码(error_code_share_*等)
    f_access_id VARCHAR(64) NOT NULL DEFAULT '',    -- 所属访问会话ID(内存中的会话标识,便于把同一次访问的记录归到一起)
    f_create_time INTEGER NOT NULL DEFAULT 0    -- 记录时间(Unix毫秒,服务端时间)
);

CREATE INDEX IF NOT EXISTS t_file_share_audit_idx_enterprise_time ON t_file_share_audit(f_enterprise_id, f_create_time);
CREATE INDEX IF NOT EXISTS t_file_share_audit_idx_actor_time ON t_file_share_audit(f_actor_id, f_create_time);
CREATE INDEX IF NOT EXISTS t_file_share_audit_idx_share ON t_file_share_audit(f_shareid);

CREATE TABLE IF NOT EXISTS t_system_setting
(
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 企业ID
    f_key VARCHAR(64) NOT NULL,    -- 设置项名,如fileshare.enabled
    f_value VARCHAR(1024) NOT NULL DEFAULT '',    -- 设置值(文本)
    f_updated_by INTEGER NOT NULL DEFAULT 0,    -- 最后修改的管理员ID
    f_update_time INTEGER NOT NULL DEFAULT 0,    -- 最后修改时间(Unix毫秒)
    PRIMARY KEY (f_enterprise_id, f_key)
);

CREATE TABLE IF NOT EXISTS t_memo
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 备忘ID
    f_ctype INTEGER NOT NULL DEFAULT 1,    -- 所属:1个人 2群,取值见conversation_type
    f_owner INTEGER NOT NULL,    -- 个人备忘为本人userid,群备忘为群id
    f_creator INTEGER NOT NULL DEFAULT 0,    -- 创建人userid
    f_editor INTEGER NOT NULL DEFAULT 0,    -- 最后修改人userid
    f_title VARCHAR(256) NOT NULL DEFAULT '',    -- 标题(1~50字)
    f_note VARCHAR(1024) NOT NULL DEFAULT '',    -- 备注(0~200字)
    f_start INTEGER NOT NULL,    -- 首次事件的本地时间,十进制yyyymmddhhmm
    f_advance INTEGER NOT NULL DEFAULT 0,    -- 提前分钟数0~1440,0为到点提醒
    f_repeat INTEGER NOT NULL DEFAULT 0,    -- 重复方式,取值见Protocol.h的memo_repeat_type
    f_weekdays INTEGER NOT NULL DEFAULT 0,    -- 每周重复的星期掩码:bit0周一..bit6周日;其它重复方式为0
    f_count INTEGER NOT NULL DEFAULT 1,    -- 提醒次数上限,0为永不结束
    f_style INTEGER NOT NULL DEFAULT 1,    -- 提醒方式:1右下角弹窗 2全屏动画
    f_target_all INTEGER NOT NULL DEFAULT 0,    -- 群备忘是否提醒全体成员:1是 0只提醒t_memo_target里的成员
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 创建时间(Unix毫秒)
    f_update_time INTEGER NOT NULL DEFAULT 0    -- 最后修改时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_memo_idx_owner ON t_memo(f_ctype, f_owner);

CREATE TABLE IF NOT EXISTS t_memo_target
(
    f_memoid INTEGER NOT NULL,    -- 备忘ID(t_memo.f_id)
    f_userid INTEGER NOT NULL,    -- 被提醒成员userid
    PRIMARY KEY (f_memoid, f_userid)
);

CREATE INDEX IF NOT EXISTS t_memo_target_idx_user ON t_memo_target(f_userid);

CREATE TABLE IF NOT EXISTS t_group_follow
(
    f_userid INTEGER NOT NULL,    -- 关注发起人userid
    f_groupid INTEGER NOT NULL,    -- 群ID
    f_target_userid INTEGER NOT NULL,    -- 被关注的成员userid
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 关注时间(Unix毫秒)
    PRIMARY KEY (f_userid, f_groupid, f_target_userid)
);

CREATE TABLE IF NOT EXISTS t_group_qa
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 问答ID
    f_groupid INTEGER NOT NULL,    -- 所属群ID
    f_question VARCHAR(128) NOT NULL DEFAULT '',    -- 问题
    f_answer VARCHAR(2048) NOT NULL DEFAULT '',    -- 答案(纯文字)
    f_editor INTEGER NOT NULL DEFAULT 0,    -- 最后修改人userid
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 创建时间(Unix毫秒)
    f_update_time INTEGER NOT NULL DEFAULT 0    -- 最后修改时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_group_qa_idx_group ON t_group_qa(f_groupid);

CREATE TABLE IF NOT EXISTS t_enterprise_qa
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 问答ID
    f_enterprise_id INTEGER NOT NULL,    -- 所属企业ID
    f_question VARCHAR(128) NOT NULL DEFAULT '',    -- 问题
    f_answer VARCHAR(2048) NOT NULL DEFAULT '',    -- 答案(纯文字)
    f_scope INTEGER NOT NULL DEFAULT 0,    -- 应用范围:0本企业全部群 1所选的群(见t_enterprise_qa_group)
    f_creator_id INTEGER NOT NULL DEFAULT 0,    -- 最后修改的管理员ID
    f_creator_name VARCHAR(64) NOT NULL DEFAULT '',    -- 最后修改的管理员姓名(冗余,便于列表直读)
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 创建时间(Unix毫秒)
    f_update_time INTEGER NOT NULL DEFAULT 0    -- 最后修改时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_enterprise_qa_idx_enterprise ON t_enterprise_qa(f_enterprise_id);

CREATE TABLE IF NOT EXISTS t_enterprise_qa_group
(
    f_qa_id INTEGER NOT NULL,    -- 企业问答ID(t_enterprise_qa.f_id)
    f_groupid INTEGER NOT NULL,    -- 群ID
    PRIMARY KEY (f_qa_id, f_groupid)
);

CREATE INDEX IF NOT EXISTS t_enterprise_qa_group_idx_group ON t_enterprise_qa_group(f_groupid);

CREATE TABLE IF NOT EXISTS t_group_link
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 链接ID
    f_code VARCHAR(32) NOT NULL,    -- 随机码(12位,全局唯一)
    f_groupid INTEGER NOT NULL,    -- 群ID
    f_creator INTEGER NOT NULL,    -- 生成者userid
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 生成者所属企业ID(链接只对本企业的人有效)
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 生成时间(Unix毫秒)
    f_expire_time INTEGER NOT NULL DEFAULT 0,    -- 到期时间(Unix毫秒);0表示永久有效
    f_revoked INTEGER NOT NULL DEFAULT 0    -- 是否已作废:0否 1是
);

CREATE UNIQUE INDEX IF NOT EXISTS t_group_link_uniq_code ON t_group_link(f_code);
CREATE INDEX IF NOT EXISTS t_group_link_idx_group ON t_group_link(f_groupid);

CREATE TABLE IF NOT EXISTS t_cloud_record
(
    f_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 录像ID
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 开启者所属企业ID(配额按企业计)
    f_owner INTEGER NOT NULL,    -- 开启者userid(录像归他)
    f_source INTEGER NOT NULL DEFAULT 1,    -- 来源:1共享中的云端录制 2独立录制上传,取值见cloudrecord_source
    f_roomid VARCHAR(64) NOT NULL DEFAULT '',    -- 屏幕共享房间号(来源为共享时)
    f_ctype INTEGER NOT NULL DEFAULT 0,    -- 共享所在会话类型:1单聊 2群聊;来源为上传时为0
    f_cid INTEGER NOT NULL DEFAULT 0,    -- 共享所在会话ID:单聊为对方userid(以开启者为准),群聊为群ID
    f_title VARCHAR(256) NOT NULL DEFAULT '',    -- 标题(默认取共享标题或文件名,开启者可改)
    f_start_time INTEGER NOT NULL DEFAULT 0,    -- 开始录制时间(Unix毫秒)
    f_end_time INTEGER NOT NULL DEFAULT 0,    -- 结束录制时间(Unix毫秒);录制中为0
    f_duration_ms INTEGER NOT NULL DEFAULT 0,    -- 时长(毫秒,不含暂停)
    f_frec_bytes INTEGER NOT NULL DEFAULT 0,    -- 原始帧文件字节数;已删除或来源为上传时为0
    f_bytes INTEGER NOT NULL DEFAULT 0,    -- 最终文件(MP4/m4a)字节数;尚未上传时为0
    f_audio_only INTEGER NOT NULL DEFAULT 0,    -- 是否只有声音(m4a):1是
    f_status INTEGER NOT NULL DEFAULT 1,    -- 状态,取值见Protocol.h的cloudrecord_status
    f_frec_file VARCHAR(64) NOT NULL DEFAULT '',    -- 原始帧文件名(rec_<id>.frec);已删除或没有时为空
    f_file VARCHAR(64) NOT NULL DEFAULT '',    -- 最终文件名(rec_<id>.mp4或.m4a);尚未上传时为空
    f_expire_time INTEGER NOT NULL DEFAULT 0,    -- 到期时间(Unix毫秒);0为永久保留
    f_warned INTEGER NOT NULL DEFAULT 0,    -- 是否已推送到期提醒:1是
    f_create_time INTEGER NOT NULL DEFAULT 0    -- 创建时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_cloud_record_idx_owner ON t_cloud_record(f_owner, f_create_time);
CREATE INDEX IF NOT EXISTS t_cloud_record_idx_enterprise ON t_cloud_record(f_enterprise_id);
CREATE INDEX IF NOT EXISTS t_cloud_record_idx_expire ON t_cloud_record(f_expire_time);
CREATE INDEX IF NOT EXISTS t_cloud_record_idx_room ON t_cloud_record(f_roomid);

CREATE TABLE IF NOT EXISTS t_cloud_record_share
(
    f_recordid INTEGER NOT NULL,    -- 录像ID(t_cloud_record.f_id)
    f_ctype INTEGER NOT NULL,    -- 会话类型:1单聊 2群聊
    f_cid INTEGER NOT NULL,    -- 会话ID:单聊为接收方userid,群聊为群ID
    f_sender INTEGER NOT NULL DEFAULT 0,    -- 发送人userid
    f_send_time INTEGER NOT NULL DEFAULT 0,    -- 最近一次发送时间(Unix毫秒)
    PRIMARY KEY (f_recordid, f_ctype, f_cid)
);

CREATE INDEX IF NOT EXISTS t_cloud_record_share_idx_conv ON t_cloud_record_share(f_ctype, f_cid);

CREATE TABLE IF NOT EXISTS t_thread_post
(
    f_postid INTEGER PRIMARY KEY AUTOINCREMENT,    -- 帖子ID
    f_groupid INTEGER NOT NULL,    -- 所属帖子群ID
    f_author INTEGER NOT NULL,    -- 作者userid
    f_client_postid VARCHAR(64) NOT NULL DEFAULT '',    -- 客户端生成的发帖去重标识
    f_content BLOB,    -- 帖子内容JSON:title/text/images/mentions
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 发帖时间(Unix毫秒)
    f_edit_time INTEGER NOT NULL DEFAULT 0,    -- 最后修改时间(Unix毫秒);0表示没有修改过
    f_active_time INTEGER NOT NULL DEFAULT 0,    -- 最新动态时间:发帖、留言、回复、补充中最晚的一次(Unix毫秒)
    f_comment_count INTEGER NOT NULL DEFAULT 0,    -- 未删除的留言与回复总数
    f_pin_time INTEGER NOT NULL DEFAULT 0,    -- 置顶时间(Unix毫秒);0表示没有置顶
    f_cardmsgid INTEGER NOT NULL DEFAULT 0    -- 发帖时写进群里的帖子卡片msgid;0表示没有写成
);

CREATE INDEX IF NOT EXISTS t_thread_post_idx_group_active ON t_thread_post(f_groupid, f_active_time);
CREATE INDEX IF NOT EXISTS t_thread_post_idx_group_post ON t_thread_post(f_groupid, f_postid);
CREATE INDEX IF NOT EXISTS t_thread_post_idx_author_client ON t_thread_post(f_author, f_client_postid);

CREATE TABLE IF NOT EXISTS t_thread_addendum
(
    f_addid INTEGER PRIMARY KEY AUTOINCREMENT,    -- 补充ID
    f_postid INTEGER NOT NULL,    -- 所属帖子ID
    f_groupid INTEGER NOT NULL,    -- 所属帖子群ID
    f_author INTEGER NOT NULL,    -- 补充人userid(即楼主)
    f_content BLOB,    -- 补充内容JSON:text/images/mentions/at
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 补充时间(Unix毫秒)
    f_cardmsgid INTEGER NOT NULL DEFAULT 0    -- 这段补充的动态卡片msgid;0表示没有写卡片
);

CREATE INDEX IF NOT EXISTS t_thread_addendum_idx_post ON t_thread_addendum(f_postid, f_addid);
CREATE INDEX IF NOT EXISTS t_thread_addendum_idx_group ON t_thread_addendum(f_groupid);

CREATE TABLE IF NOT EXISTS t_thread_comment
(
    f_commentid INTEGER PRIMARY KEY AUTOINCREMENT,    -- 留言或回复ID
    f_postid INTEGER NOT NULL,    -- 所属帖子ID
    f_groupid INTEGER NOT NULL,    -- 所属帖子群ID
    f_author INTEGER NOT NULL,    -- 作者userid
    f_parentid INTEGER NOT NULL DEFAULT 0,    -- 0为留言;否则为所属留言的ID
    f_reply_to INTEGER NOT NULL DEFAULT 0,    -- 被回复的人userid;留言为0
    f_client_commentid VARCHAR(64) NOT NULL DEFAULT '',    -- 客户端生成的去重标识
    f_content BLOB,    -- 内容JSON:text/mentions/at
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 时间(Unix毫秒)
    f_deleted INTEGER NOT NULL DEFAULT 0,    -- 已删除但因下面有回复而保留占位:1是
    f_reply_count INTEGER NOT NULL DEFAULT 0,    -- 留言下面未删除的回复数;回复本身为0
    f_cardmsgid INTEGER NOT NULL DEFAULT 0    -- 这条留言或回复的动态卡片msgid;0表示没有写卡片
);

CREATE INDEX IF NOT EXISTS t_thread_comment_idx_post ON t_thread_comment(f_postid, f_parentid, f_commentid);
CREATE INDEX IF NOT EXISTS t_thread_comment_idx_parent ON t_thread_comment(f_parentid, f_commentid);
CREATE INDEX IF NOT EXISTS t_thread_comment_idx_group ON t_thread_comment(f_groupid);

CREATE TABLE IF NOT EXISTS t_thread_reaction
(
    f_target_type INTEGER NOT NULL,    -- 目标类型:1帖子 2留言或回复(取值见Protocol.h的thread_target_type)
    f_target_id INTEGER NOT NULL,    -- 目标ID:帖子ID或留言ID
    f_userid INTEGER NOT NULL,    -- 互动用户userid
    f_reaction INTEGER NOT NULL,    -- 互动:0赞,1000+表情编号为表情回应
    f_groupid INTEGER NOT NULL,    -- 所属帖子群ID
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 互动时间(Unix毫秒)
    PRIMARY KEY (f_target_type, f_target_id, f_userid, f_reaction)
);

CREATE INDEX IF NOT EXISTS t_thread_reaction_idx_group ON t_thread_reaction(f_groupid);

CREATE TABLE IF NOT EXISTS t_bot
(
    f_bot_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 机器人ID
    f_user_id INTEGER NOT NULL DEFAULT 0,    -- 机器人账号userid(t_user.f_usertype=2);个人机器人为0
    f_enterprise_id INTEGER NOT NULL DEFAULT 0,    -- 所属企业ID
    f_owner_type INTEGER NOT NULL DEFAULT 1,    -- 归属:1企业机器人 2群机器人 3个人机器人
    f_owner_id INTEGER NOT NULL DEFAULT 0,    -- 企业ID、群号或主人userid,随归属而定
    f_name VARCHAR(64) NOT NULL DEFAULT '',    -- 名称
    f_desc VARCHAR(512) NOT NULL DEFAULT '',    -- 简介
    f_glyph VARCHAR(16) NOT NULL DEFAULT '',    -- 头像字形
    f_color VARCHAR(16) NOT NULL DEFAULT '',    -- 头像底色#RRGGBB
    f_scope INTEGER NOT NULL DEFAULT 0,    -- 企业机器人的应用范围:0本企业全部群 1所选的群(见t_bot_group)
    f_allow_group_off INTEGER NOT NULL DEFAULT 0,    -- 企业机器人是否允许群主在本群关闭:0否 1是
    f_status INTEGER NOT NULL DEFAULT 0,    -- 状态:0启用 1停用 2已删除
    f_source INTEGER NOT NULL DEFAULT 0,    -- 来源:0按规则响应 1GitHub仓库通知 2GitLab仓库通知 3自动回复预置
    f_creator_type INTEGER NOT NULL DEFAULT 1,    -- 创建人类别:1管理员 2IM用户
    f_creator_id INTEGER NOT NULL DEFAULT 0,    -- 创建人ID
    f_creator_name VARCHAR(64) NOT NULL DEFAULT '',    -- 创建人名字(冗余,便于列表直读)
    f_webhook_token VARCHAR(64) NOT NULL DEFAULT '',    -- Webhook令牌(32位十六进制)
    f_secret VARCHAR(64) NOT NULL DEFAULT '',    -- 签名密钥(32位十六进制)
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 创建时间(Unix毫秒)
    f_update_time INTEGER NOT NULL DEFAULT 0,    -- 最后修改时间(Unix毫秒)
    f_last_fire_time INTEGER NOT NULL DEFAULT 0    -- 最近一次发出消息的时间(Unix毫秒,最多每分钟写一次)
);

CREATE INDEX IF NOT EXISTS t_bot_idx_enterprise ON t_bot(f_enterprise_id);
CREATE INDEX IF NOT EXISTS t_bot_idx_owner ON t_bot(f_owner_type, f_owner_id);
CREATE INDEX IF NOT EXISTS t_bot_idx_webhook ON t_bot(f_webhook_token);

CREATE TABLE IF NOT EXISTS t_bot_rule
(
    f_rule_id INTEGER PRIMARY KEY AUTOINCREMENT,    -- 规则ID
    f_bot_id INTEGER NOT NULL,    -- 所属机器人ID
    f_seq INTEGER NOT NULL DEFAULT 0,    -- 在机器人内的顺序,从0起
    f_name VARCHAR(64) NOT NULL DEFAULT '',    -- 规则名称
    f_enabled INTEGER NOT NULL DEFAULT 1,    -- 是否启用:0否 1是
    f_trigger_type INTEGER NOT NULL DEFAULT 1,    -- 触发方式:1被@ 2关键词 3入群 4定时 5收到单聊 6外部系统推送
    f_trigger BLOB,    -- 触发方式与附加条件(JSON)
    f_action_type INTEGER NOT NULL DEFAULT 1,    -- 动作:1回复文字 2用问答作答 3呼叫外部服务
    f_action BLOB,    -- 动作详情(JSON)
    f_create_time INTEGER NOT NULL DEFAULT 0,    -- 创建时间(Unix毫秒)
    f_update_time INTEGER NOT NULL DEFAULT 0    -- 最后修改时间(Unix毫秒)
);

CREATE INDEX IF NOT EXISTS t_bot_rule_idx_bot ON t_bot_rule(f_bot_id, f_seq);

CREATE TABLE IF NOT EXISTS t_bot_group
(
    f_bot_id INTEGER NOT NULL,    -- 机器人ID
    f_groupid INTEGER NOT NULL,    -- 群ID
    PRIMARY KEY (f_bot_id, f_groupid)
);

CREATE INDEX IF NOT EXISTS t_bot_group_idx_group ON t_bot_group(f_groupid);

CREATE TABLE IF NOT EXISTS t_bot_group_off
(
    f_groupid INTEGER NOT NULL,    -- 群ID
    f_bot_id INTEGER NOT NULL,    -- 机器人ID
    f_operator_id INTEGER NOT NULL DEFAULT 0,    -- 操作的群主userid
    f_time INTEGER NOT NULL DEFAULT 0,    -- 关闭时间(Unix毫秒)
    PRIMARY KEY (f_groupid, f_bot_id)
);

CREATE TABLE IF NOT EXISTS t_bot_domain
(
    f_enterprise_id INTEGER NOT NULL,    -- 企业ID
    f_domain VARCHAR(128) NOT NULL,    -- 域名(小写)
    PRIMARY KEY (f_enterprise_id, f_domain)
);
