//用户信息表
CREATE TABLE IF NOT EXISTS  t_user  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
		 f_user_id  bigint(20) NOT NULL COMMENT '用户ID',
		 f_username  varchar(64) NOT NULL COMMENT '用户名',
         f_nickname  varchar(64) NOT NULL COMMENT '用户昵称',
         f_password  varchar(64) NOT NULL COMMENT '登录密码：v2存 MD5(SHA256(明文)+f_salt)',
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
         f_post  varchar(64) DEFAULT NULL COMMENT '岗位：本人填写的自由文本，与部门职位（t_department_member.f_position）无关',
         f_phone_visible  int(2) NOT NULL DEFAULT 0 COMMENT '手机号是否对同事公开：0=仅本人可见 1=对同事可见',
         f_landline_visible  int(2) NOT NULL DEFAULT 1 COMMENT '座机是否对同事公开：0=仅本人可见 1=对同事可见',
         f_workstation  varchar(256) DEFAULT NULL COMMENT '工位地址',
         f_status  int(2) NOT NULL DEFAULT 0 COMMENT '账号状态：0=启用 1=停用',
         f_usertype  int(2) NOT NULL DEFAULT 0 COMMENT '用户类型：0=普通用户 1=服务号用户 2=机器人',
         PRIMARY KEY ( f_user_id ),
         UNIQUE KEY uk_f_username (f_username),
         UNIQUE KEY uk_ent_employee_no (f_enterprise_id, f_employee_no),
         INDEX idx_enterprise (f_enterprise_id),
		 INDEX f_user_id (f_user_id),
         KEY  f_id  ( f_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//消息记录表：结构化列承载可查询/可更新字段；content/mentions/mentionall/quote 打包进 f_content blob
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
         f_audience     int(2)      NOT NULL DEFAULT 0 COMMENT '可见范围:0全员可见 1仅发送者与t_message_audience里登记的人可见(如只给部分群成员看的文件共享卡片)',
         f_recalled_by  bigint(20)  NOT NULL DEFAULT 0 COMMENT '撤回者userid:0表示未撤回,或撤回发生在2026-10-03之前没有记录(此时按发送方处理)。群主与管理员可撤回别人的群消息,所以不一定等于f_from',
         //f_search_text：2026-10-04 加入。f_content 是 JSON，中文被转义成 \uXXXX，
         //对它做 LIKE 搜不到中文；入库与编辑时提取一份纯文本存在这里（MessageSearchText.h），
         //会话内搜索与全局搜索都查这一列。NULL 表示还没提取（存量消息，启动后分批回填）。
         f_search_text  text        COMMENT '搜索用纯文本(文字正文/文件名/卡片标题等,UTF-8,去掉四字节字符,最长4000字节);NULL表示尚未提取',
         //f_rev：2026-10-08 加入（消息改写离线同步，docs/消息改写离线同步方案.md）。服务端改写已发出的消息
         //（公告卡片被删、通话结束、屏幕共享改标题、帖子被编辑或删除）与撤回时，取一个全局递增的新序号
         //写进来；客户端登录后用 cmd_sync_rewritten_messages 按序号拉取离线期间被改写过的消息。
         f_rev          bigint(20)  NOT NULL DEFAULT 0 COMMENT '改写序号:0表示从未被改写;每次改写内容或撤回时取全局递增的新值',
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
         KEY  idx_to_msgid ( f_to, f_msgid ),
         //按改写序号取「某序号之后被改写过的消息」用（cmd_sync_rewritten_messages）。绝大多数消息的
         //f_rev 为 0，范围查询只会落在少数被改写过的行上。
         KEY  idx_rev ( f_rev )
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
//企业租户表
CREATE TABLE IF NOT EXISTS  t_enterprise  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_enterprise_id  bigint(20) NOT NULL COMMENT '企业业务ID',
         f_name  varchar(128) NOT NULL COMMENT '企业全称',
         f_short_name  varchar(64) DEFAULT NULL COMMENT '企业简称',
         f_industry  varchar(64) DEFAULT NULL COMMENT '所属行业',
         f_region  varchar(64) DEFAULT NULL COMMENT '所在地区',
         f_plan  int(2) NOT NULL DEFAULT 0 COMMENT '套餐：0=基础版 1=专业版 2=旗舰版',
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
         f_sensitive_on  int(2) NOT NULL DEFAULT 1 COMMENT '敏感词过滤总开关：0=暂停 1=启用（词库为空时开关不起作用）',
         f_account_prefix  varchar(16) DEFAULT NULL COMMENT '新建账号的前缀：英文字母与点号、大写保存；为空时用 EMP',
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
         f_kind  int(2) NOT NULL DEFAULT 0 COMMENT '类型：0=通知型（单向推送）1=助手型（员工可回复）2=集成型（对接外部系统）',
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

//会话状态表（v2 会话与消息功能）：存"每用户每会话"的可变状态——置顶/消息提醒模式/
//已读位点/列表删除标记。会话列表本身不存表，由 t_chatmsg 派生；本表仅承载状态。
CREATE TABLE IF NOT EXISTS  t_conversation  (
         f_userid       bigint(20) NOT NULL COMMENT '会话归属用户userid',
         f_ctype        int(2)     NOT NULL DEFAULT 1 COMMENT '会话类型:1单聊 2群聊',
         f_cid          bigint(20) NOT NULL COMMENT '会话ID:单聊为对端userid,群聊为群id',
         f_pinned       int(2)     NOT NULL DEFAULT 0 COMMENT '是否置顶:0否 1是',
         f_notify_mode  int(2)     NOT NULL DEFAULT 0 COMMENT '消息提醒模式:0允许提醒 1接收不提醒 2收进消息盒子 3屏蔽(仅群聊) 4特别关注 5只提醒@我和关注的人(仅群聊);取代原f_mute(0/1含义不变),见docs/消息提醒模式与消息盒子方案.md',
         f_readmsgid    bigint(20) NOT NULL DEFAULT 0 COMMENT '已读到的最大消息msgid',
         f_deleted      int(2)     NOT NULL DEFAULT 0 COMMENT '会话是否从列表删除:0否 1是',
         f_deleted_msgid bigint(20) NOT NULL DEFAULT 0 COMMENT '删除会话时该会话的最后消息msgid锚点:只有最后消息msgid<=本值时才隐藏,来了更新的消息会话即重现',
         f_cleared_msgid bigint(20) NOT NULL DEFAULT 0 COMMENT '本人清空该会话聊天记录时的消息msgid锚点:msgid<=本值的消息对本人隐藏,对方与其他成员不受影响;0表示没有清空过',
         f_unread_mark  int(2)     NOT NULL DEFAULT 0 COMMENT '本人是否手动标为未读:0否 1是;打开会话或已读上报时清零,见docs/会话标为未读方案.md',
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
//群表。f_name_custom 区分两种群名：1 = 用户设定的群名，此后固定不变；0 = 自动群名
//（建群时没填名字、或群主把群名清空），由服务端按当前成员昵称生成，成员增减、转让
//群主、成员改昵称后都会重算，并经 cmd_group_info_changed_notify 推给全群。
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
         f_announce_id  bigint(20)    NOT NULL DEFAULT 0 COMMENT '最新已发布公告的f_id(t_group_announcement);0=本群还没有公告',
         f_announce_title varchar(128) NOT NULL DEFAULT '' COMMENT '最新已发布公告的标题(冗余快照,群聊窗头部摘要行直接读它)',
         f_announce_time bigint(20)    NOT NULL DEFAULT 0 COMMENT '最新已发布公告的发布时间(Unix毫秒)',
         f_name_custom   int(2)        NOT NULL DEFAULT 1 COMMENT '群名是否由用户设定:1是(固定不变) 0否(自动群名,成员或昵称变化时由服务端重算)',
         f_must_read_id  bigint(20)    NOT NULL DEFAULT 0 COMMENT '入群必读公告的f_id(t_group_announcement);0=本群没有入群必读(docs/群公告接入方案.md §14)',
         f_group_type    int(2)        NOT NULL DEFAULT 0 COMMENT '群类型:0普通群 1帖子群(取值见Protocol.h的group_type,docs/帖子群方案.md);建群时确定,之后不变',
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
//f_history_from 是本人能看到的最早一条群消息的 msgid（邀请时选择「同步最近 N 条」得出，0 表示不限），
//拉历史、会话内搜索、全局搜索、Pin 列表都以它为下界（docs/邀请入群方案.md §5.3）。
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
         f_history_from bigint(20) NOT NULL DEFAULT 0 COMMENT '本人可见的最早一条群消息msgid;0不限(入群时按邀请人选择的同步条数算出,群主与管理员不受限)',
         f_must_read  int(2)      NOT NULL DEFAULT 0 COMMENT '入群必读:0不需要或已确认 1待确认(待确认时不能发言,docs/群公告接入方案.md §14)',
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
         f_history_limit int(11)     NOT NULL DEFAULT -1 COMMENT '通过后可看的入群前消息条数:-1全部 0不同步 N最近N条(由邀请转来的申请记邀请人的选择)',
         f_invite_id    bigint(20)   NOT NULL DEFAULT 0 COMMENT '由入群邀请卡片转来时为t_group_invite.f_id,审批结果据此回写卡片;0表示不是',
         PRIMARY KEY ( f_id ),
         KEY  idx_group_status  ( f_groupid, f_status )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//入群邀请表（「邀请入群需被邀请人同意」开启时用，docs/邀请入群方案.md §3.3、§5.1）：一行 = 一张入群邀请卡片，
//卡片本身是邀请人与被邀请人单聊里的一条 message_type_group_invite 消息（f_msgid）。f_state 只记落库的四种状态，
//「已过期」「已失效」由服务端在下发卡片时按到期时间、群是否还在、邀请人是否还在群里算出，不落库。
//群解散时本表的行保留，卡片下发时按群不存在显示为已失效。
CREATE TABLE IF NOT EXISTS  t_group_invite  (
         f_id           bigint(20)   NOT NULL AUTO_INCREMENT COMMENT '邀请ID',
         f_groupid      bigint(20)   NOT NULL COMMENT '目标群ID',
         f_inviter      bigint(20)   NOT NULL COMMENT '邀请人userid',
         f_invitee      bigint(20)   NOT NULL COMMENT '被邀请人userid',
         f_history_limit int(11)     NOT NULL DEFAULT -1 COMMENT '入群后可看的入群前消息条数:-1全部 0不同步 N最近N条',
         f_state        int(2)       NOT NULL DEFAULT 0 COMMENT '状态:0待加入 1已加入 2等待审批 3未通过审批',
         f_msgid        bigint(20)   NOT NULL DEFAULT 0 COMMENT '卡片消息msgid(邀请人与被邀请人的单聊)',
         f_create_time  bigint(20)   NOT NULL DEFAULT 0 COMMENT '发出时间(Unix毫秒)',
         f_expire_time  bigint(20)   NOT NULL DEFAULT 0 COMMENT '到期时间(Unix毫秒),超过后仍待加入的显示为已过期',
         f_handle_time  bigint(20)   NOT NULL DEFAULT 0 COMMENT '状态最后变化的时间(Unix毫秒,0未变化)',
         PRIMARY KEY ( f_id ),
         KEY  idx_invitee_group  ( f_invitee, f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//企业敏感词库：每个企业一份，由企业管理员（或平台超级管理员代为）在 admin 控制台维护；
//单聊与群聊统一按提交者所属企业的词库检查（docs/敏感词方案.md）。f_word_norm 存归一化后的
//形式（统一大小写与全半角、去掉空白符号与表情），判重与构建匹配器都用它。
CREATE TABLE IF NOT EXISTS  t_sensitive_word  (
         f_id            bigint(20)   NOT NULL AUTO_INCREMENT COMMENT '词条ID',
         f_enterprise_id bigint(20)   NOT NULL COMMENT '所属企业ID',
         f_word          varchar(32)  NOT NULL COMMENT '词条原文（去掉首尾空白，最多32个字符）',
         f_word_norm     varchar(32)  NOT NULL COMMENT '归一化后的词条，用于判重与构建匹配器',
         f_category      int(2)       NOT NULL DEFAULT 6 COMMENT '分类：1违法违规 2色情低俗 3攻击辱骂 4商业机密 5广告引流 6其他',
         f_remark        varchar(64)  DEFAULT NULL COMMENT '备注',
         f_creator_id    bigint(20)   NOT NULL DEFAULT 0 COMMENT '添加者管理员ID',
         f_creator_name  varchar(64)  DEFAULT NULL COMMENT '添加者姓名（冗余，便于列表直读）',
         f_create_time   bigint(20)   NOT NULL DEFAULT 0 COMMENT '添加时间(Unix毫秒)',
         f_update_time   bigint(20)   NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uk_ent_norm  ( f_enterprise_id, f_word_norm ),
         KEY  idx_enterprise  ( f_enterprise_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//敏感词拦截记录：每次拦截一行，保留 180 天，由 chatserver 定时清理。名称与内容摘要里的
//4 字节字符（多数 emoji）写库前替换为 U+FFFD，因为 MySQL 的 utf8 列存不下。
CREATE TABLE IF NOT EXISTS  t_sensitive_hit_log  (
         f_id            bigint(20)   NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_enterprise_id bigint(20)   NOT NULL COMMENT '提交者所属企业ID',
         f_user_id       bigint(20)   NOT NULL DEFAULT 0 COMMENT '提交者userid；自助注册时尚无账号，为0',
         f_user_name     varchar(64)  DEFAULT NULL COMMENT '提交者账号或昵称快照',
         f_source        int(2)       NOT NULL COMMENT '来源：见 chatserversrc/SensitiveWordDefs.h 的 sensitive_hit_source',
         f_ctype         int(2)       NOT NULL DEFAULT 0 COMMENT '会话类型：0与会话无关 1单聊 2群聊',
         f_cid           bigint(20)   NOT NULL DEFAULT 0 COMMENT '会话ID：单聊为对方userid，群聊为群ID',
         f_target_name   varchar(64)  DEFAULT NULL COMMENT '会话名称快照（对方姓名或群名）',
         f_word_id       bigint(20)   NOT NULL DEFAULT 0 COMMENT '命中的词条ID（词条可能已被删除）',
         f_word          varchar(32)  NOT NULL COMMENT '命中的词条原文快照',
         f_category      int(2)       NOT NULL DEFAULT 6 COMMENT '命中词条的分类快照',
         f_snippet       varchar(128) DEFAULT NULL COMMENT '内容摘要：命中位置前后各20个字符',
         f_create_time   bigint(20)   NOT NULL COMMENT '拦截时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_ent_time  ( f_enterprise_id, f_create_time ),
         KEY  idx_create_time  ( f_create_time )
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

//群互动卡片结束状态表（群接龙功能，见 docs/群接龙方案.md §5.1）：一行 = 一张被提前结束的卡片。
//没有这一行表示卡片从未被提前结束；截止时间写在卡片内容里，按当前时间换算，不落本表。
//f_msgid 是主键，两人同时结束同一张卡片时只有先写入的那一次生效（INSERT IGNORE）。
CREATE TABLE IF NOT EXISTS  t_card_state  (
         f_msgid        bigint(20)    NOT NULL COMMENT '卡片所在消息id',
         f_closed       int(4)        NOT NULL DEFAULT 0 COMMENT '是否已被提前结束:1是',
         f_closer       bigint(20)    NOT NULL DEFAULT 0 COMMENT '结束操作者userid',
         f_close_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '结束时间(Unix毫秒)',
         PRIMARY KEY ( f_msgid )
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

//消息可见范围表（文件共享功能引入，机制本身与业务无关）：一行 = 某条消息对某个人可见。
//只对 t_chatmsg.f_audience=1 的消息生效：这类消息只有发送者本人与本表登记的人看得到，
//消息同步、未读计数、消息搜索、会话摘要都按此过滤（MessageManager 的 visibleToUserClause）。
//f_audience=0 的普通消息不在本表登记任何行。idx_user 服务「把某人从一批消息的可见范围里移除」。
CREATE TABLE IF NOT EXISTS  t_message_audience  (
         f_msgid        bigint(20)    NOT NULL COMMENT '消息ID,引用t_chatmsg.f_msgid',
         f_user_id      bigint(20)    NOT NULL COMMENT '可以看到这条消息的用户userid',
         PRIMARY KEY ( f_msgid, f_user_id ),
         KEY  idx_user  ( f_user_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//文件共享记录表：一行 = 一个共享（谁、在哪台电脑上、把哪个路径、在哪个会话里共享出来）。
//共享只在被主动取消时失效（f_state 置 2），记录本身不删，供后台查看与审计关联。
//f_shareid 自增且删除后不复用：共享卡片里引用的就是它，复用会让旧卡片指向新的共享。
//uk_owner_client 用于「发起共享」应答超时重发时去重；idx_owner 服务「我的共享」，
//idx_conv 服务按会话列出共享，idx_enterprise 服务后台按企业列出共享。
//方案见 docs/文件共享方案.md 6.3。
CREATE TABLE IF NOT EXISTS  t_file_share  (
         f_shareid        bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '共享ID(全局自增,删除后不复用)',
         f_client_share_id varchar(64)  NOT NULL DEFAULT '' COMMENT '客户端生成的UUID,与f_owner_id组成唯一键,发起共享请求重发时据此去重',
         f_owner_id       bigint(20)    NOT NULL COMMENT '分享者userid',
         f_enterprise_id  bigint(20)    NOT NULL DEFAULT 0 COMMENT '分享者所属企业ID(冗余一份,供后台按企业查询)',
         f_device_id      varchar(64)   NOT NULL DEFAULT '' COMMENT '发起共享时分享者的设备标识;共享只由这台电脑提供',
         f_ctype          int(2)        NOT NULL DEFAULT 1 COMMENT '所在会话类型:1单聊 2群聊',
         f_cid            bigint(20)    NOT NULL DEFAULT 0 COMMENT '所在会话:单聊为对端userid(分享者视角),群聊为群id',
         f_kind           int(2)        NOT NULL DEFAULT 2 COMMENT '共享类型:1磁盘 2文件夹 3单个文件,取值见Protocol.h的share_kind',
         f_name           varchar(260)  NOT NULL DEFAULT '' COMMENT '显示名:共享根的最后一级名字或磁盘卷标',
         f_display_path   varchar(1024) NOT NULL DEFAULT '' COMMENT '分享者发起时的显示路径,仅供显示,客户端不据此提供文件',
         f_include_hidden int(2)        NOT NULL DEFAULT 0 COMMENT '是否包含隐藏文件与系统文件:0否 1是',
         f_state          int(2)        NOT NULL DEFAULT 1 COMMENT '状态:1有效 2已取消',
         f_cancel_reason  int(4)        NOT NULL DEFAULT 0 COMMENT '取消原因,取值见Protocol.h的share_invalid_reason(1~5);未取消为0',
         f_canceled_by    bigint(20)    NOT NULL DEFAULT 0 COMMENT '取消操作人:后台强制取消时为管理员ID,其余为用户userid;未取消为0',
         f_create_time    bigint(20)    NOT NULL DEFAULT 0 COMMENT '发起时间(Unix毫秒)',
         f_cancel_time    bigint(20)    NOT NULL DEFAULT 0 COMMENT '取消时间(Unix毫秒);未取消为0',
         PRIMARY KEY ( f_shareid ),
         UNIQUE KEY  uk_owner_client  ( f_owner_id, f_client_share_id ),
         KEY  idx_owner  ( f_owner_id, f_state ),
         KEY  idx_conv  ( f_ctype, f_cid, f_state ),
         KEY  idx_enterprise  ( f_enterprise_id, f_state )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//文件共享成员表：一行 = 某个共享的一名成员及其写权限。被移出的成员直接删行。
//下载恒允许，不占权限位；单文件共享的权限恒为 0。idx_user 服务「共享给我的」。
CREATE TABLE IF NOT EXISTS  t_file_share_member  (
         f_shareid      bigint(20)    NOT NULL COMMENT '共享ID,引用t_file_share.f_shareid',
         f_user_id      bigint(20)    NOT NULL COMMENT '成员userid',
         f_perms        int(4)        NOT NULL DEFAULT 0 COMMENT '写权限位:1上传 2新建文件夹 4重命名 8删除 16移动,取值见Protocol.h的share_perm',
         f_add_time     bigint(20)    NOT NULL DEFAULT 0 COMMENT '加入时间(Unix毫秒)',
         PRIMARY KEY ( f_shareid, f_user_id ),
         KEY  idx_user  ( f_user_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//文件共享审计表：一行 = 一次被记录的动作。来源分两种（f_source）：服务端亲眼所见的动作
//（打开、申请传输、发起与取消共享、调整成员），以及分享者客户端上报的执行结果（下载、上传
//每个文件，新建、重命名、删除、移动）。后者依赖客户端如实上报，需求方已知悉并接受。
//三个索引分别服务后台按企业 + 时间、按操作人 + 时间、按共享查询。
CREATE TABLE IF NOT EXISTS  t_file_share_audit  (
         f_id           bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '自增主键',
         f_shareid      bigint(20)    NOT NULL DEFAULT 0 COMMENT '共享ID,引用t_file_share.f_shareid',
         f_owner_id     bigint(20)    NOT NULL DEFAULT 0 COMMENT '分享者userid(冗余一份,便于按分享者查询)',
         f_actor_id     bigint(20)    NOT NULL DEFAULT 0 COMMENT '操作人:访问者、分享者的userid,或后台管理员ID',
         f_enterprise_id bigint(20)   NOT NULL DEFAULT 0 COMMENT '分享者所属企业ID,后台按企业查询用',
         f_action       int(4)        NOT NULL DEFAULT 0 COMMENT '动作,取值见Protocol.h的share_audit_action',
         f_source       int(2)        NOT NULL DEFAULT 1 COMMENT '来源:1服务端记录 2分享者客户端上报,取值见share_audit_source',
         f_path         varchar(1024) NOT NULL DEFAULT '' COMMENT '相对共享根的路径(UTF-8,/分隔);申请传输时为条目清单的摘要',
         f_to_path      varchar(1024) NOT NULL DEFAULT '' COMMENT '重命名后的新名字或移动的目标目录;其余动作为空',
         f_size         bigint(20)    NOT NULL DEFAULT 0 COMMENT '涉及的字节数;没有字节数的动作为0',
         f_result       int(4)        NOT NULL DEFAULT 0 COMMENT '结果:0成功,否则为错误码(error_code_share_*等)',
         f_access_id    varchar(64)   NOT NULL DEFAULT '' COMMENT '所属访问会话ID(内存中的会话标识,便于把同一次访问的记录归到一起)',
         f_create_time  bigint(20)    NOT NULL DEFAULT 0 COMMENT '记录时间(Unix毫秒,服务端时间)',
         PRIMARY KEY ( f_id ),
         KEY  idx_enterprise_time  ( f_enterprise_id, f_create_time ),
         KEY  idx_actor_time  ( f_actor_id, f_create_time ),
         KEY  idx_share  ( f_shareid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//按企业的功能设置表：一行 = 某企业的某项功能设置。没有记录的企业取该功能的内置默认值
//（文件共享默认开启），故企业开通时不必预先插入记录。f_enterprise_id 为 0 的行本期不使用。
//目前有 fileshare.enabled 与 p2p.enabled 两个键（取值 '0' / '1'），由 flamingoAdmin 的企业设置写入。
CREATE TABLE IF NOT EXISTS  t_system_setting  (
         f_enterprise_id bigint(20)   NOT NULL DEFAULT 0 COMMENT '企业ID',
         f_key          varchar(64)   NOT NULL COMMENT '设置项名,如fileshare.enabled',
         f_value        varchar(1024) NOT NULL DEFAULT '' COMMENT '设置值(文本)',
         f_updated_by   bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改的管理员ID',
         f_update_time  bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)',
         PRIMARY KEY ( f_enterprise_id, f_key )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//备忘表（备忘与提醒功能，见 docs/备忘与提醒方案.md）：一行 = 一条个人备忘或群备忘。
//f_start 存<u>本地</u>的年月日时分（十进制 yyyymmddhhmm），提醒由客户端按本机时间计算，
//前提是企业内所有人在同一时区。删除是真删（同 t_group_announcement）。
CREATE TABLE IF NOT EXISTS  t_memo  (
         f_id           bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '备忘ID',
         f_ctype        int(2)        NOT NULL DEFAULT 1 COMMENT '所属:1个人 2群,取值见conversation_type',
         f_owner        bigint(20)    NOT NULL COMMENT '个人备忘为本人userid,群备忘为群id',
         f_creator      bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建人userid',
         f_editor       bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改人userid',
         f_title        varchar(256)  NOT NULL DEFAULT '' COMMENT '标题(1~50字)',
         f_note         varchar(1024) NOT NULL DEFAULT '' COMMENT '备注(0~200字)',
         f_start        bigint(20)    NOT NULL COMMENT '首次事件的本地时间,十进制yyyymmddhhmm',
         f_advance      int(11)       NOT NULL DEFAULT 0 COMMENT '提前分钟数0~1440,0为到点提醒',
         f_repeat       int(2)        NOT NULL DEFAULT 0 COMMENT '重复方式,取值见Protocol.h的memo_repeat_type',
         f_weekdays     int(4)        NOT NULL DEFAULT 0 COMMENT '每周重复的星期掩码:bit0周一..bit6周日;其它重复方式为0',
         f_count        int(11)       NOT NULL DEFAULT 1 COMMENT '提醒次数上限,0为永不结束',
         f_style        int(2)        NOT NULL DEFAULT 1 COMMENT '提醒方式:1右下角弹窗 2全屏动画',
         f_target_all   int(2)        NOT NULL DEFAULT 0 COMMENT '群备忘是否提醒全体成员:1是 0只提醒t_memo_target里的成员',
         f_create_time  bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)',
         f_update_time  bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_owner  ( f_ctype, f_owner )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//备忘提醒对象表：一行 = 某条群备忘指定提醒的一位成员。只有 f_target_all 为 0 的群备忘才有记录；
//个人备忘没有记录。成员退群或被踢时由服务端删掉他的那些行。
CREATE TABLE IF NOT EXISTS  t_memo_target  (
         f_memoid       bigint(20)    NOT NULL COMMENT '备忘ID(t_memo.f_id)',
         f_userid       bigint(20)    NOT NULL COMMENT '被提醒成员userid',
         PRIMARY KEY ( f_memoid, f_userid ),
         KEY  idx_user  ( f_userid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群里关注的成员（docs/消息提醒模式与消息盒子方案.md §12）：一行 = 某用户在某个群里关注了某位成员的发言。
//群的提醒模式为 5（只提醒@我和关注的人）时，这些成员发言会提醒。每人每群最多 20 行，整份覆盖（先删后插）；
//本人退群、被移出或群解散时删掉本人在该群的行。
CREATE TABLE IF NOT EXISTS  t_group_follow  (
         f_userid        bigint(20)    NOT NULL COMMENT '关注发起人userid',
         f_groupid       bigint(20)    NOT NULL COMMENT '群ID',
         f_target_userid bigint(20)    NOT NULL COMMENT '被关注的成员userid',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '关注时间(Unix毫秒)',
         PRIMARY KEY ( f_userid, f_groupid, f_target_userid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//群问答表（docs/群问答方案.md）：一行 = 本群的一条问答，由群主或管理员维护；删除是真删。群解散时一并删除。
CREATE TABLE IF NOT EXISTS  t_group_qa  (
         f_id            bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '问答ID',
         f_groupid       bigint(20)    NOT NULL COMMENT '所属群ID',
         f_question      varchar(128)  NOT NULL DEFAULT '' COMMENT '问题',
         f_answer        varchar(2048) NOT NULL DEFAULT '' COMMENT '答案(纯文字)',
         f_editor        bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改人userid',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)',
         f_update_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_group  ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//企业问答表（docs/群问答方案.md）：一行 = 企业管理员在 flamingoAdmin 里维护的一条问答，应用到本企业的全部群
//（f_scope=0）或 t_enterprise_qa_group 里登记的群（f_scope=1）。群里只读、不能隐藏，本群有同一问题时以本群的为准。
CREATE TABLE IF NOT EXISTS  t_enterprise_qa  (
         f_id            bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '问答ID',
         f_enterprise_id bigint(20)    NOT NULL COMMENT '所属企业ID',
         f_question      varchar(128)  NOT NULL DEFAULT '' COMMENT '问题',
         f_answer        varchar(2048) NOT NULL DEFAULT '' COMMENT '答案(纯文字)',
         f_scope         int(2)        NOT NULL DEFAULT 0 COMMENT '应用范围:0本企业全部群 1所选的群(见t_enterprise_qa_group)',
         f_creator_id    bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改的管理员ID',
         f_creator_name  varchar(64)   NOT NULL DEFAULT '' COMMENT '最后修改的管理员姓名(冗余,便于列表直读)',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)',
         f_update_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_enterprise  ( f_enterprise_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//企业问答应用到的群（f_scope=1 时才有行）：一行 = 某条企业问答应用到某个群。问答删除或群解散时删除对应的行。
CREATE TABLE IF NOT EXISTS  t_enterprise_qa_group  (
         f_qa_id         bigint(20)    NOT NULL COMMENT '企业问答ID(t_enterprise_qa.f_id)',
         f_groupid       bigint(20)    NOT NULL COMMENT '群ID',
         PRIMARY KEY ( f_qa_id, f_groupid ),
         KEY  idx_group  ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//加群链接表（docs/邀请入群方案.md §12）：一行 = 一条 flamingo://joingroup?code= 链接。f_code 是 12 位随机码，全局唯一；
//f_expire_time 为 0 表示永久有效；作废只置 f_revoked，不删行。群解散时删掉本群的链接。
CREATE TABLE IF NOT EXISTS  t_group_link  (
         f_id            bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '链接ID',
         f_code          varchar(32)   NOT NULL COMMENT '随机码(12位,全局唯一)',
         f_groupid       bigint(20)    NOT NULL COMMENT '群ID',
         f_creator       bigint(20)    NOT NULL COMMENT '生成者userid',
         f_enterprise_id bigint(20)    NOT NULL DEFAULT 0 COMMENT '生成者所属企业ID(链接只对本企业的人有效)',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '生成时间(Unix毫秒)',
         f_expire_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '到期时间(Unix毫秒);0表示永久有效',
         f_revoked       int(2)        NOT NULL DEFAULT 0 COMMENT '是否已作废:0否 1是',
         PRIMARY KEY ( f_id ),
         UNIQUE KEY  uniq_code  ( f_code ),
         KEY  idx_group  ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//云端录像表（docs/屏幕共享与录制方案.md 12.6）：一行 = 一条云端录像。来源有两种：屏幕共享中由 imgserver 录下的原始帧
//（先是 rec_<id>.frec，开启者的客户端合成后上传 rec_<id>.mp4），与独立录制在本机录好后上传的 MP4 / m4a。文件都在 imgserver
//的 cloudrecorddir 里；原始帧文件与最终文件都计入企业配额。f_expire_time 为 0 表示永久保留。删除是真删。
CREATE TABLE IF NOT EXISTS  t_cloud_record  (
         f_id            bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '录像ID',
         f_enterprise_id bigint(20)    NOT NULL DEFAULT 0 COMMENT '开启者所属企业ID(配额按企业计)',
         f_owner         bigint(20)    NOT NULL COMMENT '开启者userid(录像归他)',
         f_source        int(2)        NOT NULL DEFAULT 1 COMMENT '来源:1共享中的云端录制 2独立录制上传,取值见cloudrecord_source',
         f_roomid        varchar(64)   NOT NULL DEFAULT '' COMMENT '屏幕共享房间号(来源为共享时)',
         f_ctype         int(2)        NOT NULL DEFAULT 0 COMMENT '共享所在会话类型:1单聊 2群聊;来源为上传时为0',
         f_cid           bigint(20)    NOT NULL DEFAULT 0 COMMENT '共享所在会话ID:单聊为对方userid(以开启者为准),群聊为群ID',
         f_title         varchar(256)  NOT NULL DEFAULT '' COMMENT '标题(默认取共享标题或文件名,开启者可改)',
         f_start_time    bigint(20)    NOT NULL DEFAULT 0 COMMENT '开始录制时间(Unix毫秒)',
         f_end_time      bigint(20)    NOT NULL DEFAULT 0 COMMENT '结束录制时间(Unix毫秒);录制中为0',
         f_duration_ms   bigint(20)    NOT NULL DEFAULT 0 COMMENT '时长(毫秒,不含暂停)',
         f_frec_bytes    bigint(20)    NOT NULL DEFAULT 0 COMMENT '原始帧文件字节数;已删除或来源为上传时为0',
         f_bytes         bigint(20)    NOT NULL DEFAULT 0 COMMENT '最终文件(MP4/m4a)字节数;尚未上传时为0',
         f_audio_only    int(2)        NOT NULL DEFAULT 0 COMMENT '是否只有声音(m4a):1是',
         f_status        int(2)        NOT NULL DEFAULT 1 COMMENT '状态,取值见Protocol.h的cloudrecord_status',
         f_frec_file     varchar(64)   NOT NULL DEFAULT '' COMMENT '原始帧文件名(rec_<id>.frec);已删除或没有时为空',
         f_file          varchar(64)   NOT NULL DEFAULT '' COMMENT '最终文件名(rec_<id>.mp4或.m4a);尚未上传时为空',
         f_expire_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '到期时间(Unix毫秒);0为永久保留',
         f_warned        int(2)        NOT NULL DEFAULT 0 COMMENT '是否已推送到期提醒:1是',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)',
         PRIMARY KEY ( f_id ),
         KEY  idx_owner  ( f_owner, f_create_time ),
         KEY  idx_enterprise  ( f_enterprise_id ),
         KEY  idx_expire  ( f_expire_time ),
         KEY  idx_room  ( f_roomid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//云端录像的分享记录：一行 = 开启者把某条录像发到了某个会话一次（同一会话重复发送只留一行）。会话成员凭此观看与下载：
//单聊里发送人与 f_cid 指向的人都可以看，群聊里该群的成员都可以看。录像删除时一并删除。
CREATE TABLE IF NOT EXISTS  t_cloud_record_share  (
         f_recordid      bigint(20)    NOT NULL COMMENT '录像ID(t_cloud_record.f_id)',
         f_ctype         int(2)        NOT NULL COMMENT '会话类型:1单聊 2群聊',
         f_cid           bigint(20)    NOT NULL COMMENT '会话ID:单聊为接收方userid,群聊为群ID',
         f_sender        bigint(20)    NOT NULL DEFAULT 0 COMMENT '发送人userid',
         f_send_time     bigint(20)    NOT NULL DEFAULT 0 COMMENT '最近一次发送时间(Unix毫秒)',
         PRIMARY KEY ( f_recordid, f_ctype, f_cid ),
         KEY  idx_conv  ( f_ctype, f_cid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//帖子群的帖子表（docs/帖子群方案.md 第 5 节）：一行 = 帖子群里的一条帖子。删除是真删，删帖时在同一个事务里删掉它的补充、
//留言、回复与互动。正文等内容放在 blob 列 f_content（JSON：title / text / images / mentions），MySQL 的 utf8 列存不下
//4 字节字符，与 t_chatmsg.f_content 的做法相同。f_active_time 是「最新动态」排序的依据：发帖、留言、回复、补充都会刷新它。
//f_cardmsgid 是发帖时写进群里的帖子卡片（msgtype 25）的消息 ID，删帖时据此把卡片改写为已删除形态。
CREATE TABLE IF NOT EXISTS  t_thread_post  (
         f_postid        bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '帖子ID',
         f_groupid       bigint(20)    NOT NULL COMMENT '所属帖子群ID',
         f_author        bigint(20)    NOT NULL COMMENT '作者userid',
         f_client_postid varchar(64)   NOT NULL DEFAULT '' COMMENT '客户端生成的发帖去重标识',
         f_content       blob          COMMENT '帖子内容JSON:title/text/images/mentions',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '发帖时间(Unix毫秒)',
         f_edit_time     bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒);0表示没有修改过',
         f_active_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '最新动态时间:发帖、留言、回复、补充中最晚的一次(Unix毫秒)',
         f_comment_count int(11)       NOT NULL DEFAULT 0 COMMENT '未删除的留言与回复总数',
         f_pin_time      bigint(20)    NOT NULL DEFAULT 0 COMMENT '置顶时间(Unix毫秒);0表示没有置顶',
         f_cardmsgid     bigint(20)    NOT NULL DEFAULT 0 COMMENT '发帖时写进群里的帖子卡片msgid;0表示没有写成',
         PRIMARY KEY ( f_postid ),
         KEY  idx_group_active  ( f_groupid, f_active_time ),
         KEY  idx_group_post  ( f_groupid, f_postid ),
         KEY  idx_author_client  ( f_author, f_client_postid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//帖子群的楼主补充表：一行 = 楼主在自己的帖子正文下追加的一段补充，按 f_addid 先后排列。发出后不能修改，可以删除（真删）。
//f_cardmsgid 是这段补充的动态卡片（msgtype 26）的消息 ID，删除补充或删帖时据此把卡片改写为已删除形态（docs/帖子群方案.md 14.6）。
CREATE TABLE IF NOT EXISTS  t_thread_addendum  (
         f_addid         bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '补充ID',
         f_postid        bigint(20)    NOT NULL COMMENT '所属帖子ID',
         f_groupid       bigint(20)    NOT NULL COMMENT '所属帖子群ID',
         f_author        bigint(20)    NOT NULL COMMENT '补充人userid(即楼主)',
         f_content       blob          COMMENT '补充内容JSON:text/images/mentions/at',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '补充时间(Unix毫秒)',
         f_cardmsgid     bigint(20)    NOT NULL DEFAULT 0 COMMENT '这段补充的动态卡片msgid;0表示没有写卡片',
         PRIMARY KEY ( f_addid ),
         KEY  idx_post  ( f_postid, f_addid ),
         KEY  idx_group  ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//帖子群的留言与回复表：f_parentid 为 0 的是留言（直接回复帖子），否则是对某条留言的回复，f_parentid 为那条留言的 ID，
//f_reply_to 为被回复的人。留言下面有回复时，删除只置 f_deleted、保留一条占位，回复照常显示；没有回复时直接删除。
//f_reply_count 只对留言有意义，是它下面未删除的回复数。f_cardmsgid 是这条留言或回复的动态卡片（msgtype 26）的消息 ID，
//删除时据此把卡片改写为已删除形态（docs/帖子群方案.md 14.6）。
CREATE TABLE IF NOT EXISTS  t_thread_comment  (
         f_commentid     bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '留言或回复ID',
         f_postid        bigint(20)    NOT NULL COMMENT '所属帖子ID',
         f_groupid       bigint(20)    NOT NULL COMMENT '所属帖子群ID',
         f_author        bigint(20)    NOT NULL COMMENT '作者userid',
         f_parentid      bigint(20)    NOT NULL DEFAULT 0 COMMENT '0为留言;否则为所属留言的ID',
         f_reply_to      bigint(20)    NOT NULL DEFAULT 0 COMMENT '被回复的人userid;留言为0',
         f_client_commentid varchar(64) NOT NULL DEFAULT '' COMMENT '客户端生成的去重标识',
         f_content       blob          COMMENT '内容JSON:text/mentions/at',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '时间(Unix毫秒)',
         f_deleted       int(2)        NOT NULL DEFAULT 0 COMMENT '已删除但因下面有回复而保留占位:1是',
         f_reply_count   int(11)       NOT NULL DEFAULT 0 COMMENT '留言下面未删除的回复数;回复本身为0',
         f_cardmsgid     bigint(20)    NOT NULL DEFAULT 0 COMMENT '这条留言或回复的动态卡片msgid;0表示没有写卡片',
         PRIMARY KEY ( f_commentid ),
         KEY  idx_post  ( f_postid, f_parentid, f_commentid ),
         KEY  idx_parent  ( f_parentid, f_commentid ),
         KEY  idx_group  ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//帖子群的互动表：一行 = 某人对某个目标（帖子，或留言与回复）加的一种互动。f_reaction 取 0 表示「赞」，
//取 1000 + 表情编号表示用某个表情回应。同一人对同一目标可以加多种，每种至多一行（主键保证），取消即删除该行。
CREATE TABLE IF NOT EXISTS  t_thread_reaction  (
         f_target_type   int(2)        NOT NULL COMMENT '目标类型:1帖子 2留言或回复(取值见Protocol.h的thread_target_type)',
         f_target_id     bigint(20)    NOT NULL COMMENT '目标ID:帖子ID或留言ID',
         f_userid        bigint(20)    NOT NULL COMMENT '互动用户userid',
         f_reaction      int(11)       NOT NULL COMMENT '互动:0赞,1000+表情编号为表情回应',
         f_groupid       bigint(20)    NOT NULL COMMENT '所属帖子群ID',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '互动时间(Unix毫秒)',
         PRIMARY KEY ( f_target_type, f_target_id, f_userid, f_reaction ),
         KEY  idx_group  ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//机器人表（docs/机器人方案.md §5.5）：一行 = 一个机器人。f_owner_type 1 企业机器人（f_owner_id 为企业ID）、2 群机器人（群号）、
//3 个人机器人（主人userid）。企业机器人与群机器人在 t_user 里另有一行 f_usertype=2 的账号（f_user_id），个人机器人以主人的身份
//代答、没有账号（f_user_id 为 0）。删除是软删除（f_status=2），账号保留，历史消息仍能解析出名字。
CREATE TABLE IF NOT EXISTS  t_bot  (
         f_bot_id        bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '机器人ID',
         f_user_id       bigint(20)    NOT NULL DEFAULT 0 COMMENT '机器人账号userid(t_user.f_usertype=2);个人机器人为0',
         f_enterprise_id bigint(20)    NOT NULL DEFAULT 0 COMMENT '所属企业ID',
         f_owner_type    int(2)        NOT NULL DEFAULT 1 COMMENT '归属:1企业机器人 2群机器人 3个人机器人',
         f_owner_id      bigint(20)    NOT NULL DEFAULT 0 COMMENT '企业ID、群号或主人userid,随归属而定',
         f_name          varchar(64)   NOT NULL DEFAULT '' COMMENT '名称',
         f_desc          varchar(512)  NOT NULL DEFAULT '' COMMENT '简介',
         f_glyph         varchar(16)   NOT NULL DEFAULT '' COMMENT '头像字形',
         f_color         varchar(16)   NOT NULL DEFAULT '' COMMENT '头像底色#RRGGBB',
         f_scope         int(2)        NOT NULL DEFAULT 0 COMMENT '企业机器人的应用范围:0本企业全部群 1所选的群(见t_bot_group)',
         f_allow_group_off int(2)      NOT NULL DEFAULT 0 COMMENT '企业机器人是否允许群主在本群关闭:0否 1是',
         f_status        int(2)        NOT NULL DEFAULT 0 COMMENT '状态:0启用 1停用 2已删除',
         f_source        int(2)        NOT NULL DEFAULT 0 COMMENT '来源:0按规则响应 1GitHub仓库通知 2GitLab仓库通知 3自动回复预置',
         f_creator_type  int(2)        NOT NULL DEFAULT 1 COMMENT '创建人类别:1管理员 2IM用户',
         f_creator_id    bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建人ID',
         f_creator_name  varchar(64)   NOT NULL DEFAULT '' COMMENT '创建人名字(冗余,便于列表直读)',
         f_webhook_token varchar(64)   NOT NULL DEFAULT '' COMMENT 'Webhook令牌(32位十六进制)',
         f_secret        varchar(64)   NOT NULL DEFAULT '' COMMENT '签名密钥(32位十六进制)',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)',
         f_update_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)',
         f_last_fire_time bigint(20)   NOT NULL DEFAULT 0 COMMENT '最近一次发出消息的时间(Unix毫秒,最多每分钟写一次)',
         PRIMARY KEY ( f_bot_id ),
         KEY  idx_enterprise  ( f_enterprise_id ),
         KEY  idx_owner  ( f_owner_type, f_owner_id ),
         KEY  idx_webhook  ( f_webhook_token )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//机器人规则表：一行 = 一条规则，按 f_seq 排序，命中第一条即止。触发方式与动作的详情是 JSON，放在 blob 列（可能含四字节字符）。
CREATE TABLE IF NOT EXISTS  t_bot_rule  (
         f_rule_id       bigint(20)    NOT NULL AUTO_INCREMENT COMMENT '规则ID',
         f_bot_id        bigint(20)    NOT NULL COMMENT '所属机器人ID',
         f_seq           int(11)       NOT NULL DEFAULT 0 COMMENT '在机器人内的顺序,从0起',
         f_name          varchar(64)   NOT NULL DEFAULT '' COMMENT '规则名称',
         f_enabled       int(2)        NOT NULL DEFAULT 1 COMMENT '是否启用:0否 1是',
         f_trigger_type  int(2)        NOT NULL DEFAULT 1 COMMENT '触发方式:1被@ 2关键词 3入群 4定时 5收到单聊 6外部系统推送',
         f_trigger       blob          COMMENT '触发方式与附加条件(JSON)',
         f_action_type   int(2)        NOT NULL DEFAULT 1 COMMENT '动作:1回复文字 2用问答作答 3呼叫外部服务',
         f_action        blob          COMMENT '动作详情(JSON)',
         f_create_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)',
         f_update_time   bigint(20)    NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)',
         PRIMARY KEY ( f_rule_id ),
         KEY  idx_bot  ( f_bot_id, f_seq )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//企业机器人所选的群（f_scope=1 时才有行）：一行 = 某个企业机器人应用到某个群。机器人删除或群解散时删除对应的行。
CREATE TABLE IF NOT EXISTS  t_bot_group  (
         f_bot_id        bigint(20)    NOT NULL COMMENT '机器人ID',
         f_groupid       bigint(20)    NOT NULL COMMENT '群ID',
         PRIMARY KEY ( f_bot_id, f_groupid ),
         KEY  idx_group  ( f_groupid )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//在某群关闭了的企业机器人：一行 = 群主在某个群里关闭了某个企业机器人（该机器人须允许群主关闭）。重新打开即删除该行。
CREATE TABLE IF NOT EXISTS  t_bot_group_off  (
         f_groupid       bigint(20)    NOT NULL COMMENT '群ID',
         f_bot_id        bigint(20)    NOT NULL COMMENT '机器人ID',
         f_operator_id   bigint(20)    NOT NULL DEFAULT 0 COMMENT '操作的群主userid',
         f_time          bigint(20)    NOT NULL DEFAULT 0 COMMENT '关闭时间(Unix毫秒)',
         PRIMARY KEY ( f_groupid, f_bot_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//机器人呼叫外部服务的域名白名单：一行 = 某企业允许的一个域名（本身及其子域名）。由企业管理员在 flamingoAdmin 里维护。
CREATE TABLE IF NOT EXISTS  t_bot_domain  (
         f_enterprise_id bigint(20)    NOT NULL COMMENT '企业ID',
         f_domain        varchar(128)  NOT NULL COMMENT '域名(小写)',
         PRIMARY KEY ( f_enterprise_id, f_domain )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8
