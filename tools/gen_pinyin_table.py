#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
汉字 → 拼音表生成器（供客户端全拼搜索使用）。

用途
----
从 Unicode 官方 Unihan 数据库的 kMandarin 字段，生成客户端全拼搜索用的静态查表
文件 flamingoclient/Source/BalloonSearch/PinyinTable.inc。

之所以要生成而不是手写：3755 个 GB2312 一级汉字（本表实际覆盖到 CJK 基本区约两万
字）的读音必须来自权威数据，手写既不可能准确、也无从校对。Unihan 是 Unicode 官方
维护的汉字属性库，kMandarin 给出该字的普通话读音（带声调，多音字按使用频度排序，
首个即最常用读音）。

用法
----
    python tools/gen_pinyin_table.py <Unihan.zip 或 Unihan_Readings.txt 的路径>

不带参数时会从 Unicode 官网下载最新的 Unihan.zip 到临时目录。
生成的 .inc 直接覆盖写入 flamingoclient/Source/BalloonSearch/PinyinTable.inc。

数据处理口径
------------
· 只收 CJK 统一汉字基本区（U+4E00..U+9FFF）—— 覆盖 GB2312 / GBK 的全部常用字，
  扩展区的生僻字人名里极罕见，收进来只会把表撑大。
· 多音字取 kMandarin 的<第一个>读音（即最常用读音）。搜索场景下按最常用音匹配足够，
  也避免一个字挂多个音后匹配歧义爆炸。
· 声调一律去掉（ā/á/ǎ/à → a），ü 记作 v（与主流输入法习惯一致，如 lǜ → lv）。

