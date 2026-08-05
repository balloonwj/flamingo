#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
由 table.sql（MySQL 方言）生成 table_sqlite.sql（SQLite 方言）。

balloonwj@qq.com   2026-07-31

用法（在 flamingoserver/ 目录下执行）：
    python tools/gen_table_sqlite.py

为什么要有这个脚本：两种后端的建表语句必须描述同一套结构，手工维护两份迟早会失配。
table.sql 是结构的权威定义（MysqlManager.cpp 里那份运行期自动建表的定义历史上会漏列，
见 migrations/ 下几个补列脚本的说明），所以以它为源头单向生成 SQLite 版本。
**改了 table.sql 之后请重跑本脚本**，生成结果需人工过一遍再提交。

两种方言之间需要处理的差异：
  1. 类型：bigint(N) / int(N) 一律转成 INTEGER；varchar/blob/text/datetime/timestamp 保留原样
     （SQLite 是弱类型，声明类型只决定列亲和性，长度不做约束）。
  2. AUTO_INCREMENT：SQLite 只允许出现在 INTEGER PRIMARY KEY 上，写作 AUTOINCREMENT。
     MySQL 允许自增列不是主键（本项目里只有 t_user 是这种情况：自增列 f_id、主键 f_user_id），
     这时把自增列升为 SQLite 主键，原主键降级为唯一索引 —— 约束强度不变。
  3. 索引：MySQL 的索引名只需表内唯一，SQLite 要求**库内全局唯一**，
     而本项目里 idx_enterprise / idx_user / idx_group 等名字在多张表里重复出现，
     因此生成时统一加表名前缀。KEY/INDEX 转成 CREATE INDEX，UNIQUE KEY 转成 CREATE UNIQUE INDEX。
  4. COMMENT：SQLite 不支持，转成行尾的 -- 注释保留下来。
  5. 字符串字面量：MySQL 的双引号字符串在 SQLite 里是标识符，须转成单引号。
  6. ON UPDATE CURRENT_TIMESTAMP：SQLite 没有自动更新列，改用 AFTER UPDATE 触发器补齐。
     另外 SQLite 的 CURRENT_TIMESTAMP 是 UTC 而 MySQL 的是本地时间，
     所以默认值与触发器都写成 datetime('now','localtime')。
  7. 大小写敏感性：MySQL 默认排序规则 utf8_general_ci 不区分大小写，SQLite 区分。
     对用来做等值查找与唯一性判断的几个文本列显式加 COLLATE NOCASE（见 NOCASE_COLUMNS）。
"""

import io
import os
import re
import sys

#需要显式加 COLLATE NOCASE 的列，键为 (表名, 列名)。
#只覆盖会用于等值查找与唯一性判断的文本列：MySQL 的 utf8_general_ci 不区分大小写，
#不加的话 SQLite 上 Alice 与 alice 会被当成两个不同的账号。
#其余文本列保持 SQLite 默认的大小写敏感，避免排序与索引行为偏离预期。
NOCASE_COLUMNS = set([
    ('t_user', 'f_username'),       #登录账号，ChatSession::onLogin 按它做等值匹配
    ('t_user', 'f_mail'),           #邮箱，找回密码与管理台按它查人
    ('t_admin', 'f_email'),         #管理台登录账号
    ('t_enterprise', 'f_name'),     #企业全称，注册时按用户填的公司名做等值匹配，且要求全局唯一
])

#MySQL 类型到 SQLite 类型的映射规则，按顺序匹配（先匹配到的先用）。
#SQLite 只按声明类型推断列亲和性，长度不做约束，所以 varchar(64) 保留原样即可。
TYPE_RULES = [
    (re.compile(r'^bigint\s*\(\s*\d+\s*\)$', re.I), 'INTEGER'),
    (re.compile(r'^bigint$', re.I),                 'INTEGER'),
    (re.compile(r'^int\s*\(\s*\d+\s*\)$', re.I),    'INTEGER'),
    (re.compile(r'^int$', re.I),                    'INTEGER'),
    (re.compile(r'^tinyint\s*\(\s*\d+\s*\)$', re.I), 'INTEGER'),
    (re.compile(r'^smallint\s*\(\s*\d+\s*\)$', re.I), 'INTEGER'),
]


def strip_note_lines(text):
    """去掉 table.sql 里以 // 开头的笔记行。它们不是合法 SQL，只是作者的备注。"""
    kept = []
    for line in text.split('\n'):
        if line.strip().startswith('//'):
            continue
        kept.append(line)
    return '\n'.join(kept)


