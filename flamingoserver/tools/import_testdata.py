# -*- coding: utf-8 -*-
"""
FlamingoIM 测试数据导入器
balloonwj@qq.com   2026-05-27

把 ../testdata.sql 整体导入到 chatserver.conf 配置的库。
采用 pymysql 的 multi-statements 模式，把整份文件作为一个语句串提交给
服务端解析，能发现语法错、外键冲突、主键冲突等运行期问题。
全部成功才 commit；任何一句失败立刻 rollback。
"""
import os
import sys
import time

try:
    import pymysql
    from pymysql.constants import CLIENT
except ImportError:
    print("ERROR: pymysql 未安装，运行 `pip install pymysql` 后重试")
    sys.exit(1)

HERE = os.path.dirname(os.path.abspath(__file__))
CONF_PATH = os.path.normpath(os.path.join(HERE, "..", "etc", "chatserver.conf"))
SQL_PATH = os.path.normpath(os.path.join(HERE, "..", "testdata.sql"))


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
    print("config   : %s -> %s/%s" % (CONF_PATH, cfg.get("dbserver"), cfg.get("dbname")))

    # 整段读入；utf-8-sig 可同时兼容带 BOM / 不带 BOM 两种编码
    with open(SQL_PATH, "rb") as f:
        sql_bytes = f.read()
    sql_text = sql_bytes.decode("utf-8-sig")
    print("sql file : %s (%d KB)" % (SQL_PATH, len(sql_bytes) // 1024))

    conn = pymysql.connect(
        host=cfg["dbserver"], user=cfg["dbuser"], password=cfg["dbpassword"],
        database=cfg["dbname"], charset="utf8mb4",
        client_flag=CLIENT.MULTI_STATEMENTS, autocommit=False)
    print("connected: %s" % cfg["dbserver"])

    t0 = time.time()
    try:
        cur = conn.cursor()
        cur.execute(sql_text)
        # multi_statements 下逐个消费结果集，否则后续语句的错误会被吞掉
        rs_count = 1
        while cur.nextset():
            rs_count += 1
        cur.close()
        conn.commit()
        print("OK       : 全部语句执行成功，已 commit；结果集数=%d，耗时=%.2fs"
              % (rs_count, time.time() - t0))
    except Exception as e:
        conn.rollback()
        print("FAIL     : %s" % e)
        print("           已 rollback，库内数据未变更")
        raise
    finally:
        conn.close()


if __name__ == "__main__":
    main()