balloonwj@qq.com   2026-08-05
"""

import os
import sys
import io
import re
import zipfile
import unicodedata
import tempfile

# 生成目标：客户端搜索模块下的查表文件
OUT_REL_PATH = os.path.join("flamingoclient", "Source", "BalloonSearch", "PinyinTable.inc")

# 只收 CJK 统一汉字基本区
CJK_FIRST = 0x4E00
CJK_LAST = 0x9FFF

# Unihan 官方下载地址（不带参数运行时用）
UNIHAN_URL = "https://www.unicode.org/Public/UNIDATA/Unihan.zip"

# kMandarin 行的格式：U+4E00<TAB>kMandarin<TAB>yī
LINE_RE = re.compile(r"^U\+([0-9A-Fa-f]+)\tkMandarin\t(.+)$")


def strip_tone(reading: str) -> str:
    """
    去掉一个拼音音节的声调，并把 ü 记作 v。

    做法：先 NFD 分解，使 "ǜ" 变成 'u' + 分音符(U+0308) + 声调符(U+0300)，
    然后只保留 ASCII 字母，遇到分音符就把刚输出的 u 改成 v，其余组合符号（各声调）
    一律丢弃。
    """
    out = []
    for ch in unicodedata.normalize("NFD", reading):
        if ch == "̈":          # 分音符：说明前一个字母是 ü
            if out and out[-1] == "u":
                out[-1] = "v"
            continue
        if unicodedata.combining(ch):   # 其余组合符号都是声调，丢弃
            continue
        if ch.isascii() and ch.isalpha():
            out.append(ch.lower())
    return "".join(out)


def read_unihan_lines(path: str):
    """从 Unihan.zip 或已解压的 Unihan_Readings.txt 里逐行产出文本。"""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as zf:
            name = "Unihan_Readings.txt"
            if name not in zf.namelist():
                raise RuntimeError("压缩包里没有 %s" % name)
            with zf.open(name) as fp:
                for line in io.TextIOWrapper(fp, encoding="utf-8"):
                    yield line.rstrip("\n")
    else:
        with open(path, "r", encoding="utf-8") as fp:
            for line in fp:
                yield line.rstrip("\n")


def collect(path: str):
    """解析出 [(码点, 音节)]（按码点升序）与去重后的音节列表。"""
    entries = {}
    version = ""
    for line in read_unihan_lines(path):
        if line.startswith("#"):
            # 形如 "# Unihan_Readings.txt\n# Date: 2025-..." 的头注释，留作出处标注
            if not version and "Date:" in line:
                version = line.lstrip("# ").strip()
            continue
        m = LINE_RE.match(line)
        if not m:
            continue
        code = int(m.group(1), 16)
        if code < CJK_FIRST or code > CJK_LAST:
            continue
        first = m.group(2).split()[0]        # 多音字取最常用的那个读音
        syllable = strip_tone(first)
        if syllable:
            entries[code] = syllable

    codes = sorted(entries.keys())
    syllables = sorted(set(entries[c] for c in codes))
    return codes, entries, syllables, version


def emit(codes, entries, syllables, version, out_path: str):
    """把表写成 C++ 可直接 include 的 .inc 文件（UTF-8 无 BOM，与客户端源文件一致）。"""
    index_of = {s: i for i, s in enumerate(syllables)}

    buf = []
    buf.append("/**")
    buf.append(" *  PinyinTable.inc —— 汉字→拼音查表数据（全拼搜索用）。")
    buf.append(" *")
    buf.append(" *  <u>本文件由 tools/gen_pinyin_table.py 自动生成，请勿手工编辑</u>；")
    buf.append(" *  数据来源为 Unicode 官方 Unihan 数据库的 kMandarin 字段，要更新表只需")
    buf.append(" *  重新下载 Unihan.zip 后重跑该脚本。")
    buf.append(" *")
    buf.append(" *  口径：只收 CJK 基本区（U+4E00..U+9FFF）；多音字取 kMandarin 的第一个")
    buf.append(" *  （最常用）读音；声调一律去掉，ü 记作 v（与主流输入法习惯一致）。")
    buf.append(" *")
    buf.append(" *  数据版本：%s" % (version or "(未知)"))
    buf.append(" *  收录汉字 %d 个，不同音节 %d 个。" % (len(codes), len(syllables)))
    buf.append(" */")
    buf.append("")
    buf.append("namespace pinyin { namespace table {")
    buf.append("")
    buf.append("//全部不同音节（已去声调、ü 记作 v），按字典序排列")
    buf.append("static const char* const kSyllables[] =")
    buf.append("{")
    line = "   "
    for s in syllables:
        piece = ' "%s",' % s
        if len(line) + len(piece) > 96:
            buf.append(line)
            line = "   "
        line += piece
    if line.strip():
        buf.append(line)
    buf.append("};")
    buf.append("")
    buf.append("//本表覆盖的码点区间 [kFirstChar, kLastChar]，查表直接用 (码点 - kFirstChar)")
    buf.append("//作下标 —— 该区间内有读音的字占 %d/%d，密到不值得再存一份码点数组 + 二分查找。"
               % (len(codes), CJK_LAST - CJK_FIRST + 1))
    buf.append("static const wchar_t kFirstChar = 0x%04X;" % CJK_FIRST)
    buf.append("static const wchar_t kLastChar  = 0x%04X;" % CJK_LAST)
    buf.append("")
    buf.append("//区间内每个码点对应的音节在 kSyllables 里的下标；kNoSyllable 表示该码点")
    buf.append("//没有读音（未分配的空位，或 Unihan 未给出 kMandarin 的字）")
    buf.append("static const unsigned short kNoSyllable = 0xFFFF;")
    buf.append("static const unsigned short kSyllableIndex[] =")
    buf.append("{")
    line = "   "
    for c in range(CJK_FIRST, CJK_LAST + 1):
        s = entries.get(c)
        piece = " %d," % (index_of[s] if s is not None else 0xFFFF)
        if len(line) + len(piece) > 96:
            buf.append(line)
            line = "   "
        line += piece
    if line.strip():
        buf.append(line)
    buf.append("};")
    buf.append("")
    buf.append("}} // namespace pinyin::table")
    buf.append("")

    text = "\r\n".join(buf)
    with open(out_path, "w", encoding="utf-8", newline="") as fp:
        fp.write(text)


def main():
    if len(sys.argv) > 1:
        src = sys.argv[1]
    else:
        import urllib.request
        src = os.path.join(tempfile.gettempdir(), "Unihan.zip")
        print("下载 %s ..." % UNIHAN_URL)
        urllib.request.urlretrieve(UNIHAN_URL, src)

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_path = os.path.join(repo_root, OUT_REL_PATH)

    codes, entries, syllables, version = collect(src)
    if not codes:
        print("没有解析出任何 kMandarin 记录，请检查输入文件。")
        return 1

    emit(codes, entries, syllables, version, out_path)
    print("已生成 %s" % out_path)
    print("  收录汉字 %d 个，不同音节 %d 个，数据版本 %s" % (len(codes), len(syllables), version or "(未知)"))
    print("  文件大小 %.1f KB" % (os.path.getsize(out_path) / 1024.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