def split_top_level(text, sep=','):
    """
    按 sep 切分，但忽略括号内部与字符串字面量内部的分隔符。用于拆列定义与索引定义。
    字符串内部的逗号必须忽略，否则形如
        COMMENT '接收方:单聊为对端userid,群聊为群id'
    这样的列定义会被从注释中间切断。
    """
    parts = []
    depth = 0
    #当前所处字符串字面量的引号字符，None 表示不在字面量内。
    #table.sql 里单引号与双引号两种写法都有（MySQL 两种都接受），必须都认
    quote_char = None
    current = []
    for ch in text:
        if quote_char is not None:
            current.append(ch)
            if ch == quote_char:
                #连续两个引号是字面量内部的转义引号，但这里逐字符扫描，
                #再遇到下一个引号自然会重新进入字面量，效果等价
                quote_char = None
            continue

        if ch == "'" or ch == '"':
            quote_char = ch
            current.append(ch)
            continue

        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1

        if ch == sep and depth == 0:
            parts.append(''.join(current))
            current = []
        else:
            current.append(ch)

    if ''.join(current).strip():
        parts.append(''.join(current))
    return [p.strip() for p in parts if p.strip()]


def map_type(mysql_type):
    """把 MySQL 的列类型文本映射成 SQLite 的列类型文本。"""
    for pattern, replacement in TYPE_RULES:
        if pattern.match(mysql_type):
            return replacement
    #varchar(N) / blob / text / datetime / timestamp 等原样保留，统一成大写
    return mysql_type.upper()


def parse_column(line):
    """
    解析一行列定义，返回 dict：
        name / type / auto_increment / not_null / default / on_update_now / comment
    """
    match = re.match(r'^([a-z_0-9]+)\s+(.*)$', line, re.I | re.S)
    if not match:
        return None

    name = match.group(1)
    rest = ' '.join(match.group(2).split())

    #先摘出 COMMENT 子句，它可能包含逗号与关键字，必须最先剥离。
    #table.sql 里单引号与双引号两种写法都有，两种都要认
    comment = ''
    comment_match = re.search(r"COMMENT\s+'((?:[^']|'')*)'", rest, re.I)
    if comment_match is None:
        comment_match = re.search(r'COMMENT\s+"((?:[^"]|"")*)"', rest, re.I)
    if comment_match:
        comment = comment_match.group(1).replace("''", "'").replace('""', '"')
        rest = rest[:comment_match.start()] + rest[comment_match.end():]
        rest = ' '.join(rest.split())

    auto_increment = bool(re.search(r'\bAUTO_INCREMENT\b', rest, re.I))
    rest = re.sub(r'\bAUTO_INCREMENT\b', '', rest, flags=re.I)

    on_update_now = bool(re.search(r'\bON\s+UPDATE\s+CURRENT_TIMESTAMP\b', rest, re.I))
    rest = re.sub(r'\bON\s+UPDATE\s+CURRENT_TIMESTAMP\b', '', rest, flags=re.I)

    not_null = bool(re.search(r'\bNOT\s+NULL\b', rest, re.I))
    rest = re.sub(r'\bNOT\s+NULL\b', '', rest, flags=re.I)
    #去掉单独的 NULL（表示可空，SQLite 默认就是可空）
    rest = re.sub(r'(?<![A-Za-z_])NULL(?![A-Za-z_])(?!\s*\))', ' ', rest, count=0, flags=re.I) \
        if re.search(r'^\s*NULL\s*$', rest, re.I) else rest

    #DEFAULT 子句：可能是 NULL、数字、'字符串'、"字符串" 或 CURRENT_TIMESTAMP
    default = None
    default_match = re.search(
        r"\bDEFAULT\s+(CURRENT_TIMESTAMP|NULL|'(?:[^']|'')*'|\"(?:[^\"]|\"\")*\"|[-+0-9.]+)",
        rest, re.I)
    if default_match:
        default = default_match.group(1)
        rest = rest[:default_match.start()] + rest[default_match.end():]
        rest = ' '.join(rest.split())

    #剩下的第一个词就是类型
    rest = re.sub(r'\bNULL\b', '', rest, flags=re.I)
    rest = ' '.join(rest.split())
    if not rest:
        return None

    return {
        'name': name,
        'type': rest,
        'auto_increment': auto_increment,
        'not_null': not_null,
        'default': default,
        'on_update_now': on_update_now,
        'comment': comment,
    }


