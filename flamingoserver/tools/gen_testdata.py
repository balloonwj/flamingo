# -*- coding: utf-8 -*-
"""
FlamingoIM 管理后台测试数据生成器
balloonwj@qq.com   2026-05-22

生成规模：
  4 企业 / 每企业 10 部门(三级组织树) / 每部门 50 员工(共 2000)
  每企业 1000 条审计日志(共 4000) / 每企业 8 个服务号(共 32)
  每企业 12 条公告(共 48) / 5 个管理员(1 平台超管 + 每企业 1 个)

本脚本做两件事：
  1. 直连数据库写入：按测试 ID 区间先 DELETE 再 INSERT，幂等、不删表、保留其它数据；
  2. 导出独立 SQL 文件 gen_testdata.sql：每表 DROP TABLE IF EXISTS + CREATE TABLE
     + INSERT，可在空库独立导入。
连接参数取自 ../etc/chatserver.conf。
"""
import os
import re
import sys
import random
import hashlib
import datetime

try:
    import pymysql
except ImportError:
    print("ERROR: pymysql 未安装")
    sys.exit(1)

# ---------------- 路径与常量 ----------------
HERE = os.path.dirname(os.path.abspath(__file__))
CONF_PATH = os.path.normpath(os.path.join(HERE, "..", "etc", "chatserver.conf"))
OUT_SQL = os.path.join(HERE, "gen_testdata.sql")

ENT_COUNT     = 4       # 企业数
DEPT_PER_ENT  = 10      # 每企业部门数
EMP_PER_DEPT  = 50      # 每部门员工数
AUDIT_PER_ENT = 1000    # 每企业审计日志数
SVC_PER_ENT   = 8       # 每企业服务号数
ANN_PER_ENT   = 12      # 每企业公告数

DEPT_ID_BASE  = 1610612737   # 部门业务ID起始（沿用 chatserver 的高位部门ID空间）
USER_ID_BASE  = 5001         # 员工userid起始
EMP_PASSWORD   = "123456"    # 全体测试员工的明文登录密码
ADMIN_PASSWORD = "admin123"  # 全体测试管理员的明文登录密码

random.seed(20260522)        # 固定随机种子，使每次生成的数据完全一致
NOW = datetime.datetime(2026, 5, 22, 14, 0, 0)

# 本脚本涉及的 10 张表；导出 SQL 时按此顺序 DROP+CREATE+INSERT
TABLES = [
    "t_enterprise", "t_admin", "t_department", "t_department_member",
    "t_user", "t_admin_audit_log", "t_service_account",
    "t_service_account_binding", "t_announcement", "t_announcement_target",
]

# ---------------- 中文素材池 ----------------
SURNAMES = list("王李张刘陈杨黄赵周吴徐孙马朱胡郭何高林罗郑梁谢宋唐许韩冯邓曹彭")
GIVEN = list("伟芳娜秀英敏静丽强磊洋勇艳杰娟涛明超霞平刚辉力雅婷俊志建晓文宇轩浩然欣怡梓萱思源博")
L2_NAMES = ["技术中心", "产品中心", "运营中心", "市场中心", "职能中心", "客户成功中心"]
L3_NAMES = ["前端组", "后端组", "测试组", "数据组", "算法组", "设计组", "用户增长组",
            "渠道组", "品牌组", "人力资源组", "财务组", "行政组", "商务组", "客服组"]
POSITIONS = ["高级工程师", "工程师", "产品专员", "运营专员", "主管", "助理", "顾问"]
SIGNATURES = ["合抱之木，生于毫末", "把简单的事做到极致", "稳扎稳打", "持续精进",
              "用心做事", "脚踏实地", "保持热爱", "向上生长"]
SVC_NAMES = ["系统通知", "考勤助手", "审批助手", "IT支持", "HR小助手", "财务通知",
             "公告栏", "会议助手", "报销助手", "培训通知", "值班提醒", "数据日报"]
ANN_TITLES = ["关于五一假期安排的通知", "系统升级维护公告", "新版考勤制度说明",
              "年度团建活动通知", "信息安全月活动启动", "季度优秀员工评选",
              "办公区域调整通知", "报销流程优化说明", "防诈骗安全提醒",
              "新员工入职指引", "公司年会安排通知", "年度健康体检通知"]
