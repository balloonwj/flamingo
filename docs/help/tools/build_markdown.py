# -*- coding: utf-8 -*-
"""
把 FlamingoIM 网页版使用教程合并转换成一份 Markdown 文件 docs/help/help.md，内容与网页完全相同。

按 assets/toc.js 的顺序读取首页与全部章节页面的 <article class="doc">，转换规则：
  - 每章标题前加「第 N 章」（与 help.js 一致，三个部分各自从 1 编号），章内 <h2> / <h3> 降一级；
  - 章节之间、章内小节的链接改为指向本文件内的标题。生成时先给标题记下内部 id（章节为 toc.js 的章节 id，
    小节为「章节 id-小节 id」，没有 id 的 <h2> 与 help.js 一样按出现顺序编为 s1、s2 …），全文生成后再把链接换成
    由标题文字生成的锚点（如 #第-1-章-快速开始登录与注册）。不在标题里写 <a id> 锚点：Typora 会把这种没有内容的
    HTML 标签原样显示成源码，而且它只认 name 属性，本来也跳不过去；
  - 标题锚点要同时适用于 GitHub 与 Typora，两者由标题文字生成锚点的规则不同（见 github_slug 与 typora_anchor），
    所以标题里的全角空格一律换成半角空格；被链接的标题若在两边算出的锚点不一致（典型是与前面的标题重名），
    脚本报错并指出是哪个标题，请到章节页面里把它改成不重名的标题；
  - 文件开头不写「本文件由脚本生成」的 HTML 注释，Typora 会把注释原样显示出来；
  - 网页上用 CSS 叠在截图上的编号圆点，在 Markdown 里无法表现，于是把圆点画进图片，另存到 img-md/，
    图下的编号说明（ol.legend）照常转成有序列表；
  - 网页上缩小显示的截图用 <img width> 保持相同的显示宽度，其余用 Markdown 图片语法；
  - 提示框转成 GitHub 风格的 > [!TIP] / > [!NOTE] / > [!WARNING]，界面元素名（span.ui）加「」，按键保留 <kbd>。
修改章节页面后运行：python docs/help/tools/build_markdown.py
balloonwj@qq.com   2026-10-10
"""
import html
import os
import re
import sys
import unicodedata
from html.parser import HTMLParser

from PIL import Image, ImageDraw, ImageFont

HELP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOC_JS = os.path.join(HELP_DIR, 'assets', 'toc.js')
OUT_MD = os.path.join(HELP_DIR, 'help.md')
IMG_DIR = os.path.join(HELP_DIR, 'img')
# 画上编号圆点的截图副本存放目录（相对 docs/help）；不放进 img/，以免被 check_links.py 当作闲置图片
PIN_IMG_REL = 'img-md'
PIN_IMG_DIR = os.path.join(HELP_DIR, PIN_IMG_REL)

# 编号圆点的外观，取自 assets/help.css 的 .pin（单位为 CSS 像素，按图片的缩放比例换算成原图像素）
PIN_DIAMETER = 22          # 圆点直径
PIN_RING = 3               # 圆点外圈白边宽度
PIN_FONT_SIZE = 12         # 编号字号
PIN_COLOR = (0xff, 0x5a, 0x44)          # 浅色主题的 --accent
PIN_RING_COLOR = (255, 255, 255, 217)   # rgba(255,255,255,.85)
PIN_FONT_FILE = r'C:\Windows\Fonts\msyhbd.ttc'   # 微软雅黑粗体；没有时退回 PIL 自带字体

BLOCK_TAGS = {'h1', 'h2', 'h3', 'h4', 'p', 'ul', 'ol', 'li', 'table', 'tr', 'td', 'th', 'figure', 'figcaption',
              'div', 'section', 'colgroup', 'col', 'thead', 'tbody'}
