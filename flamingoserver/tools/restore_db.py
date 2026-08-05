# -*- coding: utf-8 -*-
"""
FlamingoIM 整库恢复器
balloonwj@qq.com   2026-05-28

适用场景：服务端 MySQL 上 flamingo 库整个没了（不只是测试数据丢了），需要
从零重建库 + 表 + 测试数据。chatserver 自身启动也能自动建库建表（见
CMysqlManager::init），但本脚本让本地无需动 chatserver 进程即可完成全套
恢复。

为什么不跑 migrations 目录下那 4 个 SQL：
    它们都是给【存量库】补漏用的（v2 密码迁移 / 加 column / 加 unique
    key / drop 旧 t_chatmsg 表）。table.sql 已经是 v2 全套最新 schema
    （含 uk_f_username / uk_ent_employee_no 两个 UNIQUE），从零建库走它
    即可。v2_add_t_user_unique_keys.sql 的注释亲口承认这点。

流程（任一步失败都会让上一步 commit 的内容保留 —— DDL 不在事务里 ——
所以脚本设计成可重复执行：每步都 IF NOT EXISTS / DELETE+INSERT）：
  1. 读 ../etc/chatserver.conf 拿 dbserver / dbuser / dbpassword / dbname
  2. 连服务器（不指定 database）→ CREATE DATABASE IF NOT EXISTS dbname
  3. 切到 dbname，读 ../table.sql：
       · 把每行的 `//...` 剥成空（table.sql 用 // 作行注释，非标准 SQL）
       · 按空行切块得到 27 个 CREATE TABLE 语句
       · 每条末尾补 `;`，MULTI_STATEMENTS 一次提交
       · 用 CREATE TABLE IF NOT EXISTS，可重复跑
  4. 读 ../testdata.sql 同 MULTI_STATEMENTS 一次提交（它自身先 DELETE
     再 INSERT，idempotent）
  5. 跑一条 SELECT 统计 t_user / t_enterprise / t_department 行数,
     打印出来给人快速 sanity check
"""
import os
import re
import sys
import time

try:
    import pymysql
    from pymysql.constants import CLIENT
except ImportError:
    print("ERROR: pymysql 未安装，运行 `pip install pymysql` 后重试")
    sys.exit(1)

HERE = os.path.dirname(os.path.abspath(__file__))
CONF_PATH      = os.path.normpath(os.path.join(HERE, "..", "etc", "chatserver.conf"))
TABLE_SQL_PATH = os.path.normpath(os.path.join(HERE, "..", "table.sql"))
SEED_SQL_PATH  = os.path.normpath(os.path.join(HERE, "..", "testdata.sql"))


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


def strip_slashslash_comments(text):
    """剥掉 table.sql 里 `//` 起的行注释,但跳过引号内的 //（COMMENT 字符串
    里可能有路径之类）。简化处理:逐行扫,在引号外遇到 // 截断到行尾。
    """
    out_lines = []
    for line in text.splitlines():
        in_squote = False
        in_dquote = False
        cut_at = -1
        i = 0
        while i < len(line):
            ch = line[i]
            if ch == "'" and not in_dquote:
                in_squote = not in_squote
            elif ch == '"' and not in_squote:
                in_dquote = not in_dquote
            elif (ch == "/" and i + 1 < len(line) and line[i+1] == "/"
                  and not in_squote and not in_dquote):
                cut_at = i
                break
            i += 1
        if cut_at >= 0:
            out_lines.append(line[:cut_at].rstrip())
        else:
            out_lines.append(line.rstrip())
    return "\n".join(out_lines)


def split_create_statements(text):
    """按"连续空行"把 table.sql 切成单个 CREATE TABLE 块,过滤纯空块。"""
    blocks = re.split(r"\n\s*\n", text)
    stmts = []
    for b in blocks:
        b = b.strip()
        if not b:
            continue
        stmts.append(b)
    return stmts


def exec_multi(conn, sql_text, label):
    """MULTI_STATEMENTS 模式一次提交。逐个 nextset 消费,否则后续错误会被吞。"""
    t0 = time.time()
    cur = conn.cursor()
    try:
        cur.execute(sql_text)
        rs = 1
        while cur.nextset():
            rs += 1
        conn.commit()
        print("  OK  [%s] 结果集=%d  耗时=%.2fs" % (label, rs, time.time() - t0))
    except Exception as e:
        conn.rollback()
        print("  FAIL[%s] %s" % (label, e))
        raise
    finally:
        cur.close()


def main():
    cfg = read_conf(CONF_PATH)
    host = cfg["dbserver"]
    user = cfg["dbuser"]
    pwd  = cfg["dbpassword"]
    db   = cfg["dbname"]
    print("config   : %s  user=%s  db=%s" % (host, user, db))

    # ---- 步骤 1:CREATE DATABASE(连服务器,不指定 database) ----
    print("[1/4] 连服务器 + CREATE DATABASE IF NOT EXISTS %s" % db)
    conn0 = pymysql.connect(host=host, user=user, password=pwd,
                            charset="utf8mb4",
                            client_flag=CLIENT.MULTI_STATEMENTS,
                            autocommit=True)
    try:
        cur = conn0.cursor()
        cur.execute("CREATE DATABASE IF NOT EXISTS `%s` DEFAULT CHARACTER SET utf8" % db)
        cur.close()
        print("  OK")
    finally:
        conn0.close()

    # ---- 步骤 2:跑 table.sql 建 27 个表 ----
    print("[2/4] 读 table.sql + 剥 // 注释 + 拼分号 + MULTI_STATEMENTS")
    with open(TABLE_SQL_PATH, "rb") as f:
        raw = f.read().decode("utf-8-sig")
    stripped = strip_slashslash_comments(raw)
    stmts = split_create_statements(stripped)
    print("  table.sql 解析出 %d 条 statement" % len(stmts))
    if len(stmts) < 20:
        print("  ABORT: 解析出的 statement 数 < 20,跟预期 27 个不符,中止")
        sys.exit(2)
    # 每条末尾补 ; 后用 MULTI_STATEMENTS 一次发
    composed = ";\n".join(stmts) + ";\n"

    conn = pymysql.connect(host=host, user=user, password=pwd, database=db,
                           charset="utf8mb4",
                           client_flag=CLIENT.MULTI_STATEMENTS,
                           autocommit=False)
    try:
        exec_multi(conn, composed, "table.sql")

        # ---- 步骤 3:跑 testdata.sql 灌测试数据 ----
        print("[3/4] 读 testdata.sql + MULTI_STATEMENTS")
        with open(SEED_SQL_PATH, "rb") as f:
            seed = f.read().decode("utf-8-sig")
        print("  testdata.sql 大小=%d KB" % (len(seed.encode('utf-8')) // 1024))
        exec_multi(conn, seed, "testdata.sql")

        # ---- 步骤 4:sanity check ----
        print("[4/4] 抽检 t_user / t_enterprise / t_department / t_admin 行数")
        cur = conn.cursor()
        for tbl in ("t_user", "t_enterprise", "t_department", "t_admin",
                    "t_announcement", "t_service_account"):
            cur.execute("SELECT COUNT(*) FROM `%s`" % tbl)
            row = cur.fetchone()
            print("  %-22s %d 行" % (tbl, row[0]))
        cur.close()
    finally:
        conn.close()

    print("DONE")


if __name__ == "__main__":
    main()
