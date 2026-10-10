# -*- coding: utf-8 -*-
"""
把现有 MySQL 库的数据整体迁移到本地 SQLite 库
balloonwj@qq.com   2026-08-03

用途：已有一套跑在 MySQL 上的部署（含真实/测试数据），想改用本地 SQLite 模式时，
把 MySQL 里的业务数据原样搬过来，省得从空库重新造数据。

设计要点：
  1. **只搬数据，不搬表结构。** 目标库的 28 张表必须已经由 chatserver 以
     dbtype=sqlite 启动时按 table_sqlite.sql 建好。两边表结构未必完全同版本
     （MySQL 那边往往落后几个版本），本脚本按「列名交集」搬运：
       - MySQL 有、SQLite 没有的列 —— 说明目标库结构更旧，搬过去会丢数据，
         直接报错停下，让人先升级目标库结构；
       - SQLite 有、MySQL 没有的列 —— 说明目标库结构更新，这些列取建表时的
         DEFAULT，脚本会把清单打出来备查。
  2. **整表替换。** 每张待迁移的表先 DELETE 再灌入，避免与库里原有数据混在一起
     产生主键冲突或半新半旧的状态。只在 MySQL 侧存在的表才会被清空，SQLite 独有
     的表（MySQL 侧结构较旧、尚未建出的表）原样保留、不动。
  3. **全程一个事务。** 任何一步失败整体回滚，库不会停在搬了一半的状态。开工前
     还会把库文件复制一份 .bak。
  4. **搬完逐表核对行数**，两边对不上就报错。

注意：
  - 运行前请先停掉 chatserver。各 Manager 在 init() 时把数据读进内存缓存并据此
    推算自增 ID 起点，边跑边换库会读到过期缓存。搬完重启即可。
  - 自增列（INTEGER PRIMARY KEY AUTOINCREMENT）连同原值一起搬，SQLite 会自动把
    sqlite_sequence 抬到已插入的最大值，后续新增不会撞号。

用法：
    python tools/migrate_mysql_to_sqlite.py                    # 参数取自 chatserver.conf
    python tools/migrate_mysql_to_sqlite.py --dry-run          # 只做结构比对，不写库
    python tools/migrate_mysql_to_sqlite.py --mysql-host 1.2.3.4 --mysql-db flamingo
"""
import argparse
import datetime
import decimal
import io
import os
import shutil
import sqlite3
import sys
import time

try:
    import pymysql
except ImportError:
    print(u"ERROR: pymysql 未安装，运行 `pip install pymysql` 后重试")
    sys.exit(1)

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER_ROOT = os.path.normpath(os.path.join(HERE, ".."))
CONF_PATH = os.path.join(SERVER_ROOT, "etc", "chatserver.conf")

# dbpath 相对服务进程工作目录：Windows 下 chatserver.exe 跑在 windowsproj/，
# Linux 下跑在 flamingoserver/，按此顺序找已存在的库文件。
DB_SEARCH_DIRS = [
    os.path.join(SERVER_ROOT, "windowsproj"),
    SERVER_ROOT,
]

# 一次 executemany 提交的行数。太小则往返次数多，太大则内存占用高；
# 本库最大的表 t_announcement_read 也就几百行，1000 足够一次吃下。
INSERT_BATCH_ROWS = 1000

# MySQL DATETIME / DATE 落到 SQLite 后的文本格式。必须与 table_sqlite.sql 里
# CURRENT_TIMESTAMP 生成的格式一致，否则字符串比较的日期区间查询会失效。
DATETIME_FORMAT = "%Y-%m-%d %H:%M:%S"
DATE_FORMAT = "%Y-%m-%d"


class MigrateError(Exception):
    """迁移失败"""
    pass


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
    """定位目标 SQLite 库文件；不自动建库（空库没有表结构，搬过去只会一串 no such table）。"""
    if explicit:
        return os.path.abspath(explicit)

    dbpath = cfg.get("dbpath", "data/flamingo.db")
    for d in DB_SEARCH_DIRS:
        candidate = os.path.normpath(os.path.join(d, dbpath))
        if os.path.isfile(candidate):
            return candidate

    return os.path.normpath(os.path.join(DB_SEARCH_DIRS[0], dbpath))