VOID_TAGS = {'img', 'br', 'col', 'meta', 'link', 'hr'}
CALLOUT_KINDS = {'tip': 'TIP', 'note': 'NOTE', 'warn': 'WARNING'}
# 中文字符与全角标点的范围（CJK 标点、汉字、全角符号）
CJK = '　-〿㐀-鿿＀-￯'
CJK_NEWLINE_RE = re.compile('(?<=[%s])[ \\t]*\\n\\s*(?=[%s])' % (CJK, CJK))
UI_SPACE_BEFORE_RE = re.compile('(?<=[%s]) (?=「)' % CJK)
UI_SPACE_AFTER_RE = re.compile('(?<=」) (?=[%s])' % CJK)
# 标题的内部 id 先以「\x00id\x00」的形式写在标题文字前面，全文生成后由 resolve_heading_links 换成锚点并去掉
ANCHOR_MARK = '\x00%s\x00'
ANCHOR_MARK_RE = re.compile('\x00([^\x00]+)\x00')
HEADING_RE = re.compile(r'^(#{1,6}) (.*)$', re.M)
INTERNAL_LINK_RE = re.compile(r'\]\(#([^)\s]+)\)')


class Node(object):
    """极简 DOM 节点：tag 为 None 时表示文本节点。"""

    def __init__(self, tag, attrs=None, text=''):
        self.tag = tag
        self.attrs = dict(attrs or [])
        self.text = text
        self.children = []

    def classes(self):
        return set((self.attrs.get('class') or '').split())

    def plain_text(self):
        if self.tag is None:
            return self.text
        return ''.join(c.plain_text() for c in self.children)


class ArticleTreeBuilder(HTMLParser):
    """只把 <article class="doc"> 内部建成节点树。"""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = None
        self.stack = []

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs)
        if self.root is None:
            if tag == 'article' and 'doc' in node.classes():
                self.root = node
                self.stack = [node]
            return
        if not self.stack:
            return
        self.stack[-1].children.append(node)
        if tag not in VOID_TAGS:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        if self.stack:
            self.stack[-1].children.append(Node(tag, attrs))

    def handle_endtag(self, tag):
        if not self.stack:
            return
        # 从栈顶向下找到同名标签再出栈，容忍个别未闭合的标签
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                break

    def handle_data(self, data):
        if self.stack:
            self.stack[-1].children.append(Node(None, text=data))


def parse_article(path):
    with open(path, encoding='utf-8') as f:
        builder = ArticleTreeBuilder()
        builder.feed(f.read())
    if builder.root is None:
        raise RuntimeError('no <article class="doc"> in ' + path)
    return builder.root


def load_toc():
    """从 toc.js 取出部分与章节（id、标题、说明、文件名），保持原顺序。"""
    with open(TOC_JS, encoding='utf-8') as f:
        src = f.read()
    parts = []
    for m in re.finditer(r"\{\s*id:\s*'(\w+)',\s*title:\s*'([^']*)',\s*desc:\s*'([^']*)',\s*chapters:\s*\[(.*?)\]\s*\}",
                         src, re.S):
        chapters = re.findall(r"\{\s*id:\s*'(\w+)',\s*file:\s*'([^']+)',\s*title:\s*'([^']*)'\s*\}", m.group(4))
        parts.append({'id': m.group(1), 'title': m.group(2), 'desc': m.group(3),
                      'chapters': [{'id': c[0], 'file': c[1], 'title': c[2]} for c in chapters]})
    if not parts:
        raise RuntimeError('cannot parse ' + TOC_JS)
    return parts


def is_punct(ch):
    """Unicode 标点或符号。CommonMark 判断强调标记能否生效时，把这两类字符都当作标点。"""
    return unicodedata.category(ch)[0] in ('P', 'S')


def emphasis(text, mark, tag, standalone=False):
    """给 text 加粗 / 加斜。首尾是标点、两侧又紧挨着文字时，**…** 在中文里不生效（CommonMark 的左右侧判定），
    此时改用 HTML 标签。standalone 为 True 表示这段文字独占一行，两侧是行首行尾，标记总能生效。"""
    text = text.strip()
    if not text:
        return ''
    if not standalone and (is_punct(text[0]) or is_punct(text[-1])):
        return '<%s>%s</%s>' % (tag, text, tag)
    return mark + text + mark