# 审计动作池：(f_action, 中文描述, 默认级别)
AUDIT_ACTIONS = [
    ("admin.login.success", "管理员登录", 0),
    ("dept.create", "新建部门", 0),
    ("dept.update", "修改部门信息", 0),
    ("dept.delete", "删除部门", 1),
    ("user.create", "新建员工", 0),
    ("user.disable", "停用员工账号", 1),
    ("user.enable", "启用员工账号", 0),
    ("user.move", "调整员工所属部门", 0),
    ("user.reset_password", "重置员工密码", 1),
    ("announcement.publish", "发布公告", 0),
    ("service.create", "新建服务号", 0),
    ("service.disable", "停用服务号", 1),
    ("role.update", "调整角色权限", 1),
    ("enterprise.update", "修改企业信息", 0),
    ("audit.export", "导出审计日志", 0),
    ("admin.login.fail", "管理员登录失败", 2),
]

# 企业定义：(企业ID, 全称, 简称, 行业, 地区, 套餐, 邮箱域名)
ENTERPRISES = [
    (1, "云启智能科技有限公司",     "云启智能", "互联网",   "深圳", 2, "yunqi"),
    (2, "恒通物流集团股份有限公司", "恒通物流", "物流运输", "上海", 1, "hengtong"),
    (3, "明德教育咨询有限公司",     "明德教育", "教育培训", "北京", 1, "mingde"),
    (4, "锦程生物医药有限公司",     "锦程生物", "生物医药", "杭州", 2, "jincheng"),
]
# 企业管理员姓名（admin_id 2..5 对应企业 1..4）
ENT_ADMIN_NAMES = ["王怡然", "周建国", "陈晓雯", "林志远"]
LOGO_COLORS = ["#ff5a44", "#3a86d9", "#2bb673", "#f4a020", "#8e63d6"]


def read_conf(path):
    """读取 chatserver.conf 的 key=value 配置项"""
    cfg = {}
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            cfg[k.strip()] = v.strip()
    return cfg


def gen_salt(seed):
    """由种子串派生 16 位十六进制盐值"""
    return hashlib.md5(("slt#" + seed).encode("utf-8")).hexdigest()[:16]


def emp_password(salt):
    """员工 v2 口令：MD5(SHA256(明文) + 盐)"""
    sha = hashlib.sha256(EMP_PASSWORD.encode("utf-8")).hexdigest()
    return hashlib.md5((sha + salt).encode("utf-8")).hexdigest()


def admin_password(salt):
    """管理员口令：MD5(明文 + 盐)"""
    return hashlib.md5((ADMIN_PASSWORD + salt).encode("utf-8")).hexdigest()


def rand_name():
    """随机中文姓名（姓 + 1~2 字名）"""
    return random.choice(SURNAMES) + "".join(
        random.choice(GIVEN) for _ in range(random.randint(1, 2)))


def rand_ip():
    """随机公网风格 IPv4"""
    return "%d.%d.%d.%d" % (random.randint(36, 223), random.randint(0, 255),
                            random.randint(0, 255), random.randint(1, 254))


def dt_ago(days_max, days_min=0):
    """返回 NOW 之前 [days_min, days_max] 天内的随机时刻"""
    sec = random.randint(int(days_min * 86400), int(days_max * 86400))
    return NOW - datetime.timedelta(seconds=sec)