def normalize_default(default_text):
    """把 MySQL 的 DEFAULT 取值转成 SQLite 能接受的写法。"""
    if default_text is None:
        return None

    if default_text.upper() == 'CURRENT_TIMESTAMP':
        #SQLite 的 CURRENT_TIMESTAMP 是 UTC，MySQL 的是本地时间；
        #为了让两种后端存进去的时间一致，这里显式取本地时间。
        #非常量表达式做默认值时 SQLite 要求整体加括号。
        return "(datetime('now','localtime'))"

    if default_text.upper() == 'NULL':
        return 'NULL'

    #MySQL 里的双引号字符串在 SQLite 中会被当成标识符，须转成单引号
    if default_text.startswith('"') and default_text.endswith('"'):
        inner = default_text[1:-1].replace('""', '"').replace("'", "''")
        return "'" + inner + "'"

    return default_text


def parse_keys(key_lines):
    """
    解析索引定义行，返回 (primary_key_columns, unique_indexes, plain_indexes)。
    unique_indexes / plain_indexes 的元素为 (索引名, [列名...])。
    """
    primary = []
    uniques = []
    plains = []

    for line in key_lines:
        m = re.match(r'^PRIMARY\s+KEY\s*\((.*)\)\s*$', line, re.I | re.S)
        if m:
            primary = [c.strip() for c in m.group(1).split(',')]
            continue

        m = re.match(r'^UNIQUE\s+(?:KEY|INDEX)\s+([a-z_0-9]+)\s*\((.*)\)\s*$', line, re.I | re.S)
        if m:
            uniques.append((m.group(1), [c.strip() for c in m.group(2).split(',')]))
            continue

        m = re.match(r'^(?:KEY|INDEX)\s+([a-z_0-9]+)\s*\((.*)\)\s*$', line, re.I | re.S)
        if m:
            plains.append((m.group(1), [c.strip() for c in m.group(2).split(',')]))
            continue

        sys.stderr.write('WARN: unrecognized key definition: %s\n' % line)

    return primary, uniques, plains


def parse_table_sql(path):
    """解析 table.sql，返回 [(表名, [列 dict...], 索引定义行 [...])]。"""
    text = strip_note_lines(io.open(path, encoding='utf-8-sig').read())
    tables = []
    pattern = re.compile(
        r'CREATE\s+TABLE\s+IF\s+NOT\s+EXISTS\s+([a-z_0-9]+)\s*\((.*?)\)\s*ENGINE',
        re.S | re.I)

    for match in pattern.finditer(text):
        name = match.group(1)
        columns = []
        key_lines = []
        for item in split_top_level(match.group(2)):
            item = ' '.join(item.split())
            if re.match(r'^(PRIMARY\s+KEY|UNIQUE\s+KEY|UNIQUE\s+INDEX|KEY|INDEX)\b', item, re.I):
                key_lines.append(item)
                continue

            column = parse_column(item)
            if column is None:
                sys.stderr.write('WARN: cannot parse column in %s: %s\n' % (name, item))
                continue
            columns.append(column)

        tables.append((name, columns, key_lines))

    return tables