def escape_md(text):
    """转义正文里会被当作 Markdown 语法的字符。"""
    text = text.replace('\\', '\\\\')
    for ch in '*_`[]':
        text = text.replace(ch, '\\' + ch)
    return text.replace('<', '&lt;')


class Converter(object):
    """把一章（或首页）的节点树转成 Markdown 文本行。"""

    def __init__(self, link_map, anchors, chapter_id, page_dir, heading_shift):
        self.link_map = link_map          # 页面相对路径（相对 docs/help）→ 章节 id，首页为 ''
        self.anchors = anchors            # 本文件内全部可用的锚点
        self.chapter_id = chapter_id      # 当前章节 id，首页为 ''
        self.page_dir = page_dir          # 当前页面所在目录（相对 docs/help），用于解析相对链接与图片
        self.heading_shift = heading_shift
        self.h2_count = 0
        self.pin_images = 0

    # ---------- 行内 ----------
    def inline(self, node, in_bold=False):
        out = []
        for c in node.children:
            out.append(self.inline_node(c, in_bold))
        return ''.join(out)

    def inline_node(self, n, in_bold=False):
        if n.tag is None:
            # 源码里落在两个中文字符之间的换行只是排版用的，浏览器显示时中文之间不会出现空格，这里直接去掉
            text = CJK_NEWLINE_RE.sub('', n.text)
            return escape_md(re.sub(r'\s+', ' ', text))
        cls = n.classes()
        if n.tag == 'span' and 'pin' in cls:
            return ''
        if n.tag == 'span' and 'ui' in cls:
            return '「' + self.inline(n, in_bold).strip() + '」'
        if n.tag in ('b', 'strong'):
            inner = self.inline(n, True)
            return inner if in_bold else emphasis(inner, '**', 'b')
        if n.tag in ('i', 'em'):
            return emphasis(self.inline(n, in_bold), '*', 'i')
        if n.tag == 'code':
            txt = n.plain_text()
            fence = '``' if '`' in txt else '`'
            pad = ' ' if fence == '``' else ''
            return fence + pad + txt + pad + fence
        if n.tag == 'kbd':
            return '<kbd>' + html.escape(n.plain_text().strip(), quote=False) + '</kbd>'
        if n.tag == 'br':
            return '<br>'
        if n.tag == 'a':
            text = self.inline(n, in_bold).strip()
            return '[%s](%s)' % (text, self.resolve_link(n.attrs.get('href', '')))
        if n.tag == 'img':
            return self.image_md(n, None)
        # small、span 等其余行内标签只保留文字
        return self.inline(n, in_bold)

    def resolve_link(self, href):
        if re.match(r'^[a-z]+:', href):
            return href
        page, _, frag = href.partition('#')
        if page:
            target = os.path.normpath(os.path.join(self.page_dir, page)).replace('\\', '/')
            if target not in self.link_map:
                raise RuntimeError('link to unknown page: %s (in %s)' % (href, self.chapter_id or 'index'))
            chap = self.link_map[target]
        else:
            chap = self.chapter_id
        if not chap and not frag:
            return '#top'
        if frag and (chap + '-' + frag) in self.anchors:
            return '#' + chap + '-' + frag
        if frag and not chap and frag in self.anchors:
            return '#' + frag
        if chap in self.anchors:
            return '#' + chap
        raise RuntimeError('cannot resolve link: %s (in %s)' % (href, self.chapter_id or 'index'))

    # ---------- 图片 ----------
    def image_md(self, img, pins):
        src = img.attrs.get('src', '')
        alt = img.attrs.get('alt', '')
        name = os.path.basename(src)
        path = os.path.normpath(os.path.join(HELP_DIR, self.page_dir, src))
        with Image.open(path) as im:
            native_w = im.size[0]
        width = img.attrs.get('width')
        rel = 'img/' + name
        if pins:
            rel = self.draw_pins(path, name, pins, int(width) if width else native_w)
        if width and int(width) != native_w:
            return '<img src="%s" width="%s" alt="%s">' % (rel, width, html.escape(alt))
        return '![%s](%s)' % (alt.replace('[', '').replace(']', ''), rel)

    def draw_pins(self, path, name, pins, display_w):
        """把编号圆点按网页上的百分比位置画进截图副本，返回副本的相对路径。"""
        im = Image.open(path).convert('RGBA')
        scale = im.size[0] / float(display_w)
        overlay = Image.new('RGBA', im.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(overlay)
        r = PIN_DIAMETER / 2.0 * scale
        ring = PIN_RING * scale
        try:
            font = ImageFont.truetype(PIN_FONT_FILE, int(round(PIN_FONT_SIZE * scale)))
        except (IOError, OSError):
            font = ImageFont.load_default()
        for left, top, label in pins:
            cx = im.size[0] * left / 100.0
            cy = im.size[1] * top / 100.0
            draw.ellipse([cx - r - ring, cy - r - ring, cx + r + ring, cy + r + ring], fill=PIN_RING_COLOR)
            draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=PIN_COLOR + (255,))
            draw.text((cx, cy), label, fill=(255, 255, 255, 255), font=font, anchor='mm')
        out = Image.alpha_composite(im, overlay).convert('RGB')
        if not os.path.isdir(PIN_IMG_DIR):
            os.makedirs(PIN_IMG_DIR)
        out.save(os.path.join(PIN_IMG_DIR, name), optimize=True)
        self.pin_images += 1
        return PIN_IMG_REL + '/' + name

    def figure(self, fig):
        img = None
        pins = []
        caption = ''
        for c in fig.children:
            if c.tag == 'img':
                img = c
            elif c.tag == 'div' and 'annotated' in c.classes():
                for a in c.children:
                    if a.tag == 'img':
                        img = a
                    elif a.tag == 'span' and 'pin' in a.classes():
                        style = a.attrs.get('style', '')
                        m = re.search(r'left:\s*([\d.]+)%;\s*top:\s*([\d.]+)%', style)
                        if not m:
                            raise RuntimeError('bad pin style: ' + style)
                        pins.append((float(m.group(1)), float(m.group(2)), a.plain_text().strip()))
            elif c.tag == 'figcaption':
                caption = self.inline(c).strip()
        if img is None:
            raise RuntimeError('figure without <img> in ' + self.chapter_id)
        blocks = [[self.image_md(img, pins)]]
        if caption:
            blocks.append([emphasis(caption, '*', 'i', True)])
        return blocks

    # ---------- 块级 ----------
    def blocks(self, node):
        """把容器的子节点转成若干块（每块是一组行），连续的行内内容合成一个段落。"""
        result = []
        run = []

        def flush():
            text = ''.join(run).strip()
            del run[:]
            if text:
                result.append([text])

        for c in node.children:
            if c.tag is None or c.tag not in BLOCK_TAGS:
                run.append(self.inline_node(c))
                continue
            flush()
            result.extend(self.block(c))
        flush()
        return result

    def block(self, n):
        cls = n.classes()
        if n.tag in ('h1', 'h2', 'h3', 'h4'):
            return [self.heading(n)]
        if n.tag == 'p':
            text = self.inline(n).strip()
            return [[text]] if text else []
        if n.tag in ('ul', 'ol'):
            return [self.list_lines(n)]
        if n.tag == 'table':
            return [self.table(n)]
        if n.tag == 'figure':
            return self.figure(n)
        if n.tag == 'div' and 'callout' in cls:
            return [self.callout(n)]
        if n.tag in ('colgroup', 'col'):
            return []
        if n.attrs.get('id') == 'parts':
            # 首页的章节卡片由脚本生成，Markdown 里用前面的目录代替
            return []
        # shot-row、section 等容器：依次展开子节点
        return self.blocks(n)

    def heading(self, n):
        level = int(n.tag[1])
        anchor = ''
        if self.chapter_id:
            if n.tag == 'h2':
                self.h2_count += 1
                anchor = self.chapter_id + '-' + (n.attrs.get('id') or ('s%d' % self.h2_count))
            elif n.attrs.get('id'):
                anchor = self.chapter_id + '-' + n.attrs['id']
        elif n.attrs.get('id'):
            anchor = n.attrs['id']
        text = heading_text(self.inline(n))
        prefix = '#' * min(level + self.heading_shift, 6)
        if anchor:
            return ['%s %s%s' % (prefix, ANCHOR_MARK % anchor, text)]
        return ['%s %s' % (prefix, text)]

    def list_lines(self, n):
        lines = []
        ordered = n.tag == 'ol'
        index = 0
        for li in n.children:
            if li.tag != 'li':
                continue
            if 'li' == li.tag and self.skip_li(li):
                continue
            index += 1
            marker = ('%d. ' % index) if ordered else '- '
            indent = ' ' * len(marker)
            item_blocks = self.blocks(li)
            if not item_blocks:
                item_blocks = [['']]
            first = True
            for b in item_blocks:
                for j, line in enumerate(b):
                    if first:
                        lines.append(marker + line)
                        first = False
                    elif j == 0 and lines:
                        # 列表项内的第二个块：空一行并缩进，属于同一项
                        lines.append('')
                        lines.append(indent + line if line else '')
                    else:
                        lines.append(indent + line if line else '')
        return lines

    def skip_li(self, li):
        # 首页「怎样阅读本教程」里介绍网页搜索框与看大图的一条只适用于网页版
        return not self.chapter_id and '搜索框' in li.plain_text()

    def table(self, n):
        """Markdown 表格不能合并单元格：rowspan 的内容在下面几行重复写出；colspan 只在第一格写内容、其余格留空，
        表体里整行横跨的 <th>（分组标题行）加粗。"""
        rows = []
        pending = {}      # 列号 → [剩余行数, 文字]，记录上面几行 rowspan 延续下来的单元格
        for row_index, tr in enumerate(self.iter_rows(n)):
            cells = []
            for c in tr.children:
                if c.tag not in ('td', 'th'):
                    continue
                self.fill_rowspan(cells, pending)
                text = self.inline(c).strip().replace('|', '\\|')
                text = re.sub(r'\s*\n\s*', ' ', text)
                span = int(c.attrs.get('colspan', '1'))
                if c.tag == 'th' and row_index > 0 and text:
                    text = emphasis(text, '**', 'b')
                rowspan = int(c.attrs.get('rowspan', '1'))
                if rowspan > 1:
                    pending[len(cells)] = [rowspan - 1, text]
                cells.append(text)
                cells.extend([''] * (span - 1))
            self.fill_rowspan(cells, pending)
            if cells:
                rows.append(cells)
        if not rows:
            return []
        width = max(len(r) for r in rows)
        rows = [r + [''] * (width - len(r)) for r in rows]
        lines = ['| ' + ' | '.join(rows[0]) + ' |', '|' + '---|' * width]
        for r in rows[1:]:
            lines.append('| ' + ' | '.join(r) + ' |')
        return lines

    @staticmethod
    def fill_rowspan(cells, pending):
        """当前列若被上面几行的 rowspan 占用，就把那段文字补进本行，并把剩余行数减一。"""
        while len(cells) in pending:
            col = len(cells)
            left, text = pending[col]
            cells.append(text)
            if left <= 1:
                del pending[col]
            else:
                pending[col] = [left - 1, text]

    def iter_rows(self, n):
        for c in n.children:
            if c.tag == 'tr':
                yield c
            elif c.tag in ('thead', 'tbody'):
                for r in self.iter_rows(c):
                    yield r

    def callout(self, n):
        kind = 'NOTE'
        for k, v in CALLOUT_KINDS.items():
            if k in n.classes():
                kind = v
        title = ''
        body = Node('div')
        for c in n.children:
            if not title and c.tag == 'b' and not body.plain_text().strip():
                title = self.inline(c, True).strip()
                continue
            body.children.append(c)
        lines = ['> [!%s]' % kind]
        if title:
            lines.append('> ' + emphasis(title, '**', 'b', True))
            lines.append('>')
        first = True
        for b in self.blocks(body):
            if not first:
                lines.append('>')
            first = False
            for line in b:
                lines.append('> ' + line if line else '>')
        return lines


