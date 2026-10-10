#include "MysqlManager.h"
#include <sstream>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "MysqlThrdMgr.h"


CMysqlManager::CMysqlManager(void)
{
	//TODO: m_strCharactSet可以放在初始化列表中初始化
    m_strCharactSet = "utf8";

	// 初始化表 
	// 1. t_user 
	{
		STableInfo info;
		//name type desc?
        info.m_strName = "t_user";
        info.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
        info.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL COMMENT '用户ID'", "bigint(20)" };
        info.m_mapField["f_username"] = { "f_username", "varchar(64) NOT NULL COMMENT '用户名'", "varchar(64)" };
        info.m_mapField["f_nickname"] = { "f_nickname", "varchar(64) NOT NULL COMMENT '用户昵称'", "varchar(64)" };
        info.m_mapField["f_password"] = { "f_password", "varchar(64) NOT NULL COMMENT '用户密码'", "varchar(64)" };
        //v2 登录改造新增：密码盐值，与 f_password 一起构成加盐哈希口令
        info.m_mapField["f_salt"] = { "f_salt", "varchar(32) DEFAULT NULL COMMENT '密码盐值，注册时生成（v2登录口令存储用）'", "varchar(32)" };
        info.m_mapField["f_facetype"] = { "f_facetype", "int(10) DEFAULT 0 COMMENT '用户头像类型'", "int(10)" };
        info.m_mapField["f_customface"] = { "f_customface", "varchar(32) DEFAULT NULL COMMENT '自定义头像名'", "varchar(32)" };
        info.m_mapField["f_customfacefmt"] = { "f_customfacefmt", "varchar(6) DEFAULT NULL COMMENT '自定义头像格式'", "varchar(6)" };
        info.m_mapField["f_gender"] = { "f_gender", "int(2)  DEFAULT 0 COMMENT '性别'", "int(2)" };
        info.m_mapField["f_birthday"] = { "f_birthday", "bigint(20)  DEFAULT 19900101 COMMENT '生日'", "bigint(20)" };
        info.m_mapField["f_signature"] = { "f_signature", "varchar(256) DEFAULT NULL COMMENT '地址'", "varchar(256)" };
        info.m_mapField["f_address"] = { "f_address", "varchar(256) DEFAULT NULL COMMENT '地址'", "varchar(256)" };
        info.m_mapField["f_phonenumber"] = { "f_phonenumber", "varchar(64) DEFAULT NULL COMMENT '电话'", "varchar(64)" };
        info.m_mapField["f_mail"] = { "f_mail", "varchar(256) DEFAULT NULL COMMENT '邮箱'", "varchar(256)" };
        info.m_mapField["f_owner_id"] = { "f_owner_id", "bigint(20) DEFAULT 0 COMMENT '群账号群主userid'", "bigint(20)" };
        info.m_mapField["f_teaminfo"] = { "f_teaminfo", "blob default null comment '好友分组信息'", "blob" };
        info.m_mapField["f_register_time"] = { "f_register_time", "datetime NOT NULL COMMENT '注册时间'", "datetime" };
        info.m_mapField["f_remark"] = { "f_remark", "varchar(64) NULL COMMENT '备注'", "varchar(64)" };
        info.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
        //企业 Admin 控制台引入的多租户与人事字段
        info.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '所属企业ID，关联 t_enterprise.f_enterprise_id'", "bigint(20)" };
        info.m_mapField["f_role"] = { "f_role", "int(2) NOT NULL DEFAULT 0 COMMENT '企业内角色：0=普通成员 1=部门负责人 2=企业管理员'", "int(2)" };
        info.m_mapField["f_employee_no"] = { "f_employee_no", "varchar(32) DEFAULT NULL COMMENT '工号'", "varchar(32)" };
        info.m_mapField["f_join_date"] = { "f_join_date", "datetime DEFAULT NULL COMMENT '入职日期'", "datetime" };
        info.m_mapField["f_landline"] = { "f_landline", "varchar(64) DEFAULT NULL COMMENT '座机'", "varchar(64)" };
        //联系人资料字段扩充（2026-10-04）：本人填写的岗位，与部门职位无关
        info.m_mapField["f_post"] = { "f_post", "varchar(64) DEFAULT NULL COMMENT '岗位：本人填写的自由文本，与部门职位无关'", "varchar(64)" };
        //手机号、座机对同事的公开范围（2026-10-06）：默认手机号仅本人可见、座机对同事可见
        info.m_mapField["f_phone_visible"] = { "f_phone_visible", "int(2) NOT NULL DEFAULT 0 COMMENT '手机号是否对同事公开：0=仅本人可见 1=对同事可见'", "int(2)" };
        info.m_mapField["f_landline_visible"] = { "f_landline_visible", "int(2) NOT NULL DEFAULT 1 COMMENT '座机是否对同事公开：0=仅本人可见 1=对同事可见'", "int(2)" };
        info.m_mapField["f_workstation"] = { "f_workstation", "varchar(256) DEFAULT NULL COMMENT '工位地址'", "varchar(256)" };
        info.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '账号状态：0=启用 1=停用'", "int(2)" };
        //v2 登录改造新增：用户类型，区分普通用户与服务号
        info.m_mapField["f_usertype"] = { "f_usertype", "int(2) NOT NULL DEFAULT 0 COMMENT '用户类型：0=普通用户 1=服务号用户'", "int(2)" };

        info.m_strKeyString = "PRIMARY KEY (f_user_id), INDEX f_user_id (f_user_id), KEY  f_id  ( f_id ), INDEX idx_enterprise (f_enterprise_id)";
        m_vecTableInfo.push_back(info);

		//info.m_mapField["AccountID"] = { "AccountID", "integer NOT NULL AUTO_INCREMENT PRIMARY KEY", "int(11)" };
		//info.m_mapField["MobilePhone"] = { "MobilePhone", "bigint NOT NULL", "bigint(20)" };
		//info.m_mapField["NickName"] = { "NickName", "char(65) not null", "char(65)" };
		//info.m_mapField["PassWord"] = { "PassWord", "char(65) not null", "char(65)" };
		////info.m_mapField["Friend"] = { "Friend", "blob not null", "blob" };
  //      info.m_mapField["Friend"] = { "Friend", "blob default null", "blob" };
		//info.m_mapField["PersonInfo"] = { "PersonInfo", "blob default null", "blob" };
		//
		//info.m_strKeyString = "UNIQUE KEY(MobilePhone)";
		//m_vecTableInfo.push_back(info);
	}

    // 2. t_contact_remark —— 联系人备注名表（企业版）
    //    原先此处是 t_user_relationship（好友关系表），已于 2026-08-05 随好友关系子系统
    //    一并删除：企业版通讯录来自组织架构 t_department_member，同事之间并无好友关系。
    //    好友关系表上唯一还有人用的"好友备注名"迁到了本表，它不依赖任何好友关系。
    {
        STableInfo remark;
        remark.m_strName = "t_contact_remark";
        remark.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '备注名归属用户userid'", "bigint(20)" };
        remark.m_mapField["f_target_userid"] = { "f_target_userid", "bigint(20) NOT NULL COMMENT '被备注的联系人userid'", "bigint(20)" };
        remark.m_mapField["f_remark"] = { "f_remark", "varchar(32) NOT NULL DEFAULT '' COMMENT '备注名'", "varchar(32)" };
        remark.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最近一次修改时间(Unix毫秒)'", "bigint(20)" };

        remark.m_strKeyString = "PRIMARY KEY (f_userid, f_target_userid), KEY idx_user (f_userid)";
        m_vecTableInfo.push_back(remark);
    }

	// 3. t_chatmsg —— 消息记录表
	{
		STableInfo chat;
		chat.m_strName = "t_chatmsg";
		chat.m_mapField["f_msgid"] = { "f_msgid", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '服务器消息ID(全局自增,单调递增)'", "bigint(20)" };
		chat.m_mapField["f_clientmsgid"] = { "f_clientmsgid", "varchar(64) NOT NULL DEFAULT '' COMMENT '客户端消息ID(UUID,去重+回执回填)'", "varchar(64)" };
		chat.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL DEFAULT 1 COMMENT '会话类型:1单聊 2群聊'", "int(2)" };
		chat.m_mapField["f_from"] = { "f_from", "bigint(20) NOT NULL COMMENT '发送方userid'", "bigint(20)" };
		chat.m_mapField["f_to"] = { "f_to", "bigint(20) NOT NULL COMMENT '接收方:单聊为对端userid,群聊为群id'", "bigint(20)" };
		chat.m_mapField["f_msgtype"] = { "f_msgtype", "int(4) NOT NULL DEFAULT 1 COMMENT '内容类型:1文本 2图片 3文件 4语音 …17系统'", "int(4)" };
		chat.m_mapField["f_content"] = { "f_content", "blob COMMENT '内容JSON(content/mentions/mentionall/quote 打包)'", "blob" };
		chat.m_mapField["f_recalled"] = { "f_recalled", "int(2) NOT NULL DEFAULT 0 COMMENT '是否已撤回:0否 1是'", "int(2)" };
		chat.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '消息时间(Unix毫秒)'", "bigint(20)" };
		//f_audience：2026-10-02 文件共享功能加入，默认 0（全员可见）
		chat.m_mapField["f_audience"] = { "f_audience", "int(2) NOT NULL DEFAULT 0 COMMENT '可见范围:0全员可见 1仅发送者与t_message_audience里登记的人可见(如只给部分群成员看的文件共享卡片)'", "int(2)" };
		//f_recalled_by：2026-10-03 加入，默认 0 表示撤回者未知，按发送方处理
		//f_search_text：2026-10-04 加入；为 NULL 的行由 chatserver 启动后分批回填
		chat.m_mapField["f_search_text"] = { "f_search_text", "text COMMENT '搜索用纯文本(文字正文/文件名/卡片标题等,UTF-8,去掉四字节字符,最长4000字节);NULL表示尚未提取'", "text" };
		chat.m_mapField["f_recalled_by"] = { "f_recalled_by", "bigint(20) NOT NULL DEFAULT 0 COMMENT '撤回者userid:0表示未撤回,或撤回发生在2026-10-03之前没有记录(此时按发送方处理)。群主与管理员可撤回别人的群消息,所以不一定等于f_from'", "bigint(20)" };
		//f_rev：2026-10-08 加入（消息改写离线同步，docs/消息改写离线同步方案.md）。默认 0 表示从未被改写，
		//每次改写内容或撤回时分配全局递增的新序号
		chat.m_mapField["f_rev"] = { "f_rev", "bigint(20) NOT NULL DEFAULT 0 COMMENT '改写序号:0表示从未被改写;每次改写内容或撤回时取全局递增的新值'", "bigint(20)" };
		//idx_to_msgid 是"按会话取最新消息"用的：拉会话列表时的查询形如
		//"WHERE f_ctype=? AND f_to IN (...) GROUP BY f_to" 取 MAX(f_msgid)，
		//本索引让它按 (f_to, f_msgid) 逐组直接跳到最大值。idx_to_from 顶不上 ——
		//它的第二列是 f_from，同一个会话内 f_msgid 是散的。
		//2026-08-12 实测（25 万行 / 50 个群）：MySQL 583ms → 303ms、SQLite 1046ms → 68ms。
		//idx_rev 供 cmd_sync_rewritten_messages 按改写序号取范围（2026-10-08）。
		chat.m_strKeyString = "PRIMARY KEY (f_msgid), KEY idx_from_to (f_from, f_to), KEY idx_to_from (f_to, f_from), KEY idx_client (f_clientmsgid), KEY idx_to_msgid (f_to, f_msgid), KEY idx_rev (f_rev)";
		m_vecTableInfo.push_back(chat);
	}

	// 4. t_department —— 企业部门表（组织架构树）
	// 采用"邻接表 + 物化路径"方案：f_parent_dept_id 表达直接父子关系，
	// f_dept_path 缓存自根到本节点(含本节点)的 dept_id 链，便于一次 LIKE 查询整棵子树
	{
		STableInfo dept;
		dept.m_strName = "t_department";
		dept.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		dept.m_mapField["f_dept_id"] = { "f_dept_id", "bigint(20) NOT NULL COMMENT '部门业务ID（协议中引用部门统一用此ID，非自增f_id）'", "bigint(20)" };
		dept.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '所属企业ID'", "bigint(20)" };
		dept.m_mapField["f_dept_name"] = { "f_dept_name", "varchar(64) NOT NULL COMMENT '部门名称'", "varchar(64)" };
		dept.m_mapField["f_parent_dept_id"] = { "f_parent_dept_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '父部门ID，0表示根部门（公司本身/顶级部门）'", "bigint(20)" };
		dept.m_mapField["f_dept_path"] = { "f_dept_path", "varchar(255) NOT NULL DEFAULT '' COMMENT '物化路径，自根到本节点(含本节点)，形如 /1001/1005/ ，用于子树查询'", "varchar(255)" };
		dept.m_mapField["f_dept_level"] = { "f_dept_level", "int(10) NOT NULL DEFAULT 1 COMMENT '部门层级，根部门为1，每向下一级+1'", "int(10)" };
		dept.m_mapField["f_dept_order"] = { "f_dept_order", "int(10) NOT NULL DEFAULT 0 COMMENT '同级部门排序权重，值小者靠前'", "int(10)" };
		dept.m_mapField["f_leader_user_id"] = { "f_leader_user_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '部门负责人userid，0表示暂未指定'", "bigint(20)" };
		dept.m_mapField["f_dept_desc"] = { "f_dept_desc", "varchar(256) DEFAULT NULL COMMENT '部门描述/职能说明'", "varchar(256)" };
		dept.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '部门状态：0=正常，1=停用（停用不物理删除，保留历史）'", "int(2)" };
		dept.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };
		dept.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
		dept.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };

		dept.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_dept_id (f_dept_id), KEY idx_parent (f_parent_dept_id), KEY idx_path (f_dept_path), KEY idx_leader (f_leader_user_id), KEY idx_enterprise (f_enterprise_id)";
		m_vecTableInfo.push_back(dept);
	}

	// 5. t_department_member —— 部门成员关系表（部门 ↔ 用户，多对多）
	// 一个部门可直接挂载多个成员；一个用户可隶属多个部门，由 f_is_primary 标识其主部门
	{
		STableInfo deptMember;
		deptMember.m_strName = "t_department_member";
		deptMember.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		deptMember.m_mapField["f_dept_id"] = { "f_dept_id", "bigint(20) NOT NULL COMMENT '部门ID，关联 t_department.f_dept_id'", "bigint(20)" };
		deptMember.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '所属企业ID（便于按租户隔离查询）'", "bigint(20)" };
		deptMember.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL COMMENT '用户ID，关联 t_user.f_user_id'", "bigint(20)" };
		deptMember.m_mapField["f_position"] = { "f_position", "varchar(64) DEFAULT NULL COMMENT '该成员在本部门的职位/职务，如高级工程师、组长'", "varchar(64)" };
		deptMember.m_mapField["f_is_primary"] = { "f_is_primary", "int(2) NOT NULL DEFAULT 1 COMMENT '是否主部门：1=主部门，0=兼属部门；每个用户至多一条为1'", "int(2)" };
		deptMember.m_mapField["f_member_order"] = { "f_member_order", "int(10) NOT NULL DEFAULT 0 COMMENT '成员在本部门内的排序权重，值小者靠前'", "int(10)" };
		deptMember.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '成员状态：0=在职，1=已移出/离职（保留历史，不物理删除）'", "int(2)" };
		deptMember.m_mapField["f_join_time"] = { "f_join_time", "datetime NOT NULL COMMENT '加入部门时间'", "datetime" };
		deptMember.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
		deptMember.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };

		deptMember.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_dept_user (f_dept_id, f_user_id), KEY idx_user (f_user_id), KEY idx_dept (f_dept_id)";
		m_vecTableInfo.push_back(deptMember);
	}

	// 6. t_enterprise —— 企业租户表
	{
		STableInfo ent;
		ent.m_strName = "t_enterprise";
		ent.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		ent.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL COMMENT '企业业务ID'", "bigint(20)" };
		ent.m_mapField["f_name"] = { "f_name", "varchar(128) NOT NULL COMMENT '企业全称'", "varchar(128)" };
		ent.m_mapField["f_short_name"] = { "f_short_name", "varchar(64) DEFAULT NULL COMMENT '企业简称'", "varchar(64)" };
		ent.m_mapField["f_industry"] = { "f_industry", "varchar(64) DEFAULT NULL COMMENT '所属行业'", "varchar(64)" };
		ent.m_mapField["f_region"] = { "f_region", "varchar(64) DEFAULT NULL COMMENT '所在地区'", "varchar(64)" };
		ent.m_mapField["f_plan"] = { "f_plan", "int(2) NOT NULL DEFAULT 0 COMMENT '套餐：0=基础版 1=专业版 2=企业版'", "int(2)" };
		ent.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '状态：0=待审核 1=活跃 2=已停用'", "int(2)" };
		//公司根部门：代表"公司本身"这一层，员工可直接挂其下；0 表示尚未建立
		ent.m_mapField["f_root_dept_id"] = { "f_root_dept_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '公司根部门ID，关联 t_department.f_dept_id；0表示尚未建立'", "bigint(20)" };
		ent.m_mapField["f_admin_name"] = { "f_admin_name", "varchar(64) DEFAULT NULL COMMENT '主管理员姓名'", "varchar(64)" };
		ent.m_mapField["f_admin_email"] = { "f_admin_email", "varchar(128) DEFAULT NULL COMMENT '主管理员邮箱'", "varchar(128)" };
		ent.m_mapField["f_logo_color"] = { "f_logo_color", "varchar(16) DEFAULT NULL COMMENT '企业Logo底色'", "varchar(16)" };
		ent.m_mapField["f_open_date"] = { "f_open_date", "datetime DEFAULT NULL COMMENT '开通日期'", "datetime" };
		ent.m_mapField["f_license_expire_time"] = { "f_license_expire_time", "datetime DEFAULT NULL COMMENT '企业版授权到期时间'", "datetime" };
		ent.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };
		ent.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
		ent.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };
		//敏感词过滤总开关：2026-10-02 企业级敏感词功能加入（docs/敏感词方案.md）。
		//默认 1：词库为空时开关不起作用，管理员加了词就期望它立即生效
		ent.m_mapField["f_sensitive_on"] = { "f_sensitive_on", "int(2) NOT NULL DEFAULT 1 COMMENT '敏感词过滤总开关：0=暂停 1=启用'", "int(2)" };
		//新建账号的前缀（2026-10-06，docs/登录账号与手机号登录方案.md）：为空时用 EMP
		ent.m_mapField["f_account_prefix"] = { "f_account_prefix", "varchar(16) DEFAULT NULL COMMENT '新建账号的前缀：英文字母与点号、大写保存；为空时用 EMP'", "varchar(16)" };

		//企业全称唯一：客户端注册时用户填的是公司全称，服务端要靠它精确定位到唯一一家企业，
		//重名会让"这个人该进哪一家"无法判定。
		ent.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_enterprise_id (f_enterprise_id), UNIQUE KEY uniq_enterprise_name (f_name)";
		m_vecTableInfo.push_back(ent);
	}

	// 7. t_admin —— 控制台管理员表
	{
		STableInfo admin;
		admin.m_strName = "t_admin";
		admin.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		admin.m_mapField["f_admin_id"] = { "f_admin_id", "bigint(20) NOT NULL COMMENT '管理员业务ID'", "bigint(20)" };
		admin.m_mapField["f_email"] = { "f_email", "varchar(128) NOT NULL COMMENT '登录邮箱（即登录账号）'", "varchar(128)" };
		admin.m_mapField["f_password"] = { "f_password", "varchar(64) NOT NULL COMMENT '登录密码：MD5(明文密码 + f_salt) 的十六进制串'", "varchar(64)" };
		admin.m_mapField["f_salt"] = { "f_salt", "varchar(32) NOT NULL COMMENT '每个管理员独立的密码盐值'", "varchar(32)" };
		admin.m_mapField["f_name"] = { "f_name", "varchar(64) NOT NULL COMMENT '管理员姓名'", "varchar(64)" };
		admin.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '管辖范围：0=平台超级管理员，非0=仅限该企业'", "bigint(20)" };
		admin.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '状态：0=正常 1=停用'", "int(2)" };
		admin.m_mapField["f_last_login_time"] = { "f_last_login_time", "datetime DEFAULT NULL COMMENT '末次登录时间'", "datetime" };
		admin.m_mapField["f_last_login_ip"] = { "f_last_login_ip", "varchar(64) DEFAULT NULL COMMENT '末次登录IP'", "varchar(64)" };
		admin.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };
		admin.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
		admin.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };

		admin.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_admin_id (f_admin_id), UNIQUE KEY uniq_email (f_email)";
		m_vecTableInfo.push_back(admin);
	}

	// 8. t_admin_session —— 管理员登录会话表
	{
		STableInfo sess;
		sess.m_strName = "t_admin_session";
		sess.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		sess.m_mapField["f_token"] = { "f_token", "varchar(64) NOT NULL COMMENT '会话令牌，登录时下发'", "varchar(64)" };
		sess.m_mapField["f_admin_id"] = { "f_admin_id", "bigint(20) NOT NULL COMMENT '所属管理员ID'", "bigint(20)" };
		sess.m_mapField["f_login_ip"] = { "f_login_ip", "varchar(64) DEFAULT NULL COMMENT '登录IP'", "varchar(64)" };
		sess.m_mapField["f_login_time"] = { "f_login_time", "datetime NOT NULL COMMENT '登录时间'", "datetime" };
		sess.m_mapField["f_expire_time"] = { "f_expire_time", "datetime NOT NULL COMMENT '令牌过期时间'", "datetime" };
		sess.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };

		sess.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_token (f_token), KEY idx_admin (f_admin_id)";
		m_vecTableInfo.push_back(sess);
	}

	// 9. t_admin_audit_log —— 控制台审计/操作日志表
	{
		STableInfo audit;
		audit.m_strName = "t_admin_audit_log";
		audit.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		audit.m_mapField["f_actor_type"] = { "f_actor_type", "int(2) NOT NULL DEFAULT 0 COMMENT '操作者类型：0=管理员 1=系统'", "int(2)" };
		audit.m_mapField["f_actor_id"] = { "f_actor_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '操作者管理员ID，系统操作为0'", "bigint(20)" };
		audit.m_mapField["f_actor_name"] = { "f_actor_name", "varchar(64) DEFAULT NULL COMMENT '操作者姓名（冗余，便于日志直读）'", "varchar(64)" };
		audit.m_mapField["f_ip"] = { "f_ip", "varchar(64) DEFAULT NULL COMMENT '操作来源IP'", "varchar(64)" };
		audit.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '关联企业ID，平台级操作为0'", "bigint(20)" };
		audit.m_mapField["f_action"] = { "f_action", "varchar(64) NOT NULL COMMENT '操作类型，如 dept.create / user.disable / login.success'", "varchar(64)" };
		audit.m_mapField["f_target"] = { "f_target", "varchar(256) DEFAULT NULL COMMENT '操作目标的文字描述'", "varchar(256)" };
		audit.m_mapField["f_severity"] = { "f_severity", "int(2) NOT NULL DEFAULT 0 COMMENT '级别：0=info 1=warn 2=danger'", "int(2)" };
		audit.m_mapField["f_detail"] = { "f_detail", "varchar(512) DEFAULT NULL COMMENT '操作详情'", "varchar(512)" };
		audit.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '操作发生时间'", "datetime" };

		audit.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_enterprise (f_enterprise_id), KEY idx_action (f_action), KEY idx_create_time (f_create_time)";
		m_vecTableInfo.push_back(audit);
	}

	// 10. t_user_permission —— 用户权限覆盖表
	{
		STableInfo perm;
		perm.m_strName = "t_user_permission";
		perm.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		perm.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL COMMENT '用户ID'", "bigint(20)" };
		perm.m_mapField["f_perm_key"] = { "f_perm_key", "varchar(64) NOT NULL COMMENT '权限项键名，如 msg.external'", "varchar(64)" };
		perm.m_mapField["f_granted"] = { "f_granted", "int(2) NOT NULL DEFAULT 0 COMMENT '是否授予：0=禁止 1=允许（覆盖角色默认值）'", "int(2)" };
		perm.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
		perm.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };

		perm.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_user_perm (f_user_id, f_perm_key)";
		m_vecTableInfo.push_back(perm);
	}

	// 11. t_user_activity_log —— 员工操作历史表
	{
		STableInfo activity;
		activity.m_strName = "t_user_activity_log";
		activity.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		activity.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL COMMENT '用户ID'", "bigint(20)" };
		activity.m_mapField["f_action"] = { "f_action", "varchar(64) NOT NULL COMMENT '操作类型，如 file.upload / msg.recall'", "varchar(64)" };
		activity.m_mapField["f_detail"] = { "f_detail", "varchar(512) DEFAULT NULL COMMENT '操作详情'", "varchar(512)" };
		activity.m_mapField["f_severity"] = { "f_severity", "int(2) NOT NULL DEFAULT 0 COMMENT '级别：0=info 1=warn 2=danger'", "int(2)" };
		activity.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '操作发生时间'", "datetime" };

		activity.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_user (f_user_id), KEY idx_create_time (f_create_time)";
		m_vecTableInfo.push_back(activity);
	}

	// 12. t_announcement —— 平台公告表
	{
		STableInfo ann;
		ann.m_strName = "t_announcement";
		ann.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		ann.m_mapField["f_announcement_id"] = { "f_announcement_id", "bigint(20) NOT NULL COMMENT '公告业务ID'", "bigint(20)" };
		ann.m_mapField["f_title"] = { "f_title", "varchar(256) NOT NULL COMMENT '公告标题'", "varchar(256)" };
		ann.m_mapField["f_body"] = { "f_body", "text DEFAULT NULL COMMENT '公告正文'", "text" };
		ann.m_mapField["f_severity"] = { "f_severity", "int(2) NOT NULL DEFAULT 0 COMMENT '级别：0=普通 1=提醒 2=警告'", "int(2)" };
		ann.m_mapField["f_sender_admin_id"] = { "f_sender_admin_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '发布者管理员ID'", "bigint(20)" };
		ann.m_mapField["f_sender_name"] = { "f_sender_name", "varchar(64) DEFAULT NULL COMMENT '发布者名称（冗余）'", "varchar(64)" };
		ann.m_mapField["f_audience_type"] = { "f_audience_type", "int(2) NOT NULL DEFAULT 0 COMMENT '受众类型：0=全部企业 1=指定企业 2=指定部门 3=指定员工 4=平台内部'", "int(2)" };
		ann.m_mapField["f_pinned"] = { "f_pinned", "int(2) NOT NULL DEFAULT 0 COMMENT '是否置顶：0=否 1=是'", "int(2)" };
		ann.m_mapField["f_scheduled"] = { "f_scheduled", "int(2) NOT NULL DEFAULT 0 COMMENT '是否定时发布：0=否 1=是'", "int(2)" };
		ann.m_mapField["f_scheduled_time"] = { "f_scheduled_time", "datetime DEFAULT NULL COMMENT '定时发布时间'", "datetime" };
		ann.m_mapField["f_sent_time"] = { "f_sent_time", "datetime DEFAULT NULL COMMENT '实际发布时间'", "datetime" };
		ann.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '状态：0=草稿 1=已发布 2=已撤回'", "int(2)" };
		ann.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };
		ann.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
		ann.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };

		ann.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_announcement_id (f_announcement_id)";
		m_vecTableInfo.push_back(ann);
	}

	// 13. t_announcement_target —— 公告投放目标表
	{
		STableInfo annTarget;
		annTarget.m_strName = "t_announcement_target";
		annTarget.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		annTarget.m_mapField["f_announcement_id"] = { "f_announcement_id", "bigint(20) NOT NULL COMMENT '公告ID'", "bigint(20)" };
		annTarget.m_mapField["f_target_type"] = { "f_target_type", "int(2) NOT NULL COMMENT '目标类型：1=企业 2=部门 3=员工'", "int(2)" };
		annTarget.m_mapField["f_target_id"] = { "f_target_id", "bigint(20) NOT NULL COMMENT '目标ID（企业ID/部门ID/用户ID）'", "bigint(20)" };
		annTarget.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };

		annTarget.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_announcement (f_announcement_id)";
		m_vecTableInfo.push_back(annTarget);
	}

	// 14. t_announcement_read —— 公告已读回执表
	{
		STableInfo annRead;
		annRead.m_strName = "t_announcement_read";
		annRead.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		annRead.m_mapField["f_announcement_id"] = { "f_announcement_id", "bigint(20) NOT NULL COMMENT '公告ID'", "bigint(20)" };
		annRead.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL COMMENT '已读用户ID'", "bigint(20)" };
		annRead.m_mapField["f_read_time"] = { "f_read_time", "datetime NOT NULL COMMENT '已读时间'", "datetime" };

		annRead.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_announcement_user (f_announcement_id, f_user_id), KEY idx_announcement (f_announcement_id)";
		m_vecTableInfo.push_back(annRead);
	}

	// 15. t_login_record —— 用户登录记录表
	{
		STableInfo loginRec;
		loginRec.m_strName = "t_login_record";
		loginRec.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		loginRec.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL COMMENT '登录的用户ID'", "bigint(20)" };
		loginRec.m_mapField["f_login_time"] = { "f_login_time", "datetime NOT NULL COMMENT '登录时间'", "datetime" };
		loginRec.m_mapField["f_device"] = { "f_device", "varchar(128) DEFAULT NULL COMMENT '设备描述'", "varchar(128)" };
		loginRec.m_mapField["f_device_kind"] = { "f_device_kind", "int(2) NOT NULL DEFAULT 0 COMMENT '设备类型：1=PC 2=Android 3=iOS 4=Web 5=Mac'", "int(2)" };
		loginRec.m_mapField["f_os"] = { "f_os", "varchar(64) DEFAULT NULL COMMENT '操作系统'", "varchar(64)" };
		loginRec.m_mapField["f_ip"] = { "f_ip", "varchar(64) DEFAULT NULL COMMENT '登录IP'", "varchar(64)" };
		loginRec.m_mapField["f_location"] = { "f_location", "varchar(128) DEFAULT NULL COMMENT '登录地理位置'", "varchar(128)" };
		loginRec.m_mapField["f_result"] = { "f_result", "int(2) NOT NULL DEFAULT 0 COMMENT '结果：0=成功 1=失败'", "int(2)" };
		loginRec.m_mapField["f_fail_reason"] = { "f_fail_reason", "varchar(128) DEFAULT NULL COMMENT '失败原因（f_result=1时）'", "varchar(128)" };
		loginRec.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };

		loginRec.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_user (f_user_id), KEY idx_login_time (f_login_time)";
		m_vecTableInfo.push_back(loginRec);
	}

	// 16. t_service_account —— 服务号表
	{
		STableInfo svc;
		svc.m_strName = "t_service_account";
		svc.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		svc.m_mapField["f_service_id"] = { "f_service_id", "bigint(20) NOT NULL COMMENT '服务号业务ID'", "bigint(20)" };
		svc.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL COMMENT '所属企业ID'", "bigint(20)" };
		svc.m_mapField["f_name"] = { "f_name", "varchar(64) NOT NULL COMMENT '服务号名称'", "varchar(64)" };
		svc.m_mapField["f_kind"] = { "f_kind", "int(2) NOT NULL DEFAULT 0 COMMENT '类型：0=通知型 1=助手型'", "int(2)" };
		svc.m_mapField["f_description"] = { "f_description", "varchar(256) DEFAULT NULL COMMENT '服务号描述'", "varchar(256)" };
		svc.m_mapField["f_avatar_color"] = { "f_avatar_color", "varchar(16) DEFAULT NULL COMMENT '头像底色'", "varchar(16)" };
		svc.m_mapField["f_avatar_glyph"] = { "f_avatar_glyph", "varchar(16) DEFAULT NULL COMMENT '头像字形/图标'", "varchar(16)" };
		svc.m_mapField["f_owner_admin_id"] = { "f_owner_admin_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '所有者管理员ID'", "bigint(20)" };
		svc.m_mapField["f_allow_unsubscribe"] = { "f_allow_unsubscribe", "int(2) NOT NULL DEFAULT 1 COMMENT '是否允许员工取消订阅：0=否 1=是'", "int(2)" };
		svc.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '状态：0=正常 1=停用'", "int(2)" };
		svc.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };
		svc.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
		svc.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };

		svc.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_service_id (f_service_id), KEY idx_enterprise (f_enterprise_id)";
		m_vecTableInfo.push_back(svc);
	}

	// 17. t_service_account_binding —— 服务号绑定表
	{
		STableInfo svcBind;
		svcBind.m_strName = "t_service_account_binding";
		svcBind.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		svcBind.m_mapField["f_service_id"] = { "f_service_id", "bigint(20) NOT NULL COMMENT '服务号ID'", "bigint(20)" };
		svcBind.m_mapField["f_bind_type"] = { "f_bind_type", "int(2) NOT NULL COMMENT '绑定类型：0=本企业全员 1=部门 2=员工'", "int(2)" };
		svcBind.m_mapField["f_bind_target_id"] = { "f_bind_target_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '绑定目标ID（部门ID或用户ID；全员时为0）'", "bigint(20)" };
		svcBind.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };

		svcBind.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_service (f_service_id)";
		m_vecTableInfo.push_back(svcBind);
	}

	// 18. 原 t_friend_request（加好友申请表）已于 2026-08-05 随好友关系子系统一并删除。
	//     企业版没有"加好友"这一步，通讯录直接由组织架构给出。编号 18 空出不复用，
	//     免得下次读这段的人以为漏了一张表。

	// 19. t_blacklist —— 黑名单表（拉黑仍在用：发消息 / 转发 / 群发助手都要查它）
	{
		STableInfo bl;
		bl.m_strName = "t_blacklist";
		bl.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '拉黑发起方userid'", "bigint(20)" };
		bl.m_mapField["f_blocked_userid"] = { "f_blocked_userid", "bigint(20) NOT NULL COMMENT '被拉黑方userid'", "bigint(20)" };
		bl.m_mapField["f_create_time"] = { "f_create_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '拉黑时间'", "timestamp" };
		bl.m_strKeyString = "PRIMARY KEY (f_userid, f_blocked_userid)";
		m_vecTableInfo.push_back(bl);
	}

	// 20. t_conversation —— 会话状态表（v2 会话与消息功能；置顶/消息提醒模式/已读位点）
	{
		STableInfo cv;
		cv.m_strName = "t_conversation";
		cv.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '会话归属用户userid'", "bigint(20)" };
		cv.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL DEFAULT 1 COMMENT '会话类型:1单聊 2群聊'", "int(2)" };
		cv.m_mapField["f_cid"] = { "f_cid", "bigint(20) NOT NULL COMMENT '会话ID:单聊为对端userid,群聊为群id'", "bigint(20)" };
		cv.m_mapField["f_pinned"] = { "f_pinned", "int(2) NOT NULL DEFAULT 0 COMMENT '是否置顶:0否 1是'", "int(2)" };
		//消息提醒模式：取代原 f_mute（0/1 两值含义不变，2~4 为新增）
		cv.m_mapField["f_notify_mode"] = { "f_notify_mode", "int(2) NOT NULL DEFAULT 0 COMMENT '消息提醒模式:0允许提醒 1接收不提醒 2收进消息盒子 3屏蔽(仅群聊) 4特别关注 5只提醒@我和关注的人(仅群聊)'", "int(2)" };
		cv.m_mapField["f_readmsgid"] = { "f_readmsgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '已读到的最大消息msgid'", "bigint(20)" };
		cv.m_mapField["f_deleted"] = { "f_deleted", "int(2) NOT NULL DEFAULT 0 COMMENT '会话是否从列表删除:0否 1是'", "int(2)" };
		//删除锚点：本列 NOT NULL 且默认 0，不会为 NULL。
		//MessageManager 读本列时仍套了一层 IFNULL(f_deleted_msgid,-1)，那条兜底分支实际永远走不到，
		//日后清理该处时可一并去掉。
		cv.m_mapField["f_deleted_msgid"] = { "f_deleted_msgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '删除会话时的最后消息msgid锚点'", "bigint(20)" };
		//清空锚点（cmd_clear_conversation）：msgid 不大于本值的消息对本人隐藏；0 表示没有清空过
		cv.m_mapField["f_cleared_msgid"] = { "f_cleared_msgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '本人清空该会话聊天记录时的消息msgid锚点'", "bigint(20)" };
		//手动标为未读（docs/会话标为未读方案.md）
		cv.m_mapField["f_unread_mark"] = { "f_unread_mark", "int(2) NOT NULL DEFAULT 0 COMMENT '本人是否手动标为未读:0否 1是'", "int(2)" };
		cv.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '本行更新时间,Unix毫秒'", "bigint(20)" };
		cv.m_strKeyString = "PRIMARY KEY (f_userid, f_ctype, f_cid)";
		m_vecTableInfo.push_back(cv);
	}

	// 21. t_message_deleted —— 本端消息删除表（v2 会话与消息功能；每用户软删除）
	{
		STableInfo md;
		md.m_strName = "t_message_deleted";
		md.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '执行删除的用户userid'", "bigint(20)" };
		md.m_mapField["f_msgid"] = { "f_msgid", "bigint(20) NOT NULL COMMENT '被该用户删除的消息msgid'", "bigint(20)" };
		md.m_strKeyString = "PRIMARY KEY (f_userid, f_msgid)";
		m_vecTableInfo.push_back(md);
	}

	// 21a. t_message_deleted_range —— 按时间范围清理聊天记录的区间表（cmd_delete_messages_by_time）。
	// 与 t_message_deleted 同为本人视角的软删除，只记区间、不逐条展开，写入量与消息条数无关。
	// 结构以 table_mysql.sql 为准，两处须保持一致。
	{
		STableInfo mdr;
		mdr.m_strName = "t_message_deleted_range";
		mdr.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '执行清理的用户userid'", "bigint(20)" };
		mdr.m_mapField["f_begin_time"] = { "f_begin_time", "bigint(20) NOT NULL COMMENT '区间开始,Unix毫秒(含)'", "bigint(20)" };
		mdr.m_mapField["f_end_time"] = { "f_end_time", "bigint(20) NOT NULL COMMENT '区间结束,Unix毫秒(含)'", "bigint(20)" };
		mdr.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '本行写入时间,Unix毫秒,供排查'", "bigint(20)" };
		mdr.m_strKeyString = "PRIMARY KEY (f_userid, f_begin_time)";
		m_vecTableInfo.push_back(mdr);
	}

	// 21b. t_file_ack —— 文件收取状态表（发送方要知道对方收没收）
	// 独立成表而不往 t_chatmsg 加列：这是消息的附加状态、只有文件消息才有，
	// 与 t_message_deleted 同一思路，不动核心消息表的结构。
	// 主键取 f_msgid 即可 —— 当前只有单聊文件消息，一条消息唯一对应一个接收方；
	// 日后若群文件消息也要这个状态，须把接收方 userid 一并纳入主键。
	{
		STableInfo fa;
		fa.m_strName = "t_file_ack";
		fa.m_mapField["f_msgid"] = { "f_msgid", "bigint(20) NOT NULL COMMENT '文件消息的msgid'", "bigint(20)" };
		fa.m_mapField["f_state"] = { "f_state", "int(2) NOT NULL DEFAULT 0 COMMENT '收取状态:1正在接收 2已接收 3已拒绝'", "int(2)" };
		fa.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '状态更新时间,Unix毫秒'", "bigint(20)" };
		fa.m_strKeyString = "PRIMARY KEY (f_msgid)";
		m_vecTableInfo.push_back(fa);
	}

	// 22. t_group —— 群组表（v2 群组功能；群本体独立于 t_user）
	{
		STableInfo gp;
		gp.m_strName = "t_group";
		gp.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '群ID(高位id空间)'", "bigint(20)" };
		gp.m_mapField["f_name"] = { "f_name", "varchar(64) NOT NULL COMMENT '群名称'", "varchar(64)" };
		gp.m_mapField["f_facetype"] = { "f_facetype", "int(4) NOT NULL DEFAULT 0 COMMENT '群头像类型'", "int(4)" };
		gp.m_mapField["f_customface"] = { "f_customface", "varchar(256) NOT NULL DEFAULT '' COMMENT '群自定义头像文件标识'", "varchar(256)" };
		gp.m_mapField["f_announcement"] = { "f_announcement", "varchar(1024) NOT NULL DEFAULT '' COMMENT '群公告'", "varchar(1024)" };
		gp.m_mapField["f_ownerid"] = { "f_ownerid", "bigint(20) NOT NULL COMMENT '群主userid'", "bigint(20)" };
		gp.m_mapField["f_join_mode"] = { "f_join_mode", "int(2) NOT NULL DEFAULT 1 COMMENT '加群方式:0自由 1需审批 2禁止'", "int(2)" };
		gp.m_mapField["f_mute_all"] = { "f_mute_all", "int(2) NOT NULL DEFAULT 0 COMMENT '全员禁言:0否 1是'", "int(2)" };
		gp.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)'", "bigint(20)" };
		// 以下六列为群管理功能新增。
		//
		// 两个默认值需要特别说明：
		//   f_new_see_history 默认 1：新成员默认能看入群前的历史消息。
		//   f_member_invite 默认 0：普通成员发出的邀请转为待审批申请。取 0 是因为它与界面上
		//   该开关的默认态一致，且收紧权限比放宽安全，群主需要时可以自行打开。
		gp.m_mapField["f_intro"] = { "f_intro", "varchar(512) NOT NULL DEFAULT '' COMMENT '群简介'", "varchar(512)" };
		gp.m_mapField["f_at_all_scope"] = { "f_at_all_scope", "int(2) NOT NULL DEFAULT 0 COMMENT '谁能@全体:0仅群主和管理员 1所有人'", "int(2)" };
		gp.m_mapField["f_new_see_history"] = { "f_new_see_history", "int(2) NOT NULL DEFAULT 1 COMMENT '新成员可看入群前历史消息:0否 1是'", "int(2)" };
		gp.m_mapField["f_member_invite"] = { "f_member_invite", "int(2) NOT NULL DEFAULT 0 COMMENT '普通成员邀请好友免审批:0否(转待审批) 1是(直接入群)'", "int(2)" };
		gp.m_mapField["f_welcome_on"] = { "f_welcome_on", "int(2) NOT NULL DEFAULT 0 COMMENT '入群欢迎语开关:0关 1开'", "int(2)" };
		gp.m_mapField["f_welcome_text"] = { "f_welcome_text", "varchar(256) NOT NULL DEFAULT '' COMMENT '入群欢迎语正文'", "varchar(256)" };
		// 以下三列为群公告功能新增，与既有的 f_announcement 一起构成"本群最新一条
		// 已发布公告"的冗余快照，由服务端在发布 / 撤回后同步刷新（见
		// GroupManager::refreshLatestAnnouncement）。冗余的收益是：所有只读展示
		// （群资料对话框、cmd_get_group_info 应答、群资料变更推送）都不必连
		// t_group_announcement，老客户端也能照常显示最新公告。
		// 三列默认值即"本群还没有公告"。
		gp.m_mapField["f_announce_id"] = { "f_announce_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最新已发布公告的f_id;0=本群还没有公告'", "bigint(20)" };
		gp.m_mapField["f_announce_title"] = { "f_announce_title", "varchar(128) NOT NULL DEFAULT '' COMMENT '最新已发布公告的标题(冗余快照)'", "varchar(128)" };
		gp.m_mapField["f_announce_time"] = { "f_announce_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最新已发布公告的发布时间(Unix毫秒)'", "bigint(20)" };
		// 群名是否由用户设定（1 是 / 0 自动群名，由服务端按成员昵称重算）。
		gp.m_mapField["f_name_custom"] = { "f_name_custom", "int(2) NOT NULL DEFAULT 1 COMMENT '群名是否由用户设定:1是 0否(自动群名)'", "int(2)" };
		gp.m_mapField["f_must_read_id"] = { "f_must_read_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '入群必读公告的f_id;0=本群没有入群必读'", "bigint(20)" };
		// 群类型（docs/帖子群方案.md）：0 普通群、1 帖子群
		gp.m_mapField["f_group_type"] = { "f_group_type", "int(2) NOT NULL DEFAULT 0 COMMENT '群类型:0普通群 1帖子群'", "int(2)" };
		gp.m_strKeyString = "PRIMARY KEY (f_groupid)";
		m_vecTableInfo.push_back(gp);
	}

	// 23. t_group_member —— 群成员表（v2 群组功能；成员+角色+群名片+禁言）
	{
		STableInfo gm;
		gm.m_strName = "t_group_member";
		gm.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '群ID'", "bigint(20)" };
		gm.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '成员userid'", "bigint(20)" };
		gm.m_mapField["f_role"] = { "f_role", "int(2) NOT NULL DEFAULT 0 COMMENT '角色:0成员 1管理员 2群主'", "int(2)" };
		gm.m_mapField["f_card"] = { "f_card", "varchar(64) NOT NULL DEFAULT '' COMMENT '群名片;空则用账号昵称'", "varchar(64)" };
		gm.m_mapField["f_muted"] = { "f_muted", "int(2) NOT NULL DEFAULT 0 COMMENT '是否被禁言:0否 1是'", "int(2)" };
		gm.m_mapField["f_join_time"] = { "f_join_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '入群时间(Unix毫秒)'", "bigint(20)" };
		// 禁言到期时间与原因为群管理功能新增。f_mute_until 与 f_muted 一起表达禁言态：
		// f_muted=1 且 f_mute_until=0 即"永久禁言"。
		gm.m_mapField["f_mute_until"] = { "f_mute_until", "bigint(20) NOT NULL DEFAULT 0 COMMENT '禁言到期时间(Unix毫秒);f_muted=1且本列为0表示永久禁言'", "bigint(20)" };
		gm.m_mapField["f_mute_reason"] = { "f_mute_reason", "varchar(128) NOT NULL DEFAULT '' COMMENT '禁言原因'", "varchar(128)" };
		// 本人可见的最早一条群消息 msgid（docs/邀请入群方案.md §5.3）。默认 0 表示不限
		gm.m_mapField["f_history_from"] = { "f_history_from", "bigint(20) NOT NULL DEFAULT 0 COMMENT '本人可见的最早一条群消息msgid;0不限(入群时按邀请人选择的同步条数算出,群主与管理员不受限)'", "bigint(20)" };
		gm.m_mapField["f_must_read"] = { "f_must_read", "int(2) NOT NULL DEFAULT 0 COMMENT '入群必读:0不需要或已确认 1待确认'", "int(2)" };
		gm.m_strKeyString = "PRIMARY KEY (f_groupid, f_userid), KEY idx_user (f_userid)";
		m_vecTableInfo.push_back(gm);
	}

	// 24. t_group_join_request —— 加群申请表（v2 群组功能；需审批的加群申请）
	{
		STableInfo gj;
		gj.m_strName = "t_group_join_request";
		gj.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '申请ID'", "bigint(20)" };
		gj.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '目标群ID'", "bigint(20)" };
		gj.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '申请人userid'", "bigint(20)" };
		gj.m_mapField["f_verify_msg"] = { "f_verify_msg", "varchar(256) DEFAULT NULL COMMENT '验证留言'", "varchar(256)" };
		gj.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '状态:0待处理 1已通过 2已拒绝'", "int(2)" };
		gj.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '申请时间(Unix毫秒)'", "bigint(20)" };
		gj.m_mapField["f_handle_time"] = { "f_handle_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '处理时间(Unix毫秒,0未处理)'", "bigint(20)" };
		gj.m_mapField["f_handler"] = { "f_handler", "bigint(20) NOT NULL DEFAULT 0 COMMENT '处理人userid(0未处理)'", "bigint(20)" };
		// 来源与推荐人为群管理功能新增，供加群审批页显示"来源：xxx / 推荐人：xxx"。
		// f_source 默认 1（群搜索）。
		gj.m_mapField["f_source"] = { "f_source", "int(2) NOT NULL DEFAULT 1 COMMENT '申请来源:0好友邀请 1群搜索 2扫码加群'", "int(2)" };
		gj.m_mapField["f_recommender"] = { "f_recommender", "bigint(20) NOT NULL DEFAULT 0 COMMENT '推荐人userid(仅好友邀请时非0)'", "bigint(20)" };
		// 通过后可看的入群前消息条数（docs/邀请入群方案.md §3.4）。默认 -1 表示全部
		gj.m_mapField["f_history_limit"] = { "f_history_limit", "int(11) NOT NULL DEFAULT -1 COMMENT '通过后可看的入群前消息条数:-1全部 0不同步 N最近N条(由邀请转来的申请记邀请人的选择)'", "int(11)" };
		// 由入群邀请卡片转来的申请记下邀请 id，审批结果据此回写卡片（docs/邀请入群方案.md §3.4）。默认 0 表示不是
		gj.m_mapField["f_invite_id"] = { "f_invite_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '由入群邀请卡片转来时为t_group_invite.f_id,审批结果据此回写卡片;0表示不是'", "bigint(20)" };
		gj.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_group_status (f_groupid, f_status)";
		m_vecTableInfo.push_back(gj);
	}

	// 24.2 t_group_invite —— 入群邀请表（「邀请入群需被邀请人同意」开启时用；一行=一张入群邀请卡片，
	// 已过期 / 已失效不落库、下发时计算，见 docs/邀请入群方案.md §3.3、§5.1）
	{
		STableInfo gi;
		gi.m_strName = "t_group_invite";
		gi.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '邀请ID'", "bigint(20)" };
		gi.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '目标群ID'", "bigint(20)" };
		gi.m_mapField["f_inviter"] = { "f_inviter", "bigint(20) NOT NULL COMMENT '邀请人userid'", "bigint(20)" };
		gi.m_mapField["f_invitee"] = { "f_invitee", "bigint(20) NOT NULL COMMENT '被邀请人userid'", "bigint(20)" };
		gi.m_mapField["f_history_limit"] = { "f_history_limit", "int(11) NOT NULL DEFAULT -1 COMMENT '入群后可看的入群前消息条数:-1全部 0不同步 N最近N条'", "int(11)" };
		gi.m_mapField["f_state"] = { "f_state", "int(2) NOT NULL DEFAULT 0 COMMENT '状态:0待加入 1已加入 2等待审批 3未通过审批'", "int(2)" };
		gi.m_mapField["f_msgid"] = { "f_msgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '卡片消息msgid(邀请人与被邀请人的单聊)'", "bigint(20)" };
		gi.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '发出时间(Unix毫秒)'", "bigint(20)" };
		gi.m_mapField["f_expire_time"] = { "f_expire_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '到期时间(Unix毫秒),超过后仍待加入的显示为已过期'", "bigint(20)" };
		gi.m_mapField["f_handle_time"] = { "f_handle_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '状态最后变化的时间(Unix毫秒,0未变化)'", "bigint(20)" };
		gi.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_invitee_group (f_invitee, f_groupid)";
		m_vecTableInfo.push_back(gi);
	}

	// 24.1 原为 t_group_sensitive_word（群敏感词库表），随群级敏感词于 2026-10-02 删除，
	// 敏感词改为企业级词库，见 docs/敏感词方案.md。编号留空，不复用。

	// 24.2 t_group_announcement —— 群公告表（v2 群公告功能）
	//
	// 一行 = 某群的一条公告。删除是真删（DELETE FROM）而不是软删 —— 群主 / 管理员
	// 点了删除就是要它彻底消失，留一行"已删除"的记录在库里既没人看、也让"共 N 条"
	// 的口径变得含糊。t_group 上那四列（f_announce_id / f_announce_title /
	// f_announce_time / f_announcement）是本表"最新一条公告"的冗余快照。
	{
		STableInfo ga;
		ga.m_strName = "t_group_announcement";
		ga.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '公告ID'", "bigint(20)" };
		ga.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '所属群ID'", "bigint(20)" };
		ga.m_mapField["f_title"] = { "f_title", "varchar(128) NOT NULL DEFAULT '' COMMENT '公告标题(必填)'", "varchar(128)" };
		ga.m_mapField["f_content"] = { "f_content", "varchar(2048) NOT NULL DEFAULT '' COMMENT '公告正文'", "varchar(2048)" };
		ga.m_mapField["f_severity"] = { "f_severity", "int(2) NOT NULL DEFAULT 0 COMMENT '优先级:0通知 1重要 2紧急'", "int(2)" };
		ga.m_mapField["f_publisher"] = { "f_publisher", "bigint(20) NOT NULL DEFAULT 0 COMMENT '发布人userid'", "bigint(20)" };
		ga.m_mapField["f_publish_time"] = { "f_publish_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '发布时间(Unix毫秒)'", "bigint(20)" };
		ga.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_group_time (f_groupid, f_publish_time)";
		m_vecTableInfo.push_back(ga);
	}

	// 25. t_card_interaction —— 群互动卡片互动表（v2 群互动卡片功能）
	{
		STableInfo ci;
		ci.m_strName = "t_card_interaction";
		ci.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		ci.m_mapField["f_msgid"] = { "f_msgid", "bigint(20) NOT NULL COMMENT '卡片所在消息id'", "bigint(20)" };
		ci.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '互动用户userid'", "bigint(20)" };
		ci.m_mapField["f_data"] = { "f_data", "varchar(1024) NOT NULL DEFAULT '' COMMENT '互动数据:投票为选项下标JSON数组/接龙为接龙文本/待办为勾选标记'", "varchar(1024)" };
		ci.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '互动时间(Unix毫秒)'", "bigint(20)" };
		ci.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_msg_user (f_msgid, f_userid), KEY idx_msgid (f_msgid)";
		m_vecTableInfo.push_back(ci);
	}

	// 26. t_file —— 文件元数据表（v2 文件/图片服务功能；f_id 即对外 fileid，磁盘按 f_md5 命名）
	{
		STableInfo file;
		file.m_strName = "t_file";
		file.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '文件记录ID,即对外fileid'", "bigint(20)" };
		file.m_mapField["f_md5"] = { "f_md5", "char(32) NOT NULL DEFAULT '' COMMENT '文件内容MD5(小写十六进制),秒传与磁盘文件名依据'", "char(32)" };
		file.m_mapField["f_name"] = { "f_name", "varchar(256) NOT NULL DEFAULT '' COMMENT '上传时的原始文件名'", "varchar(256)" };
		file.m_mapField["f_ext"] = { "f_ext", "varchar(32) NOT NULL DEFAULT '' COMMENT '扩展名(不含点)'", "varchar(32)" };
		file.m_mapField["f_size"] = { "f_size", "bigint(20) NOT NULL DEFAULT 0 COMMENT '文件总字节数'", "bigint(20)" };
		file.m_mapField["f_scene"] = { "f_scene", "int(4) NOT NULL DEFAULT 0 COMMENT '用途场景:1聊天文件 2群文件 3头像 4图片消息'", "int(4)" };
		file.m_mapField["f_uploaderid"] = { "f_uploaderid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '上传者用户userid'", "bigint(20)" };
		file.m_mapField["f_status"] = { "f_status", "int(4) NOT NULL DEFAULT 0 COMMENT '状态:0待上传 1已完成'", "int(4)" };
		file.m_mapField["f_createtime"] = { "f_createtime", "bigint(20) NOT NULL DEFAULT 0 COMMENT '记录创建时间(Unix毫秒)'", "bigint(20)" };
		file.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_md5 (f_md5), KEY idx_uploader (f_uploaderid)";
		m_vecTableInfo.push_back(file);
	}

	// 27. t_group_file —— 群文件登记表（v2 群文件功能；一行=某文件登记进某群，f_id 即对外 entryid）
	{
		STableInfo gfile;
		gfile.m_strName = "t_group_file";
		gfile.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '群文件条目ID,即对外entryid'", "bigint(20)" };
		gfile.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '所属群ID'", "bigint(20)" };
		gfile.m_mapField["f_fileid"] = { "f_fileid", "bigint(20) NOT NULL COMMENT '文件ID,引用t_file.f_id'", "bigint(20)" };
		gfile.m_mapField["f_filetype"] = { "f_filetype", "int(4) NOT NULL DEFAULT 6 COMMENT '文件类别:1文档 2图片 3视频 4音频 5压缩包 6其它'", "int(4)" };
		gfile.m_mapField["f_uploaderid"] = { "f_uploaderid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '登记者(上传者)userid'", "bigint(20)" };
		gfile.m_mapField["f_uploadtime"] = { "f_uploadtime", "bigint(20) NOT NULL DEFAULT 0 COMMENT '登记时间(Unix毫秒)'", "bigint(20)" };
		gfile.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_group (f_groupid), KEY idx_fileid (f_fileid)";
		m_vecTableInfo.push_back(gfile);
	}

	// 28. t_favorite —— 「我的收藏」表（收藏对象为联系人或群，随通讯录应答下发）
	//     本表与下面的 t_message_pin 此前一直漏在本清单之外：SQLite 侧由
	//     SqliteSchema 自动建表、跑得好好的，MySQL 侧却要靠手工执行 table_mysql.sql 才有，
	//     换 MySQL 部署时会在第一次收藏 / Pin 时报表不存在。这里补齐，两种后端行为一致。
	{
		STableInfo fav;
		fav.m_strName = "t_favorite";
		fav.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '收藏归属用户userid'", "bigint(20)" };
		fav.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL COMMENT '收藏对象类型:1联系人 2群组'", "int(2)" };
		fav.m_mapField["f_cid"] = { "f_cid", "bigint(20) NOT NULL COMMENT '收藏对象ID:联系人为对端userid,群组为群id'", "bigint(20)" };
		fav.m_mapField["f_order"] = { "f_order", "int(11) NOT NULL DEFAULT 0 COMMENT '手动排序权重,值小者靠前;0表示未手动排序'", "int(11)" };
		fav.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '收藏时间,Unix毫秒;列表按本列倒序'", "bigint(20)" };
		fav.m_strKeyString = "PRIMARY KEY (f_userid, f_ctype, f_cid), KEY idx_favorite_user (f_userid)";
		m_vecTableInfo.push_back(fav);
	}

	// 29. t_message_pin —— 消息 Pin 表（被钉住的消息，一条消息至多一行）
	{
		STableInfo pin;
		pin.m_strName = "t_message_pin";
		pin.m_mapField["f_msgid"] = { "f_msgid", "bigint(20) NOT NULL COMMENT '被Pin的消息msgid'", "bigint(20)" };
		pin.m_mapField["f_operator"] = { "f_operator", "bigint(20) NOT NULL DEFAULT 0 COMMENT '执行Pin操作的userid'", "bigint(20)" };
		pin.m_mapField["f_pin_time"] = { "f_pin_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT 'Pin时间,Unix毫秒'", "bigint(20)" };
		pin.m_strKeyString = "PRIMARY KEY (f_msgid)";
		m_vecTableInfo.push_back(pin);
	}

	// 30. t_client_version —— 客户端版本发布记录表（v2 自动更新功能；一行=某平台的一次版本发布）
	{
		STableInfo ver;
		ver.m_strName = "t_client_version";
		ver.m_mapField["f_id"] = { "f_id", "int(11) NOT NULL AUTO_INCREMENT COMMENT '自增主键'", "int(11)" };
		ver.m_mapField["f_platform"] = { "f_platform", "int(4) NOT NULL DEFAULT 1 COMMENT '客户端平台:1 Windows PC 2 Android(预留)'", "int(4)" };
		ver.m_mapField["f_version"] = { "f_version", "varchar(32) NOT NULL DEFAULT '' COMMENT '本条记录发布的版本号,形如3.3.0'", "varchar(32)" };
		ver.m_mapField["f_min_version"] = { "f_min_version", "varchar(32) NOT NULL DEFAULT '' COMMENT '允许登录的最低版本号,低于它的客户端被拒绝登录并强制升级'", "varchar(32)" };
		ver.m_mapField["f_release_note"] = { "f_release_note", "varchar(2048) NOT NULL DEFAULT '' COMMENT '更新说明,展示在升级窗里'", "varchar(2048)" };
		ver.m_mapField["f_package_md5"] = { "f_package_md5", "varchar(32) NOT NULL DEFAULT '' COMMENT '升级包内容MD5(小写十六进制32位),同时是它在filecache里的文件名'", "varchar(32)" };
		ver.m_mapField["f_package_size"] = { "f_package_size", "bigint(20) NOT NULL DEFAULT 0 COMMENT '升级包字节数'", "bigint(20)" };
		ver.m_mapField["f_package_name"] = { "f_package_name", "varchar(255) NOT NULL DEFAULT '' COMMENT '升级包原始文件名,仅用于界面显示与本地落地命名'", "varchar(255)" };
		ver.m_mapField["f_forced"] = { "f_forced", "int(4) NOT NULL DEFAULT 0 COMMENT '是否强制升级:0否 1是。为1时客户端升级窗不给稍后出口、也不放行登录;与f_min_version相互独立,任一命中即按强制处理'", "int(4)" };
		ver.m_mapField["f_enabled"] = { "f_enabled", "int(4) NOT NULL DEFAULT 0 COMMENT '是否已发布:0草稿不下发 1已发布'", "int(4)" };
		ver.m_mapField["f_publish_time"] = { "f_publish_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '发布时间(Unix毫秒)'", "bigint(20)" };
		ver.m_mapField["f_creator_adminid"] = { "f_creator_adminid", "int(11) NOT NULL DEFAULT 0 COMMENT '发布人的管理员ID,引用t_admin.f_admin_id;0表示非控制台创建'", "int(11)" };
		ver.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '记录创建时间(Unix毫秒),f_publish_time回滚时会被刷新而本列不变'", "bigint(20)" };
		ver.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_platform_enabled (f_platform, f_enabled)";
		m_vecTableInfo.push_back(ver);
	}

	// 31. t_contact_tag —— 联系人标签表（企业版；一行=某人给某个联系人贴的一个标签）
	//     与上面第 2 条的 t_contact_remark 是同族的两张表：都是单向的、只影响"本人看对方"
	//     这一侧的显示。标签进主键是为了让"同一个联系人下不重复贴同一个标签"由主键保证，
	//     不必在代码里再查一遍。列宽 varchar(16) 与 ContactTagManager 的字节上限校验对应。
	{
		STableInfo tag;
		tag.m_strName = "t_contact_tag";
		tag.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '标签归属用户userid（谁贴的）'", "bigint(20)" };
		tag.m_mapField["f_target_userid"] = { "f_target_userid", "bigint(20) NOT NULL COMMENT '被贴标签的联系人userid（贴给谁）'", "bigint(20)" };
		tag.m_mapField["f_tag"] = { "f_tag", "varchar(16) NOT NULL DEFAULT '' COMMENT '标签文本，去掉首尾空白后的内容'", "varchar(16)" };
		tag.m_mapField["f_order"] = { "f_order", "int(10) NOT NULL DEFAULT 0 COMMENT '同一联系人下的显示顺序，值小者靠前，从0起算'", "int(10)" };
		tag.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最近一次整份覆盖的时间(Unix毫秒)'", "bigint(20)" };

		tag.m_strKeyString = "PRIMARY KEY (f_userid, f_target_userid, f_tag), KEY idx_user (f_userid)";
		m_vecTableInfo.push_back(tag);
	}

	// 32. t_message_audience —— 消息可见范围表（文件共享功能引入，机制与业务无关；一行=某条消息对某个人可见）
	//     只对 t_chatmsg.f_audience=1 的消息生效，过滤逻辑在 MessageManager 的 visibleToUserClause。
	//     定义须与 table_mysql.sql 同名表逐列一致（SQLite 侧由 tools/gen_table_sqlite.py 从 table_mysql.sql 生成）。
	{
		STableInfo audience;
		audience.m_strName = "t_message_audience";
		audience.m_mapField["f_msgid"] = { "f_msgid", "bigint(20) NOT NULL COMMENT '消息ID,引用t_chatmsg.f_msgid'", "bigint(20)" };
		audience.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL COMMENT '可以看到这条消息的用户userid'", "bigint(20)" };
		audience.m_strKeyString = "PRIMARY KEY (f_msgid, f_user_id), KEY idx_user (f_user_id)";
		m_vecTableInfo.push_back(audience);
	}

	// 33. t_file_share —— 文件共享记录表（一行=一个共享；只在被主动取消时置为已取消，记录不删）
	//     f_shareid 自增且不复用：共享卡片里引用的就是它。uk_owner_client 供「发起共享」重发去重。
	//     方案见 docs/文件共享方案.md 6.3。
	{
		STableInfo share;
		share.m_strName = "t_file_share";
		share.m_mapField["f_shareid"] = { "f_shareid", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '共享ID(全局自增,删除后不复用)'", "bigint(20)" };
		share.m_mapField["f_client_share_id"] = { "f_client_share_id", "varchar(64) NOT NULL DEFAULT '' COMMENT '客户端生成的UUID,与f_owner_id组成唯一键,发起共享请求重发时据此去重'", "varchar(64)" };
		share.m_mapField["f_owner_id"] = { "f_owner_id", "bigint(20) NOT NULL COMMENT '分享者userid'", "bigint(20)" };
		share.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '分享者所属企业ID(冗余一份,供后台按企业查询)'", "bigint(20)" };
		share.m_mapField["f_device_id"] = { "f_device_id", "varchar(64) NOT NULL DEFAULT '' COMMENT '发起共享时分享者的设备标识;共享只由这台电脑提供'", "varchar(64)" };
		share.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL DEFAULT 1 COMMENT '所在会话类型:1单聊 2群聊'", "int(2)" };
		share.m_mapField["f_cid"] = { "f_cid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '所在会话:单聊为对端userid(分享者视角),群聊为群id'", "bigint(20)" };
		share.m_mapField["f_kind"] = { "f_kind", "int(2) NOT NULL DEFAULT 2 COMMENT '共享类型:1磁盘 2文件夹 3单个文件,取值见Protocol.h的share_kind'", "int(2)" };
		share.m_mapField["f_name"] = { "f_name", "varchar(260) NOT NULL DEFAULT '' COMMENT '显示名:共享根的最后一级名字或磁盘卷标'", "varchar(260)" };
		share.m_mapField["f_display_path"] = { "f_display_path", "varchar(1024) NOT NULL DEFAULT '' COMMENT '分享者发起时的显示路径,仅供显示,客户端不据此提供文件'", "varchar(1024)" };
		share.m_mapField["f_include_hidden"] = { "f_include_hidden", "int(2) NOT NULL DEFAULT 0 COMMENT '是否包含隐藏文件与系统文件:0否 1是'", "int(2)" };
		share.m_mapField["f_state"] = { "f_state", "int(2) NOT NULL DEFAULT 1 COMMENT '状态:1有效 2已取消'", "int(2)" };
		share.m_mapField["f_cancel_reason"] = { "f_cancel_reason", "int(4) NOT NULL DEFAULT 0 COMMENT '取消原因,取值见Protocol.h的share_invalid_reason(1~5);未取消为0'", "int(4)" };
		share.m_mapField["f_canceled_by"] = { "f_canceled_by", "bigint(20) NOT NULL DEFAULT 0 COMMENT '取消操作人:后台强制取消时为管理员ID,其余为用户userid;未取消为0'", "bigint(20)" };
		share.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '发起时间(Unix毫秒)'", "bigint(20)" };
		share.m_mapField["f_cancel_time"] = { "f_cancel_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '取消时间(Unix毫秒);未取消为0'", "bigint(20)" };
		share.m_strKeyString = "PRIMARY KEY (f_shareid), UNIQUE KEY uk_owner_client (f_owner_id, f_client_share_id), "
		                       "KEY idx_owner (f_owner_id, f_state), KEY idx_conv (f_ctype, f_cid, f_state), "
		                       "KEY idx_enterprise (f_enterprise_id, f_state)";
		m_vecTableInfo.push_back(share);
	}

	// 34. t_file_share_member —— 文件共享成员表（一行=某个共享的一名成员及其写权限；被移出的成员直接删行）
	{
		STableInfo member;
		member.m_strName = "t_file_share_member";
		member.m_mapField["f_shareid"] = { "f_shareid", "bigint(20) NOT NULL COMMENT '共享ID,引用t_file_share.f_shareid'", "bigint(20)" };
		member.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL COMMENT '成员userid'", "bigint(20)" };
		member.m_mapField["f_perms"] = { "f_perms", "int(4) NOT NULL DEFAULT 0 COMMENT '写权限位:1上传 2新建文件夹 4重命名 8删除 16移动,取值见Protocol.h的share_perm'", "int(4)" };
		member.m_mapField["f_add_time"] = { "f_add_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '加入时间(Unix毫秒)'", "bigint(20)" };
		member.m_strKeyString = "PRIMARY KEY (f_shareid, f_user_id), KEY idx_user (f_user_id)";
		m_vecTableInfo.push_back(member);
	}

	// 35. t_file_share_audit —— 文件共享审计表（一行=一次被记录的动作；来源分服务端记录与分享者客户端上报）
	{
		STableInfo audit;
		audit.m_strName = "t_file_share_audit";
		audit.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增主键'", "bigint(20)" };
		audit.m_mapField["f_shareid"] = { "f_shareid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '共享ID,引用t_file_share.f_shareid'", "bigint(20)" };
		audit.m_mapField["f_owner_id"] = { "f_owner_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '分享者userid(冗余一份,便于按分享者查询)'", "bigint(20)" };
		audit.m_mapField["f_actor_id"] = { "f_actor_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '操作人:访问者、分享者的userid,或后台管理员ID'", "bigint(20)" };
		audit.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '分享者所属企业ID,后台按企业查询用'", "bigint(20)" };
		audit.m_mapField["f_action"] = { "f_action", "int(4) NOT NULL DEFAULT 0 COMMENT '动作,取值见Protocol.h的share_audit_action'", "int(4)" };
		audit.m_mapField["f_source"] = { "f_source", "int(2) NOT NULL DEFAULT 1 COMMENT '来源:1服务端记录 2分享者客户端上报,取值见share_audit_source'", "int(2)" };
		audit.m_mapField["f_path"] = { "f_path", "varchar(1024) NOT NULL DEFAULT '' COMMENT '相对共享根的路径(UTF-8,/分隔);申请传输时为条目清单的摘要'", "varchar(1024)" };
		audit.m_mapField["f_to_path"] = { "f_to_path", "varchar(1024) NOT NULL DEFAULT '' COMMENT '重命名后的新名字或移动的目标目录;其余动作为空'", "varchar(1024)" };
		audit.m_mapField["f_size"] = { "f_size", "bigint(20) NOT NULL DEFAULT 0 COMMENT '涉及的字节数;没有字节数的动作为0'", "bigint(20)" };
		audit.m_mapField["f_result"] = { "f_result", "int(4) NOT NULL DEFAULT 0 COMMENT '结果:0成功,否则为错误码(error_code_share_*等)'", "int(4)" };
		audit.m_mapField["f_access_id"] = { "f_access_id", "varchar(64) NOT NULL DEFAULT '' COMMENT '所属访问会话ID(内存中的会话标识,便于把同一次访问的记录归到一起)'", "varchar(64)" };
		audit.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '记录时间(Unix毫秒,服务端时间)'", "bigint(20)" };
		audit.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_enterprise_time (f_enterprise_id, f_create_time), "
		                       "KEY idx_actor_time (f_actor_id, f_create_time), KEY idx_share (f_shareid)";
		m_vecTableInfo.push_back(audit);
	}

	// 36. t_system_setting —— 按企业的功能设置表（一行=某企业的某项功能设置；没有记录时取该功能的内置默认值）
	//     目前有 fileshare.enabled 与 p2p.enabled 两个键（'0' / '1'），由 flamingoAdmin 的企业设置写入。
	{
		STableInfo setting;
		setting.m_strName = "t_system_setting";
		setting.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '企业ID'", "bigint(20)" };
		setting.m_mapField["f_key"] = { "f_key", "varchar(64) NOT NULL COMMENT '设置项名,如fileshare.enabled'", "varchar(64)" };
		setting.m_mapField["f_value"] = { "f_value", "varchar(1024) NOT NULL DEFAULT '' COMMENT '设置值(文本)'", "varchar(1024)" };
		setting.m_mapField["f_updated_by"] = { "f_updated_by", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改的管理员ID'", "bigint(20)" };
		setting.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)'", "bigint(20)" };
		setting.m_strKeyString = "PRIMARY KEY (f_enterprise_id, f_key)";
		m_vecTableInfo.push_back(setting);
	}

	// 37. t_card_state —— 群互动卡片结束状态表（群接龙功能；一行=一张被提前结束的卡片，没有记录表示从未被提前结束）
	//     f_msgid 是主键，两人同时结束同一张卡片时只有先写入的那一次生效（INSERT IGNORE）。
	{
		STableInfo cs;
		cs.m_strName = "t_card_state";
		cs.m_mapField["f_msgid"] = { "f_msgid", "bigint(20) NOT NULL COMMENT '卡片所在消息id'", "bigint(20)" };
		cs.m_mapField["f_closed"] = { "f_closed", "int(4) NOT NULL DEFAULT 0 COMMENT '是否已被提前结束:1是'", "int(4)" };
		cs.m_mapField["f_closer"] = { "f_closer", "bigint(20) NOT NULL DEFAULT 0 COMMENT '结束操作者userid'", "bigint(20)" };
		cs.m_mapField["f_close_time"] = { "f_close_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '结束时间(Unix毫秒)'", "bigint(20)" };
		cs.m_strKeyString = "PRIMARY KEY (f_msgid)";
		m_vecTableInfo.push_back(cs);
	}

	// 38. t_sensitive_word —— 企业敏感词库（企业级敏感词功能；一行=某企业的一条词条，由管理端维护）
	//     f_word_norm 存归一化后的形式（统一大小写与全半角、去掉空白符号与表情），判重与构建匹配器都用它；
	//     (f_enterprise_id, f_word_norm) 唯一，保证同一企业里不会有归一化后相同的两条。
	{
		STableInfo sw;
		sw.m_strName = "t_sensitive_word";
		sw.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '词条ID'", "bigint(20)" };
		sw.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL COMMENT '所属企业ID'", "bigint(20)" };
		sw.m_mapField["f_word"] = { "f_word", "varchar(32) NOT NULL COMMENT '词条原文（去掉首尾空白，最多32个字符）'", "varchar(32)" };
		sw.m_mapField["f_word_norm"] = { "f_word_norm", "varchar(32) NOT NULL COMMENT '归一化后的词条，用于判重与构建匹配器'", "varchar(32)" };
		sw.m_mapField["f_category"] = { "f_category", "int(2) NOT NULL DEFAULT 6 COMMENT '分类：1违法违规 2色情低俗 3攻击辱骂 4商业机密 5广告引流 6其他'", "int(2)" };
		sw.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };
		sw.m_mapField["f_creator_id"] = { "f_creator_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '添加者管理员ID'", "bigint(20)" };
		sw.m_mapField["f_creator_name"] = { "f_creator_name", "varchar(64) DEFAULT NULL COMMENT '添加者姓名'", "varchar(64)" };
		sw.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '添加时间(Unix毫秒)'", "bigint(20)" };
		sw.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)'", "bigint(20)" };
		sw.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uk_ent_norm (f_enterprise_id, f_word_norm), KEY idx_enterprise (f_enterprise_id)";
		m_vecTableInfo.push_back(sw);
	}

	// 39. t_sensitive_hit_log —— 敏感词拦截记录（企业级敏感词功能；一行=一次拦截，保留 180 天，由 chatserver 定时清理）
	//     名称与内容摘要里的 4 字节字符写库前替换为 U+FFFD，因为本表的 utf8 列存不下。
	{
		STableInfo sh;
		sh.m_strName = "t_sensitive_hit_log";
		sh.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID'", "bigint(20)" };
		sh.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL COMMENT '提交者所属企业ID'", "bigint(20)" };
		sh.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '提交者userid；自助注册时尚无账号，为0'", "bigint(20)" };
		sh.m_mapField["f_user_name"] = { "f_user_name", "varchar(64) DEFAULT NULL COMMENT '提交者账号或昵称快照'", "varchar(64)" };
		sh.m_mapField["f_source"] = { "f_source", "int(2) NOT NULL COMMENT '来源：见 SensitiveWordDefs.h 的 sensitive_hit_source'", "int(2)" };
		sh.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL DEFAULT 0 COMMENT '会话类型：0与会话无关 1单聊 2群聊'", "int(2)" };
		sh.m_mapField["f_cid"] = { "f_cid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '会话ID：单聊为对方userid，群聊为群ID'", "bigint(20)" };
		sh.m_mapField["f_target_name"] = { "f_target_name", "varchar(64) DEFAULT NULL COMMENT '会话名称快照'", "varchar(64)" };
		sh.m_mapField["f_word_id"] = { "f_word_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '命中的词条ID'", "bigint(20)" };
		sh.m_mapField["f_word"] = { "f_word", "varchar(32) NOT NULL COMMENT '命中的词条原文快照'", "varchar(32)" };
		sh.m_mapField["f_category"] = { "f_category", "int(2) NOT NULL DEFAULT 6 COMMENT '命中词条的分类快照'", "int(2)" };
		sh.m_mapField["f_snippet"] = { "f_snippet", "varchar(128) DEFAULT NULL COMMENT '内容摘要：命中位置前后各20个字符'", "varchar(128)" };
		sh.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL COMMENT '拦截时间(Unix毫秒)'", "bigint(20)" };
		sh.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_ent_time (f_enterprise_id, f_create_time), KEY idx_create_time (f_create_time)";
		m_vecTableInfo.push_back(sh);
	}

	// 40. t_memo —— 备忘表（备忘与提醒功能；一行=一条个人备忘或群备忘，f_start 为本地时间 yyyymmddhhmm）
	{
		STableInfo mm;
		mm.m_strName = "t_memo";
		mm.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '备忘ID'", "bigint(20)" };
		mm.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL DEFAULT 1 COMMENT '所属:1个人 2群'", "int(2)" };
		mm.m_mapField["f_owner"] = { "f_owner", "bigint(20) NOT NULL COMMENT '个人备忘为本人userid,群备忘为群id'", "bigint(20)" };
		mm.m_mapField["f_creator"] = { "f_creator", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建人userid'", "bigint(20)" };
		mm.m_mapField["f_editor"] = { "f_editor", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改人userid'", "bigint(20)" };
		mm.m_mapField["f_title"] = { "f_title", "varchar(256) NOT NULL DEFAULT '' COMMENT '标题(1~50字)'", "varchar(256)" };
		mm.m_mapField["f_note"] = { "f_note", "varchar(1024) NOT NULL DEFAULT '' COMMENT '备注(0~200字)'", "varchar(1024)" };
		mm.m_mapField["f_start"] = { "f_start", "bigint(20) NOT NULL COMMENT '首次事件的本地时间,十进制yyyymmddhhmm'", "bigint(20)" };
		mm.m_mapField["f_advance"] = { "f_advance", "int(11) NOT NULL DEFAULT 0 COMMENT '提前分钟数0~1440'", "int(11)" };
		mm.m_mapField["f_repeat"] = { "f_repeat", "int(2) NOT NULL DEFAULT 0 COMMENT '重复方式,见memo_repeat_type'", "int(2)" };
		mm.m_mapField["f_weekdays"] = { "f_weekdays", "int(4) NOT NULL DEFAULT 0 COMMENT '每周重复的星期掩码bit0周一..bit6周日'", "int(4)" };
		mm.m_mapField["f_count"] = { "f_count", "int(11) NOT NULL DEFAULT 1 COMMENT '提醒次数上限,0为永不结束'", "int(11)" };
		mm.m_mapField["f_style"] = { "f_style", "int(2) NOT NULL DEFAULT 1 COMMENT '提醒方式:1右下角弹窗 2全屏动画'", "int(2)" };
		mm.m_mapField["f_target_all"] = { "f_target_all", "int(2) NOT NULL DEFAULT 0 COMMENT '群备忘是否提醒全体成员'", "int(2)" };
		mm.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)'", "bigint(20)" };
		mm.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)'", "bigint(20)" };
		mm.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_owner (f_ctype, f_owner)";
		m_vecTableInfo.push_back(mm);
	}

	// 41. t_memo_target —— 备忘提醒对象表（一行=某条群备忘指定提醒的一位成员；提醒全体成员的群备忘没有记录）
	{
		STableInfo mt;
		mt.m_strName = "t_memo_target";
		mt.m_mapField["f_memoid"] = { "f_memoid", "bigint(20) NOT NULL COMMENT '备忘ID'", "bigint(20)" };
		mt.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '被提醒成员userid'", "bigint(20)" };
		mt.m_strKeyString = "PRIMARY KEY (f_memoid, f_userid), KEY idx_user (f_userid)";
		m_vecTableInfo.push_back(mt);
	}

	// 41-1. t_group_follow —— 群里关注的成员（一行=某用户在某个群里关注了某位成员的发言，docs/消息提醒模式与消息盒子方案.md §12）
	{
		STableInfo gf;
		gf.m_strName = "t_group_follow";
		gf.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '关注发起人userid'", "bigint(20)" };
		gf.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '群ID'", "bigint(20)" };
		gf.m_mapField["f_target_userid"] = { "f_target_userid", "bigint(20) NOT NULL COMMENT '被关注的成员userid'", "bigint(20)" };
		gf.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '关注时间(Unix毫秒)'", "bigint(20)" };
		gf.m_strKeyString = "PRIMARY KEY (f_userid, f_groupid, f_target_userid)";
		m_vecTableInfo.push_back(gf);
	}

	// 41-2. t_group_qa —— 群问答（一行=本群的一条问答，docs/群问答方案.md）
	{
		STableInfo qa;
		qa.m_strName = "t_group_qa";
		qa.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '问答ID'", "bigint(20)" };
		qa.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '所属群ID'", "bigint(20)" };
		qa.m_mapField["f_question"] = { "f_question", "varchar(128) NOT NULL DEFAULT '' COMMENT '问题'", "varchar(128)" };
		qa.m_mapField["f_answer"] = { "f_answer", "varchar(2048) NOT NULL DEFAULT '' COMMENT '答案(纯文字)'", "varchar(2048)" };
		qa.m_mapField["f_editor"] = { "f_editor", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改人userid'", "bigint(20)" };
		qa.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)'", "bigint(20)" };
		qa.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)'", "bigint(20)" };
		qa.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_group (f_groupid)";
		m_vecTableInfo.push_back(qa);
	}

	// 41-3. t_enterprise_qa —— 企业问答（flamingoAdmin 维护，应用到本企业全部群或所选的群，docs/群问答方案.md）
	{
		STableInfo eq;
		eq.m_strName = "t_enterprise_qa";
		eq.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '问答ID'", "bigint(20)" };
		eq.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL COMMENT '所属企业ID'", "bigint(20)" };
		eq.m_mapField["f_question"] = { "f_question", "varchar(128) NOT NULL DEFAULT '' COMMENT '问题'", "varchar(128)" };
		eq.m_mapField["f_answer"] = { "f_answer", "varchar(2048) NOT NULL DEFAULT '' COMMENT '答案(纯文字)'", "varchar(2048)" };
		eq.m_mapField["f_scope"] = { "f_scope", "int(2) NOT NULL DEFAULT 0 COMMENT '应用范围:0本企业全部群 1所选的群'", "int(2)" };
		eq.m_mapField["f_creator_id"] = { "f_creator_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改的管理员ID'", "bigint(20)" };
		eq.m_mapField["f_creator_name"] = { "f_creator_name", "varchar(64) NOT NULL DEFAULT '' COMMENT '最后修改的管理员姓名'", "varchar(64)" };
		eq.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)'", "bigint(20)" };
		eq.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)'", "bigint(20)" };
		eq.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_enterprise (f_enterprise_id)";
		m_vecTableInfo.push_back(eq);
	}

	// 41-4. t_enterprise_qa_group —— 企业问答应用到的群（f_scope=1 时才有行）
	{
		STableInfo eg;
		eg.m_strName = "t_enterprise_qa_group";
		eg.m_mapField["f_qa_id"] = { "f_qa_id", "bigint(20) NOT NULL COMMENT '企业问答ID'", "bigint(20)" };
		eg.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '群ID'", "bigint(20)" };
		eg.m_strKeyString = "PRIMARY KEY (f_qa_id, f_groupid), KEY idx_group (f_groupid)";
		m_vecTableInfo.push_back(eg);
	}

	// 41-5. t_group_link —— 加群链接（一行=一条 flamingo://joingroup?code= 链接，docs/邀请入群方案.md §12）
	{
		STableInfo gl;
		gl.m_strName = "t_group_link";
		gl.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '链接ID'", "bigint(20)" };
		gl.m_mapField["f_code"] = { "f_code", "varchar(32) NOT NULL COMMENT '随机码(12位,全局唯一)'", "varchar(32)" };
		gl.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '群ID'", "bigint(20)" };
		gl.m_mapField["f_creator"] = { "f_creator", "bigint(20) NOT NULL COMMENT '生成者userid'", "bigint(20)" };
		gl.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '生成者所属企业ID'", "bigint(20)" };
		gl.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '生成时间(Unix毫秒)'", "bigint(20)" };
		gl.m_mapField["f_expire_time"] = { "f_expire_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '到期时间(Unix毫秒);0表示永久有效'", "bigint(20)" };
		gl.m_mapField["f_revoked"] = { "f_revoked", "int(2) NOT NULL DEFAULT 0 COMMENT '是否已作废:0否 1是'", "int(2)" };
		gl.m_strKeyString = "PRIMARY KEY (f_id), UNIQUE KEY uniq_code (f_code), KEY idx_group (f_groupid)";
		m_vecTableInfo.push_back(gl);
	}

	// 42. t_cloud_record —— 云端录像表（屏幕共享与录制方案 12.6；一行=一条云端录像，文件在 imgserver 的 cloudrecorddir）
	{
		STableInfo cr;
		cr.m_strName = "t_cloud_record";
		cr.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '录像ID'", "bigint(20)" };
		cr.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '开启者所属企业ID(配额按企业计)'", "bigint(20)" };
		cr.m_mapField["f_owner"] = { "f_owner", "bigint(20) NOT NULL COMMENT '开启者userid(录像归他)'", "bigint(20)" };
		cr.m_mapField["f_source"] = { "f_source", "int(2) NOT NULL DEFAULT 1 COMMENT '来源:1共享中的云端录制 2独立录制上传'", "int(2)" };
		cr.m_mapField["f_roomid"] = { "f_roomid", "varchar(64) NOT NULL DEFAULT '' COMMENT '屏幕共享房间号(来源为共享时)'", "varchar(64)" };
		cr.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL DEFAULT 0 COMMENT '共享所在会话类型:1单聊 2群聊;来源为上传时为0'", "int(2)" };
		cr.m_mapField["f_cid"] = { "f_cid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '共享所在会话ID:单聊为对方userid(以开启者为准),群聊为群ID'", "bigint(20)" };
		cr.m_mapField["f_title"] = { "f_title", "varchar(256) NOT NULL DEFAULT '' COMMENT '标题(默认取共享标题或文件名,开启者可改)'", "varchar(256)" };
		cr.m_mapField["f_start_time"] = { "f_start_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '开始录制时间(Unix毫秒)'", "bigint(20)" };
		cr.m_mapField["f_end_time"] = { "f_end_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '结束录制时间(Unix毫秒);录制中为0'", "bigint(20)" };
		cr.m_mapField["f_duration_ms"] = { "f_duration_ms", "bigint(20) NOT NULL DEFAULT 0 COMMENT '时长(毫秒,不含暂停)'", "bigint(20)" };
		cr.m_mapField["f_frec_bytes"] = { "f_frec_bytes", "bigint(20) NOT NULL DEFAULT 0 COMMENT '原始帧文件字节数;已删除或来源为上传时为0'", "bigint(20)" };
		cr.m_mapField["f_bytes"] = { "f_bytes", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最终文件(MP4/m4a)字节数;尚未上传时为0'", "bigint(20)" };
		cr.m_mapField["f_audio_only"] = { "f_audio_only", "int(2) NOT NULL DEFAULT 0 COMMENT '是否只有声音(m4a):1是'", "int(2)" };
		cr.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 1 COMMENT '状态,取值见Protocol.h的cloudrecord_status'", "int(2)" };
		cr.m_mapField["f_frec_file"] = { "f_frec_file", "varchar(64) NOT NULL DEFAULT '' COMMENT '原始帧文件名(rec_<id>.frec);已删除或没有时为空'", "varchar(64)" };
		cr.m_mapField["f_file"] = { "f_file", "varchar(64) NOT NULL DEFAULT '' COMMENT '最终文件名(rec_<id>.mp4或.m4a);尚未上传时为空'", "varchar(64)" };
		cr.m_mapField["f_expire_time"] = { "f_expire_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '到期时间(Unix毫秒);0为永久保留'", "bigint(20)" };
		cr.m_mapField["f_warned"] = { "f_warned", "int(2) NOT NULL DEFAULT 0 COMMENT '是否已推送到期提醒:1是'", "int(2)" };
		cr.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)'", "bigint(20)" };
		cr.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_owner (f_owner, f_create_time), KEY idx_enterprise (f_enterprise_id), "
		                    "KEY idx_expire (f_expire_time), KEY idx_room (f_roomid)";
		m_vecTableInfo.push_back(cr);
	}

	// 43. t_cloud_record_share —— 云端录像的分享记录（一行=某条录像发到过某个会话；会话成员凭此观看与下载）
	{
		STableInfo cs;
		cs.m_strName = "t_cloud_record_share";
		cs.m_mapField["f_recordid"] = { "f_recordid", "bigint(20) NOT NULL COMMENT '录像ID'", "bigint(20)" };
		cs.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL COMMENT '会话类型:1单聊 2群聊'", "int(2)" };
		cs.m_mapField["f_cid"] = { "f_cid", "bigint(20) NOT NULL COMMENT '会话ID:单聊为接收方userid,群聊为群ID'", "bigint(20)" };
		cs.m_mapField["f_sender"] = { "f_sender", "bigint(20) NOT NULL DEFAULT 0 COMMENT '发送人userid'", "bigint(20)" };
		cs.m_mapField["f_send_time"] = { "f_send_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最近一次发送时间(Unix毫秒)'", "bigint(20)" };
		cs.m_strKeyString = "PRIMARY KEY (f_recordid, f_ctype, f_cid), KEY idx_conv (f_ctype, f_cid)";
		m_vecTableInfo.push_back(cs);
	}

	// 44. t_thread_post —— 帖子群的帖子（一行=一条帖子，内容 JSON 放 blob 以容纳 4 字节字符，docs/帖子群方案.md 第 5 节）
	{
		STableInfo tp;
		tp.m_strName = "t_thread_post";
		tp.m_mapField["f_postid"] = { "f_postid", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '帖子ID'", "bigint(20)" };
		tp.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '所属帖子群ID'", "bigint(20)" };
		tp.m_mapField["f_author"] = { "f_author", "bigint(20) NOT NULL COMMENT '作者userid'", "bigint(20)" };
		tp.m_mapField["f_client_postid"] = { "f_client_postid", "varchar(64) NOT NULL DEFAULT '' COMMENT '客户端生成的发帖去重标识'", "varchar(64)" };
		tp.m_mapField["f_content"] = { "f_content", "blob COMMENT '帖子内容JSON:title/text/images/mentions'", "blob" };
		tp.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '发帖时间(Unix毫秒)'", "bigint(20)" };
		tp.m_mapField["f_edit_time"] = { "f_edit_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)'", "bigint(20)" };
		tp.m_mapField["f_active_time"] = { "f_active_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最新动态时间(Unix毫秒)'", "bigint(20)" };
		tp.m_mapField["f_comment_count"] = { "f_comment_count", "int(11) NOT NULL DEFAULT 0 COMMENT '未删除的留言与回复总数'", "int(11)" };
		tp.m_mapField["f_pin_time"] = { "f_pin_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '置顶时间(Unix毫秒);0未置顶'", "bigint(20)" };
		tp.m_mapField["f_cardmsgid"] = { "f_cardmsgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '帖子卡片msgid'", "bigint(20)" };
		tp.m_strKeyString = "PRIMARY KEY (f_postid), KEY idx_group_active (f_groupid, f_active_time), KEY idx_group_post (f_groupid, f_postid), KEY idx_author_client (f_author, f_client_postid)";
		m_vecTableInfo.push_back(tp);
	}

	// 45. t_thread_addendum —— 帖子群的楼主补充（一行=楼主在帖子正文下追加的一段补充）
	{
		STableInfo ta;
		ta.m_strName = "t_thread_addendum";
		ta.m_mapField["f_addid"] = { "f_addid", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '补充ID'", "bigint(20)" };
		ta.m_mapField["f_postid"] = { "f_postid", "bigint(20) NOT NULL COMMENT '所属帖子ID'", "bigint(20)" };
		ta.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '所属帖子群ID'", "bigint(20)" };
		ta.m_mapField["f_author"] = { "f_author", "bigint(20) NOT NULL COMMENT '补充人userid'", "bigint(20)" };
		ta.m_mapField["f_content"] = { "f_content", "blob COMMENT '补充内容JSON:text/images/mentions'", "blob" };
		ta.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '补充时间(Unix毫秒)'", "bigint(20)" };
		//动态卡片 msgid：删除补充或删帖时据此改写卡片（docs/帖子群方案.md 14.6）
		ta.m_mapField["f_cardmsgid"] = { "f_cardmsgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '动态卡片msgid'", "bigint(20)" };
		ta.m_strKeyString = "PRIMARY KEY (f_addid), KEY idx_post (f_postid, f_addid), KEY idx_group (f_groupid)";
		m_vecTableInfo.push_back(ta);
	}

	// 46. t_thread_comment —— 帖子群的留言与回复（f_parentid 为 0 是留言，否则是对那条留言的回复）
	{
		STableInfo tc;
		tc.m_strName = "t_thread_comment";
		tc.m_mapField["f_commentid"] = { "f_commentid", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '留言或回复ID'", "bigint(20)" };
		tc.m_mapField["f_postid"] = { "f_postid", "bigint(20) NOT NULL COMMENT '所属帖子ID'", "bigint(20)" };
		tc.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '所属帖子群ID'", "bigint(20)" };
		tc.m_mapField["f_author"] = { "f_author", "bigint(20) NOT NULL COMMENT '作者userid'", "bigint(20)" };
		tc.m_mapField["f_parentid"] = { "f_parentid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '0为留言;否则为所属留言ID'", "bigint(20)" };
		tc.m_mapField["f_reply_to"] = { "f_reply_to", "bigint(20) NOT NULL DEFAULT 0 COMMENT '被回复的人userid'", "bigint(20)" };
		tc.m_mapField["f_client_commentid"] = { "f_client_commentid", "varchar(64) NOT NULL DEFAULT '' COMMENT '客户端生成的去重标识'", "varchar(64)" };
		tc.m_mapField["f_content"] = { "f_content", "blob COMMENT '内容JSON:text/mentions'", "blob" };
		tc.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '时间(Unix毫秒)'", "bigint(20)" };
		tc.m_mapField["f_deleted"] = { "f_deleted", "int(2) NOT NULL DEFAULT 0 COMMENT '已删除但保留占位:1是'", "int(2)" };
		tc.m_mapField["f_reply_count"] = { "f_reply_count", "int(11) NOT NULL DEFAULT 0 COMMENT '留言下面未删除的回复数'", "int(11)" };
		//动态卡片 msgid：删除留言、回复或删帖时据此改写卡片（docs/帖子群方案.md 14.6）
		tc.m_mapField["f_cardmsgid"] = { "f_cardmsgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '动态卡片msgid'", "bigint(20)" };
		tc.m_strKeyString = "PRIMARY KEY (f_commentid), KEY idx_post (f_postid, f_parentid, f_commentid), KEY idx_parent (f_parentid, f_commentid), KEY idx_group (f_groupid)";
		m_vecTableInfo.push_back(tc);
	}

	// 47. t_thread_reaction —— 帖子群的互动（一行=某人对某个目标加的一种互动；0 为赞，1000+表情编号为表情回应）
	{
		STableInfo tr;
		tr.m_strName = "t_thread_reaction";
		tr.m_mapField["f_target_type"] = { "f_target_type", "int(2) NOT NULL COMMENT '目标类型:1帖子 2留言或回复'", "int(2)" };
		tr.m_mapField["f_target_id"] = { "f_target_id", "bigint(20) NOT NULL COMMENT '目标ID'", "bigint(20)" };
		tr.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '互动用户userid'", "bigint(20)" };
		tr.m_mapField["f_reaction"] = { "f_reaction", "int(11) NOT NULL COMMENT '互动:0赞,1000+表情编号'", "int(11)" };
		tr.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '所属帖子群ID'", "bigint(20)" };
		tr.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '互动时间(Unix毫秒)'", "bigint(20)" };
		tr.m_strKeyString = "PRIMARY KEY (f_target_type, f_target_id, f_userid, f_reaction), KEY idx_group (f_groupid)";
		m_vecTableInfo.push_back(tr);
	}

	// 48. t_bot —— 机器人（一行=一个企业机器人、群机器人或个人机器人，docs/机器人方案.md §5.5；删除为软删除）
	{
		STableInfo bt;
		bt.m_strName = "t_bot";
		bt.m_mapField["f_bot_id"] = { "f_bot_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '机器人ID'", "bigint(20)" };
		bt.m_mapField["f_user_id"] = { "f_user_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '机器人账号userid;个人机器人为0'", "bigint(20)" };
		bt.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '所属企业ID'", "bigint(20)" };
		bt.m_mapField["f_owner_type"] = { "f_owner_type", "int(2) NOT NULL DEFAULT 1 COMMENT '归属:1企业 2群 3个人'", "int(2)" };
		bt.m_mapField["f_owner_id"] = { "f_owner_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '企业ID、群号或主人userid'", "bigint(20)" };
		bt.m_mapField["f_name"] = { "f_name", "varchar(64) NOT NULL DEFAULT '' COMMENT '名称'", "varchar(64)" };
		bt.m_mapField["f_desc"] = { "f_desc", "varchar(512) NOT NULL DEFAULT '' COMMENT '简介'", "varchar(512)" };
		bt.m_mapField["f_glyph"] = { "f_glyph", "varchar(16) NOT NULL DEFAULT '' COMMENT '头像字形'", "varchar(16)" };
		bt.m_mapField["f_color"] = { "f_color", "varchar(16) NOT NULL DEFAULT '' COMMENT '头像底色#RRGGBB'", "varchar(16)" };
		bt.m_mapField["f_scope"] = { "f_scope", "int(2) NOT NULL DEFAULT 0 COMMENT '应用范围:0全部群 1所选的群'", "int(2)" };
		bt.m_mapField["f_allow_group_off"] = { "f_allow_group_off", "int(2) NOT NULL DEFAULT 0 COMMENT '是否允许群主在本群关闭'", "int(2)" };
		bt.m_mapField["f_status"] = { "f_status", "int(2) NOT NULL DEFAULT 0 COMMENT '状态:0启用 1停用 2已删除'", "int(2)" };
		bt.m_mapField["f_source"] = { "f_source", "int(2) NOT NULL DEFAULT 0 COMMENT '来源:0规则 1GitHub 2GitLab 3自动回复'", "int(2)" };
		bt.m_mapField["f_creator_type"] = { "f_creator_type", "int(2) NOT NULL DEFAULT 1 COMMENT '创建人类别:1管理员 2IM用户'", "int(2)" };
		bt.m_mapField["f_creator_id"] = { "f_creator_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建人ID'", "bigint(20)" };
		bt.m_mapField["f_creator_name"] = { "f_creator_name", "varchar(64) NOT NULL DEFAULT '' COMMENT '创建人名字'", "varchar(64)" };
		bt.m_mapField["f_webhook_token"] = { "f_webhook_token", "varchar(64) NOT NULL DEFAULT '' COMMENT 'Webhook令牌'", "varchar(64)" };
		bt.m_mapField["f_secret"] = { "f_secret", "varchar(64) NOT NULL DEFAULT '' COMMENT '签名密钥'", "varchar(64)" };
		bt.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)'", "bigint(20)" };
		bt.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)'", "bigint(20)" };
		bt.m_mapField["f_last_fire_time"] = { "f_last_fire_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最近一次发出消息的时间(Unix毫秒)'", "bigint(20)" };
		bt.m_strKeyString = "PRIMARY KEY (f_bot_id), KEY idx_enterprise (f_enterprise_id), KEY idx_owner (f_owner_type, f_owner_id), KEY idx_webhook (f_webhook_token)";
		m_vecTableInfo.push_back(bt);
	}

	// 49. t_bot_rule —— 机器人规则（一行=一条规则，触发与动作的详情 JSON 放 blob 以容纳 4 字节字符）
	{
		STableInfo br;
		br.m_strName = "t_bot_rule";
		br.m_mapField["f_rule_id"] = { "f_rule_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '规则ID'", "bigint(20)" };
		br.m_mapField["f_bot_id"] = { "f_bot_id", "bigint(20) NOT NULL COMMENT '所属机器人ID'", "bigint(20)" };
		br.m_mapField["f_seq"] = { "f_seq", "int(11) NOT NULL DEFAULT 0 COMMENT '顺序'", "int(11)" };
		br.m_mapField["f_name"] = { "f_name", "varchar(64) NOT NULL DEFAULT '' COMMENT '规则名称'", "varchar(64)" };
		br.m_mapField["f_enabled"] = { "f_enabled", "int(2) NOT NULL DEFAULT 1 COMMENT '是否启用'", "int(2)" };
		br.m_mapField["f_trigger_type"] = { "f_trigger_type", "int(2) NOT NULL DEFAULT 1 COMMENT '触发方式'", "int(2)" };
		br.m_mapField["f_trigger"] = { "f_trigger", "blob COMMENT '触发方式与附加条件(JSON)'", "blob" };
		br.m_mapField["f_action_type"] = { "f_action_type", "int(2) NOT NULL DEFAULT 1 COMMENT '动作'", "int(2)" };
		br.m_mapField["f_action"] = { "f_action", "blob COMMENT '动作详情(JSON)'", "blob" };
		br.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '创建时间(Unix毫秒)'", "bigint(20)" };
		br.m_mapField["f_update_time"] = { "f_update_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最后修改时间(Unix毫秒)'", "bigint(20)" };
		br.m_strKeyString = "PRIMARY KEY (f_rule_id), KEY idx_bot (f_bot_id, f_seq)";
		m_vecTableInfo.push_back(br);
	}

	// 50. t_bot_group —— 企业机器人所选的群（f_scope=1 时才有行）
	{
		STableInfo bg;
		bg.m_strName = "t_bot_group";
		bg.m_mapField["f_bot_id"] = { "f_bot_id", "bigint(20) NOT NULL COMMENT '机器人ID'", "bigint(20)" };
		bg.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '群ID'", "bigint(20)" };
		bg.m_strKeyString = "PRIMARY KEY (f_bot_id, f_groupid), KEY idx_group (f_groupid)";
		m_vecTableInfo.push_back(bg);
	}

	// 51. t_bot_group_off —— 在某群关闭了的企业机器人
	{
		STableInfo bo;
		bo.m_strName = "t_bot_group_off";
		bo.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '群ID'", "bigint(20)" };
		bo.m_mapField["f_bot_id"] = { "f_bot_id", "bigint(20) NOT NULL COMMENT '机器人ID'", "bigint(20)" };
		bo.m_mapField["f_operator_id"] = { "f_operator_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '操作的群主userid'", "bigint(20)" };
		bo.m_mapField["f_time"] = { "f_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '关闭时间(Unix毫秒)'", "bigint(20)" };
		bo.m_strKeyString = "PRIMARY KEY (f_groupid, f_bot_id)";
		m_vecTableInfo.push_back(bo);
	}

	// 52. t_bot_domain —— 机器人呼叫外部服务的域名白名单
	{
		STableInfo bd;
		bd.m_strName = "t_bot_domain";
		bd.m_mapField["f_enterprise_id"] = { "f_enterprise_id", "bigint(20) NOT NULL COMMENT '企业ID'", "bigint(20)" };
		bd.m_mapField["f_domain"] = { "f_domain", "varchar(128) NOT NULL COMMENT '域名(小写)'", "varchar(128)" };
		bd.m_strKeyString = "PRIMARY KEY (f_enterprise_id, f_domain)";
		m_vecTableInfo.push_back(bd);
	}
}