def convert_value(v):
    """把 pymysql 取回的值转成 sqlite3 能直接绑定的类型。

    需要转换的有几类：
      datetime/date/time  -> 文本，格式与 SQLite 侧 CURRENT_TIMESTAMP 一致
      Decimal             -> 文本，避免转 float 丢精度
      bytes               -> 能按 UTF-8 解出来就转成文本，详见下面的说明
      其余（int/str/None）sqlite3 原生支持，原样返回

    关于 bytes：MySQL 侧 t_chatmsg.f_content、t_user.f_teaminfo 声明的是 blob，
    pymysql 因此返回 bytes，直接绑定进 SQLite 会存成 BLOB。功能上没问题——
    SQLite 表里这两列也声明为 BLOB，DatabaseSqlite.cpp 的取值分支明确处理了
    SQLITE_BLOB，LIKE 匹配也照常工作——但 chatserver 自己写入时用的是字符串
    字面量，落库是 TEXT。两者混在同一列里是个隐患：SQLite 里 BLOB 与 TEXT
    永远不相等，将来只要有人写出 `f_content = '...'` 这样的等值比较，迁移过来
    的行就会一条都匹配不上。所以这里统一按 chatserver 的写法转成文本。
    确实是二进制、解不出 UTF-8 的内容则原样保留为 bytes，不做破坏性转换。
    """
    if isinstance(v, datetime.datetime):
        return v.strftime(DATETIME_FORMAT)
    if isinstance(v, datetime.date):
        return v.strftime(DATE_FORMAT)
    if isinstance(v, datetime.timedelta):
        # MySQL 的 TIME 类型，pymysql 给的是 timedelta
        return str(v)
    if isinstance(v, decimal.Decimal):
        return str(v)
    if isinstance(v, (bytes, bytearray)):
        try:
            return bytes(v).decode("utf-8")
        except UnicodeDecodeError:
            return bytes(v)
    return v


def load_mysql_schema(mycur, dbname):
    """取 MySQL 侧 {表名: [列名, ...]}，列顺序按表定义顺序。"""
    mycur.execute(
        "SELECT TABLE_NAME, COLUMN_NAME FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA=%s ORDER BY TABLE_NAME, ORDINAL_POSITION", (dbname,))
    schema = {}
    for table, column in mycur.fetchall():
        schema.setdefault(table, []).append(column)
    return schema


def load_sqlite_schema(lite):
    """取 SQLite 侧 {表名: [列名, ...]}。"""
    schema = {}
    for (table,) in lite.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'"):
        schema[table] = [r[1] for r in lite.execute('PRAGMA table_info("%s")' % table)]
    return schema


def preflight(my_schema, lite_schema):
    """结构比对。返回 (待迁移表列表, SQLite 独有列清单)；发现会丢数据的情况直接抛错。

    待迁移表 = 两边都有的表，按名字排序。
    """
    only_mysql_tables = sorted(set(my_schema) - set(lite_schema))
    if only_mysql_tables:
        raise MigrateError(
            u"下列表只在 MySQL 侧存在，目标库没有，搬过去会丢整张表：%s\n"
            u"           说明目标库表结构偏旧。请先用最新的 table_mysql.sql 重跑 "
            u"tools/gen_table_sqlite.py，再以 dbtype=sqlite 启动一次 chatserver 建表。"
            % ", ".join(only_mysql_tables))

    tables = sorted(set(my_schema) & set(lite_schema))

    missing = []          # MySQL 有、SQLite 没有的列，一旦出现就是丢数据
    defaulted = []        # SQLite 有、MySQL 没有的列，取建表默认值
    for t in tables:
        litcols = lite_schema[t]
        mycols = my_schema[t]
        for c in mycols:
            if c not in litcols:
                missing.append("%s.%s" % (t, c))
        for c in litcols:
            if c not in mycols:
                defaulted.append("%s.%s" % (t, c))

    if missing:
        raise MigrateError(
            u"下列列只在 MySQL 侧存在，目标库没有对应列，搬过去会丢数据：%s\n"
            u"           说明目标库表结构偏旧，处理办法同上。" % ", ".join(missing))

    return tables, defaulted


def migrate_table(mycon, lite, table, columns):
    """把一张表从 MySQL 搬到 SQLite。调用方负责事务与异常处理。

    mycon    已连接的 pymysql 连接
    lite     已连接的 sqlite3 连接
    table    表名
    columns  要搬的列名列表（两边的交集，顺序以 MySQL 侧为准）

    返回：实际插入的行数。
    """
    collist = ", ".join('`%s`' % c for c in columns)
    mycur = mycon.cursor()
    mycur.execute("SELECT %s FROM `%s`" % (collist, table))

    lite.execute('DELETE FROM "%s"' % table)

    insert_sql = 'INSERT INTO "%s" (%s) VALUES (%s)' % (
        table,
        ", ".join('"%s"' % c for c in columns),
        ", ".join(["?"] * len(columns)))

    inserted = 0
    while True:
        rows = mycur.fetchmany(INSERT_BATCH_ROWS)
        if not rows:
            break
        converted = []
        for row in rows:
            converted.append(tuple(convert_value(v) for v in row))
        lite.executemany(insert_sql, converted)
        inserted += len(converted)

    mycur.close()
    return inserted


