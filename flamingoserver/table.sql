//用户信息表
CREATE TABLE IF NOT EXISTS  t_user  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
		 f_user_id  bigint(20) NOT NULL COMMENT '用户ID',
		 f_username  varchar(64) NOT NULL COMMENT '用户名',
         f_nickname  varchar(64) NOT NULL COMMENT '用户昵称',
         f_password  varchar(64) DEFAULT NULL COMMENT '用户密码',
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
         PRIMARY KEY ( f_user_id ),
		 INDEX f_user_id (f_user_id),
         KEY  f_id  ( f_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//alter table t_user add f_teaminfo blob default null comment "好友分组信息";
//alter table t_user drop column f_teaminfo;

//用户关系表
//为了避免冗余，一定要保证f_user_id1小于f_user_id2
CREATE TABLE IF NOT EXISTS  t_user_relationship  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_user_id1  bigint(20) NOT NULL COMMENT '第一个用户id',
         f_user_id2  bigint(20) NOT NULL COMMENT '第二个用户id',		 
		 f_user1_teamname VARCHAR(32) NOT NULL DEFAULT "我的好友" COMMENT "用户2在用户1的好友分组名称",
		 f_user1_markname VARCHAR(32) COMMENT "用户2在用户1的备注名称",		 
		 f_user2_teamname VARCHAR(32) NOT NULL DEFAULT "我的好友" COMMENT "用户1在用户2的好友分组名称",
		 f_user2_markname VARCHAR(32) COMMENT "用户1在用户2的备注名称",
         f_update_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         KEY  f_id  ( f_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8

//ALTER TABLE t_user_relationship CHANGE f_create_time f_update_time timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间';		
//alter table t_user_relationship modify f_user1_teamname varchar(32) not null default "我的好友" comment "用户2在用户1的好友分组名称";
//alter table t_user_relationship modify f_user2_teamname varchar(32) not null default "我的好友" comment "用户1在用户2的好友分组名称";
//alter table t_user_relationship add f_user1_markname VARCHAR(32) COMMENT "用户2在用户1的备注名称";
//alter table t_user_relationship add f_user2_markname VARCHAR(32) COMMENT "用户1在用户2的备注名称";

//消息记录表
CREATE TABLE IF NOT EXISTS  t_chatmsg  (
         f_id  bigint(20) NOT NULL AUTO_INCREMENT COMMENT '自增ID',
         f_senderid  bigint(20) NOT NULL COMMENT '发送者id',
         f_targetid  bigint(20) NOT NULL COMMENT '接收者id',
		 f_msgcontent  BLOB NOT NULL COMMENT '聊天内容',
         f_create_time  timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '创建时间',
         f_remark  varchar(64) DEFAULT NULL COMMENT '备注',
         PRIMARY KEY ( f_id ),
         KEY  f_id  ( f_id )
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8