def render_table(name, columns, key_lines):
    """把一张表渲染成 SQLite 的 CREATE TABLE / CREATE INDEX / CREATE TRIGGER 语句文本。"""
    primary, uniques, plains = parse_keys(key_lines)

    auto_columns = [c['name'] for c in columns if c['auto_increment']]
    auto_column = auto_columns[0] if auto_columns else None

    #自增列在 SQLite 里必须是 INTEGER PRIMARY KEY。MySQL 允许自增列不是主键，
    #这时把自增列升为主键，原主键降级成唯一索引，约束强度不变。
    demoted_primary = None
    if auto_column is not None and primary != [auto_column]:
        demoted_primary = primary
        primary = [auto_column]

    lines = []
    lines.append('CREATE TABLE IF NOT EXISTS %s' % name)
    lines.append('(')

    rendered = []
    for column in columns:
        parts = [column['name']]

        if auto_column is not None and column['name'] == auto_column:
            #AUTOINCREMENT 保证 ID 单调递增、删除行后不复用，
            #这是 t_chatmsg.f_msgid 这类"服务器消息ID须单调递增"的硬要求
            parts.append('INTEGER PRIMARY KEY AUTOINCREMENT')
        else:
            parts.append(map_type(column['type']))

            if (name, column['name']) in NOCASE_COLUMNS:
                parts.append('COLLATE NOCASE')

            if column['not_null']:
                parts.append('NOT NULL')

            default = normalize_default(column['default'])
            if default is not None:
                parts.append('DEFAULT %s' % default)

        text = '    ' + ' '.join(parts)
        comment = column['comment']
        rendered.append((text, comment))

    #复合主键（或没有自增列的单列主键）写成表级约束
    table_constraints = []
    if auto_column is None and primary:
        table_constraints.append('    PRIMARY KEY (%s)' % ', '.join(primary))

    body = []
    for text, comment in rendered:
        body.append((text, comment))
    for constraint in table_constraints:
        body.append((constraint, ''))

    for index, (text, comment) in enumerate(body):
        suffix = ',' if index < len(body) - 1 else ''
        if comment:
            lines.append('%s%s    -- %s' % (text, suffix, comment))
        else:
            lines.append('%s%s' % (text, suffix))

    lines.append(');')

    #索引：SQLite 的索引名是库内全局唯一的，而本项目里 idx_enterprise / idx_user 等
    #在多张表里重复出现，所以统一加表名前缀
    index_lines = []
    if demoted_primary:
        index_lines.append(
            "CREATE UNIQUE INDEX IF NOT EXISTS %s_uk_primary ON %s(%s);"
            "    -- 原 MySQL 主键降级而来，唯一性约束不变"
            % (name, name, ', '.join(demoted_primary)))

    for index_name, cols in uniques:
        index_lines.append('CREATE UNIQUE INDEX IF NOT EXISTS %s_%s ON %s(%s);'
                           % (name, index_name, name, ', '.join(cols)))

    for index_name, cols in plains:
        #与主键完全重合的索引没有意义，SQLite 的主键本身就是索引，跳过
        if cols == primary:
            continue
        index_lines.append('CREATE INDEX IF NOT EXISTS %s_%s ON %s(%s);'
                           % (name, index_name, name, ', '.join(cols)))

    #ON UPDATE CURRENT_TIMESTAMP 的替代：AFTER UPDATE 触发器。
    #WHEN 条件保证只在本次 UPDATE 没有显式改写该列时才自动填，语义与 MySQL 一致，
    #同时避免触发器自身的 UPDATE 再次触发（SQLite 默认 recursive_triggers 为关，双保险）
    trigger_lines = []
    for column in columns:
        if not column['on_update_now']:
            continue
        trigger_lines.append(
            'CREATE TRIGGER IF NOT EXISTS trg_%s_%s AFTER UPDATE ON %s\n'
            'FOR EACH ROW WHEN NEW.%s = OLD.%s\n'
            'BEGIN\n'
            "    UPDATE %s SET %s = datetime('now','localtime') WHERE rowid = NEW.rowid;\n"
            'END;'
            % (name, column['name'], name,
               column['name'], column['name'],
               name, column['name']))

    return '\n'.join(lines), index_lines, trigger_lines


