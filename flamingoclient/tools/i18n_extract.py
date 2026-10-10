# -*- coding: utf-8 -*-
"""
PC 客户端多语言的提取脚本：扫描源码里需要翻译的界面文字，更新 FlamingoLang/lang/ 下的四个 .po。

收录两类文字（方案见 docs/多语言方案.md 第 5、6.4 节）：
  1. TR / TRC / TRF / TR_NOOP 的字面量参数：TR(原文)、TRC(上下文, 原文)、TRF(原文模板, 参数...)、TR_NOOP(原文)，
     以及 UTF-8 版本 TR8(原文)、TRC8(上下文, 原文)、TRF8(原文模板, 参数...)。
     参数必须是字符串字面量（可以是相邻的几段字面量拼接）；传变量的调用无法静态提取，跳过。
  2. 内嵌 XML 布局（原始字符串字面量 R"xml(...)xml" 等）里 text / placeholder / title / tooltip 四个属性中
     含汉字的值。运行时由 i18n::TranslateXml 翻译。

更新规则：
  - 已有条目保留译文与译者注释（如「#. 待审校」），引用位置（#: 文件:行号）按本次扫描重写；
  - 新出现的原文追加为空译文（即尚未翻译，运行时显示原文）；
  - 代码里已不存在、但已有译文的条目改写成作废条目（#~），留着备查；重新出现时自动恢复；
  - 条目按第一处引用的文件与行号排序，保证每次生成的差异稳定。

用法（在仓库任意目录执行均可）：
  python flamingoclient/tools/i18n_extract.py            更新四个 .po
  python flamingoclient/tools/i18n_extract.py --check    只比较不写入；与源码不一致时返回 1
  python flamingoclient/tools/i18n_extract.py --report   只打印每种语言的翻译进度

与 FlamingoUnit 的守卫用例 [langpack] 配合：用例检查代码里每条标记的文字都已收进 .po，
忘了跑本脚本时单测会失败。
balloonwj@qq.com   2026-10-03
"""

import io
import os
import re
import sys

LANGUAGES = ('zh-TW', 'en-US', 'ja-JP', 'ko-KR')

# 扫描的目录（相对 flamingoclient），以及其中要跳过的子目录名
SCAN_ROOTS = ('Source', 'FlamingoMedia', 'FlamingoFileShare', 'FlamingoUpdater', 'FlamingoLock', 'FlamingoCaption',
              'FlamingoThreadGroup', 'FlamingoScreenshot', 'FlamingoBot')
SKIP_DIRS = ('Tests', 'third-party', 'Debug', 'Release', 'Debug_x64', 'Release_x64')
SOURCE_EXTS = ('.cpp', '.h', '.inl')

# 标记宏：宏名 -> (上下文参数下标, 原文参数下标)；上下文下标为 None 表示没有上下文
MARK_MACROS = {
    'TR': (None, 0),
    'TRC': (0, 1),
    'TRF': (None, 0),
    'TR_NOOP': (None, 0),
    'TR8': (None, 0),
    'TRC8': (0, 1),
    'TRF8': (None, 0),
}

# XML 里需要翻译的属性
XML_TEXT_ATTRS = ('text', 'placeholder', 'title', 'tooltip')
XML_ATTR_RE = re.compile(r'(?<![\w\-:.])(' + '|'.join(XML_TEXT_ATTRS) + r')\s*=\s*("([^"]*)"|\'([^\']*)\')')
XML_COMMENT_RE = re.compile(r'<!--.*?-->', re.S)
XML_ENTITIES = (('&lt;', '<'), ('&gt;', '>'), ('&quot;', '"'), ('&apos;', "'"), ('&amp;', '&'))

CJK_RE = re.compile(u'[\u3400-\u9fff\uf900-\ufaff]')

# 「待审校」译者注释：机器起草、尚未经母语者审校的条目
REVIEW_COMMENT = u'#. 待审校'


def client_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ---------------------------------------------------------------------------
# C++ 源码的词法扫描：只认注释、字符字面量、字符串字面量（含原始字符串）、标识符与括号逗号
# ---------------------------------------------------------------------------

STRING_PREFIX_RE = re.compile(r'(u8|u|U|L)?(R)?"')
IDENT_RE = re.compile(r'[A-Za-z_]\w*')

HEX_DIGITS = '0123456789abcdefABCDEF'

SIMPLE_ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', '0': '\0', '"': '"', "'": "'", '\\': '\\',
                  'a': '\a', 'b': '\b', 'f': '\f', 'v': '\v', '?': '?'}


