# -*- coding: utf-8 -*-
"""
FlamingoIM 测试数据导入器（SQLite 版）
balloonwj@qq.com   2026-08-03

把 ../testdata.sql 整体导入到 SQLite 本地库，与 import_testdata.py（MySQL 版）
配套，让两种后端能用同一份测试数据。

之所以能直接复用同一份 SQL：testdata.sql 是纯 DML（清理用的 DELETE + INSERT），
不含建表语句，也没有用到任何 MySQL 专有语法。经扫描确认，全文没有反引号标识符、
没有反斜杠转义、没有 NOW() / CURDATE() / FROM_UNIXTIME() 之类的函数调用、没有
INSERT IGNORE 与 ON DUPLICATE KEY UPDATE，唯一需要特殊处理的只有开头那句
`SET NAMES utf8`（SQLite 不认这条语句，且它对 SQLite 无意义 —— SQLite 的文本
一律按 UTF-8 存储）。

本脚本仍然把方言翻译与不兼容检测写全了，是为了将来 testdata.sql 长出新语句时
能当场报错，而不是静默导入一份错数据。

用法：
    python tools/import_testdata_sqlite.py                 # 用 chatserver.conf 里的 dbpath
    python tools/import_testdata_sqlite.py --db 路径       # 显式指定库文件
    python tools/import_testdata_sqlite.py --dry-run       # 只解析与检查，不落库

注意：导入后需要重启 chatserver。UserManager / DepartmentManager 等在 init() 时
把数据读进内存缓存并据此推算自增 ID 的起点，运行中直接改库它们是看不到的。
"""
import argparse
import io
import os
import re
import shutil
import sqlite3
import sys
import time

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER_ROOT = os.path.normpath(os.path.join(HERE, ".."))
CONF_PATH = os.path.join(SERVER_ROOT, "etc", "chatserver.conf")
SQL_PATH = os.path.join(SERVER_ROOT, "testdata.sql")

# dbpath 是相对于服务进程工作目录的。Windows 下 chatserver.exe 跑在 windowsproj/，
# Linux 下跑在 flamingoserver/，所以按这个优先级依次找。
DB_SEARCH_DIRS = [
    os.path.join(SERVER_ROOT, "windowsproj"),
    SERVER_ROOT,
]

# testdata.sql 里出现下列语法就说明它已经用上了 SQLite 不认识的 MySQL 专有写法，
# 必须停下来让人处理，不能猜着改。键是特征名，值是匹配用的正则。
INCOMPATIBLE_PATTERNS = [
    (u"ON DUPLICATE KEY UPDATE（SQLite 应写 ON CONFLICT ... DO UPDATE）",
     re.compile(r"(?i)\bON\s+DUPLICATE\s+KEY\b")),
    (u"反斜杠转义（MySQL 按转义符处理，SQLite 按普通字符处理，语义不同）",
     re.compile(r"\\")),
    (u"MySQL 日期函数 DATE_ADD / DATE_SUB / INTERVAL",
     re.compile(r"(?i)\b(DATE_ADD|DATE_SUB|INTERVAL)\b")),
    (u"MySQL 时间函数 NOW / CURDATE / FROM_UNIXTIME / UNIX_TIMESTAMP",
     re.compile(r"(?i)\b(NOW|CURDATE|FROM_UNIXTIME|UNIX_TIMESTAMP)\s*\(")),
]

# 可以安全丢弃的语句：SQLite 不支持，且丢掉不影响结果
DISCARD_PATTERNS = [
    re.compile(r"(?i)^SET\s+NAMES\b"),
    re.compile(r"(?i)^SET\s+FOREIGN_KEY_CHECKS\b"),
    re.compile(r"(?i)^SET\s+SQL_MODE\b"),
]


