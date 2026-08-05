# -*- coding: utf-8 -*-
"""
Sanity check: 验证 testdata.sql 导入后关键表的关键计数与结构是否符合预期
balloonwj@qq.com   2026-05-27
"""
import io
import os
import sys

# 把 stdout 强制改成 UTF-8，避开 Windows 控制台 GBK 默认带来的 UnicodeEncodeError
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

import pymysql

HERE = os.path.dirname(os.path.abspath(__file__))
CONF_PATH = os.path.normpath(os.path.join(HERE, "..", "etc", "chatserver.conf"))


def read_conf(path):
    cfg = {}
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            cfg[k.strip()] = v.strip()
    return cfg


def main():
    cfg = read_conf(CONF_PATH)
    conn = pymysql.connect(
        host=cfg["dbserver"], user=cfg["dbuser"], password=cfg["dbpassword"],
        database=cfg["dbname"], charset="utf8mb4", autocommit=True)
    cur = conn.cursor()

    print("===== 关键计数 =====")
    queries = [
        ("企业数", "SELECT COUNT(*) FROM t_enterprise"),
        ("管理员数", "SELECT COUNT(*) FROM t_admin"),
        ("部门总数", "SELECT COUNT(*) FROM t_department"),
        ("明远科技部门数", "SELECT COUNT(*) FROM t_department WHERE f_enterprise_id=1"),
        ("用户总数（5001-5312）", "SELECT COUNT(*) FROM t_user WHERE f_user_id BETWEEN 5001 AND 5312"),
        ("新增用户数（5301-5312）", "SELECT COUNT(*) FROM t_user WHERE f_user_id BETWEEN 5301 AND 5312"),
        ("部门成员总数", "SELECT COUNT(*) FROM t_department_member WHERE f_enterprise_id IN (1,2,3)"),
        ("公告总数", "SELECT COUNT(*) FROM t_announcement WHERE f_announcement_id BETWEEN 1 AND 50"),
        ("公告投放目标数", "SELECT COUNT(*) FROM t_announcement_target WHERE f_announcement_id BETWEEN 1 AND 50"),
        ("公告已读回执数", "SELECT COUNT(*) FROM t_announcement_read WHERE f_announcement_id BETWEEN 1 AND 50"),
    ]
    for label, sql in queries:
        cur.execute(sql)
        n = cur.fetchone()[0]
        print("  %-30s %d" % (label, n))

    print()
    print("===== 明远科技部门树（按 level 排）=====")
    cur.execute(
        "SELECT f_dept_id, f_dept_level, f_parent_dept_id, f_dept_name, f_dept_path "
        "FROM t_department WHERE f_enterprise_id=1 ORDER BY f_dept_level, f_dept_id")
    rows = cur.fetchall()
    for did, lvl, pid, name, path in rows:
        indent = "  " * (lvl - 1)
        print("  L%d %s%s  id=%d  parent=%d" % (lvl, indent, name, did, pid))

    print()
    print("===== 各级部门员工数 =====")
    cur.execute(
        "SELECT d.f_dept_level, COUNT(DISTINCT m.f_user_id) "
        "FROM t_department d JOIN t_department_member m ON d.f_dept_id=m.f_dept_id "
        "WHERE d.f_enterprise_id=1 GROUP BY d.f_dept_level ORDER BY d.f_dept_level")
    for lvl, n in cur.fetchall():
        print("  L%d 员工数：%d" % (lvl, n))

    print()
    print("===== 新增公告（11-15）正文长度 =====")
    cur.execute(
        "SELECT f_announcement_id, f_title, CHAR_LENGTH(f_body) "
        "FROM t_announcement WHERE f_announcement_id BETWEEN 11 AND 15 ORDER BY f_announcement_id")
    for aid, title, blen in cur.fetchall():
        print("  #%d  %s  (%d 字)" % (aid, title, blen))

    print()
    print("===== 改写后公告（1-10）正文长度，应远大于原 30 字占位 =====")
    cur.execute(
        "SELECT f_announcement_id, CHAR_LENGTH(f_body) "
        "FROM t_announcement WHERE f_announcement_id BETWEEN 1 AND 10 ORDER BY f_announcement_id")
    for aid, blen in cur.fetchall():
        print("  #%d  %d 字" % (aid, blen))

    cur.close()
    conn.close()


if __name__ == "__main__":
    main()
