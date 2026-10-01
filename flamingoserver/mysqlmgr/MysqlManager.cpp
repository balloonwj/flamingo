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

	// 3. t_chatmsg —— 消息记录表（v2 重建；旧 v1 结构已废弃，见 migrations/v2_drop_old_chatmsg.sql）
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
		//idx_to_msgid 是"按会话取最新消息"用的：拉会话列表时的查询形如
		//"WHERE f_ctype=? AND f_to IN (...) GROUP BY f_to" 取 MAX(f_msgid)，
		//本索引让它按 (f_to, f_msgid) 逐组直接跳到最大值。idx_to_from 顶不上 ——
		//它的第二列是 f_from，同一个会话内 f_msgid 是散的。
		//2026-08-12 实测（25 万行 / 50 个群）：MySQL 583ms → 303ms、SQLite 1046ms → 68ms。
		//注意 checkTable 只补列不补索引，本行只对<u>新建库</u>生效；存量库须执行
		//migrations/v6_add_t_chatmsg_idx_to_msgid.sql。
		chat.m_strKeyString = "PRIMARY KEY (f_msgid), KEY idx_from_to (f_from, f_to), KEY idx_to_from (f_to, f_from), KEY idx_client (f_clientmsgid), KEY idx_to_msgid (f_to, f_msgid)";
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
		//公司根部门：代表"公司本身"这一层，员工可直接挂其下。已有库由 checkTable 自动 ALTER ADD COLUMN，
		//补出来的列默认值为 0，需由 migrations 下的迁移脚本建出根部门后回填。
		ent.m_mapField["f_root_dept_id"] = { "f_root_dept_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '公司根部门ID，关联 t_department.f_dept_id；0表示尚未建立'", "bigint(20)" };
		ent.m_mapField["f_admin_name"] = { "f_admin_name", "varchar(64) DEFAULT NULL COMMENT '主管理员姓名'", "varchar(64)" };
		ent.m_mapField["f_admin_email"] = { "f_admin_email", "varchar(128) DEFAULT NULL COMMENT '主管理员邮箱'", "varchar(128)" };
		ent.m_mapField["f_logo_color"] = { "f_logo_color", "varchar(16) DEFAULT NULL COMMENT '企业Logo底色'", "varchar(16)" };
		ent.m_mapField["f_open_date"] = { "f_open_date", "datetime DEFAULT NULL COMMENT '开通日期'", "datetime" };
		ent.m_mapField["f_license_expire_time"] = { "f_license_expire_time", "datetime DEFAULT NULL COMMENT '企业版授权到期时间'", "datetime" };
		ent.m_mapField["f_create_time"] = { "f_create_time", "datetime NOT NULL COMMENT '创建时间'", "datetime" };
		ent.m_mapField["f_update_time"] = { "f_update_time", "timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'", "timestamp" };
		ent.m_mapField["f_remark"] = { "f_remark", "varchar(64) DEFAULT NULL COMMENT '备注'", "varchar(64)" };

		//企业全称唯一：客户端注册时用户填的是公司全称，服务端要靠它精确定位到唯一一家企业，
		//重名会让"这个人该进哪一家"无法判定。注意 checkTable 只补列不补索引，存量库须执行
		//migrations/v4_add_t_enterprise_name_unique.sql，这里的定义只对新建库生效。
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

	// 20. t_conversation —— 会话状态表（v2 会话与消息功能；置顶/免打扰/已读位点）
	{
		STableInfo cv;
		cv.m_strName = "t_conversation";
		cv.m_mapField["f_userid"] = { "f_userid", "bigint(20) NOT NULL COMMENT '会话归属用户userid'", "bigint(20)" };
		cv.m_mapField["f_ctype"] = { "f_ctype", "int(2) NOT NULL DEFAULT 1 COMMENT '会话类型:1单聊 2群聊'", "int(2)" };
		cv.m_mapField["f_cid"] = { "f_cid", "bigint(20) NOT NULL COMMENT '会话ID:单聊为对端userid,群聊为群id'", "bigint(20)" };
		cv.m_mapField["f_pinned"] = { "f_pinned", "int(2) NOT NULL DEFAULT 0 COMMENT '是否置顶:0否 1是'", "int(2)" };
		cv.m_mapField["f_mute"] = { "f_mute", "int(2) NOT NULL DEFAULT 0 COMMENT '是否免打扰:0否 1是'", "int(2)" };
		cv.m_mapField["f_readmsgid"] = { "f_readmsgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '已读到的最大消息msgid'", "bigint(20)" };
		cv.m_mapField["f_deleted"] = { "f_deleted", "int(2) NOT NULL DEFAULT 0 COMMENT '会话是否从列表删除:0否 1是'", "int(2)" };
		//删除锚点：老库由 _CheckTable 自动 ALTER ADD COLUMN 补上。ALTER 拼的是本结构的
		//第二个字段（m_strType，即下面那一整串完整定义，见 _CheckTable 里
		//"add column " << field.m_strName << " " << field.m_strType），带 NOT NULL 与
		//DEFAULT，MySQL 会用默认值填满已有行，补出来的列不会为 NULL。
		//MessageManager 读本列时仍套了一层 IFNULL(f_deleted_msgid,-1)，那是按"补出来的
		//列可为 NULL"这一<u>错误</u>前提写的；它不影响正确性（非空列上 IFNULL 原样返回），
		//只是那条兜底分支实际永远走不到。日后清理该处时可一并去掉。
		cv.m_mapField["f_deleted_msgid"] = { "f_deleted_msgid", "bigint(20) NOT NULL DEFAULT 0 COMMENT '删除会话时的最后消息msgid锚点'", "bigint(20)" };
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
	// 结构以 table.sql 为准，两处须保持一致。
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
		// 以下七列为群管理功能新增。老库由 _CheckTable 自动 alter 加列，用的是本结构的
		// 第二个字段（m_strType，即下面这一整串完整定义），故加出来的列带 NOT NULL 与
		// DEFAULT，MySQL 会用该默认值填满已有行 —— 老库升级后每个群都拿到与新建群一致
		// 的初值，读取处无需额外兜底。
		//
		// 两个默认值需要特别说明：
		//   f_new_see_history 默认 1 —— 升级前没有这项限制、历史消息本来就都能看，
		//   默认 1 使老群升级后行为不变。
		//   f_member_invite 默认 0 —— 这一项<u>会</u>改变老群的行为：升级前
		//   onInviteToGroup 只校验"邀请人是群成员"，等价于免审批；升级后普通成员的
		//   邀请将转为待审批申请。之所以仍取 0，是因为它与界面上该开关的默认态一致，
		//   且收紧权限比静默放宽安全 —— 群主发现后可以自行打开。
		gp.m_mapField["f_intro"] = { "f_intro", "varchar(512) NOT NULL DEFAULT '' COMMENT '群简介'", "varchar(512)" };
		gp.m_mapField["f_at_all_scope"] = { "f_at_all_scope", "int(2) NOT NULL DEFAULT 0 COMMENT '谁能@全体:0仅群主和管理员 1所有人'", "int(2)" };
		gp.m_mapField["f_new_see_history"] = { "f_new_see_history", "int(2) NOT NULL DEFAULT 1 COMMENT '新成员可看入群前历史消息:0否 1是'", "int(2)" };
		gp.m_mapField["f_member_invite"] = { "f_member_invite", "int(2) NOT NULL DEFAULT 0 COMMENT '普通成员邀请好友免审批:0否(转待审批) 1是(直接入群)'", "int(2)" };
		gp.m_mapField["f_welcome_on"] = { "f_welcome_on", "int(2) NOT NULL DEFAULT 0 COMMENT '入群欢迎语开关:0关 1开'", "int(2)" };
		gp.m_mapField["f_welcome_text"] = { "f_welcome_text", "varchar(256) NOT NULL DEFAULT '' COMMENT '入群欢迎语正文'", "varchar(256)" };
		gp.m_mapField["f_sensitive_on"] = { "f_sensitive_on", "int(2) NOT NULL DEFAULT 0 COMMENT '消息敏感词过滤开关:0关 1开'", "int(2)" };
		// 以下三列为群公告功能新增，与既有的 f_announcement 一起构成"本群最新一条
		// 已发布公告"的冗余快照，由服务端在发布 / 撤回后同步刷新（见
		// GroupManager::refreshLatestAnnouncement）。冗余的收益是：所有只读展示
		// （群资料对话框、cmd_get_group_info 应答、群资料变更推送）都不必连
		// t_group_announcement，老客户端也能照常显示最新公告。
		// 三列默认值即"本群还没有公告"，与新建群一致，老库升级后无需数据迁移。
		gp.m_mapField["f_announce_id"] = { "f_announce_id", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最新已发布公告的f_id;0=本群还没有公告'", "bigint(20)" };
		gp.m_mapField["f_announce_title"] = { "f_announce_title", "varchar(128) NOT NULL DEFAULT '' COMMENT '最新已发布公告的标题(冗余快照)'", "varchar(128)" };
		gp.m_mapField["f_announce_time"] = { "f_announce_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '最新已发布公告的发布时间(Unix毫秒)'", "bigint(20)" };
		// 群名是否由用户设定（1 是 / 0 自动群名，由服务端按成员昵称重算）。默认 1：
		// 老库升级补列时，已有的群一律按"用户设定"处理，不会被悄悄改名；当年由客户端
		// 自动拼出的群名另由 migrations/v7_mark_auto_group_names.py 识别后改标记。
		gp.m_mapField["f_name_custom"] = { "f_name_custom", "int(2) NOT NULL DEFAULT 1 COMMENT '群名是否由用户设定:1是 0否(自动群名)'", "int(2)" };
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
		// 禁言到期时间与原因为群管理功能新增。f_mute_until 默认 0 是刻意的：它与
		// f_muted 一起表达禁言态 —— f_muted=1 且 f_mute_until=0 即"永久禁言"，
		// 于是老库里已有的 f_muted=1 的行升级后自动落在"永久禁言"上，与升级前
		// （只有 0/1、没有时长概念）的语义完全一致，无需任何数据迁移。
		gm.m_mapField["f_mute_until"] = { "f_mute_until", "bigint(20) NOT NULL DEFAULT 0 COMMENT '禁言到期时间(Unix毫秒);f_muted=1且本列为0表示永久禁言'", "bigint(20)" };
		gm.m_mapField["f_mute_reason"] = { "f_mute_reason", "varchar(128) NOT NULL DEFAULT '' COMMENT '禁言原因'", "varchar(128)" };
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
		// f_source 默认 1（群搜索）—— 老库里已有的申请都是走搜索加群来的，没有别的入口。
		gj.m_mapField["f_source"] = { "f_source", "int(2) NOT NULL DEFAULT 1 COMMENT '申请来源:0好友邀请 1群搜索 2扫码加群'", "int(2)" };
		gj.m_mapField["f_recommender"] = { "f_recommender", "bigint(20) NOT NULL DEFAULT 0 COMMENT '推荐人userid(仅好友邀请时非0)'", "bigint(20)" };
		gj.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_group_status (f_groupid, f_status)";
		m_vecTableInfo.push_back(gj);
	}

	// 24.1 t_group_sensitive_word —— 群敏感词库表（v2 群管理功能）
	//
	// 一行 = 某群的一条敏感词。群开启 f_sensitive_on 后，群内文本消息发送前按本表
	// 做子串匹配，命中即拒发。按群隔离、单群词量小，服务端按群缓存在内存里，随
	// 增删失效重载 —— 每条消息都查一次库不可接受。
	{
		STableInfo sw;
		sw.m_strName = "t_group_sensitive_word";
		sw.m_mapField["f_id"] = { "f_id", "bigint(20) NOT NULL AUTO_INCREMENT COMMENT '词条ID'", "bigint(20)" };
		sw.m_mapField["f_groupid"] = { "f_groupid", "bigint(20) NOT NULL COMMENT '所属群ID'", "bigint(20)" };
		sw.m_mapField["f_word"] = { "f_word", "varchar(64) NOT NULL COMMENT '敏感词'", "varchar(64)" };
		sw.m_mapField["f_create_time"] = { "f_create_time", "bigint(20) NOT NULL DEFAULT 0 COMMENT '添加时间(Unix毫秒)'", "bigint(20)" };
		sw.m_strKeyString = "PRIMARY KEY (f_id), KEY idx_group (f_groupid)";
		m_vecTableInfo.push_back(sw);
	}

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
	//     SqliteSchema 自动建表、跑得好好的，MySQL 侧却要靠手工执行 table.sql 才有，
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

	////////////////////// 2. 检查库中表是否正确 /////////////////////////
	for (size_t i = 0; i < m_vecTableInfo.size(); i++)
	{
		STableInfo table = m_vecTableInfo[i];
		if (!checkTable(table))
		{
			//LOG_FATAL << "CMysqlManager::Init, table check failed : " << table.m_strName;
			return false;
		}
	}
	////////////////////// 2. 检查库中表是否正确 /////////////////////////

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

bool CMysqlManager::checkTable(const STableInfo& table)
{
	if (NULL == m_poConn)
	{
		return false;
	}

	if (table.m_strName.find_first_not_of("\t\r\n ") == std::string::npos)
	{
		//LOGW << "CMysqlManager::_CheckTable, tale info not valid";
		return true;
	}

	std::stringstream ss;
	ss << "desc " << table.m_strName;
	QueryResult* pResult = m_poConn->query(ss.str());
	if (NULL == pResult)
	{
		//LOGI << "CMysqlManager::_CheckTable, no table" << table.m_strName << ", begin create.....";
		if (createTable(table))
		{
			//LOGI << "CMysqlManager::_CheckTable, " << table.m_strName << ", end create.....";
			return true;
		}
		return false;
	}
	else // 检查字段是否匹配， 暂时只检查是否存在， 还需进一步看类型是否需要修改 
	{
		std::map<std::string, std::string> mapOldTable;
		Field* pRow = pResult->fetch();
		while (pRow != NULL)
		{
            std::string name = pRow[0].getString();
            std::string type = pRow[1].getString();
			mapOldTable[name] = type;

			if (pResult->nextRow() == false)
			{
				break;
			}
			pRow = pResult->fetch();
		}

		pResult->endQuery();
        delete pResult;

		for (std::map<std::string, STableField>::const_iterator it = table.m_mapField.begin();
			it != table.m_mapField.end(); ++it)
		{
			STableField field = it->second;
			if (mapOldTable.find(field.m_strName) == mapOldTable.end())
			{
                std::stringstream ss;
				ss << "alter table " << table.m_strName << " add column "
					<< field.m_strName << " " << field.m_strType;

                std::string sql = ss.str();
				if (m_poConn->execute(sql.c_str()))
				{
					//LOGI << sql;
					continue;
				}
				else
				{
					//LOGE << "CMysqlManager::_CheckTable failed : " << sql;
					return false;
				}
			}
		}
	}

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