CMysqlManager::~CMysqlManager(void)
{

}

bool CMysqlManager::init(const char* host, const char* user, const char* pwd, const char* dbname)
{
	m_strHost = host;
	m_strUser = user;
    //数据库密码可能为空
    if (pwd != NULL)
	    m_strPassword = pwd;
	m_strDataBase = dbname;

	//注意：检查数据库是否存在时，需要将数据库名称设置为空
	m_poConn.reset(new CDatabaseMysql());
    if (!m_poConn->initialize(m_strHost, m_strUser, m_strPassword, ""))
	{
		//LOG_FATAL << "CMysqlManager::Init failed, please check params(" << m_strHost << ", " << m_strUser << ", " << m_strPassword << ")";
		return false;
	}

	////////////////////// 1. 检查库是否存在 /////////////////////////
	if (!isDBExist())
	{
		if (!createDB())
		{
			return false;
		}
	}

	//再次确定是否可以连接上数据库
    m_poConn.reset(new CDatabaseMysql());
	if (!m_poConn->initialize(m_strHost, m_strUser, m_strPassword, m_strDataBase))
	{
		//LOG_FATAL << "CMysqlManager::Init failed, please check params(" << m_strHost << ", " << m_strUser
		//	<< ", " << m_strPassword << ", " << m_strDataBase << ")";
		return false;
	}

	////////////////////// 2. 建出缺少的表（已有的表不做任何改动） /////////////////////////
	for (size_t i = 0; i < m_vecTableInfo.size(); i++)
	{
		STableInfo table = m_vecTableInfo[i];
		if (!ensureTable(table))
		{
			//LOG_FATAL << "CMysqlManager::Init, table check failed : " << table.m_strName;
			return false;
		}
	}
	////////////////////// 2. 建出缺少的表（已有的表不做任何改动） /////////////////////////

    m_poConn.reset();
    return true;
}