def heading_text(text):
    """标题文字：全角空格换成半角空格。GitHub 生成锚点时删掉全角空格，Typora 却把它换成连字符，两边会对不上。"""
    return text.replace('\u3000', ' ').strip()


def rendered_text(md_text):
    """估算一行标题在页面上显示出来的文字：去掉内部 id 标记、HTML 标签、强调与代码标记、反斜杠转义。"""
    t = ANCHOR_MARK_RE.sub('', md_text)
    t = re.sub(r'<[^>]+>', '', t)
    t = t.replace('**', '').replace('`', '')
    t = re.sub(r'\\(.)', r'\1', t)
    return html.unescape(t).strip()


def github_slug(text):
    r"""GitHub 由标题文字生成锚点的规则：转小写，只保留字母、组合符号、十进制数字、连接标点、空格与连字符
    （Ruby 正则 [^\p{Word}\- ]），再把空格换成连字符。同名标题由调用方加 -1、-2 … 后缀。"""
    out = []
    for ch in text.lower():
        cat = unicodedata.category(ch)
        if ch in ' -' or cat[0] in ('L', 'M') or cat in ('Nd', 'Pc'):
            out.append(ch)
    return ''.join(out).replace(' ', '-')


def typora_anchor(text):
    """Typora（42.x，appsrc/window/frame.js 的 titleToAnchor）由标题文字生成锚点的规则：转小写、去掉首尾空白，
    破折号换成连字符，每个空白字符与连字符都换成连字符，再删掉全部 Unicode 标点以及 + 和 =。
    同名标题的第二个起加 -2、-3 … 后缀（与 GitHub 的 -1、-2 … 不同）。"""
    t = text.lower().strip()
    t = t.replace('\u2014', '---').replace('\u2013', '--')
    t = re.sub(r'\s', '-', t)
    return ''.join(ch for ch in t if ch == '-' or (not unicodedata.category(ch).startswith('P') and ch not in '+='))


