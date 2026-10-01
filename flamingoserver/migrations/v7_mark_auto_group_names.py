#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把存量群里"由旧客户端自动拼出来的群名"改标记为自动群名，并按当前成员立即重算。

背景：t_group 新增了 f_name_custom 列（1 = 群名由用户设定，0 = 自动群名，由服务端
      按成员昵称生成并随成员变化重算，规则见 chatserversrc/GroupAutoName.h）。该列
      默认值为 1，所以升级后所有存量群都按"用户设定"处理，群名保持不变。
      但此前客户端在建群时如果用户没填群名，会自己把所选成员的昵称拼成群名（如
      "莫梅、黄涛波、江怡、向瑶娜"）当作普通群名提交，这类群在成员增减后群名就过时了。
      本脚本把这类群找出来，经人工确认后改为自动群名。

判定"疑似旧客户端拼出来的群名"的规则（与旧客户端 CreateGroupDlg 的拼法一致）：
  · 群名恰好是"群聊"（旧客户端在一个昵称都取不到时的兜底名）；或
  · 去掉末尾的"等"后按"、"拆开，段数为 1 到 4（带"等"时必须恰好 4 段），且每一段
    都等于某个用户的昵称，或等于群主给某人设的备注名（旧客户端取的是显示名，
    设了备注的联系人显示的是备注）。
这只是疑似，最终以人工确认为准，所以默认只列出、不修改。

用法：
    # 1) 列出候选（不改库）。SQLite 须显式给出库文件路径；不给 --sqlite 时连 MySQL，
    #    连接参数取自 etc/chatserver.conf。
    python v7_mark_auto_group_names.py --sqlite ../windowsproj/data/flamingo.db
    python v7_mark_auto_group_names.py

    # 2) 确认后，对选定的群执行：先备份（仅 SQLite），再把 f_name_custom 置 0 并重算群名。
    python v7_mark_auto_group_names.py --sqlite ../windowsproj/data/flamingo.db --apply 4000000001 4000000002
    python v7_mark_auto_group_names.py --sqlite ../windowsproj/data/flamingo.db --apply-all

执行前提：chatserver 运行中也可以执行 —— GroupManager 不缓存群资料，下一次查询即读到
          新群名。但在线客户端不会收到推送，要等重新拉取群列表（如重新登录）才看到新名字。
幂等性：只处理 f_name_custom = 1 的群，已改为自动群名的群不会再被列出或改动。