bool CMysqlManager::isDBExist()
{
	if (NULL == m_poConn)
	{
		return false;
	}

	QueryResult* pResult = m_poConn->query("show databases");
	if (NULL == pResult)
	{
		//LOGI << "CMysqlManager::_IsDBExist, no database(" << m_strDataBase << ")";
		return false;
	}

	Field* pRow = pResult->fetch();
	while (pRow != NULL)
	{
		std::string name = pRow[0].getString();
		if (name == m_strDataBase)
		{
			//LOGI << "CMysqlManager::_IsDBExist, find database(" << m_strDataBase << ")";
			pResult->endQuery();
			return true;
		}
		
		if (pResult->nextRow() == false)
		{
			break;
		}
		pRow = pResult->fetch();
	}

	//LOGI << "CMysqlManager::_IsDBExist, no database(" << m_strDataBase << ")";
	pResult->endQuery();

    delete pResult;
	return false;
}

bool CMysqlManager::createDB()
{
	if (NULL == m_poConn)
	{
		return false;
	}

	uint32_t uAffectedCount = 0;
	int nErrno = 0;

	std::stringstream ss;
	ss << "create database " << m_strDataBase;
	if (m_poConn->execute(ss.str().c_str(), uAffectedCount, nErrno))
	{
		if (uAffectedCount == 1)
		{
			//LOGI << "CMysqlManager::_CreateDB, create database " <<
			//	m_strDataBase << " success";
			return true;
		}
	}
	else
	{
		//LOGE << "CMysqlManager::_CreateDB, create database " << m_strDataBase << " failed("
		//	<< nErrno << ")";
		return false;
	}
	return false;
}