def resolve_heading_links(text):
    """把全文里 ](#内部 id) 形式的链接换成由标题文字生成的锚点，并去掉标题上的内部 id 标记。
    被链接的标题在 GitHub 与 Typora 两边算出的锚点必须一致，否则报错。返回 (新文本, 被链接的标题数)。"""
    github_seen = {}
    typora_seen = {}
    targets = {}
    for m in HEADING_RE.finditer(text):
        shown = rendered_text(m.group(2))
        slug = github_slug(shown)
        anchor = typora_anchor(shown)
        github_key = slug if slug not in github_seen else '%s-%d' % (slug, github_seen[slug])
        typora_key = anchor if anchor not in typora_seen else '%s-%d' % (anchor, typora_seen[anchor] + 1)
        github_seen[slug] = github_seen.get(slug, 0) + 1
        typora_seen[anchor] = typora_seen.get(anchor, 0) + 1
        mark = ANCHOR_MARK_RE.match(m.group(2))
        if mark:
            targets[mark.group(1)] = (github_key, typora_key, shown)

    used = set(INTERNAL_LINK_RE.findall(text))
    problems = []
    for anchor_id in sorted(used):
        if anchor_id not in targets:
            problems.append('link #%s points to no heading' % anchor_id)
        elif targets[anchor_id][0] != targets[anchor_id][1]:
            github_key, typora_key, shown = targets[anchor_id]
            problems.append('heading "%s": GitHub anchor #%s, Typora anchor #%s (rename it so that it is unique)'
                            % (shown, github_key, typora_key))
    if problems:
        raise RuntimeError('cannot build heading links:\n  ' + '\n  '.join(problems))

    def replace_link(m):
        return '](#%s)' % targets[m.group(1)][0]

    text = INTERNAL_LINK_RE.sub(replace_link, text)
    return ANCHOR_MARK_RE.sub('', text), len(used)