def main():
    parser = argparse.ArgumentParser(
        description=u"把 MySQL 库的数据整体迁移到本地 SQLite 库")
    parser.add_argument("--mysql-host", default="")
    parser.add_argument("--mysql-port", type=int, default=3306)
    parser.add_argument("--mysql-user", default="")
    parser.add_argument("--mysql-password", default="")
    parser.add_argument("--mysql-db", default="")
    parser.add_argument("--db", default="",
                        help=u"目标 SQLite 库文件；默认取 chatserver.conf 的 dbpath")
    parser.add_argument("--dry-run", action="store_true",
                        help=u"只做结构比对与行数统计，不写库")
    parser.add_argument("--no-backup", action="store_true",
                        help=u"不备份目标库文件（默认会先复制一份 .bak）")
    args = parser.parse_args()

    cfg = read_conf(CONF_PATH)
    host = args.mysql_host or cfg.get("dbserver", "127.0.0.1")
    user = args.mysql_user or cfg.get("dbuser", "root")
    password = args.mysql_password or cfg.get("dbpassword", "")
    dbname = args.mysql_db or cfg.get("dbname", "flamingo")
    db_path = resolve_db_path(cfg, args.db)

    print(u"源  MySQL : %s@%s:%d/%s" % (user, host, args.mysql_port, dbname))
    print(u"目标 SQLite: %s" % db_path)

    if not os.path.isfile(db_path):
        raise MigrateError(
            u"目标库文件不存在。请先以 dbtype=sqlite 启动一次 chatserver 建好表结构。")

    mycon = pymysql.connect(host=host, port=args.mysql_port, user=user,
                            password=password, database=dbname, charset="utf8mb4")
    lite = sqlite3.connect(db_path)

    try:
        mycur = mycon.cursor()
        my_schema = load_mysql_schema(mycur, dbname)
        lite_schema = load_sqlite_schema(lite)
        print(u"表数量    : MySQL %d 张，SQLite %d 张"
              % (len(my_schema), len(lite_schema)))

        tables, defaulted = preflight(my_schema, lite_schema)
        print(u"结构比对  : 通过，%d 张表可迁移" % len(tables))

        untouched = sorted(set(lite_schema) - set(my_schema))
        if untouched:
            print(u"            目标库独有的表不动：%s" % ", ".join(untouched))
        if defaulted:
            print(u"            目标库比源库多出的列将取建表默认值（%d 个）："
                  % len(defaulted))
            for c in defaulted:
                print(u"              %s" % c)

        # 先统计源库各表行数，dry-run 也能看到将搬多少
        source_counts = {}
        for t in tables:
            c = mycon.cursor()
            c.execute("SELECT COUNT(*) FROM `%s`" % t)
            source_counts[t] = c.fetchone()[0]
            c.close()
        print(u"源库总行数: %d" % sum(source_counts.values()))

        if args.dry_run:
            print(u"dry-run   : 未写库，到此结束")
            return 0

        if not args.no_backup:
            backup = db_path + ".bak"
            shutil.copyfile(db_path, backup)
            print(u"已备份    : %s" % backup)

        print()
        print(u"%-30s %10s %10s %10s" % (u"表名", u"迁移前", u"源库", u"迁移后"))
        print(u"-" * 64)

        t0 = time.time()
        lite.execute("BEGIN")
        results = []
        for t in tables:
            before = lite.execute('SELECT COUNT(*) FROM "%s"' % t).fetchone()[0]
            migrate_table(mycon, lite, t, my_schema[t])
            results.append((t, before))
        lite.commit()
        elapsed = time.time() - t0

        # 提交之后再核对，确认是真的落盘了而不是只在事务缓冲里
        total = 0
        mismatch = []
        for t, before in results:
            after = lite.execute('SELECT COUNT(*) FROM "%s"' % t).fetchone()[0]
            total += after
            flag = u""
            if after != source_counts[t]:
                flag = u"  <-- 不一致"
                mismatch.append(t)
            print(u"%-30s %10d %10d %10d%s"
                  % (t, before, source_counts[t], after, flag))
        print(u"-" * 64)
        print(u"%-30s %10s %10d %10d" % (u"合计", u"", sum(source_counts.values()), total))
        print()

        if mismatch:
            raise MigrateError(u"下列表迁移前后行数与源库对不上：%s" % ", ".join(mismatch))

        print(u"成功      : %d 张表、%d 行全部迁移完成并已提交，耗时 %.2fs"
              % (len(tables), total, elapsed))
        print(u"提醒      : 请重启 chatserver，各 Manager 在 init() 时才会读到新数据"
              u"并重新推算自增 ID 起点。")
    finally:
        lite.close()
        mycon.close()

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except MigrateError as exc:
        print()
        print(u"失败      : %s" % exc)
        sys.exit(1)
