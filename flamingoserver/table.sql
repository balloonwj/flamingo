//用户信息表
CREATE TABLE IF NOT EXISTS  t_user  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
		 f_user_id  bigint(20) NOT NULL COMMENT '用户ID',
		 f_username  varchar(64) NOT NULL COMMENT '用户名',
         f_nickname  varchar(64) NOT NULL COMMENT '用户昵称',
         f_password  varchar(64) DEFAULT NULL COMMENT '登录密码：v2存 MD5(SHA256(明文)+f_salt)',
         f_salt  varchar(32) DEFAULT NULL COMMENT '密码盐值，注册时生成（v2登录口令存储用）',
		 f_facetype  int(10) DEFAULT 0 COMMENT '用户头像类型',
		 f_customface  varchar(32) DEFAULT NULL COMMENT '自定义头像名',
		 f_customfacefmt  varchar(6) DEFAULT NULL COMMENT '自定义头像格式',
		 f_gender  int(2) DEFAULT 0 COMMENT '性别',
		 f_birthday  bigint(20) DEFAULT 19900101 COMMENT '生日',
		 f_signature  varchar(256) DEFAULT NULL COMMENT '个性签名',
		 f_address  varchar(256) DEFAULT NULL COMMENT '地址',
		 f_phonenumber  varchar(64) DEFAULT NULL COMMENT '电话',
		 f_mail  varchar(256) DEFAULT NULL COMMENT '邮箱',
         f_register_time  datetime NOT NULL COMMENT '注册时间',
		 f_owner_id  bigint(20) DEFAULT 0 COMMENT '群账号群主userid',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_teaminfo blob DEFAULT NULL COMMENT '好友分组信息',       
         f_enterprise_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '所属企业ID，关联 t_enterprise.f_enterprise_id',
         f_role  int(2) NOT NULL DEFAULT 0 COMMENT '企业内角色：0=普通成员 1=部门负责人 2=企业管理员',
         f_employee_no  varchar(32) DEFAULT NULL COMMENT '工号',
         f_join_date  datetime DEFAULT NULL COMMENT '入职日期',
         f_landline  varchar(64) DEFAULT NULL COMMENT '座机',
         f_workstation  varchar(256) DEFAULT NULL COMMENT '工位地址',
         f_status  int(2) NOT NULL DEFAULT 0 COMMENT '账号状态：0=启用 1=停用',
         f_usertype  int(2) NOT NULL DEFAULT 0 COMMENT '用户类型：0=普通用户 1=服务号用户',
         PRIMARY KEY ( f_user_id ),
         UNIQUE KEY uk_f_username (f_username),
         UNIQUE KEY uk_ent_employee_no (f_enterprise_id, f_employee_no),
         INDEX idx_enterprise (f_enterprise_id),
		 INDEX f_user_id (f_user_id),
         KEY  f_id  ( f_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//alter table t_user add f_teaminfo blob default null comment "好友分组信息";
//alter table t_user drop column f_teaminfo;

//用户关系表 t_user_relationship 已于企业版删除（2026-08-05）。
//企业版通讯录来自组织架构 t_department_member，好友关系表实测 0 行、从未被使用；
//其上唯一还有人用的"好友备注名"已迁到独立的 t_contact_remark（见本文件后半部分）。
//表名不要复用，存量库里的残表可手工 DROP。

//消息记录表（v2 重建：旧 v1 结构 f_senderid/f_targetid/f_msgcontent 已废弃，
//迁移脚本 migrations/v2_drop_old_chatmsg.sql 先 DROP 旧表，checkTable 启动时按本结构重建）
//结构化列承载可查询/可更新字段；content/mentions/mentionall/quote 打包进 f_content blob
CREATE TABLE IF NOT EXISTS  t_chatmsg  (
         f_msgid        bigint(20)  NOT NULL AUTO_INCREMENT COMMENT '服务器消息ID(全局自增,单调递增)',
         f_clientmsgid  varchar(64) NOT NULL DEFAULT '' COMMENT '客户端消息ID(UUID,去重+回执回填)',
         f_ctype        int(2)      NOT NULL DEFAULT 1 COMMENT '会话类型:1单聊 2群聊(本阶段恒为1)',
         f_from         bigint(20)  NOT NULL COMMENT '发送方userid',
         f_to           bigint(20)  NOT NULL COMMENT '接收方:单聊为对端userid,群聊为群id',
         f_msgtype      int(4)      NOT NULL DEFAULT 1 COMMENT '内容类型:1文本 2图片 3文件 4语音 …17系统',
         f_content      blob        COMMENT '内容JSON(content/mentions/mentionall/quote 打包)',
         f_recalled     int(2)      NOT NULL DEFAULT 0 COMMENT '是否已撤回:0否 1是',
         f_create_time  bigint(20)  NOT NULL DEFAULT 0 COMMENT '消息时间(Unix毫秒)',
         PRIMARY KEY ( f_msgid ),
         KEY  idx_from_to  ( f_from, f_to ),
         KEY  idx_to_from  ( f_to, f_from ),
         KEY  idx_client   ( f_clientmsgid ),
         //按会话取最新消息用。拉会话列表时要按会话取"最后一条消息"，查询形如
         //"WHERE f_ctype=? AND f_to IN (...) GROUP BY f_to" 取 MAX(f_msgid)，
         //本索引让它按 (f_to, f_msgid) 逐组直接跳到最大值。
         //idx_to_from 顶不上：它的第二列是 f_from，同一个会话内 msgid 是散的。
         //2026-08-12 两种后端都实测过（25 万行 / 50 个群，见
         //docs/会话列表拉取性能优化方案.md）：SQLite 1046ms → 68ms，MySQL 583ms → 303ms。
         //注意 MySQL 侧 checkTable 只补列不补索引，本行只对<u>新建库</u>生效；
         //存量库须执行 migrations/v6_add_t_chatmsg_idx_to_msgid.sql。
         KEY  idx_to_msgid ( f_to, f_msgid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//企业部门表（组织架构树）
//采用"邻接表 + 物化路径"混合方案：f_parent_dept_id 表达直接父子关系，
//f_dept_path 缓存自根到本节点(含本节点)的完整 dept_id 链，便于一次 LIKE 查询整棵子树。
CREATE TABLE IF NOT EXISTS  t_department  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_dept_id  bigint(20) NOT NULL COMMENT '部门业务ID（协议中引用部门统一用此ID，非自增f_id）',
         f_enterprise_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '所属企业ID',
         f_dept_name  varchar(64) NOT NULL COMMENT '部门名称',
         f_parent_dept_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '父部门ID，0表示根部门（公司本身/顶级部门）',
         f_dept_path  varchar(255) NOT NULL DEFAULT '' COMMENT '物化路径，自根到本节点(含本节点)，形如 /1001/1005/ ，用于子树查询',
         f_dept_level  int(10) NOT NULL DEFAULT 1 COMMENT '部门层级，根部门为1，每向下一级+1',
         f_dept_order  int(10) NOT NULL DEFAULT 0 COMMENT '同级部门排序权重，值小者靠前',
         f_leader_user_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '部门负责人userid，0表示暂未指定',
         f_dept_desc  varchar(256) DEFAULT NULL COMMENT '部门描述/职能说明',
         f_status  int(2) NOT NULL DEFAULT 0 COMMENT '部门状态：0=正常，1=停用（停用不物理删除，保留历史）',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_dept_id  ( f_dept_id ),
         KEY  idx_parent  ( f_parent_dept_id ),
         KEY  idx_path  ( f_dept_path ),
         KEY  idx_leader  ( f_leader_user_id ),
         KEY  idx_enterprise  ( f_enterprise_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//部门成员关系表（部门 ↔ 用户，多对多）
//一个部门可直接挂载多个成员；一个用户可隶属多个部门，由 f_is_primary 标识其主部门。
//注意：部门树的"子部门"由 t_department 自身父子关系表达，不在本表中。
CREATE TABLE IF NOT EXISTS  t_department_member  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_dept_id  bigint(20) NOT NULL COMMENT '部门ID，关联 t_department.f_dept_id',
         f_enterprise_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '所属企业ID（便于按租户隔离查询）',
         f_user_id  bigint(20) NOT NULL COMMENT '用户ID，关联 t_user.f_user_id',
         f_position  varchar(64) DEFAULT NULL COMMENT '该成员在本部门的职位/职务，如高级工程师、组长',
         f_is_primary  int(2) NOT NULL DEFAULT 1 COMMENT '是否主部门：1=主部门，0=兼属部门；每个用户至多一条为1',
         f_member_order  int(10) NOT NULL DEFAULT 0 COMMENT '成员在本部门内的排序权重，值小者靠前',
         f_status  int(2) NOT NULL DEFAULT 0 COMMENT '成员状态：0=在职，1=已移出/离职（保留历史，不物理删除）',
         f_join_time  datetime NOT NULL COMMENT '加入部门时间',
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_dept_user  ( f_dept_id, f_user_id ),
         KEY  idx_user  ( f_user_id ),
         KEY  idx_dept  ( f_dept_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//==================== 以下为企业 Admin 控制台相关表 ====================
//现有表多租户改造（MysqlManager 的 checkTable 会自动 ALTER ADD COLUMN，此处仅作记录）：
//alter table t_user add f_enterprise_id bigint(20) not null default 0 comment '所属企业ID';
//alter table t_user add f_role int(2) not null default 0 comment '企业内角色';
//alter table t_user add f_employee_no varchar(32) default null comment '工号';
//alter table t_user add f_join_date datetime default null comment '入职日期';
//alter table t_user add f_landline varchar(64) default null comment '座机';
//alter table t_user add f_workstation varchar(256) default null comment '工位地址';
//alter table t_user add f_status int(2) not null default 0 comment '账号状态';
//---- v2 登录改造（checkTable 自动 ALTER）----
//alter table t_user add f_salt varchar(32) default null comment '密码盐值，注册时生成';
//alter table t_user add f_usertype int(2) not null default 0 comment '用户类型：0=普通用户 1=服务号用户';
//alter table t_department add f_enterprise_id bigint(20) not null default 0 comment '所属企业ID';
//alter table t_department_member add f_enterprise_id bigint(20) not null default 0 comment '所属企业ID';
//alter table t_enterprise add f_root_dept_id bigint(20) not null default 0 comment '公司根部门ID';
//---- 企业全称唯一（注意：checkTable 只会自动补列，不会补索引）----
//存量库须手工执行 migrations/v4_add_t_enterprise_name_unique.sql；新库由下面的建表语句直接带上。
//加唯一键的动机：客户端注册时用户填的是公司全称，服务端要靠它精确定位唯一一家企业。
//alter table t_enterprise add unique key uniq_enterprise_name (f_name);

//企业租户表
CREATE TABLE IF NOT EXISTS  t_enterprise  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_enterprise_id  bigint(20) NOT NULL COMMENT '企业业务ID',
         f_name  varchar(128) NOT NULL COMMENT '企业全称',
         f_short_name  varchar(64) DEFAULT NULL COMMENT '企业简称',
         f_industry  varchar(64) DEFAULT NULL COMMENT '所属行业',
         f_region  varchar(64) DEFAULT NULL COMMENT '所在地区',
         f_plan  int(2) NOT NULL DEFAULT 0 COMMENT '套餐：0=基础版 1=专业版 2=企业版',
         f_status  int(2) NOT NULL DEFAULT 0 COMMENT '状态：0=待审核 1=活跃 2=已停用',
         f_root_dept_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '公司根部门ID，关联 t_department.f_dept_id；代表"公司本身"这一层，员工可直接挂其下；0表示尚未建立（仅存量数据迁移前会出现）',
         f_admin_name  varchar(64) DEFAULT NULL COMMENT '主管理员姓名',
         f_admin_email  varchar(128) DEFAULT NULL COMMENT '主管理员邮箱',
         f_logo_color  varchar(16) DEFAULT NULL COMMENT '企业Logo底色，如 #ff5a44',
         f_open_date  datetime DEFAULT NULL COMMENT '开通日期',
         f_license_expire_time  datetime DEFAULT NULL COMMENT '企业版授权到期时间',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_enterprise_id  ( f_enterprise_id ),
         UNIQUE KEY  uniq_enterprise_name  ( f_name )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//控制台管理员表
CREATE TABLE IF NOT EXISTS  t_admin  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_admin_id  bigint(20) NOT NULL COMMENT '管理员业务ID',
         f_email  varchar(128) NOT NULL COMMENT '登录邮箱（即登录账号）',
         f_password  varchar(64) NOT NULL COMMENT '登录密码：MD5(明文密码 + f_salt) 的十六进制串',
         f_salt  varchar(32) NOT NULL COMMENT '每个管理员独立的密码盐值',
         f_name  varchar(64) NOT NULL COMMENT '管理员姓名',
         f_enterprise_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '管辖范围：0=平台超级管理员，非0=仅限该企业',
         f_status  int(2) NOT NULL DEFAULT 0 COMMENT '状态：0=正常 1=停用',
         f_last_login_time  datetime DEFAULT NULL COMMENT '末次登录时间',
         f_last_login_ip  varchar(64) DEFAULT NULL COMMENT '末次登录IP',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_admin_id  ( f_admin_id ),
         UNIQUE KEY  uniq_email  ( f_email )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//管理员登录会话表
CREATE TABLE IF NOT EXISTS  t_admin_session  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_token  varchar(64) NOT NULL COMMENT '会话令牌，登录时下发',
         f_admin_id  bigint(20) NOT NULL COMMENT '所属管理员ID',
         f_login_ip  varchar(64) DEFAULT NULL COMMENT '登录IP',
         f_login_time  datetime NOT NULL COMMENT '登录时间',
         f_expire_time  datetime NOT NULL COMMENT '令牌过期时间',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_token  ( f_token ),
         KEY  idx_admin  ( f_admin_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//控制台审计/操作日志表
CREATE TABLE IF NOT EXISTS  t_admin_audit_log  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_actor_type  int(2) NOT NULL DEFAULT 0 COMMENT '操作者类型：0=管理员 1=系统',
         f_actor_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '操作者管理员ID，系统操作为0',
         f_actor_name  varchar(64) DEFAULT NULL COMMENT '操作者姓名（冗余，便于日志直读）',
         f_ip  varchar(64) DEFAULT NULL COMMENT '操作来源IP',
         f_enterprise_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '关联企业ID，平台级操作为0',
         f_action  varchar(64) NOT NULL COMMENT '操作类型，如 dept.create / user.disable / login.success',
         f_target  varchar(256) DEFAULT NULL COMMENT '操作目标的文字描述',
         f_severity  int(2) NOT NULL DEFAULT 0 COMMENT '级别：0=info 1=warn 2=danger',
         f_detail  varchar(512) DEFAULT NULL COMMENT '操作详情',
         f_create_time  datetime NOT NULL COMMENT '操作发生时间',
         PRIMARY KEY ( f_id ),
         KEY  idx_enterprise  ( f_enterprise_id ),
         KEY  idx_action  ( f_action ),
         KEY  idx_create_time  ( f_create_time )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//用户权限覆盖表（13个权限项及角色默认值固定为代码常量，本表只存逐用户覆盖）
CREATE TABLE IF NOT EXISTS  t_user_permission  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_user_id  bigint(20) NOT NULL COMMENT '用户ID',
         f_perm_key  varchar(64) NOT NULL COMMENT '权限项键名，如 msg.external、data.export_org',
         f_granted  int(2) NOT NULL DEFAULT 0 COMMENT '是否授予：0=禁止 1=允许（覆盖角色默认值）',
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_user_perm  ( f_user_id, f_perm_key )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//员工操作历史表（IM客户端操作流水，供控制台"操作历史"时间线展示）
CREATE TABLE IF NOT EXISTS  t_user_activity_log  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_user_id  bigint(20) NOT NULL COMMENT '用户ID',
         f_action  varchar(64) NOT NULL COMMENT '操作类型，如 file.upload / msg.recall / login.success',
         f_detail  varchar(512) DEFAULT NULL COMMENT '操作详情',
         f_severity  int(2) NOT NULL DEFAULT 0 COMMENT '级别：0=info 1=warn 2=danger',
         f_create_time  datetime NOT NULL COMMENT '操作发生时间',
         PRIMARY KEY ( f_id ),
         KEY  idx_user  ( f_user_id ),
         KEY  idx_create_time  ( f_create_time )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//平台公告表
CREATE TABLE IF NOT EXISTS  t_announcement  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_announcement_id  bigint(20) NOT NULL COMMENT '公告业务ID',
         f_title  varchar(256) NOT NULL COMMENT '公告标题',
         f_body  text DEFAULT NULL COMMENT '公告正文',
         f_severity  int(2) NOT NULL DEFAULT 0 COMMENT '级别：0=普通 1=提醒 2=警告',
         f_sender_admin_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '发布者管理员ID',
         f_sender_name  varchar(64) DEFAULT NULL COMMENT '发布者名称（冗余，如平台运营、安全团队）',
         f_audience_type  int(2) NOT NULL DEFAULT 0 COMMENT '受众类型：0=全部企业 1=指定企业 2=指定部门 3=指定员工 4=平台内部',
         f_pinned  int(2) NOT NULL DEFAULT 0 COMMENT '是否置顶：0=否 1=是',
         f_scheduled  int(2) NOT NULL DEFAULT 0 COMMENT '是否定时发布：0=否 1=是',
         f_scheduled_time  datetime DEFAULT NULL COMMENT '定时发布时间（f_scheduled=1时有效）',
         f_sent_time  datetime DEFAULT NULL COMMENT '实际发布时间',
         f_status  int(2) NOT NULL DEFAULT 0 COMMENT '状态：0=草稿 1=已发布 2=已撤回',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_announcement_id  ( f_announcement_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//公告投放目标表（受众为指定企业/部门/员工时，一条公告对应多个目标）
CREATE TABLE IF NOT EXISTS  t_announcement_target  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_announcement_id  bigint(20) NOT NULL COMMENT '公告ID',
         f_target_type  int(2) NOT NULL COMMENT '目标类型：1=企业 2=部门 3=员工',
         f_target_id  bigint(20) NOT NULL COMMENT '目标ID（企业ID/部门ID/用户ID）',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         PRIMARY KEY ( f_id ),
         KEY  idx_announcement  ( f_announcement_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//公告已读回执表（一个用户读过某公告即一条记录）
CREATE TABLE IF NOT EXISTS  t_announcement_read  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_announcement_id  bigint(20) NOT NULL COMMENT '公告ID',
         f_user_id  bigint(20) NOT NULL COMMENT '已读用户ID',
         f_read_time  datetime NOT NULL COMMENT '已读时间',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_announcement_user  ( f_announcement_id, f_user_id ),
         KEY  idx_announcement  ( f_announcement_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//用户登录记录表
CREATE TABLE IF NOT EXISTS  t_login_record  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_user_id  bigint(20) NOT NULL COMMENT '登录的用户ID',
         f_login_time  datetime NOT NULL COMMENT '登录时间',
         f_device  varchar(128) DEFAULT NULL COMMENT '设备描述',
         f_device_kind  int(2) NOT NULL DEFAULT 0 COMMENT '设备类型：1=PC 2=Android 3=iOS 4=Web 5=Mac',
         f_os  varchar(64) DEFAULT NULL COMMENT '操作系统',
         f_ip  varchar(64) DEFAULT NULL COMMENT '登录IP',
         f_location  varchar(128) DEFAULT NULL COMMENT '登录地理位置',
         f_result  int(2) NOT NULL DEFAULT 0 COMMENT '结果：0=成功 1=失败',
         f_fail_reason  varchar(128) DEFAULT NULL COMMENT '失败原因（f_result=1时）',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         PRIMARY KEY ( f_id ),
         KEY  idx_user  ( f_user_id ),
         KEY  idx_login_time  ( f_login_time )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//服务号表
CREATE TABLE IF NOT EXISTS  t_service_account  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_service_id  bigint(20) NOT NULL COMMENT '服务号业务ID',
         f_enterprise_id  bigint(20) NOT NULL COMMENT '所属企业ID',
         f_name  varchar(64) NOT NULL COMMENT '服务号名称',
         f_kind  int(2) NOT NULL DEFAULT 0 COMMENT '类型：0=通知型（单向推送）1=助手型（员工可回复）',
         f_description  varchar(256) DEFAULT NULL COMMENT '服务号描述',
         f_avatar_color  varchar(16) DEFAULT NULL COMMENT '头像底色',
         f_avatar_glyph  varchar(16) DEFAULT NULL COMMENT '头像字形/图标',
         f_owner_admin_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '所有者管理员ID',
         f_allow_unsubscribe  int(2) NOT NULL DEFAULT 1 COMMENT '是否允许员工取消订阅：0=否 1=是',
         f_status  int(2) NOT NULL DEFAULT 0 COMMENT '状态：0=正常 1=停用',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_service_id  ( f_service_id ),
         KEY  idx_enterprise  ( f_enterprise_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//服务号绑定表（服务号可见范围：本企业全员/部门/员工）
CREATE TABLE IF NOT EXISTS  t_service_account_binding  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_service_id  bigint(20) NOT NULL COMMENT '服务号ID',
         f_bind_type  int(2) NOT NULL COMMENT '绑定类型：0=本企业全员 1=部门 2=员工',
         f_bind_target_id  bigint(20) NOT NULL DEFAULT 0 COMMENT '绑定目标ID（部门ID或用户ID；全员时为0）',
         f_create_time  datetime NOT NULL COMMENT '创建时间',
         PRIMARY KEY ( f_id ),
         KEY  idx_service  ( f_service_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//加好友申请表 t_friend_request 已于企业版删除（2026-08-05），随好友关系子系统一并移除。
//表名不要复用，存量库里的残表可手工 DROP。

//联系人备注名表（企业版）：一行 = f_userid 给 f_target_userid 起的备注名。
//备注名是<单向>的（只影响"本人看对方"的显示，不影响对方），故主键取 (f_userid, f_target_userid)。
//本表取代原 t_user_relationship 的 f_userX_markname 两列：企业版通讯录来自组织架构
//（t_department_member），同事之间并无好友关系，备注名必须能独立于好友关系存在。
//清除备注直接删行，不留空串行，故表里的每一行都是"确实设过备注"。
CREATE TABLE IF NOT EXISTS  t_contact_remark  (
         f_userid         bigint(20) NOT NULL COMMENT '备注名归属用户userid',
         f_target_userid  bigint(20) NOT NULL COMMENT '被备注的联系人userid',
         f_remark         varchar(32) NOT NULL DEFAULT '' COMMENT '备注名',
         f_update_time    bigint(20) NOT NULL DEFAULT 0 COMMENT '最近一次修改时间(Unix毫秒)',
         PRIMARY KEY ( f_userid, f_target_userid ),
         KEY  idx_user  ( f_userid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//联系人标签表（企业版）：一行 = f_userid 给 f_target_userid 贴的一个标签（形如"设计""外包"）。
//与 t_contact_remark 完全同构，同样是<单向>的 —— 只影响"本人看对方"这一侧的显示，
//对方看不到也不受影响，故主键的第一列是 f_userid。
//一行只存<一个>标签，不把多个标签打包成逗号串：打包串既没法按标签检索，
//也会让"同一个标签重复添加"要靠字符串比对去重。
//客户端一次提交某联系人的完整标签列表，服务端整份覆盖（先删后插），空列表即清空。
CREATE TABLE IF NOT EXISTS  t_contact_tag  (
         f_userid         bigint(20) NOT NULL COMMENT '标签归属用户userid（谁贴的）',
         f_target_userid  bigint(20) NOT NULL COMMENT '被贴标签的联系人userid（贴给谁）',
         f_tag            varchar(16) NOT NULL DEFAULT '' COMMENT '标签文本，去掉首尾空白后的内容',
         f_order          int(10) NOT NULL DEFAULT 0 COMMENT '同一联系人下的显示顺序，值小者靠前，从0起算',
         f_update_time    bigint(20) NOT NULL DEFAULT 0 COMMENT '最近一次整份覆盖的时间(Unix毫秒)',
         PRIMARY KEY ( f_userid, f_target_userid, f_tag ),
         KEY  idx_user  ( f_userid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//黑名单表：一行表示 f_userid 把 f_blocked_userid 加入了自己的黑名单。
//一行表示 f_userid 把 f_blocked_userid 加入了自己的黑名单；联合主键天然去重
CREATE TABLE IF NOT EXISTS  t_blacklist  (
         f_userid          bigint(20) NOT NULL COMMENT '拉黑发起方userid',
         f_blocked_userid  bigint(20) NOT NULL COMMENT '被拉黑方userid',
         f_create_time     timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '拉黑时间',
         PRIMARY KEY ( f_userid, f_blocked_userid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//会话状态表（v2 会话与消息功能）：存"每用户每会话"的可变状态——置顶/免打扰/
//已读位点/列表删除标记。会话列表本身不存表，由 t_chatmsg 派生；本表仅承载状态。
CREATE TABLE IF NOT EXISTS  t_conversation  (
         f_userid       bigint(20) NOT NULL COMMENT '会话归属用户userid',
         f_ctype        int(2)     NOT NULL DEFAULT 1 COMMENT '会话类型:1单聊 2群聊',
         f_cid          bigint(20) NOT NULL COMMENT '会话ID:单聊为对端userid,群聊为群id',
         f_pinned       int(2)     NOT NULL DEFAULT 0 COMMENT '是否置顶:0否 1是',
         f_mute         int(2)     NOT NULL DEFAULT 0 COMMENT '是否免打扰:0否 1是',
         f_readmsgid    bigint(20) NOT NULL DEFAULT 0 COMMENT '已读到的最大消息msgid',
         f_deleted      int(2)     NOT NULL DEFAULT 0 COMMENT '会话是否从列表删除:0否 1是',
         f_deleted_msgid bigint(20) NOT NULL DEFAULT 0 COMMENT '删除会话时该会话的最后消息msgid锚点:只有最后消息msgid<=本值时才隐藏,来了更新的消息会话即重现',
         f_update_time  bigint(20) NOT NULL DEFAULT 0 COMMENT '本行更新时间,Unix毫秒',
         PRIMARY KEY ( f_userid, f_ctype, f_cid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//本端消息删除表（v2 会话与消息功能）：一行表示某用户从自己视角删除了某条消息。
//消息本体仍在 t_chatmsg、对端不受影响；sync_messages/search 查询按本表过滤。
CREATE TABLE IF NOT EXISTS  t_message_deleted  (
         f_userid  bigint(20) NOT NULL COMMENT '执行删除的用户userid',
         f_msgid   bigint(20) NOT NULL COMMENT '被该用户删除的消息msgid',
         PRIMARY KEY ( f_userid, f_msgid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//按时间范围清理聊天记录的区间表（cmd_delete_messages_by_time 1521）：一行表示某用户清理了
//某段时间内自己的全部单聊与群聊消息。与 t_message_deleted 是同一种"本人视角软删除"，区别是
//只记区间、不逐条展开，清理一次的写入量与消息条数无关（逐条展开时一次可能写入几十万行，会长时间
//占住网络 I/O 线程）。读取时按 t_chatmsg.f_create_time 落在任一区间内过滤，见
//chatserversrc/MessageDeleteRange.h 与 docs/清理聊天记录方案.md §5。
//同一用户的区间在写入时已合并为互不相交、也不首尾相接的若干段，所以每个用户只有寥寥几行。
//列名刻意不用 f_create_time：读取过滤是相关子查询，与 t_chatmsg.f_create_time 同名容易混淆。
CREATE TABLE IF NOT EXISTS  t_message_deleted_range  (
         f_userid       bigint(20) NOT NULL COMMENT '执行清理的用户userid',
         f_begin_time   bigint(20) NOT NULL COMMENT '区间开始,Unix毫秒(含)',
         f_end_time     bigint(20) NOT NULL COMMENT '区间结束,Unix毫秒(含)',
         f_update_time  bigint(20) NOT NULL DEFAULT 0 COMMENT '本行写入时间,Unix毫秒,供排查',
         PRIMARY KEY ( f_userid, f_begin_time )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//文件收取状态表：一行表示某条文件消息被接收方收到了什么程度（正在接收/已接收/已拒绝）。
//接收方经 cmd_file_transfer_ack 上报，服务端落库并转推发送方；发送方离线时不推，
//等其下次 cmd_sync_messages 拉历史时随消息带出，故此状态必须持久化而非仅内存转发。
//独立成表而不往 t_chatmsg 加列：这是只有文件消息才有的附加状态，与 t_message_deleted 同思路。
//主键取 f_msgid —— 当前只有单聊文件消息，一条消息唯一对应一个接收方；日后群文件消息
//若也要此状态，须把接收方 userid 一并纳入主键。
CREATE TABLE IF NOT EXISTS  t_file_ack  (
         f_msgid       bigint(20) NOT NULL COMMENT '文件消息的msgid',
         f_state       int(2)     NOT NULL DEFAULT 0 COMMENT '收取状态:1正在接收 2已接收 3已拒绝',
         f_update_time bigint(20) NOT NULL DEFAULT 0 COMMENT '状态更新时间,Unix毫秒',
         PRIMARY KEY ( f_msgid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//消息Pin表（v2 消息 Pin 功能）：一行表示某条消息被钉在它所属的会话里。Pin 是<u>会话内共享</u>
//的状态——单聊里对方也看得到，群里全体成员都看得到，故本表不含"归属用户"这一维。
//主键取 f_msgid：msgid 全局唯一，且经 t_chatmsg 的 f_ctype/f_from/f_to 就能定位它属于哪个
//会话，本表不必再存一份会话键。单聊的 cid 是"各人视角各不同"的（我看到的 cid 是对方、对方
//看到的 cid 是我），存进这张共享表反而两边对不齐。
//独立成表而不往 t_chatmsg 加列：这是只有极少数消息才有的附加状态，与 t_message_deleted、
//t_file_ack 同思路。取消 Pin 走 DELETE，不留软删标记。
//f_operator 不只是留给日后显示"某某钉了这条消息"——取消 Pin 的权限判断要用它：群里只有
//钉它的人本人与群主/管理员能取消，判断依据就是本列。
CREATE TABLE IF NOT EXISTS  t_message_pin  (
         f_msgid     bigint(20) NOT NULL COMMENT '被Pin的消息msgid',
         f_operator  bigint(20) NOT NULL DEFAULT 0 COMMENT '执行Pin的用户userid,取消Pin的权限判断依据',
         f_pin_time  bigint(20) NOT NULL DEFAULT 0 COMMENT 'Pin时间,Unix毫秒;重复Pin不刷新本列',
         PRIMARY KEY ( f_msgid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//收藏表（v2「我的收藏」功能）：一行表示某用户把某个联系人或某个群加进了自己的收藏。
//键的形态与 t_conversation 一致（userid + ctype + cid），一套键同时表达联系人与群；
//之所以不复用 t_conversation：那张表的一行代表"有过会话"，仅为标记收藏就往里插行，
//会让一个从没聊过的同事凭空冒出一条会话记录。
//注意与 1901~1903 的"收藏消息"是两回事，两者数据互不相干。
CREATE TABLE IF NOT EXISTS  t_favorite  (
         f_userid       bigint(20) NOT NULL COMMENT '收藏归属用户userid',
         f_ctype        int(2)     NOT NULL COMMENT '收藏对象类型:1联系人 2群组',
         f_cid          bigint(20) NOT NULL COMMENT '收藏对象ID:联系人为对端userid,群组为群id',
         f_order        int(11)    NOT NULL DEFAULT 0 COMMENT '手动排序权重,值小者靠前;0表示未手动排序(本期恒为0,留给日后拖拽排序)',
         f_create_time  bigint(20) NOT NULL DEFAULT 0 COMMENT '收藏时间,Unix毫秒;列表按本列倒序,最近收藏的排最前',
         PRIMARY KEY ( f_userid, f_ctype, f_cid ),
         INDEX idx_favorite_user ( f_userid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群组表（v2 群组功能）：群本体彻底独立于 t_user。群 id 仍在高位 id 空间
//（>=GROUPID_BOUBDARY），与消息 cid 共用同一 id 空间。
//特别关注表：记录"某用户特别关注了哪些联系人"。被关注者上下线时，关注方的客户端
//会在桌面右下角弹一个置顶小窗提示（见 docs/特别关注接入方案.md）。
//
//键的形态与 t_favorite 一致（归属者 + 对象），但<u>不带 ctype</u> —— 特别关注只对
//联系人有效，群与群成员都不参与，多一列 ctype 只会让人误以为群也能标。
//
//之所以不复用 t_favorite 加一列：收藏与特别关注是两件事（常用入口的书签 vs 要盯
//上下线的人），一个对象可以只收藏不关注、也可以只关注不收藏；挤进一张表两个语义
//会互相牵制，删收藏时还得小心别把关注一并删掉。
//
//注意：服务端不需要知道"谁关注了谁"来决定推送 —— 上下线仍按企业全员广播
//（见 ChatServer::broadcastToEnterprise），弹不弹由客户端拿本表的名单自己判断。
CREATE TABLE IF NOT EXISTS  t_starred  (
         f_userid       bigint(20) NOT NULL COMMENT '关注方userid(名单的归属者)',
         f_target       bigint(20) NOT NULL COMMENT '被关注的联系人userid',
         f_create_time  bigint(20) NOT NULL DEFAULT 0 COMMENT '标记时间,Unix毫秒;列表按本列倒序,最近标记的排最前',
         PRIMARY KEY ( f_userid, f_target ),
         INDEX idx_starred_user ( f_userid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群表。f_name_custom 区分两种群名：1 = 用户设定的群名，此后固定不变；0 = 自动群名
//（建群时没填名字、或群主把群名清空），由服务端按当前成员昵称生成，成员增减、转让
//群主、成员改昵称后都会重算，并经 cmd_group_info_changed_notify 推给全群。
//默认值取 1：老库升级补列时，已有的群一律按"用户设定"处理，不会被悄悄改名；其中
//当年由客户端自动拼出来的群名，另由 migrations/v7_mark_auto_group_names.py 识别后改标记。
CREATE TABLE IF NOT EXISTS  t_group  (
         f_groupid       bigint(20)    NOT NULL COMMENT '群ID(高位id空间,>=GROUPID_BOUBDARY)',
         f_name          varchar(64)   NOT NULL COMMENT '群名称',
         f_facetype      int(4)        NOT NULL DEFAULT 0 COMMENT '群头像类型',
         f_customface    varchar(256)  NOT NULL DEFAULT '' COMMENT '群自定义头像文件标识',
         f_announcement  varchar(1024) NOT NULL DEFAULT '' COMMENT '群公告',
         f_ownerid       bigint(20)    NOT NULL COMMENT '群主userid',
         f_join_mode     int(2)        NOT NULL DEFAULT 1 COMMENT '加群方式:0自由 1需审批 2禁止',
         f_mute_all      int(2)        NOT NULL DEFAULT 0 COMMENT '全员禁言:0否 1是',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)',
         f_intro         varchar(512)  NOT NULL DEFAULT '' COMMENT '群简介',
         f_at_all_scope  int(2)        NOT NULL DEFAULT 0 COMMENT '谁能@全体:0仅群主和管理员 1所有人',
         f_new_see_history int(2)      NOT NULL DEFAULT 1 COMMENT '新成员可看入群前历史消息:0否 1是',
         f_member_invite int(2)        NOT NULL DEFAULT 0 COMMENT '普通成员邀请好友免审批:0否(转待审批) 1是(直接入群)',
         f_welcome_on    int(2)        NOT NULL DEFAULT 0 COMMENT '入群欢迎语开关:0关 1开',
         f_welcome_text  varchar(256)  NOT NULL DEFAULT '' COMMENT '入群欢迎语正文',
         f_sensitive_on  int(2)        NOT NULL DEFAULT 0 COMMENT '消息敏感词过滤开关:0关 1开',
         f_announce_id   bigint(20)    NOT NULL DEFAULT 0 COMMENT '最新已发布公告的f_id(t_group_announcement);0=本群还没有公告',
         f_announce_title varchar(128) NOT NULL DEFAULT '' COMMENT '最新已发布公告的标题(冗余快照,群聊窗头部摘要行直接读它)',
         f_announce_time bigint(20)    NOT NULL DEFAULT 0 COMMENT '最新已发布公告的发布时间(Unix毫秒)',
         f_name_custom   int(2)        NOT NULL DEFAULT 1 COMMENT '群名是否由用户设定:1是(固定不变) 0否(自动群名,成员或昵称变化时由服务端重算)',
         PRIMARY KEY ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群公告表（v2 群公告功能）：一行 = 某群的一条公告。
//删除是<u>真删</u>（DELETE FROM），不做软删 —— 群主 / 管理员点了删除就是要它彻底
//消失，留一行"已删除"的记录在库里既没人看、也让"共 N 条"的口径变得含糊。
//t_group 上的 f_announce_id / f_announce_title / f_announce_time / f_announcement
//四列是本表"最新一条公告"的冗余快照，由服务端在发布 / 删除后同步刷新 —— 这样
//只读展示（群资料对话框、cmd_get_group_info 应答、群资料变更推送）一处都不用改
//就能显示最新公告。
CREATE TABLE IF NOT EXISTS  t_group_announcement  (
         f_id           bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '公告ID',
         f_groupid      bigint(20)    NOT NULL COMMENT '所属群ID',
         f_title        varchar(128)  NOT NULL DEFAULT '' COMMENT '公告标题(必填)',
         f_content      varchar(2048) NOT NULL DEFAULT '' COMMENT '公告正文',
         f_severity     int(2)        NOT NULL DEFAULT 0 COMMENT '优先级:0通知 1重要 2紧急',
         f_publisher    bigint(20)    NOT NULL DEFAULT 0 COMMENT '发布人userid',
         f_publish_time bigint(20)    NOT NULL DEFAULT 0 COMMENT '发布时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_group_time  ( f_groupid, f_publish_time )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群成员表（v2 群组功能）：一行表示某用户在某群的成员身份，含角色/群名片/禁言。
//主键 (f_groupid,f_userid) 服务"群→成员列表"；idx_user 服务"用户→群列表"。
CREATE TABLE IF NOT EXISTS  t_group_member  (
         f_groupid    bigint(20)  NOT NULL COMMENT '群ID',
         f_userid     bigint(20)  NOT NULL COMMENT '成员userid',
         f_role       int(2)      NOT NULL DEFAULT 0 COMMENT '角色:0成员 1管理员 2群主',
         f_card       varchar(64) NOT NULL DEFAULT '' COMMENT '群名片;空则用账号昵称',
         f_muted      int(2)      NOT NULL DEFAULT 0 COMMENT '是否被禁言:0否 1是',
         f_join_time  bigint(20)  NOT NULL DEFAULT 0 COMMENT '入群时间(Unix毫秒)',
         f_mute_until bigint(20)  NOT NULL DEFAULT 0 COMMENT '禁言到期时间(Unix毫秒);f_muted=1且本列为0表示永久禁言',
         f_mute_reason varchar(128) NOT NULL DEFAULT '' COMMENT '禁言原因',
         PRIMARY KEY ( f_groupid, f_userid ),
         KEY  idx_user  ( f_userid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//加群申请表（v2 群组功能）：群 join_mode=需审批 时，申请会落此表等待群主/管理员处理。
//时间列用 bigint 存 Unix 毫秒，与协议时间戳一致。
CREATE TABLE IF NOT EXISTS  t_group_join_request  (
         f_id           bigint(20)   NOT NULL AUTO_INCREMENT COMMENT '申请ID',
         f_groupid      bigint(20)   NOT NULL COMMENT '目标群ID',
         f_userid       bigint(20)   NOT NULL COMMENT '申请人userid',
         f_verify_msg   varchar(256) DEFAULT NULL COMMENT '验证留言',
         f_status       int(2)       NOT NULL DEFAULT 0 COMMENT '状态:0待处理 1已通过 2已拒绝',
         f_create_time  bigint(20)   NOT NULL DEFAULT 0 COMMENT '申请时间(Unix毫秒)',
         f_handle_time  bigint(20)   NOT NULL DEFAULT 0 COMMENT '处理时间(Unix毫秒,0未处理)',
         f_handler      bigint(20)   NOT NULL DEFAULT 0 COMMENT '处理人userid(0未处理)',
         f_source       int(2)       NOT NULL DEFAULT 1 COMMENT '申请来源:0好友邀请 1群搜索 2扫码加群',
         f_recommender  bigint(20)   NOT NULL DEFAULT 0 COMMENT '推荐人userid(仅好友邀请时非0)',
         PRIMARY KEY ( f_id ),
         KEY  idx_group_status  ( f_groupid, f_status )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群敏感词库表（v2 群管理功能）：一行 = 某群的一条敏感词。群开启 f_sensitive_on
//后，群内文本消息发送前按本表做子串匹配，命中即拒发。词库按群隔离、量小，
//服务端按群缓存在内存里，随增删失效重载。
CREATE TABLE IF NOT EXISTS  t_group_sensitive_word  (
         f_id           bigint(20)  NOT NULL AUTO_INCREMENT COMMENT '词条ID',
         f_groupid      bigint(20)  NOT NULL COMMENT '所属群ID',
         f_word         varchar(64) NOT NULL COMMENT '敏感词',
         f_create_time  bigint(20)  NOT NULL DEFAULT 0 COMMENT '添加时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_group  ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群互动卡片互动表（v2 群互动卡片功能）：一行 = 某用户对某卡片(投票/接龙/待办)的一次互动。
//卡片本体是一条 msgtype=10/11/12 的消息；本表只存互动，每人每卡片至多一行。
CREATE TABLE IF NOT EXISTS  t_card_interaction  (
         f_id           bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_msgid        bigint(20)    NOT NULL COMMENT '卡片所在消息id',
         f_userid       bigint(20)    NOT NULL COMMENT '互动用户userid',
         f_data         varchar(1024) NOT NULL DEFAULT '' COMMENT '互动数据:投票为选项下标JSON数组/接龙为接龙文本/待办为勾选标记',
         f_create_time  bigint(20)    NOT NULL DEFAULT 0 COMMENT '互动时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_msg_user  ( f_msgid, f_userid ),
         KEY  idx_msgid  ( f_msgid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//文件元数据表（v2 文件/图片服务功能）：一行 = 一个上传文件的元信息。
//f_id 即对外的 fileid；磁盘文件以 f_md5 命名，故相同内容自动去重。
//f_status=1(已完成) 的行可被秒传命中；客户端首次引用 fileid 时惰性置 1。
CREATE TABLE IF NOT EXISTS  t_file  (
         f_id           bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '文件记录ID,即对外fileid',
         f_md5          char(32)      NOT NULL DEFAULT '' COMMENT '文件内容MD5(小写十六进制),秒传与磁盘文件名依据',
         f_name         varchar(256)  NOT NULL DEFAULT '' COMMENT '上传时的原始文件名',
         f_ext          varchar(32)   NOT NULL DEFAULT '' COMMENT '扩展名(不含点)',
         f_size         bigint(20)    NOT NULL DEFAULT 0 COMMENT '文件总字节数',
         f_scene        int(4)        NOT NULL DEFAULT 0 COMMENT '用途场景:1聊天文件 2群文件 3头像 4图片消息',
         f_uploaderid   bigint(20)    NOT NULL DEFAULT 0 COMMENT '上传者用户userid',
         f_status       int(4)        NOT NULL DEFAULT 0 COMMENT '状态:0待上传 1已完成',
         f_createtime   bigint(20)    NOT NULL DEFAULT 0 COMMENT '记录创建时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_md5  ( f_md5 ),
         KEY  idx_uploader  ( f_uploaderid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群文件登记表（v2 群文件功能）：一行 = 某文件被登记进某群的文件库。
//文件元数据（名称/扩展名/大小/MD5）在 t_file，本表只存"群↔文件"登记关系，
//故删除群文件只删本表行、不动 t_file 与磁盘文件（内容去重/秒传/他群可能引用）。
//f_id 即对外 entryid；idx_group 服务"群→文件列表"分页查询；
//idx_fileid 服务下载鉴权（按 fileid 反查所属群，校验调用者是否群成员）。
CREATE TABLE IF NOT EXISTS  t_group_file  (
         f_id           bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '群文件条目ID,即对外entryid',
         f_groupid      bigint(20)    NOT NULL COMMENT '所属群ID',
         f_fileid       bigint(20)    NOT NULL COMMENT '文件ID,引用t_file.f_id',
         f_filetype     int(4)        NOT NULL DEFAULT 6 COMMENT '文件类别:1文档 2图片 3视频 4音频 5压缩包 6其它',
         f_uploaderid   bigint(20)    NOT NULL DEFAULT 0 COMMENT '登记者(上传者)userid',
         f_uploadtime   bigint(20)    NOT NULL DEFAULT 0 COMMENT '登记时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_group  ( f_groupid ),
         KEY  idx_fileid  ( f_fileid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//客户端版本发布记录表（v2 自动更新功能）：一行 = 某平台的一次版本发布。
//取数规则为“按 f_platform 过滤，在 f_enabled=1 的记录里取 f_publish_time 最大的一条”，
//故 idx_platform_enabled 覆盖这两列；历史发布记录保留不删，便于回溯与回滚到旧版本
//（回滚只需把旧版本那行重新发布一次，不必改动已有数据）。
//f_package_md5 既是升级包的内容校验值，也是它在 fileserver filecache 目录下的文件名。
CREATE TABLE IF NOT EXISTS  t_client_version  (
         f_id           int(11)       NOT NULL AUTO_INCREMENT COMMENT '自增主键',
         f_platform     int(4)        NOT NULL DEFAULT 1 COMMENT '客户端平台:1 Windows PC 2 Android(预留)',
         f_version      varchar(32)   NOT NULL DEFAULT '' COMMENT '本条记录发布的版本号,形如3.3.0',
         f_min_version  varchar(32)   NOT NULL DEFAULT '' COMMENT '允许登录的最低版本号,低于它的客户端被拒绝登录并强制升级',
         f_release_note varchar(2048) NOT NULL DEFAULT '' COMMENT '更新说明,展示在升级窗里',
         f_package_md5  varchar(32)   NOT NULL DEFAULT '' COMMENT '升级包内容MD5(小写十六进制32位),fileserver在升级包目录里按它反查实际文件',
         f_package_size bigint(20)    NOT NULL DEFAULT 0 COMMENT '升级包字节数',
         f_package_name varchar(255)  NOT NULL DEFAULT '' COMMENT '升级包原始文件名,仅用于界面显示与本地落地命名',
         f_forced       int(4)        NOT NULL DEFAULT 0 COMMENT '是否强制升级:0否 1是。为1时客户端升级窗不给稍后出口、也不放行登录;与f_min_version相互独立,任一命中即按强制处理',
         f_enabled      int(4)        NOT NULL DEFAULT 0 COMMENT '是否已发布:0草稿不下发 1已发布',
         f_publish_time bigint(20)    NOT NULL DEFAULT 0 COMMENT '发布时间(Unix毫秒)',
         f_creator_adminid int(11)     NOT NULL DEFAULT 0 COMMENT '发布人的管理员ID,引用t_admin.f_admin_id;0表示非控制台创建',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '记录创建时间(Unix毫秒),f_publish_time回滚时会被刷新而本列不变',
         PRIMARY KEY ( f_id ),
         KEY  idx_platform_enabled  ( f_platform, f_enabled )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8