def strip_sql_comments(text):
    """去掉 SQL 里以 -- 开头到行尾的注释，但不动字符串字面量内部的内容。"""
    result = []
    for line in text.split('\n'):
        quote_char = None
        cut = None
        i = 0
        while i < len(line):
            ch = line[i]
            if quote_char is not None:
                if ch == quote_char:
                    quote_char = None
                i += 1
                continue

            if ch == "'" or ch == '"':
                quote_char = ch
                i += 1
                continue

            if ch == '-' and i + 1 < len(line) and line[i + 1] == '-':
                cut = i
                break

            i += 1

        if cut is not None:
            line = line[:cut]

        line = line.rstrip()
        if line:
            result.append(line)

    return '\n'.join(result)


def render_c_string(text):
    """
    把文本渲染成 C 字符串字面量。

    非 ASCII 字节一律用**八进制**转义（\\NNN，固定三位）。这样做有两个原因：
      1. 生成出来的 .cpp 是纯 ASCII，不需要 BOM，也不会被任何编辑器改坏；
      2. 更关键的是躲开 MSVC 的执行字符集问题 —— 源文件里直接写中文时，
         MSVC 会把窄字符串字面量转成系统 ANSI 代码页（本机是 GBK），
         而 SQLite 要的是 UTF-8，默认值里的中文会变成乱码。
         八进制转义描述的是**字节**，不受执行字符集影响。
    八进制而非十六进制是因为 \\xNN 后面若紧跟十六进制字符会被继续吞掉，
    八进制转义则最多吃三位，没有歧义。
    """
    pieces = []
    for line in text.split('\n'):
        pieces.append('"%s\\n"' % escape_c_bytes(line))

    return '\n        '.join(pieces)


def escape_c_bytes(text):
    """把一段文本转义成可直接嵌进 C 字符串字面量的内容（不含两端引号）。"""
    chunk = []
    for byte in bytearray(text.encode('utf-8')):
        ch = chr(byte)
        if byte >= 0x80 or byte < 0x20:
            chunk.append('\\%03o' % byte)
        elif ch == '"':
            chunk.append('\\"')
        elif ch == '\\':
            chunk.append('\\\\')
        else:
            chunk.append(ch)

    return ''.join(chunk)


def make_add_column_clause(table, column, auto_column):
    """
    生成 ALTER TABLE ... ADD COLUMN 用的列定义片段，返回 None 表示该列不适合走补列路径。

    SQLite 对 ADD COLUMN 有额外限制，必须在这里处理掉：
      - 新列不能是 PRIMARY KEY，所以自增主键列不参与补列（主键列都缺了的表已经没救，
        补一列也修不回来，只能重建）；
      - 新列若声明 NOT NULL，其默认值必须是**常量**，不能是括号表达式。
        本项目里 timestamp 列的默认值是 datetime('now','localtime')，属于表达式，
        因此补这类列时只能退而求其次：去掉 NOT NULL 与默认值，存量行取 NULL。
    """
    if auto_column is not None and column['name'] == auto_column:
        return None

    parts = [map_type(column['type'])]

    if (table, column['name']) in NOCASE_COLUMNS:
        parts.append('COLLATE NOCASE')

    default = normalize_default(column['default'])
    is_expression_default = (default is not None and default.startswith('('))

    if is_expression_default:
        #表达式默认值在 ADD COLUMN 里不被接受，连带 NOT NULL 也只能放弃
        return ' '.join(parts)

    if column['not_null']:
        #NOT NULL 必须配常量默认值，否则存量行无值可填，SQLite 会直接拒绝
        if default is None or default == 'NULL':
            return ' '.join(parts)
        parts.append('NOT NULL')
    if default is not None:
        parts.append('DEFAULT %s' % default)

    return ' '.join(parts)