def build_dataset():
    """生成全部测试数据，返回 {表名: (列名列表, 行元组列表)}"""
    rows_ent, rows_admin, rows_dept, rows_member = [], [], [], []
    rows_user, rows_audit, rows_svc, rows_bind = [], [], [], []
    rows_ann, rows_anntgt = [], []

    # ---- 企业 + 管理员 ----
    # admin_id 1 = 平台超级管理员；admin_id 2..5 = 各企业管理员
    rows_admin.append((
        1, "li@flamingo.com", admin_password(gen_salt("admin1")), gen_salt("admin1"),
        "李正阳", 0, 0, dt_ago(2), rand_ip(),
        datetime.datetime(2025, 1, 10, 10, 0, 0), "平台超级管理员"))

    for (eid, full, short, industry, region, plan, domain) in ENTERPRISES:
        admin_id = eid + 1
        admin_name = ENT_ADMIN_NAMES[eid - 1]
        admin_email = "admin@%s.com" % domain
        ent_create = dt_ago(720, 400)
        rows_ent.append((
            eid, full, short, industry, region, plan, 1,
            admin_name, admin_email, random.choice(LOGO_COLORS),
            ent_create, ent_create + datetime.timedelta(days=730),
            ent_create, "测试数据"))
        rows_admin.append((
            admin_id, admin_email, admin_password(gen_salt("admin%d" % admin_id)),
            gen_salt("admin%d" % admin_id), admin_name, eid, 0,
            dt_ago(5), rand_ip(), ent_create, "%s管理员" % short))

    # ---- 部门(三级树) + 员工 + 部门成员 ----
    next_user_id = USER_ID_BASE
    for ei, (eid, full, short, industry, region, plan, domain) in enumerate(ENTERPRISES):
        admin_id = eid + 1
        admin_name = ENT_ADMIN_NAMES[eid - 1]
        # 选定本企业 3 个二级部门名、6 个三级部门名
        l2_names = random.sample(L2_NAMES, 3)
        l3_names = random.sample(L3_NAMES, 6)
        dept_ids = [DEPT_ID_BASE + ei * DEPT_PER_ENT + li for li in range(DEPT_PER_ENT)]
        root_id = dept_ids[0]
        # 三级部门挂到的二级父部门：l3 序号 0,1->l2#0  2,3->l2#1  4,5->l2#2
        l3_parent_local = [1, 1, 2, 2, 3, 3]
        ent_create = dt_ago(720, 400)

        # 10 个部门：local 0=根，1~3=二级，4~9=三级
        dept_meta = []  # (local_idx, dept_id, name, parent_id, path, level)
        # 根
        dept_meta.append((0, root_id, short + "总部", 0, "/%d/" % root_id, 1))
        # 二级
        for k in range(3):
            did = dept_ids[1 + k]
            dept_meta.append((1 + k, did, l2_names[k], root_id,
                              "/%d/%d/" % (root_id, did), 2))
        # 三级
        for k in range(6):
            did = dept_ids[4 + k]
            l2_local = l3_parent_local[k]
            l2_id = dept_ids[l2_local]
            dept_meta.append((4 + k, did, l3_names[k], l2_id,
                              "/%d/%d/%d/" % (root_id, l2_id, did), 3))

        # 先建员工，再回填部门负责人
        dept_leader = {}   # dept_id -> 该部门第 1 名员工的 user_id
        for (local_idx, did, dname, parent_id, path, level) in dept_meta:
            for m in range(EMP_PER_DEPT):
                uid = next_user_id
                next_user_id += 1
                salt = gen_salt("u%d" % uid)
                username = str(10000000 + uid)        # 8 位数字登录账号
                nickname = rand_name()
                gender = random.randint(0, 1)
                byear = random.randint(1980, 2001)
                birthday = byear * 10000 + random.randint(1, 12) * 100 + random.randint(1, 28)
                # 手机号可用于登录，必须唯一：取 138 + 8 位用户 ID，与 testdata.sql 及开发库补的号码一致
                # （docs/登录账号与手机号登录方案.md 第 6 节）。原先这里随机取号，可能重复；那次随机数调用
                # 仍保留，使随机序列不变，其后生成的数据与改动前完全相同。
                random.randint(3000000000, 9999999999)
                phone = "138%08d" % uid
                mail = "%s@%s.com" % (username, domain)
                join_dt = dt_ago(700, 30)
                emp_no = "%s%d%04d" % (domain[:2].upper(), join_dt.year, uid)
                is_leader = (m == 0)
                role = 1 if is_leader else 0
                workstation = "%s座%d层%02d工位" % (
                    random.choice("ABCDE"), random.randint(3, 28), random.randint(1, 99))
                rows_user.append((
                    uid, username, nickname, emp_password(salt), salt,
                    0, None, None, gender, birthday,
                    random.choice(SIGNATURES), region + "市", phone, mail, 0,
                    join_dt, "测试数据", eid, role, emp_no,
                    join_dt, None, workstation, 0, 0))
                position = "部门负责人" if is_leader else random.choice(POSITIONS)
                rows_member.append((
                    did, eid, uid, position, 1, m, 0, join_dt, "测试数据"))
                if is_leader:
                    dept_leader[did] = uid

        # 部门行（回填负责人）
        for (local_idx, did, dname, parent_id, path, level) in dept_meta:
            rows_dept.append((
                did, eid, dname, parent_id, path, level, local_idx,
                dept_leader.get(did, 0), "%s负责本领域工作" % dname, 0,
                ent_create + datetime.timedelta(days=random.randint(1, 60)),
                "测试数据"))

        # ---- 审计日志（每企业 AUDIT_PER_ENT 条）----
        for _ in range(AUDIT_PER_ENT):
            action, desc, base_sev = random.choice(AUDIT_ACTIONS)
            is_system = (random.random() < 0.05)
            if is_system:
                actor_type, actor_id, actor_name = 1, 0, "系统"
            else:
                actor_type, actor_id, actor_name = 0, admin_id, admin_name
            severity = base_sev if random.random() < 0.7 else random.choice([0, 0, 1, 2])
            rows_audit.append((
                actor_type, actor_id, actor_name, rand_ip(), eid, action,
                "%s：%s" % (desc, rand_name() if "user" in action else short),
                severity, "%s（自动生成的测试审计记录）" % desc,
                dt_ago(90)))

        # ---- 服务号（每企业 SVC_PER_ENT 个）+ 绑定 ----
        svc_names = random.sample(SVC_NAMES, SVC_PER_ENT)
        for k in range(SVC_PER_ENT):
            sid = ei * SVC_PER_ENT + k + 1
            kind = random.randint(0, 1)
            rows_svc.append((
                sid, eid, svc_names[k], kind,
                "%s服务号，用于%s相关消息推送" % (svc_names[k],
                                                "通知" if kind == 0 else "智能助手"),
                random.choice(LOGO_COLORS),
                random.choice(["通", "助", "知", "铃", "信", "报", "会", "务"]),
                admin_id, 1, 0, ent_create + datetime.timedelta(days=random.randint(10, 200)),
                "测试数据"))
            rows_bind.append((sid, 0, 0,
                              ent_create + datetime.timedelta(days=random.randint(10, 200))))

        # ---- 公告（每企业 ANN_PER_ENT 条）+ 投放目标 ----
        ann_titles = random.sample(ANN_TITLES, ANN_PER_ENT)
        for k in range(ANN_PER_ENT):
            aid = ei * ANN_PER_ENT + k + 1
            severity = random.choice([0, 0, 0, 1, 2])
            pinned = 1 if k < 2 else 0
            create_dt = dt_ago(60)
            if k < 8:                       # 已发布
                status, scheduled = 1, 0
                scheduled_time, sent_time = None, create_dt + datetime.timedelta(hours=2)
            elif k < 11:                    # 草稿
                status, scheduled = 0, 0
                scheduled_time, sent_time = None, None
            else:                           # 定时待发
                status, scheduled = 0, 1
                scheduled_time, sent_time = NOW + datetime.timedelta(days=3), None
            rows_ann.append((
                aid, ann_titles[k],
                "%s\n\n这是一条自动生成的测试公告正文，用于验证管理后台公告模块。" % ann_titles[k],
                severity, admin_id, admin_name, 1, pinned, scheduled,
                scheduled_time, sent_time, status, create_dt, "测试数据"))
            rows_anntgt.append((aid, 1, eid, create_dt))   # 投放目标=本企业

    return {
        "t_enterprise": ((
            "f_enterprise_id", "f_name", "f_short_name", "f_industry", "f_region",
            "f_plan", "f_status", "f_admin_name", "f_admin_email", "f_logo_color",
            "f_open_date", "f_license_expire_time", "f_create_time", "f_remark"),
            rows_ent),
        "t_admin": ((
            "f_admin_id", "f_email", "f_password", "f_salt", "f_name",
            "f_enterprise_id", "f_status", "f_last_login_time", "f_last_login_ip",
            "f_create_time", "f_remark"), rows_admin),
        "t_department": ((
            "f_dept_id", "f_enterprise_id", "f_dept_name", "f_parent_dept_id",
            "f_dept_path", "f_dept_level", "f_dept_order", "f_leader_user_id",
            "f_dept_desc", "f_status", "f_create_time", "f_remark"), rows_dept),
        "t_department_member": ((
            "f_dept_id", "f_enterprise_id", "f_user_id", "f_position",
            "f_is_primary", "f_member_order", "f_status", "f_join_time",
            "f_remark"), rows_member),
        "t_user": ((
            "f_user_id", "f_username", "f_nickname", "f_password", "f_salt",
            "f_facetype", "f_customface", "f_customfacefmt", "f_gender",
            "f_birthday", "f_signature", "f_address", "f_phonenumber", "f_mail",
            "f_owner_id", "f_register_time", "f_remark", "f_enterprise_id",
            "f_role", "f_employee_no", "f_join_date", "f_landline",
            "f_workstation", "f_status", "f_usertype"), rows_user),
        "t_admin_audit_log": ((
            "f_actor_type", "f_actor_id", "f_actor_name", "f_ip",
            "f_enterprise_id", "f_action", "f_target", "f_severity",
            "f_detail", "f_create_time"), rows_audit),
        "t_service_account": ((
            "f_service_id", "f_enterprise_id", "f_name", "f_kind",
            "f_description", "f_avatar_color", "f_avatar_glyph",
            "f_owner_admin_id", "f_allow_unsubscribe", "f_status",
            "f_create_time", "f_remark"), rows_svc),
        "t_service_account_binding": ((
            "f_service_id", "f_bind_type", "f_bind_target_id", "f_create_time"),
            rows_bind),
        "t_announcement": ((
            "f_announcement_id", "f_title", "f_body", "f_severity",
            "f_sender_admin_id", "f_sender_name", "f_audience_type", "f_pinned",
            "f_scheduled", "f_scheduled_time", "f_sent_time", "f_status",
            "f_create_time", "f_remark"), rows_ann),
        "t_announcement_target": ((
            "f_announcement_id", "f_target_type", "f_target_id", "f_create_time"),
            rows_anntgt),
    }


