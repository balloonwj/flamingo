# -*- coding: utf-8 -*-
"""
生成 FlamingoIM 使用教程的站内搜索数据 docs/help/assets/search-index.js。

逐个读取 assets/toc.js 里登记的章节页面，按 <h2> 把 <article class="doc"> 切成若干段，每段记下标题、锚点和纯文本，
写成 window.HELP_INDEX = [...]，供 help.js 在浏览器里直接搜索（用 file:// 打开时浏览器不允许 fetch 读取 JSON，
所以数据放在 .js 文件里随页面加载）。没有 id 的 <h2> 按出现顺序编为 s1、s2 …，与 help.js 的编号规则一致。
修改或新增章节后运行：python docs/help/tools/build_index.py
balloonwj@qq.com   2026-10-09
"""
import json
import os
import re
import sys
from html.parser import HTMLParser

HELP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOC_JS = os.path.join(HELP_DIR, 'assets', 'toc.js')
OUT_JS = os.path.join(HELP_DIR, 'assets', 'search-index.js')
# 不计入正文的标签：脚本、样式，以及图片上的编号圆点
SKIP_TAGS = {'script', 'style'}
SKIP_CLASSES = {'pin'}


class ArticleParser(HTMLParser):
    """把 article.doc 按 h1 / h2 切段，收集每段的纯文本。"""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.in_article = 0
        self.skip = 0
        self.heading = None          # 正在收集的标题标签名（h1 / h2），None 表示在收集正文
        self.heading_text = []
        self.heading_id = ''
        self.sections = []           # [标题, 锚点, 正文]
        self.h2_count = 0
        self.depth_stack = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        classes = set((a.get('class') or '').split())
        if tag == 'article' and 'doc' in classes:
            self.in_article = 1
            self.sections.append(['', '', ''])
            return
        if not self.in_article:
            return
        if tag in SKIP_TAGS or classes & SKIP_CLASSES:
            self.skip += 1
            self.depth_stack.append(tag)
            return
        if self.skip:
            self.depth_stack.append(tag)
            return
        if tag in ('h1', 'h2'):
            self.heading = tag
            self.heading_text = []
            if tag == 'h2':
                self.h2_count += 1
                self.heading_id = a.get('id') or ('s%d' % self.h2_count)
            else:
                self.heading_id = ''
        if tag in ('p', 'li', 'td', 'th', 'figcaption', 'br', 'h3', 'div'):
            self.add_text(' ')

    def handle_endtag(self, tag):
        if not self.in_article:
            return
        if self.skip and self.depth_stack:
            self.depth_stack.pop()
            if tag in SKIP_TAGS or not self.depth_stack:
                self.skip = max(0, self.skip - 1) if tag in SKIP_TAGS or not self.depth_stack else self.skip
            return
        if tag == 'article':
            self.in_article = 0
            return
        if tag == self.heading:
            text = clean(''.join(self.heading_text))
            if self.heading == 'h1':
                self.sections[0][0] = text
            else:
                self.sections.append([text, self.heading_id, ''])
            self.heading = None

    def handle_data(self, data):
        if not self.in_article or self.skip:
            return
        if self.heading:
            self.heading_text.append(data)
        else:
            self.add_text(data)

    def add_text(self, text):
        if self.sections:
            self.sections[-1][2] += text


def clean(s):
    return re.sub(r'\s+', ' ', s).strip()


def load_toc():
    with open(TOC_JS, encoding='utf-8') as fp:
        src = fp.read()
    # toc.js 里每章一行：{ id: 'c01', file: 'client/01-quick-start.html', title: '…' }
    return re.findall(r"\{\s*id:\s*'([^']+)',\s*file:\s*'([^']+)',\s*title:\s*'([^']+)'\s*\}", src)


def main():
    index = []
    missing = []
    for cid, file_rel, title in load_toc():
        path = os.path.join(HELP_DIR, file_rel.replace('/', os.sep))
        if not os.path.exists(path):
            missing.append(file_rel)
            continue
        with open(path, encoding='utf-8') as fp:
            parser = ArticleParser()
            parser.feed(fp.read())
        sections = [[s[0] or title, s[1], clean(s[2])] for s in parser.sections]
        index.append({'c': cid, 't': title, 's': sections})
    with open(OUT_JS, 'w', encoding='utf-8', newline='\n') as fp:
        fp.write('/* 由 tools/build_index.py 生成，请勿手工修改。 */\n')
        fp.write('window.HELP_INDEX = ')
        json.dump(index, fp, ensure_ascii=False, separators=(',', ':'))
        fp.write(';\n')
    print('indexed %d chapters, %d sections -> %s' % (len(index), sum(len(p['s']) for p in index), OUT_JS))
    if missing:
        print('not written yet: %d chapters (%s ...)' % (len(missing), ', '.join(missing[:3])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