def is_hex_run(text, start, width):
    """text 从 start 起是否恰有 width 个十六进制数字。"""
    run = text[start:start + width]
    return len(run) == width and all(ch in HEX_DIGITS for ch in run)


def decode_c_escapes(body):
    """还原普通字符串字面量里的转义。"""
    out = []
    i = 0
    n = len(body)
    while i < n:
        c = body[i]
        if c != '\\' or i + 1 >= n:
            out.append(c)
            i += 1
            continue
        e = body[i + 1]
        if e in SIMPLE_ESCAPES and not (e == '0' and i + 2 < n and body[i + 2] in '01234567'):
            out.append(SIMPLE_ESCAPES[e])
            i += 2
        elif e == 'x' and i + 2 < n and body[i + 2] in HEX_DIGITS:
            j = i + 2
            while j < n and body[j] in HEX_DIGITS:
                j += 1
            out.append(chr(int(body[i + 2:j], 16)))
            i = j
        elif (e == 'u' or e == 'U') and is_hex_run(body, i + 2, 4 if e == 'u' else 8):
            width = 4 if e == 'u' else 8
            out.append(chr(int(body[i + 2:i + 2 + width], 16)))
            i += 2 + width
        elif e in '01234567':
            j = i + 1
            while j < n and j < i + 4 and body[j] in '01234567':
                j += 1
            out.append(chr(int(body[i + 1:j], 8)))
            i = j
        else:
            out.append(e)
            i += 2
    return ''.join(out)


class Token(object):
    __slots__ = ('kind', 'value', 'line', 'raw')

    def __init__(self, kind, value, line, raw=False):
        self.kind = kind      # 'ident' / 'string' / 'punct'
        self.value = value    # 标识符名、还原后的字符串内容、标点字符
        self.line = line      # 所在行号（从 1 起）
        self.raw = raw        # 字符串是否为原始字符串字面量


def tokenize(text):
    """把 C++ 源码切成词法单元，丢弃注释、字符字面量、数字与其它符号。"""
    tokens = []
    i = 0
    n = len(text)
    line = 1
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            line += text.count('\n', i, j)
            i = j
            continue
        # 预处理指令照常扫描：宏定义里的 TR_NOOP(L"...") 要收录（如 App/UIText.h），未标记的中文宏也要计入；
        # #define TR(source) 这类宏定义的参数不是字面量，不会误收。只有 #include 整行跳过：引号里是文件路径，
        # 不是代码里的字面量，路径里的反斜杠也不是转义
        if text.startswith('#include', i) and text[text.rfind('\n', 0, i) + 1:i].strip() == '':
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        m = STRING_PREFIX_RE.match(text, i)
        if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            start_line = line
            if m.group(2):
                # 原始字符串：R"delim( ... )delim"
                j = m.end()
                k = text.find('(', j)
                delim = text[j:k]
                close = ')' + delim + '"'
                end = text.find(close, k + 1)
                if end < 0:
                    end = n
                body = text[k + 1:end]
                line += body.count('\n')
                tokens.append(Token('string', body, start_line, raw=True))
                i = end + len(close)
                continue
            j = m.end()
            while j < n and text[j] != '"':
                if text[j] == '\\':
                    j += 2
                    continue
                if text[j] == '\n':
                    break
                j += 1
            body = text[m.end():j]
            tokens.append(Token('string', decode_c_escapes(body), start_line))
            i = j + 1
            continue
        if c == "'":
            # 字符字面量：跳过，避免把 '"' 当成字符串开头
            j = i + 1
            while j < n and text[j] != "'" and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            i = j + 1
            continue
        if c.isalpha() or c == '_':
            m = IDENT_RE.match(text, i)
            tokens.append(Token('ident', m.group(0), line))
            i = m.end()
            continue
        if c.isdigit():
            # 数字字面量（含 0x1F、1e5、10'000 这类写法）整体跳过
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] in "._'"):
                j += 1
            i = j
            continue
        if c in '(),;{}':
            tokens.append(Token('punct', c, line))
        i += 1
    return tokens


def split_call_args(tokens, open_index):
    """从左括号开始，按顶层逗号切分调用的实参；返回 (实参列表, 右括号之后的下标)。"""
    args = [[]]
    depth = 0
    i = open_index
    while i < len(tokens):
        t = tokens[i]
        if t.kind == 'punct' and t.value == '(':
            depth += 1
            if depth > 1:
                args[-1].append(t)
        elif t.kind == 'punct' and t.value == ')':
            depth -= 1
            if depth == 0:
                return args, i + 1
            args[-1].append(t)
        elif t.kind == 'punct' and t.value == ',' and depth == 1:
            args.append([])
        elif t.kind == 'punct' and t.value == ';':
            # 语句结束仍未闭合：宏定义或残缺代码，放弃这一处
            return None, i + 1
        else:
            args[-1].append(t)
        i += 1
    return None, i