# 直连写库时各表的清理条件（按测试 ID 区间 DELETE，不删表、不动其它数据）
DELETE_WHERE = {
    "t_announcement_target":     "f_announcement_id BETWEEN 1 AND %d" % (ENT_COUNT * ANN_PER_ENT),
    "t_announcement":            "f_announcement_id BETWEEN 1 AND %d" % (ENT_COUNT * ANN_PER_ENT),
    "t_service_account_binding": "f_service_id BETWEEN 1 AND %d" % (ENT_COUNT * SVC_PER_ENT),
    "t_service_account":         "f_service_id BETWEEN 1 AND %d" % (ENT_COUNT * SVC_PER_ENT),
    "t_admin_audit_log":         "f_enterprise_id BETWEEN 1 AND %d" % ENT_COUNT,
    "t_department_member":       "f_enterprise_id BETWEEN 1 AND %d" % ENT_COUNT,
    "t_user":                    "f_user_id BETWEEN %d AND %d" % (
                                     USER_ID_BASE, USER_ID_BASE + ENT_COUNT * DEPT_PER_ENT * EMP_PER_DEPT),
    "t_department":              "f_enterprise_id BETWEEN 1 AND %d" % ENT_COUNT,
    "t_admin":                   "f_admin_id BETWEEN 1 AND %d" % (ENT_COUNT + 1),
    "t_enterprise":              "f_enterprise_id BETWEEN 1 AND %d" % ENT_COUNT,
}