def split_statements(text):
    """按分号切分 SQL 语句。

    需要自己写而不能直接 text.split(';')，因为分号可能出现在字符串字面量里
    （测试数据里的中文备注就带分号）。扫描时同时跳过 `--` 行注释与 `/* */`
    块注释，这样注释里的分号和引号都不会干扰切分。

    返回：语句字符串列表，已去掉首尾空白，不含空语句。
    """
    statements = []
    buf = []
    i = 0
    n = len(text)
    in_string = False        # 是否位于单引号字符串字面量内部
    while i < n:
        ch = text[i]

        if in_string:
            buf.append(ch)
            if ch == "'":
                # SQLite / MySQL 都用两个连续单引号表示一个字面单引号
                if i + 1 < n and text[i + 1] == "'":
                    buf.append(text[i + 1])
                    i += 2
                    continue
                in_string = False
            i += 1
            continue

        # 以下是「不在字符串里」的分支
        if ch == "'":
            in_string = True
            buf.append(ch)
            i += 1
            continue

        if ch == "-" and i + 1 < n and text[i + 1] == "-":
            # 行注释，直接吃到行尾（保留换行，避免把两条语句粘在一起）
            j = text.find("\n", i)
            if j < 0:
                break
            i = j
            continue

        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                break
            i = j + 2
            continue

        if ch == ";":
            statements.append("".join(buf).strip())
            buf = []
            i += 1
            continue

        buf.append(ch)
        i += 1

    tail = "".join(buf).strip()
    if tail:
        statements.append(tail)

    return [s for s in statements if s]


def translate_statement(sql):
    """把一条 MySQL 语句改写成 SQLite 能执行的等价语句。

    目前只需要两类改写，都保持语义完全一致：
      1. 反引号标识符 `t_user` -> "t_user"（SQLite 其实也认反引号，改成标准
         双引号只是为了让报错信息更好读）
      2. INSERT IGNORE INTO -> INSERT OR IGNORE INTO

    返回：改写后的语句。
    """
    sql = re.sub(r"`(\w+)`", r'"\1"', sql)
    sql = re.sub(r"(?i)\bINSERT\s+IGNORE\s+INTO\b", "INSERT OR IGNORE INTO", sql)
    return sql


def read_conf(path):
    """读 key=value 形式的配置文件，返回 dict。以 # 开头的行按注释跳过。"""
    cfg = {}
    with io.open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            cfg[k.strip()] = v.strip()
    return cfg


def resolve_db_path(cfg, explicit):
    """定位 SQLite 库文件。

    explicit 非空时直接用它；否则取配置里的 dbpath，按 DB_SEARCH_DIRS 的顺序
    找一个已存在的文件。找不到就返回第一候选并由调用方报错 —— 这里不自动建库，
    因为空库里没有表结构，导入只会得到一串 no such table。
    """
    if explicit:
        return os.path.abspath(explicit)

    dbpath = cfg.get("dbpath", "data/flamingo.db")
    for d in DB_SEARCH_DIRS:
        candidate = os.path.normpath(os.path.join(d, dbpath))
        if os.path.isfile(candidate):
            return candidate

    return os.path.normpath(os.path.join(DB_SEARCH_DIRS[0], dbpath))


def check_incompatible(text):
    """扫描整份 SQL，命中不兼容特征就返回描述列表；全部通过则返回空列表。"""
    hits = []
    for name, pattern in INCOMPATIBLE_PATTERNS:
        found = pattern.findall(text)
        if found:
            hits.append(u"%s：命中 %d 处" % (name, len(found)))
    return hits


def collect_touched_tables(statements):
    """从语句列表里收集被 INSERT / DELETE 命中的表名，用于导入后核对行数。"""
    tables = []
    seen = set()
    for sql in statements:
        m = re.match(r'(?is)^\s*(?:INSERT(?:\s+OR\s+\w+)?\s+INTO|DELETE\s+FROM)\s+["`]?(\w+)["`]?', sql)
        if m:
            name = m.group(1)
            if name not in seen:
                seen.add(name)
                tables.append(name)
    return tables


def count_rows(con, tables):
    """统计每张表的行数，返回 {表名: 行数}。表不存在时记为 None。"""
    counts = {}
    for t in tables:
        try:
            counts[t] = con.execute('SELECT COUNT(*) FROM "%s"' % t).fetchone()[0]
        except sqlite3.Error:
            counts[t] = None
    return counts