def literal_value(arg_tokens):
    """实参若只由相邻的字符串字面量组成，返回拼接后的内容；否则返回 None。"""
    if not arg_tokens or any(t.kind != 'string' for t in arg_tokens):
        return None
    return ''.join(t.value for t in arg_tokens)


def decode_xml_entities(value):
    for ent, ch in XML_ENTITIES:
        value = value.replace(ent, ch)
    return value


class Message(object):
    """一条要翻译的文字及其在源码中的全部引用位置。"""
    __slots__ = ('context', 'msgid', 'refs')

    def __init__(self, context, msgid):
        self.context = context
        self.msgid = msgid
        self.refs = []


def scan_file(path, rel, messages):
    with io.open(path, 'rb') as fp:
        data = fp.read()
    try:
        text = data.decode('utf-8-sig')
    except UnicodeDecodeError:
        # 个别老文件仍是 GBK：不含 TR 标记，跳过
        return
    tokens = tokenize(text)

    def add(context, msgid, line):
        key = (context or '', msgid)
        msg = messages.get(key)
        if msg is None:
            msg = Message(context or '', msgid)
            messages[key] = msg
        msg.refs.append((rel, line))

    i = 0
    while i < len(tokens):
        t = tokens[i]
        if t.kind == 'ident' and t.value in MARK_MACROS and i + 1 < len(tokens) \
                and tokens[i + 1].kind == 'punct' and tokens[i + 1].value == '(':
            # 宏定义本身（#define TR(source) ...）已随预处理行跳过，这里都是真正的调用
            args, nxt = split_call_args(tokens, i + 1)
            if args is not None:
                ctx_index, src_index = MARK_MACROS[t.value]
                source = literal_value(args[src_index]) if src_index < len(args) else None
                context = literal_value(args[ctx_index]) if ctx_index is not None and ctx_index < len(args) else None
                if source:
                    add(context, source, t.line)
            i += 1
            continue
        if t.kind == 'string' and t.raw and '<' in t.value:
            body = XML_COMMENT_RE.sub(lambda m: '\n' * m.group(0).count('\n'), t.value)
            for m in XML_ATTR_RE.finditer(body):
                value = m.group(3) if m.group(3) is not None else m.group(4)
                value = decode_xml_entities(value)
                if CJK_RE.search(value):
                    add('', value, t.line + body.count('\n', 0, m.start()))
        i += 1


def scan_sources(root):
    messages = {}
    for scan_root in SCAN_ROOTS:
        base = os.path.join(root, scan_root)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
            for name in sorted(filenames):
                if not name.endswith(SOURCE_EXTS):
                    continue
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, root).replace(os.sep, '/')
                scan_file(path, rel, messages)
    return messages


# ---------------------------------------------------------------------------
# .po 的读写
# ---------------------------------------------------------------------------

class PoEntry(object):
    __slots__ = ('comments', 'context', 'msgid', 'msgstr', 'obsolete')

    def __init__(self):
        self.comments = []    # 译者注释（# 与 #. 开头，不含 #: 引用与 #, 标志）
        self.context = ''
        self.msgid = None
        self.msgstr = ''
        self.obsolete = False


PO_STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
PO_ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\'}


def po_unquote(s):
    m = PO_STRING_RE.search(s)
    if not m:
        return ''
    body = m.group(1)
    out = []
    i = 0
    while i < len(body):
        c = body[i]
        if c == '\\' and i + 1 < len(body):
            out.append(PO_ESCAPES.get(body[i + 1], body[i + 1]))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def po_quote(s):
    s = s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t').replace('\r', '\\r')
    return '"' + s + '"'


def read_po_text(path):
    with io.open(path, 'r', encoding='utf-8-sig') as fp:
        return fp.read().replace('\r\n', '\n')


def read_po(path):
    return parse_po(read_po_text(path))