def build_create_table(cur, table):
    """从 information_schema + SHOW INDEX 重建 CREATE TABLE 语句。
    chatserver(MSVC 编译)建表时的中文 COMMENT 是 GBK 字节、却存进了声明为 utf8
    的表元数据，直接读 SHOW CREATE TABLE 会解码失败。本函数只取纯 ASCII 的元数据
    列(列名/类型/是否可空/默认值/EXTRA/索引)，不取中文 COMMENT，从而规避编码问题。
    重建出的表结构(列、类型、默认值、键)与实时库完全一致，仅不含列注释。"""
    cur.execute(
        "SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, COLUMN_DEFAULT, EXTRA "
        "FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = %s "
        "ORDER BY ORDINAL_POSITION", (table,))
    coldefs = []
    for name, ctype, nullable, default, extra in cur.fetchall():
        extra_l = (extra or "").lower()
        d = "  `%s` %s" % (name, ctype)
        if nullable == "NO":
            d += " NOT NULL"
        if "auto_increment" in extra_l:
            d += " AUTO_INCREMENT"
        # information_schema 对“DEFAULT NULL”列可能返回 None，也可能返回字面
        # 字符串 "NULL"（随 MySQL 版本而异），两者都须按 SQL NULL 处理。
        defv = None if (default is None or str(default).upper() == "NULL") else str(default)
        if defv is not None:
            if defv.upper() in ("CURRENT_TIMESTAMP", "CURRENT_TIMESTAMP()"):
                d += " DEFAULT CURRENT_TIMESTAMP"
            elif re.match(r"^-?\d+(\.\d+)?$", defv):
                d += " DEFAULT %s" % defv
            else:
                d += " DEFAULT '%s'" % defv.replace("'", "''")
        elif nullable == "YES" and not re.search(r"text|blob|json", ctype, re.I):
            # text/blob/json 列不能带 DEFAULT 子句（部分 MySQL 版本会报错），
            # 它们本就隐式可空，省略 DEFAULT NULL 更可移植。
            d += " DEFAULT NULL"
        if "on update current_timestamp" in extra_l:
            d += " ON UPDATE CURRENT_TIMESTAMP"
        coldefs.append(d)

    # 索引：SHOW INDEX 的列均为 ASCII 标识符，无编码问题
    cur.execute("SHOW INDEX FROM " + table)
    indexes = {}
    order = []
    for r in cur.fetchall():
        non_unique, key_name, seq, col = r[1], r[2], r[3], r[4]
        if key_name not in indexes:
            indexes[key_name] = {"non_unique": non_unique, "cols": {}}
            order.append(key_name)
        indexes[key_name]["cols"][seq] = col
    keydefs = []
    for kn in order:
        info = indexes[kn]
        cols = ", ".join("`%s`" % info["cols"][s] for s in sorted(info["cols"]))
        if kn == "PRIMARY":
            keydefs.append("  PRIMARY KEY (%s)" % cols)
        elif info["non_unique"] == 0:
            keydefs.append("  UNIQUE KEY `%s` (%s)" % (kn, cols))
        else:
            keydefs.append("  KEY `%s` (%s)" % (kn, cols))

    body = ",\n".join(coldefs + keydefs)
    return "CREATE TABLE `%s` (\n%s\n) ENGINE=InnoDB DEFAULT CHARSET=utf8" % (table, body)


