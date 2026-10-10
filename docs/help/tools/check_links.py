# -*- coding: utf-8 -*-
"""
检查 FlamingoIM 使用教程的链接与图片：
  1. 每个页面里的相对链接都指向存在的文件，带 #锚点 的链接锚点在目标页面里存在（help.js 自动编号的 s1、s2 … 也算）；
  2. 每个 <img> 引用的图片文件存在，并且写了 alt；
  3. 列出 img/ 里没有被任何页面引用的图片；
  4. toc.js 登记的章节文件是否都已写好，页面 body 的 data-chapter 是否与 toc.js 一致。
用法：python docs/help/tools/check_links.py      有问题时退出码为 1
balloonwj@qq.com   2026-10-09
"""
import os
import re
import sys
from html.parser import HTMLParser
from urllib.parse import unquote

HELP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class PageParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.imgs = []
        self.ids = set()
        self.h2_count = 0
        self.chapter = None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if a.get('id'):
            self.ids.add(a['id'])
        if tag == 'h2':
            self.h2_count += 1
            if not a.get('id'):
                self.ids.add('s%d' % self.h2_count)
        if tag == 'a' and a.get('href'):
            self.links.append(a['href'])
        if tag == 'img':
            self.imgs.append((a.get('src', ''), a.get('alt')))
        if tag == 'body':
            self.chapter = a.get('data-chapter')


def parse(path):
    p = PageParser()
    with open(path, encoding='utf-8') as fp:
        p.feed(fp.read())
    return p


def main():
    pages = {}
    for d, _sub, files in os.walk(HELP_DIR):
        if os.sep + 'tools' in d:
            continue
        for f in files:
            if f.endswith('.html'):
                path = os.path.normpath(os.path.join(d, f))
                pages[path] = parse(path)

    problems = []
    used_imgs = set()
    for path, p in sorted(pages.items()):
        rel = os.path.relpath(path, HELP_DIR)
        for href in p.links:
            if re.match(r'^(https?:|mailto:|javascript:)', href):
                continue
            target, _, anchor = href.partition('#')
            tpath = path if not target else os.path.normpath(os.path.join(os.path.dirname(path), unquote(target)))
            if not os.path.exists(tpath):
                problems.append('%s: link to missing file %s' % (rel, href))
                continue
            if anchor and tpath in pages and anchor not in pages[tpath].ids:
                problems.append('%s: link to missing anchor %s' % (rel, href))
        for src, alt in p.imgs:
            ipath = os.path.normpath(os.path.join(os.path.dirname(path), unquote(src)))
            if not os.path.exists(ipath):
                problems.append('%s: missing image %s' % (rel, src))
            else:
                used_imgs.add(ipath)
            if not alt:
                problems.append('%s: image without alt %s' % (rel, src))

    with open(os.path.join(HELP_DIR, 'assets', 'toc.js'), encoding='utf-8') as fp:
        toc = re.findall(r"\{\s*id:\s*'([^']+)',\s*file:\s*'([^']+)'", fp.read())
    missing_pages = []
    for cid, frel in toc:
        fpath = os.path.normpath(os.path.join(HELP_DIR, frel))
        if fpath not in pages:
            missing_pages.append(frel)
        elif pages[fpath].chapter != cid:
            problems.append('%s: data-chapter is %r, toc.js says %r' % (frel, pages[fpath].chapter, cid))

    img_dir = os.path.join(HELP_DIR, 'img')
    unused = sorted(f for f in os.listdir(img_dir) if os.path.normpath(os.path.join(img_dir, f)) not in used_imgs)

    print('pages: %d, problems: %d, chapters not written: %d, unused images: %d'
          % (len(pages), len(problems), len(missing_pages), len(unused)))
    for x in problems:
        print('  PROBLEM', x)
    if missing_pages:
        print('  not written:', ' '.join(missing_pages))
    if unused:
        print('  unused images:', ' '.join(unused))
    return 1 if problems or missing_pages else 0


if __name__ == '__main__':
    sys.exit(main())