balloonwj@qq.com   2026-09-30
"""

import argparse
import datetime
import os
import sqlite3
import sys

# 自动群名的生成规则，必须与 chatserversrc/GroupAutoName.cpp 保持一致。
MAX_NAMES_IN_GROUP_NAME = 4        # 群名里最多出现的成员昵称个数
MAX_GROUP_NAME_CHARS = 64          # 群名最多字符数，与 t_group.f_name 的 varchar(64) 一致
NAME_SEPARATOR = u"、"             # 昵称之间的分隔符
MORE_SUFFIX = u"等"                # 人数更多时的后缀
FALLBACK_NAME = u"群聊"            # 除群主外没有可用昵称时的群名

# MySQL 默认端口。chatserver.conf 不含端口配置项，服务端也是按默认端口连的。
DEFAULT_DB_PORT = 3306

# MySQL 补列时用的列定义，与 mysqlmgr/MysqlManager.cpp 里 f_name_custom 的定义一致。
MYSQL_NAME_CUSTOM_COLUMN = (
    "f_name_custom int(2) NOT NULL DEFAULT 1 "
    "COMMENT '群名是否由用户设定:1是 0否(自动群名)'")

# SQLite 补列时用的列定义，与 sqliteapi/SqliteSchema.cpp 里生成的一致。
SQLITE_NAME_CUSTOM_COLUMN = "f_name_custom INTEGER NOT NULL DEFAULT 1"


def join_order_key(member):
    """成员在自动群名里的排序键：入群时间早的在前，时间相同按 userid 升序。"""
    return (member[2], member[0])


def build_auto_group_name(ownerid, members):
    """按规则生成自动群名，与 C++ 的 BuildAutoGroupName 逐条对应。

    @param ownerid 群主 userid。
    @param members 成员列表，每项为 (userid, nickname, jointime)，可含群主。
    @return 自动群名。
    """
    others = [m for m in members if m[0] != ownerid and m[1]]
    if not others:
        return FALLBACK_NAME
    others.sort(key=join_order_key)
    name = NAME_SEPARATOR.join(m[1] for m in others[:MAX_NAMES_IN_GROUP_NAME])
    if len(others) > MAX_NAMES_IN_GROUP_NAME:
        name += MORE_SUFFIX
    # Python 字符串按码位计数，与 C++ 按 UTF-8 字符截断的口径相同
    return name[:MAX_GROUP_NAME_CHARS]


def looks_like_old_auto_name(name, known_names):
    """判断群名是否像旧客户端拼出来的自动群名（规则见文件头）。

    @param name        群名。
    @param known_names 可能出现在群名里的名字集合：全部用户昵称，加上群主设的备注名。
    @return 疑似时返回 True。
    """
    if name == FALLBACK_NAME:
        return True
    body = name
    has_more = body.endswith(MORE_SUFFIX)
    if has_more:
        body = body[:-len(MORE_SUFFIX)]
    parts = body.split(NAME_SEPARATOR)
    if has_more and len(parts) != MAX_NAMES_IN_GROUP_NAME:
        return False
    if not 1 <= len(parts) <= MAX_NAMES_IN_GROUP_NAME:
        return False
    return all(p and p in known_names for p in parts)


def load_mysql_config():
    """从 etc/chatserver.conf 读出 MySQL 连接参数，缺文件或缺项时回退到本机默认值。

    @return 五元组 (host, port, user, password, dbname)。
    """
    values = {
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
                # 跳过空行、注释行与不含等号的行
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                key = key.strip()
                if key in values:
                    values[key] = value.strip()
    except OSError as exc:
        print("读取 %s 失败（%s），改用默认的本机连接参数。" % (conf_path, exc),
              file=sys.stderr)
    return (values["dbserver"], DEFAULT_DB_PORT, values["dbuser"],
            values["dbpassword"], values["dbname"])


class Db(object):
    """对 SQLite 与 MySQL 两种连接的最小封装，只提供本脚本用到的操作。

    两种驱动的占位符不同（sqlite3 用 ?，pymysql 用 %s），由 m_placeholder 统一。
    """

    def __init__(self, sqlite_path):
        """sqlite_path 非空时打开该 SQLite 库文件，否则按 chatserver.conf 连 MySQL。"""
        self.m_sqlite_path = sqlite_path     # SQLite 库文件路径；连 MySQL 时为 None
        if sqlite_path:
            if not os.path.isfile(sqlite_path):
                raise SystemExit("SQLite 库文件不存在：%s" % sqlite_path)
            self.m_conn = sqlite3.connect(sqlite_path)
            self.m_placeholder = "?"
        else:
            import pymysql
            host, port, user, password, dbname = load_mysql_config()
            self.m_conn = pymysql.connect(host=host, port=port, user=user,
                                          password=password, database=dbname,
                                          charset="utf8")
            self.m_placeholder = "%s"

    def query(self, sql, params=()):
        """执行查询并返回全部行。sql 里的占位符统一写成 {p}。"""
        cur = self.m_conn.cursor()
        cur.execute(sql.replace("{p}", self.m_placeholder), params)
        rows = cur.fetchall()
        cur.close()
        return rows

    def execute(self, sql, params=()):
        """执行一条写语句（不提交）。sql 里的占位符统一写成 {p}。"""
        cur = self.m_conn.cursor()
        cur.execute(sql.replace("{p}", self.m_placeholder), params)
        cur.close()

    def has_name_custom_column(self):
        """t_group 上是否已有 f_name_custom 列（chatserver 升级后首次启动时会自动补上）。"""
        if self.m_sqlite_path:
            cols = [r[1] for r in self.query("PRAGMA table_info(t_group)")]
        else:
            cols = [r[0] for r in self.query("SHOW COLUMNS FROM t_group")]
        return "f_name_custom" in cols

    def add_name_custom_column(self):
        """补上 f_name_custom 列，定义与服务端建表 / 补列时的一致。"""
        column = SQLITE_NAME_CUSTOM_COLUMN if self.m_sqlite_path else MYSQL_NAME_CUSTOM_COLUMN
        self.execute("ALTER TABLE t_group ADD COLUMN " + column)

    def backup(self):
        """SQLite 模式下做一次在线备份（chatserver 开着也安全），返回备份文件路径。
        MySQL 模式不做备份，返回 None，请自行用 mysqldump 备份。"""
        if not self.m_sqlite_path:
            return None
        stamp = datetime.datetime.now().strftime("%Y%m%d%H%M%S")
        path = "%s.bak-v7-%s" % (self.m_sqlite_path, stamp)
        dest = sqlite3.connect(path)
        self.m_conn.backup(dest)
        dest.close()
        return path


def load_members(db, groupid):
    """取一个群的全部成员 (userid, nickname, jointime)，用户行缺失时昵称为空串。"""
    rows = db.query(
        "SELECT m.f_userid, u.f_nickname, m.f_join_time FROM t_group_member m "
        "LEFT JOIN t_user u ON u.f_user_id = m.f_userid WHERE m.f_groupid = {p}",
        (groupid,))
    return [(int(r[0]), r[1] or u"", int(r[2] or 0)) for r in rows]


def find_candidates(db):
    """找出疑似旧客户端拼出来的群名，返回 [(groupid, name, ownerid, 成员数, 重算后的群名)]。"""
    has_column = db.has_name_custom_column()
    where = " WHERE f_name_custom = 1" if has_column else ""
    groups = db.query("SELECT f_groupid, f_name, f_ownerid FROM t_group" + where
                      + " ORDER BY f_groupid")

    nicknames = set(r[0] for r in db.query("SELECT f_nickname FROM t_user") if r[0])
    candidates = []
    for groupid, name, ownerid in groups:
        groupid = int(groupid)
        ownerid = int(ownerid)
        remarks = set(r[0] for r in db.query(
            "SELECT f_remark FROM t_contact_remark WHERE f_userid = {p}", (ownerid,)) if r[0])
        if not looks_like_old_auto_name(name, nicknames | remarks):
            continue
        members = load_members(db, groupid)
        candidates.append((groupid, name, ownerid, len(members),
                           build_auto_group_name(ownerid, members)))
    return candidates


def print_candidates(candidates):
    """把候选群打印成表格。"""
    if not candidates:
        print("没有找到疑似旧客户端自动拼出来的群名。")
        return
    print("疑似旧客户端自动拼出来的群名，共 %d 个：" % len(candidates))
    print("%-12s %-8s %-6s  %s  ->  %s" % ("群ID", "群主", "成员数", "当前群名", "改为自动群名后"))
    for groupid, name, ownerid, count, new_name in candidates:
        print("%-12d %-8d %-6d  %s  ->  %s" % (groupid, ownerid, count, name, new_name))


def apply_groups(db, groupids):
    """把选定的群改为自动群名并立即重算。只处理 f_name_custom = 1 的群。"""
    backup_path = db.backup()
    if backup_path:
        print("已备份到 %s" % backup_path)
    else:
        print("MySQL 模式不做自动备份，请确认已用 mysqldump 备份。")

    if not db.has_name_custom_column():
        db.add_name_custom_column()
        print("t_group 上还没有 f_name_custom 列，已补上。")

    changed = 0
    for groupid in groupids:
        rows = db.query("SELECT f_name, f_ownerid, f_name_custom FROM t_group "
                        "WHERE f_groupid = {p}", (groupid,))
        if not rows:
            print("群 %d 不存在，跳过。" % groupid)
            continue
        old_name, ownerid, name_custom = rows[0][0], int(rows[0][1]), int(rows[0][2])
        if name_custom == 0:
            print("群 %d 已经是自动群名，跳过。" % groupid)
            continue
        new_name = build_auto_group_name(ownerid, load_members(db, groupid))
        db.execute("UPDATE t_group SET f_name_custom = 0, f_name = {p} WHERE f_groupid = {p}",
                   (new_name, groupid))
        changed += 1
        print("群 %d：%s  ->  %s" % (groupid, old_name, new_name))

    db.m_conn.commit()
    print("完成，共改动 %d 个群。在线客户端要重新拉取群列表（如重新登录）后才看到新群名。" % changed)


def main():
    # 中文 Windows 控制台默认是 GBK，统一按 UTF-8 输出；SystemExit 的提示走 stderr，一并设置
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(description=u"把旧客户端自动拼出来的群名改为自动群名")
    parser.add_argument("--sqlite", metavar="PATH",
                        help=u"SQLite 库文件路径；不给时按 etc/chatserver.conf 连 MySQL")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--apply", nargs="+", type=int, metavar="GROUPID",
                       help=u"对这些群执行修改（群ID须在候选列表里）")
    group.add_argument("--apply-all", action="store_true",
                       help=u"对候选列表里的全部群执行修改")
    args = parser.parse_args()

    db = Db(args.sqlite)
    candidates = find_candidates(db)

    # 默认只列出候选，不改库
    if not args.apply and not args.apply_all:
        print_candidates(candidates)
        print("\n以上只是列出，没有修改数据库。确认后用 --apply 群ID ... 或 --apply-all 执行。")
        return

    candidate_ids = [c[0] for c in candidates]
    if args.apply_all:
        targets = candidate_ids
    else:
        # 只允许改候选列表里的群，防止手误把用户设定的群名也改掉
        unknown = [g for g in args.apply if g not in candidate_ids]
        if unknown:
            raise SystemExit("这些群不在候选列表里，拒绝执行：%s" % ", ".join(str(g) for g in unknown))
        targets = args.apply
    apply_groups(db, targets)


if __name__ == "__main__":
    main()