def render_schema_source(tables, statements):
    """生成 sqliteapi/SqliteSchema.cpp 的内容（纯 ASCII）。"""
    lines = []
    lines.append('/**')
    lines.append(' *  SQLite 库结构数据，SqliteSchema.cpp')
    lines.append(' *  balloonwj@qq.com   2026-07-31')
    lines.append(' *')
    lines.append(' *  本文件由 tools/gen_table_sqlite.py 从 table.sql 自动生成，请勿手工编辑。')
    lines.append(' *  改了 table.sql 之后重跑该脚本：python tools/gen_table_sqlite.py')
    lines.append(' *')
    lines.append(' *  文件内容与同时生成的 table_sqlite.sql 一致，只是把语句嵌进了程序，')
    lines.append(' *  这样 chatserver 建表时不依赖运行期能否读到那个 .sql 文件。')
    lines.append(' *  非 ASCII 字节用八进制转义，原因见生成脚本里 render_c_string 的注释。')
    lines.append(' */')
    lines.append('#include "SqliteSchema.h"')
    lines.append('')
    lines.append('namespace sqliteapi {')
    lines.append('')

    #建表语句与「索引 / 触发器」语句必须分开：存量库补列时，索引可能建在尚未补上的列上，
    #所以执行顺序必须是 建表 -> 补列 -> 建索引与触发器。合成一个数组就没法插入补列这一步。
    table_statements = []
    post_statements = []
    for statement in statements:
        if re.match(r'^\s*CREATE\s+TABLE\b', statement, re.I):
            table_statements.append(statement)
        else:
            post_statements.append(statement)

    lines.append('const char* const SCHEMA_TABLE_STATEMENTS[] =')
    lines.append('{')
    for statement in table_statements:
        lines.append('    %s,' % render_c_string(statement))
    lines.append('};')
    lines.append('')
    lines.append('const int SCHEMA_TABLE_STATEMENT_COUNT = %d;' % len(table_statements))
    lines.append('')

    lines.append('const char* const SCHEMA_POST_STATEMENTS[] =')
    lines.append('{')
    for statement in post_statements:
        lines.append('    %s,' % render_c_string(statement))
    lines.append('};')
    lines.append('')
    lines.append('const int SCHEMA_POST_STATEMENT_COUNT = %d;' % len(post_statements))
    lines.append('')

    lines.append('const char* const SCHEMA_TABLES[] =')
    lines.append('{')
    for name, columns, key_lines in tables:
        lines.append('    "%s",' % name)
    lines.append('};')
    lines.append('')
    lines.append('const int SCHEMA_TABLE_COUNT = %d;' % len(tables))
    lines.append('')

    column_entries = []
    for name, columns, key_lines in tables:
        auto_columns = [c['name'] for c in columns if c['auto_increment']]
        auto_column = auto_columns[0] if auto_columns else None
        for column in columns:
            clause = make_add_column_clause(name, column, auto_column)
            if clause is None:
                continue
            column_entries.append((name, column['name'], clause))

    lines.append('const SchemaColumn SCHEMA_COLUMNS[] =')
    lines.append('{')
    for table, column, clause in column_entries:
        #clause 里可能带中文默认值（如 DEFAULT '我的好友'），同样要按字节转义，
        #理由见 render_c_string 的注释
        lines.append('    { "%s", "%s", "%s" },'
                     % (table, column, escape_c_bytes(clause)))
    lines.append('};')
    lines.append('')
    lines.append('const int SCHEMA_COLUMN_COUNT = %d;' % len(column_entries))
    lines.append('')
    lines.append('} // namespace sqliteapi')
    lines.append('')

    return '\n'.join(lines)


