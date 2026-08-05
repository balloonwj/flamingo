# -*- coding: utf-8 -*-
"""
一次性清理：把 gen_testdata.py 残留的企业 4（锦程生物医药）相关数据从库里删干净
balloonwj@qq.com   2026-05-27

testdata.sql 的清理段只覆盖企业 1-3，所以之前 gen_testdata.py 写过的企业 4 数据
（1 个企业、1 个管理员、10 个部门、500 名员工、8 个服务号、12 条公告、若干审计日志）
一直残留在库里。本脚本按 f_enterprise_id=4 / sender_admin_id=5 / service_account
反查的方式逐表清理，全部成功才 commit。
"""
import io
import os
import sys

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
        database=cfg["dbname"], charset="utf8mb4", autocommit=False)
    cur = conn.cursor()

    print("===== 清理前关键计数 =====")
    counts_before = {
        "t_enterprise where id=4":           "SELECT COUNT(*) FROM t_enterprise WHERE f_enterprise_id=4",
        "t_admin where id=5":                "SELECT COUNT(*) FROM t_admin WHERE f_admin_id=5",
        "t_department where ent=4":          "SELECT COUNT(*) FROM t_department WHERE f_enterprise_id=4",
        "t_department_member where ent=4":   "SELECT COUNT(*) FROM t_department_member WHERE f_enterprise_id=4",
        "t_user where ent=4":                "SELECT COUNT(*) FROM t_user WHERE f_enterprise_id=4",
        "t_admin_audit_log where ent=4":     "SELECT COUNT(*) FROM t_admin_audit_log WHERE f_enterprise_id=4",
        "t_service_account where ent=4":     "SELECT COUNT(*) FROM t_service_account WHERE f_enterprise_id=4",
        "t_announcement by admin=5":         "SELECT COUNT(*) FROM t_announcement WHERE f_sender_admin_id=5",
    }
    for label, sql in counts_before.items():
        cur.execute(sql)
        print("  %-40s %d" % (label, cur.fetchone()[0]))

    # 反查待删 service_id 和 announcement_id
    cur.execute("SELECT f_service_id FROM t_service_account WHERE f_enterprise_id=4")
    service_ids = [r[0] for r in cur.fetchall()]
    cur.execute("SELECT f_announcement_id FROM t_announcement WHERE f_sender_admin_id=5")
    ann_ids = [r[0] for r in cur.fetchall()]
    print("  关联 service_id：%s" % service_ids)
    print("  关联 announcement_id：%s" % ann_ids)

    print()
    print("===== 执行删除（一个事务）=====")

    def run_delete(sql, params=None):
        cur.execute(sql, params or ())
        print("  -%4d  %s" % (cur.rowcount, sql))

    try:
        # 先清外围依赖再清主表，避免外键问题（库里目前没硬外键，但顺序仍然语义友好）
        if service_ids:
            placeholders = ",".join(["%s"] * len(service_ids))
            run_delete(
                "DELETE FROM t_service_account_binding WHERE f_service_id IN (%s)" % placeholders,
                service_ids)
        run_delete("DELETE FROM t_service_account WHERE f_enterprise_id=4")

        if ann_ids:
            placeholders = ",".join(["%s"] * len(ann_ids))
            run_delete(
                "DELETE FROM t_announcement_read WHERE f_announcement_id IN (%s)" % placeholders,
                ann_ids)
            run_delete(
                "DELETE FROM t_announcement_target WHERE f_announcement_id IN (%s)" % placeholders,
                ann_ids)
            run_delete(
                "DELETE FROM t_announcement WHERE f_announcement_id IN (%s)" % placeholders,
                ann_ids)

        run_delete("DELETE FROM t_admin_audit_log WHERE f_enterprise_id=4")
        run_delete("DELETE FROM t_department_member WHERE f_enterprise_id=4")
        run_delete("DELETE FROM t_user WHERE f_enterprise_id=4")
        run_delete("DELETE FROM t_department WHERE f_enterprise_id=4")
        run_delete("DELETE FROM t_admin WHERE f_admin_id=5")
        run_delete("DELETE FROM t_enterprise WHERE f_enterprise_id=4")

        conn.commit()
        print()
        print("OK       : 已 commit")
    except Exception as e:
        conn.rollback()
        print("FAIL     : %s（已 rollback）" % e)
        raise
    finally:
        cur.close()
        conn.close()


if __name__ == "__main__":
    main()