def write_sql_file(conn, dataset):
    """导出独立 SQL：每表 DROP TABLE IF EXISTS + CREATE TABLE + INSERT"""
    parts = []
    parts.append("-- FlamingoIM 管理后台测试数据 (gen_testdata.sql)")
    parts.append("-- 由 tools/gen_testdata.py 自动生成于 %s"
                 % NOW.strftime("%Y-%m-%d %H:%M:%S"))
    parts.append("-- 逻辑：每张表 DROP TABLE IF EXISTS + CREATE TABLE + INSERT，可在空库独立导入。")
    parts.append("SET NAMES utf8;")
    parts.append("")

    cur = conn.cursor()
    for table in TABLES:
        columns, rows = dataset[table]
        create_sql = build_create_table(cur, table)

        parts.append("-- ===== %s（%d 行）=====" % (table, len(rows)))
        parts.append("DROP TABLE IF EXISTS %s;" % table)
        parts.append(create_sql + ";")
        collist = ", ".join(columns)
        for i in range(0, len(rows), 200):
            chunk = rows[i:i + 200]
            values = ",\n".join(
                "(" + ",".join(conn.escape(v) for v in row) + ")" for row in chunk)
            parts.append("INSERT INTO %s (%s) VALUES\n%s;" % (table, collist, values))
        parts.append("")
    cur.close()

    with open(OUT_SQL, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts))


def main():
    cfg = read_conf(CONF_PATH)
    print("config   : %s -> %s/%s" % (CONF_PATH, cfg.get("dbserver"), cfg.get("dbname")))

    conn = pymysql.connect(
        host=cfg["dbserver"], user=cfg["dbuser"], password=cfg["dbpassword"],
        database=cfg["dbname"], charset="utf8", autocommit=False)
    print("connected: %s" % cfg["dbserver"])

    dataset = build_dataset()
    for t in TABLES:
        print("  generated %-26s %d rows" % (t, len(dataset[t][1])))

    try:
        # 阶段 1：直连写库（按测试 ID 区间先 DELETE 再 INSERT，整体一个事务）
        try:
            cur = conn.cursor()
            for table in ["t_announcement_target", "t_announcement",
                          "t_service_account_binding", "t_service_account",
                          "t_admin_audit_log", "t_department_member", "t_user",
                          "t_department", "t_admin", "t_enterprise"]:
                cur.execute("DELETE FROM %s WHERE %s" % (table, DELETE_WHERE[table]))
            for table in TABLES:
                columns, rows = dataset[table]
                if not rows:
                    continue
                ph = ",".join(["%s"] * len(columns))
                sql = "INSERT INTO %s (%s) VALUES (%s)" % (table, ", ".join(columns), ph)
                cur.executemany(sql, rows)
            cur.close()
            conn.commit()
            print("DB write : committed.")
        except Exception:
            conn.rollback()
            raise

        # 阶段 2：导出独立 SQL 文件（数据已提交，此步失败也不影响库中数据）
        write_sql_file(conn, dataset)
        print("SQL file : %s (%d KB)" % (OUT_SQL, os.path.getsize(OUT_SQL) // 1024))
        print("DONE.")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