def split_statements(sql_text):
    """把建表脚本切成一条条独立语句。触发器体内部有分号，须按 END; 特殊处理。"""
    statements = []
    buffer = []
    in_trigger = False
    for line in sql_text.split('\n'):
        stripped = line.strip()
        if not stripped:
            continue

        buffer.append(line)

        if re.match(r'^CREATE\s+TRIGGER\b', stripped, re.I):
            in_trigger = True

        if in_trigger:
            #触发器要一直读到单独一行的 END;
            if re.match(r'^END\s*;\s*$', stripped, re.I):
                in_trigger = False
                statements.append('\n'.join(buffer).strip())
                buffer = []
            continue

        if stripped.endswith(';'):
            statements.append('\n'.join(buffer).strip())
            buffer = []

    if [l for l in buffer if l.strip()]:
        sys.stderr.write('WARN: trailing text not terminated by ;\n')

    return statements


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    source = os.path.join(root, 'table.sql')
    target = os.path.join(root, 'table_sqlite.sql')

    tables = parse_table_sql(source)
    if len(tables) == 0:
        sys.stderr.write('ERROR: no table parsed from %s\n' % source)
        return 1

    out = []
    out.append('-- ============================================================')
    out.append('--  FlamingoIM 本地 SQLite 模式的建表脚本')
    out.append('--  balloonwj@qq.com   2026-07-31')
    out.append('--')
    out.append('--  本文件由 tools/gen_table_sqlite.py 从 table.sql 自动生成，请勿手工编辑。')
    out.append('--  改了 table.sql 之后重跑该脚本即可：python tools/gen_table_sqlite.py')
    out.append('--')
    out.append('--  chatserver 在 dbtype=sqlite 时启动会执行本脚本建表；语句全部是')
    out.append('--  IF NOT EXISTS 形式，重复执行安全。缺列的自动补齐由 CSqliteManager')
    out.append('--  用 PRAGMA table_info 比对后 ALTER TABLE ADD COLUMN 完成，不在本文件里。')
    out.append('-- ============================================================')
    out.append('')

    total_index = 0
    total_trigger = 0
    for name, columns, key_lines in tables:
        table_sql, index_lines, trigger_lines = render_table(name, columns, key_lines)
        out.append(table_sql)
        if index_lines:
            out.append('')
            out.extend(index_lines)
        if trigger_lines:
            out.append('')
            out.extend(trigger_lines)
        out.append('')
        total_index += len(index_lines)
        total_trigger += len(trigger_lines)

    sql_text = '\n'.join(out)
    io.open(target, 'w', encoding='utf-8', newline='\r\n').write(sql_text)

    #再由同一份解析结果生成嵌进程序的结构数据，保证两个产物永远同源
    statements = split_statements(strip_sql_comments(sql_text))
    #文件头注释按项目规范用中文，所以整个文件仍是 UTF-8 带 BOM（与仓库其余源文件一致）；
    #真正要紧的是**字符串字面量**保持八进制转义，那部分不受编译器执行字符集影响。
    schema_source = os.path.join(root, 'sqliteapi', 'SqliteSchema.cpp')
    io.open(schema_source, 'w', encoding='utf-8-sig', newline='\r\n').write(
        render_schema_source(tables, statements))

    sys.stdout.write('generated %s\n' % target)
    sys.stdout.write('generated %s\n' % schema_source)
    sys.stdout.write('tables: %d, indexes: %d, triggers: %d, statements: %d\n'
                     % (len(tables), total_index, total_trigger, len(statements)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