def parse_po(text):
    """解析 .po 文本：返回 (文件头文本, 条目列表)。文件头是第一个空行之前的全部内容（含 msgid "" 的头条目）。"""
    blocks = text.split('\n\n')
    header = blocks[0].rstrip('\n') + '\n'
    entries = []
    for block in blocks[1:]:
        lines = [l for l in block.split('\n') if l.strip()]
        if not lines:
            continue
        entry = PoEntry()
        field = None
        for line in lines:
            obsolete = line.startswith('#~')
            if obsolete:
                entry.obsolete = True
                line = line[2:].strip()
            if line.startswith('#:') or line.startswith('#,'):
                continue
            if line.startswith('#'):
                entry.comments.append(line)
                continue
            if line.startswith('msgctxt'):
                field = 'context'
                entry.context = po_unquote(line)
            elif line.startswith('msgid'):
                field = 'msgid'
                entry.msgid = po_unquote(line)
            elif line.startswith('msgstr'):
                field = 'msgstr'
                entry.msgstr = po_unquote(line)
            elif line.startswith('"') and field:
                setattr(entry, field, getattr(entry, field) + po_unquote(line))
        if entry.msgid is not None:
            entries.append(entry)
    return header, entries


def format_entry(entry, refs):
    lines = []
    prefix = '#~ ' if entry.obsolete else ''
    lines.extend(entry.comments)
    if not entry.obsolete:
        for rel, line in refs:
            lines.append('#: %s:%d' % (rel, line))
    if entry.context:
        lines.append(prefix + 'msgctxt ' + po_quote(entry.context))
    lines.append(prefix + 'msgid ' + po_quote(entry.msgid))
    lines.append(prefix + 'msgstr ' + po_quote(entry.msgstr))
    return '\n'.join(lines) + '\n'


def build_po(header, old_entries, messages):
    """按扫描结果生成新的 .po 文本。"""
    old = {}
    for e in old_entries:
        key = (e.context, e.msgid)
        # 同一条目同时有正常与作废两份时，以正常的为准
        if key not in old or old[key].obsolete:
            old[key] = e

    ordered = sorted(messages.values(), key=lambda m: (m.refs[0][0], m.refs[0][1], m.context, m.msgid))
    parts = [header]
    for msg in ordered:
        prev = old.get((msg.context, msg.msgid))
        entry = PoEntry()
        entry.context = msg.context
        entry.msgid = msg.msgid
        if prev is not None:
            entry.comments = list(prev.comments)
            entry.msgstr = prev.msgstr
        refs = sorted(set(msg.refs))
        parts.append(format_entry(entry, refs))

    # 代码里已不存在但有译文的条目：作废，留着备查
    for key in sorted(old.keys()):
        if key in messages:
            continue
        prev = old[key]
        if not prev.msgstr:
            continue
        entry = PoEntry()
        entry.context = prev.context
        entry.msgid = prev.msgid
        entry.msgstr = prev.msgstr
        entry.comments = list(prev.comments)
        entry.obsolete = True
        parts.append(format_entry(entry, []))
    return '\n'.join(parts)


def report(entries_by_lang):
    for lang in LANGUAGES:
        entries = [e for e in entries_by_lang[lang] if not e.obsolete]
        total = len(entries)
        translated = sum(1 for e in entries if e.msgstr)
        review = sum(1 for e in entries if e.msgstr and REVIEW_COMMENT in e.comments)
        obsolete = sum(1 for e in entries_by_lang[lang] if e.obsolete)
        print(u'  %-6s 共 %4d 条，已翻译 %4d 条（其中待审校 %4d 条），未翻译 %4d 条，作废 %d 条'
              % (lang, total, translated, review, total - translated, obsolete))


def main(argv):
    check = '--check' in argv
    only_report = '--report' in argv
    root = client_root()
    lang_dir = os.path.join(root, 'FlamingoLang', 'lang')

    if only_report:
        entries_by_lang = {}
        for lang in LANGUAGES:
            entries_by_lang[lang] = read_po(os.path.join(lang_dir, lang + '.po'))[1]
        report(entries_by_lang)
        return 0

    messages = scan_sources(root)
    print(u'扫描到 %d 条需要翻译的文字。' % len(messages))
    differs = []
    entries_by_lang = {}
    for lang in LANGUAGES:
        path = os.path.join(lang_dir, lang + '.po')
        old_text = read_po_text(path)
        header, entries = parse_po(old_text)
        new_text = build_po(header, entries, messages)
        if new_text != old_text:
            differs.append(lang)
            if not check:
                with io.open(path, 'w', encoding='utf-8', newline='\n') as fp:
                    fp.write(new_text)
        entries_by_lang[lang] = parse_po(new_text)[1]
    report(entries_by_lang)
    if check:
        if differs:
            print(u'以下 .po 与源码不一致，请运行 python flamingoclient/tools/i18n_extract.py：' + u'、'.join(differs))
            return 1
        print(u'四个 .po 都与源码一致。')
        return 0
    if differs:
        print(u'已更新：' + u'、'.join(differs))
    else:
        print(u'四个 .po 都已是最新，无需改动。')
    return 0


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
    sys.exit(main(sys.argv[1:]))