def main():
    parser = argparse.ArgumentParser(
        description=u"把 testdata.sql 导入 SQLite 本地库")
    parser.add_argument("--db", default="",
                        help=u"SQLite 库文件路径；默认取 etc/chatserver.conf 的 dbpath")
    parser.add_argument("--sql", default=SQL_PATH,
                        help=u"要导入的 SQL 文件；默认 flamingoserver/testdata.sql")
    parser.add_argument("--dry-run", action="store_true",
                        help=u"只解析与兼容性检查，不写库")
    parser.add_argument("--no-backup", action="store_true",
                        help=u"不备份原库文件（默认会先复制一份 .bak）")
    args = parser.parse_args()

    cfg = read_conf(CONF_PATH)
    db_path = resolve_db_path(cfg, args.db)
    print(u"配置文件 : %s (dbtype=%s, dbpath=%s)"
          % (CONF_PATH, cfg.get("dbtype", "?"), cfg.get("dbpath", "?")))
    print(u"目标库   : %s" % db_path)

    if not os.path.isfile(db_path):
        print(u"错误     : 库文件不存在。请先以 dbtype=sqlite 启动一次 chatserver，"
              u"让它把 28 张表建出来，再运行本脚本。")
        return 1

    with io.open(args.sql, "r", encoding="utf-8-sig") as f:
        text = f.read()
    print(u"数据文件 : %s (%d KB)" % (args.sql, len(text.encode("utf-8")) // 1024))

    hits = check_incompatible(text)
    if hits:
        print(u"错误     : 该 SQL 含 SQLite 不兼容的 MySQL 专有语法，已中止：")
        for h in hits:
            print(u"           - %s" % h)
        return 1
    print(u"兼容检查 : 通过，未发现 MySQL 专有语法")

    raw_statements = split_statements(text)
    statements = []
    discarded = 0
    for sql in raw_statements:
        if any(p.search(sql) for p in DISCARD_PATTERNS):
            discarded += 1
            continue
        statements.append(translate_statement(sql))
    print(u"语句解析 : 可执行 %d 条，丢弃 %d 条（SET NAMES 等 SQLite 无对应语句）"
          % (len(statements), discarded))

    tables = collect_touched_tables(statements)
    print(u"涉及表   : %d 张" % len(tables))

    if args.dry_run:
        print(u"dry-run  : 未写库，到此结束")
        return 0

    if not args.no_backup:
        backup = db_path + ".bak"
        shutil.copyfile(db_path, backup)
        print(u"已备份   : %s" % backup)

    con = sqlite3.connect(db_path)
    try:
        before = count_rows(con, tables)

        t0 = time.time()
        # 用一个显式事务包住全部语句：任何一条失败就整体回滚，不留半截数据
        con.execute("BEGIN")
        for idx, sql in enumerate(statements):
            try:
                con.execute(sql)
            except sqlite3.Error as e:
                con.rollback()
                print(u"失败     : 第 %d 条语句出错，已回滚，库内数据未变更" % (idx + 1))
                print(u"           %s" % e)
                print(u"           语句片段: %s" % sql[:200].replace("\n", " "))
                return 1
        con.commit()
        elapsed = time.time() - t0

        after = count_rows(con, tables)
        print(u"成功     : %d 条语句全部执行完成并已提交，耗时 %.2fs"
              % (len(statements), elapsed))
        print()
        print(u"%-30s %10s %10s %10s" % (u"表名", u"导入前", u"导入后", u"净增"))
        print(u"-" * 64)
        total_after = 0
        for t in tables:
            b = before.get(t)
            a = after.get(t)
            if a is None:
                print(u"%-30s %10s %10s %10s" % (t, b, u"表不存在", u"-"))
                continue
            total_after += a
            print(u"%-30s %10d %10d %+10d" % (t, b or 0, a, a - (b or 0)))
        print(u"-" * 64)
        print(u"%-30s %10s %10d" % (u"合计", u"", total_after))
        print()
        print(u"提醒     : 请重启 chatserver，各 Manager 在 init() 时才会把这批数据"
              u"读进缓存并重新推算自增 ID 起点。")
    finally:
        con.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
