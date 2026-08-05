#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
校验"公司根部门"这套数据结构的后置条件，供迁移之后与日常巡检使用。

组织架构树要求每个企业有且只有一个代表公司本身的根部门，员工可以直接挂在它下面。
这条约束一部分由服务端代码保证（建企业时连带建根部门、根部门不允许删除或移动、
新增员工未指定部门时落到根部门），另一部分由存量数据迁移脚本
migrations/v3_add_enterprise_root_dept.py 保证。本脚本把这些约束逐条查一遍，
任何一条不满足就打印明细并以非 0 退出码结束。

逐条校验：
  A. 每家企业的 f_root_dept_id 非 0。
  B. 该 ID 对应的部门存在、属于本企业、父部门为 0、层级为 1、状态为在用。
  C. 公司根部门的名字与企业全称一致（企业改名时服务端会同步改）。
  D. 每家企业内"父部门为 0 且在用"的部门恰好一个，即不存在第二个根。
  E. 本企业每个部门的物化路径都以公司根部门开头，且层级与路径段数自洽。
  F. 不存在"有企业归属却不属于任何在职部门"的普通用户，即没有孤儿员工。

用法：
    python3 check_root_dept.py

balloonwj@qq.com   2026-07-31
"""

import os
import sys
import pymysql

# MySQL 默认端口。chatserver.conf 不含端口配置项，服务端也是按默认端口连的。
DEFAULT_DB_PORT = 3306


def load_db_config():
    """从 flamingoserver/etc/chatserver.conf 读出数据库连接参数。

    直接读服务端自己的配置文件，而不是在脚本里再硬编码一份：仓库里已有的运维
    脚本抄的是某个历史地址，早已连不上了。配置文件是简单的 key=value 格式
    （对应服务端的 base/ConfigFileReader），缺文件或缺项时回退到本机默认值。
    @return 五元组 (host, port, user, password, dbname)。
    """
    defaults = {
        "dbserver": "127.0.0.1",
        "dbuser": "root",
        "dbpassword": "123456",
        "dbname": "flamingo",
    }

    conf_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             os.pardir, "etc", "chatserver.conf")
    try:
        with open(conf_path, "r", encoding="utf-8", errors="replace") as conf:
            for line in conf:
                line = line.strip()
                # 跳过空行与以 # 开头的注释行。
                if not line or line.startswith("#"):
                    continue
                if "=" not in line:
                    continue
                key, _, value = line.partition("=")
                key = key.strip()
                if key in defaults:
                    defaults[key] = value.strip()
    except OSError as exc:
        print("读取 %s 失败（%s），改用默认的本机连接参数。" % (conf_path, exc),
              file=sys.stderr)

    return (defaults["dbserver"], DEFAULT_DB_PORT, defaults["dbuser"],
            defaults["dbpassword"], defaults["dbname"])


DB_HOST, DB_PORT, DB_USER, DB_PASSWORD, DB_NAME = load_db_config()

# 部门状态与成员状态取值，与 table.sql 中各表的 f_status 注释一致。
DEPT_STATUS_NORMAL = 0      # 部门正常在用
MEMBER_STATUS_ACTIVE = 0    # 成员在职

# 公司根部门的固定属性。
ROOT_PARENT_DEPT_ID = 0     # 无父部门
ROOT_DEPT_LEVEL = 1         # 层级为最外层

# 普通用户的 f_usertype 取值；服务号用户（1）本就不进组织架构，不参与孤儿判定。
USER_TYPE_NORMAL = 0


def column_exists(cur, table_name, column_name):
    """判断某表是否已有某列。"""
    cur.execute(
        "SELECT COUNT(*) FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = %s AND TABLE_NAME = %s AND COLUMN_NAME = %s",
        (DB_NAME, table_name, column_name))
    return cur.fetchone()[0] > 0


def path_segment_count(dept_path):
    """数出物化路径里的部门段数。路径形如 /1001/1005/，段数即层级。"""
    return len([seg for seg in dept_path.split("/") if seg])


def check_enterprise(cur, enterprise_id, enterprise_name, root_dept_id, failures):
    """校验单个企业的 A~E 项，把不满足的条目追加进 failures。"""
    # A. 根部门ID必须已写入。
    if root_dept_id == 0:
        failures.append("企业 %d（%s）没有公司根部门，f_root_dept_id 为 0。"
                        % (enterprise_id, enterprise_name))
        return

    # B/C. 根部门本身的各项属性。
    cur.execute(
        "SELECT f_enterprise_id, f_dept_name, f_parent_dept_id, f_dept_level, f_dept_path, f_status "
        "FROM t_department WHERE f_dept_id = %s", (root_dept_id,))
    row = cur.fetchone()
    if row is None:
        failures.append("企业 %d（%s）的 f_root_dept_id 指向不存在的部门 %d。"
                        % (enterprise_id, enterprise_name, root_dept_id))
        return

    owner_id, dept_name, parent_id, dept_level, dept_path, dept_status = row
    if owner_id != enterprise_id:
        failures.append("企业 %d 的根部门 %d 却属于企业 %d。"
                        % (enterprise_id, root_dept_id, owner_id))
    if parent_id != ROOT_PARENT_DEPT_ID:
        failures.append("企业 %d 的根部门 %d 的父部门是 %d，应为 %d。"
                        % (enterprise_id, root_dept_id, parent_id, ROOT_PARENT_DEPT_ID))
    if dept_level != ROOT_DEPT_LEVEL:
        failures.append("企业 %d 的根部门 %d 的层级是 %d，应为 %d。"
                        % (enterprise_id, root_dept_id, dept_level, ROOT_DEPT_LEVEL))
    if dept_status != DEPT_STATUS_NORMAL:
        failures.append("企业 %d 的根部门 %d 已被停用，状态为 %d。"
                        % (enterprise_id, root_dept_id, dept_status))
    if dept_name != enterprise_name:
        failures.append("企业 %d 的根部门名「%s」与企业全称「%s」不一致。"
                        % (enterprise_id, dept_name, enterprise_name))

    # D. 本企业不允许出现第二个根。
    cur.execute(
        "SELECT f_dept_id FROM t_department "
        "WHERE f_enterprise_id = %s AND f_parent_dept_id = %s AND f_status = %s",
        (enterprise_id, ROOT_PARENT_DEPT_ID, DEPT_STATUS_NORMAL))
    top_level_ids = [r[0] for r in cur.fetchall()]
    if len(top_level_ids) != 1 or top_level_ids[0] != root_dept_id:
        failures.append("企业 %d（%s）的顶级在用部门为 %s，应当只有根部门 %d 一个。"
                        % (enterprise_id, enterprise_name, top_level_ids, root_dept_id))

    # E. 本企业所有部门的路径都应在根部门子树内，且层级与路径段数自洽。
    root_prefix = "/%d/" % root_dept_id
    cur.execute(
        "SELECT f_dept_id, f_dept_path, f_dept_level FROM t_department WHERE f_enterprise_id = %s",
        (enterprise_id,))
    for dept_id, path, level in cur.fetchall():
        if not path.startswith(root_prefix):
            failures.append("企业 %d 的部门 %d 路径「%s」不在根部门 %d 的子树内。"
                            % (enterprise_id, dept_id, path, root_dept_id))
        expected_level = path_segment_count(path)
        if level != expected_level:
            failures.append("企业 %d 的部门 %d 层级为 %d，与其路径「%s」推出的 %d 不符。"
                            % (enterprise_id, dept_id, level, path, expected_level))


def check_orphan_users(cur, has_usertype_column, failures):
    """校验 F 项：不应存在有企业归属却无任何在职部门的普通用户。"""
    usertype_filter = ""
    if has_usertype_column:
        usertype_filter = " AND u.f_usertype = %d" % USER_TYPE_NORMAL

    cur.execute(
        "SELECT u.f_user_id, u.f_username, u.f_enterprise_id FROM t_user u "
        "LEFT JOIN t_department_member m "
        "       ON m.f_user_id = u.f_user_id AND m.f_status = %s "
        "WHERE u.f_enterprise_id <> 0 AND m.f_user_id IS NULL" + usertype_filter,
        (MEMBER_STATUS_ACTIVE,))
    for user_id, username, enterprise_id in cur.fetchall():
        failures.append("用户 %d（%s）属于企业 %d，却不在任何在职部门里，组织架构中看不到他。"
                        % (user_id, username, enterprise_id))


def survey_before_migration(cur, has_usertype_column):
    """迁移之前的现状调查：只读，不改任何数据。

    t_enterprise 还没有 f_root_dept_id 列时无从校验，改为把迁移将要处理的规模
    摸清楚：每家企业现在有几个顶级部门、全库有多少孤儿员工。据此可以预估迁移
    的影响面，也便于迁移之后拿数字对账。
    """
    print("t_enterprise 尚无 f_root_dept_id 列，说明还没有执行迁移。"
          "以下为迁移前的现状统计（只读）：\n")

    cur.execute(
        "SELECT f_enterprise_id, f_name FROM t_enterprise ORDER BY f_enterprise_id")
    enterprises = cur.fetchall()
    print("企业共 %d 家：" % len(enterprises))
    for enterprise_id, enterprise_name in enterprises:
        # 现在的顶级部门就是迁移后要被下沉一级、改挂到公司根部门下的那些。
        cur.execute(
            "SELECT f_dept_name FROM t_department "
            "WHERE f_enterprise_id = %s AND f_parent_dept_id = %s AND f_status = %s "
            "ORDER BY f_dept_order, f_dept_id",
            (enterprise_id, ROOT_PARENT_DEPT_ID, DEPT_STATUS_NORMAL))
        top_names = [r[0] for r in cur.fetchall()]

        cur.execute(
            "SELECT COUNT(*) FROM t_department WHERE f_enterprise_id = %s", (enterprise_id,))
        dept_total = cur.fetchone()[0]

        print("  - 企业 %d（%s）：部门共 %d 个，其中顶级在用部门 %d 个 %s"
              % (enterprise_id, enterprise_name, dept_total, len(top_names), top_names))

    # 孤儿员工：迁移会把他们补挂到各自企业的公司根部门。
    orphan_failures = []
    check_orphan_users(cur, has_usertype_column, orphan_failures)
    print("\n没有任何在职部门归属的员工共 %d 人：" % len(orphan_failures))
    for item in orphan_failures:
        print("  - " + item)
    if not orphan_failures:
        print("  （无）")

    print("\n现状调查结束。执行 migrations/v3_add_enterprise_root_dept.py 完成迁移后，"
          "再跑一次本脚本即可转入正式校验。")


def main():
    conn = pymysql.connect(host=DB_HOST, port=DB_PORT, user=DB_USER,
                           password=DB_PASSWORD, database=DB_NAME,
                           charset="utf8mb4", autocommit=True)
    try:
        cur = conn.cursor()

        has_usertype_column = column_exists(cur, "t_user", "f_usertype")

        # 迁移尚未执行时没有可校验的对象，转为只读的现状调查。
        if not column_exists(cur, "t_enterprise", "f_root_dept_id"):
            survey_before_migration(cur, has_usertype_column)
            return 0

        failures = []
        cur.execute(
            "SELECT f_enterprise_id, f_name, f_root_dept_id FROM t_enterprise "
            "ORDER BY f_enterprise_id")
        enterprises = cur.fetchall()
        for enterprise_id, enterprise_name, root_dept_id in enterprises:
            check_enterprise(cur, enterprise_id, enterprise_name, root_dept_id, failures)

        check_orphan_users(cur, has_usertype_column, failures)

        print("已检查企业 %d 家。" % len(enterprises))
        if failures:
            print("发现 %d 处不满足约束：" % len(failures), file=sys.stderr)
            for item in failures:
                print("  - " + item, file=sys.stderr)
            return 1

        print("全部校验通过。")
        return 0
    finally:
        conn.close()


if __name__ == "__main__":
    sys.exit(main())