def join_blocks(blocks):
    return '\n\n'.join('\n'.join(b) for b in blocks if b)


def collect_anchors(parts):
    """先扫一遍全部章节，得到可用的锚点集合，供链接解析时检查。"""
    anchors = {'top'}
    for part in parts:
        anchors.add('part-' + part['id'])
        for ch in part['chapters']:
            anchors.add(ch['id'])
            root = parse_article(os.path.join(HELP_DIR, ch['file']))
            count = [0]

            def walk(node):
                for c in node.children:
                    if c.tag == 'h2':
                        count[0] += 1
                        anchors.add(ch['id'] + '-' + (c.attrs.get('id') or ('s%d' % count[0])))
                    elif c.tag in ('h3', 'h4') and c.attrs.get('id'):
                        anchors.add(ch['id'] + '-' + c.attrs['id'])
                    if c.tag is not None:
                        walk(c)
            walk(root)
    return anchors


def main():
    parts = load_toc()
    link_map = {'index.html': ''}
    for part in parts:
        for ch in part['chapters']:
            link_map[ch['file']] = ch['id']
    anchors = collect_anchors(parts)
    index_root = parse_article(os.path.join(HELP_DIR, 'index.html'))
    for c in index_root.children:
        if c.tag == 'h2' and c.attrs.get('id'):
            anchors.add(c.attrs['id'])

    out = []

    # 首页：hero 里的标题与介绍、阅读说明、示例数据说明
    conv = Converter(link_map, anchors, '', '', 0)
    blocks = []
    for c in index_root.children:
        if c.tag == 'section':
            for h in c.children:
                if h.tag == 'h1':
                    blocks.append(['# ' + ANCHOR_MARK % 'top' + heading_text(conv.inline(h))])
                elif h.tag == 'p':
                    blocks.append([conv.inline(h).strip()])
        elif c.tag is not None:
            blocks.extend(conv.block(c))
        elif c.text.strip():
            blocks.append([escape_md(c.text.strip())])
    out.append(join_blocks(blocks))

    # 目录
    toc = ['## 目录']
    for pi, part in enumerate(parts):
        lines = ['- [第%s部分　%s](#part-%s)' % ('一二三四五六七八九'[pi], part['title'], part['id'])]
        for ci, ch in enumerate(part['chapters']):
            lines.append('  %d. [%s](#%s)' % (ci + 1, ch['title'], ch['id']))
        toc.append('\n'.join(lines))
    out.append('\n\n'.join(toc))

    pin_images = 0
    for pi, part in enumerate(parts):
        out.append('# %s第%s部分 %s\n\n%s' %
                   (ANCHOR_MARK % ('part-' + part['id']), '一二三四五六七八九'[pi], part['title'], part['desc']))
        for ci, ch in enumerate(part['chapters']):
            root = parse_article(os.path.join(HELP_DIR, ch['file']))
            conv = Converter(link_map, anchors, ch['id'], os.path.dirname(ch['file']), 1)
            blocks = []
            for b in conv.blocks(root):
                if b and b[0].startswith('## ') and not blocks:
                    # 章标题：<h1> 降为 ##，前面加「第 N 章」，锚点用章节 id
                    title = ANCHOR_MARK_RE.sub('', b[0][3:])
                    blocks.append(['## %s第 %d 章 %s' % (ANCHOR_MARK % ch['id'], ci + 1, title)])
                else:
                    blocks.append(b)
            out.append(join_blocks(blocks))
            pin_images += conv.pin_images

    text = '\n\n'.join(out).rstrip('\n') + '\n'
    text = re.sub(r'\n{3,}', '\n\n', text)
    # 网页源码在界面元素名两侧留了空格，给按钮样式留出间距；换成「」之后，中文与「」之间不需要这个空格
    text = UI_SPACE_BEFORE_RE.sub('', text)
    text = UI_SPACE_AFTER_RE.sub('', text)
    text, link_targets = resolve_heading_links(text)
    with open(OUT_MD, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    chapters = sum(len(p['chapters']) for p in parts)
    print('wrote %s: %d chapters, %d lines, %d bytes, %d images with pins in %s/, %d heading link targets' %
          (os.path.relpath(OUT_MD, os.path.dirname(HELP_DIR)), chapters, text.count('\n'), len(text.encode('utf-8')),
           pin_images, PIN_IMG_REL, link_targets))
    return 0


if __name__ == '__main__':
    sys.exit(main())