bool CMysqlManager::ensureTable(const STableInfo& table)
{
	if (NULL == m_poConn)
	{
		return false;
	}

	if (table.m_strName.find_first_not_of("\t\r\n ") == std::string::npos)
	{
		return true;
	}

	//表已存在就不做任何改动：自 2026-10-09 起启动时不再给已有的表补列。表结构有变化时，
	//已有的库须手工修改或重建（table_mysql.sql 只描述最终结构，不含任何 ALTER 语句）
	std::stringstream ss;
	ss << "desc " << table.m_strName;
	QueryResult* pResult = m_poConn->query(ss.str());
	if (pResult != NULL)
	{
		pResult->endQuery();
		delete pResult;
		return true;
	}

	if (!createTable(table))
	{
		return false;
	}
	LOGI("mysql init: table %s did not exist, created", table.m_strName.c_str());
	return true;
}

bool CMysqlManager::createTable(const STableInfo& table)
{
	if (table.m_mapField.size() == 0)
	{
		//LOGE << "CMysqlManager::_CreateTable, table info not valid, " << table.m_strName;
		return false;
	}

    std::stringstream ss;
	ss << "CREATE TABLE IF NOT EXISTS " << table.m_strName << " (";
	
	for (std::map<std::string, STableField>::const_iterator it = table.m_mapField.begin();
		it != table.m_mapField.end(); ++it)
	{
		if (it != table.m_mapField.begin())
		{
			ss << ", ";
		}

		STableField field = it->second;
		ss << field.m_strName << " " << field.m_strType;		
	}

	if (table.m_strKeyString != "")
	{
		ss << ", " << table.m_strKeyString;
	}

	ss << ")default charset = utf8, ENGINE = InnoDB;";
	if (m_poConn->execute(ss.str().c_str()))
	{
		return true;
	}

    LOGE("Create table error, sql: %s", ss.str().c_str());
    return false;
}

