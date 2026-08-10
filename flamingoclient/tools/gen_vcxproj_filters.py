#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
按 Flamingo.vcxproj 重新生成 Flamingo.vcxproj.filters
balloonwj@qq.com   2026-08-05

用途
----
`.filters` 只决定 VS「解决方案资源管理器」里的目录树长什么样，不参与编译。
但它一直是手工维护的，新增文件极容易忘了往里加 —— 2026-08-05 曾出现过
vcxproj 有 773 个源文件、filters 里只有 245 个的局面，三分之二的文件在 VS 里
根本看不见。本脚本把它改成可重复生成，新增文件后重跑一次即可。

分组规则
--------
1. 位于子目录的文件（如 `BalloonMain\\Xxx.cpp`、`net\\Yyy.h`）—— 按其所在
   目录路径分组，目录层级原样映射成 filter 层级。
2. 位于 Source 根目录的文件 —— **优先沿用 .filters 里已有的归属**，把
   `UI` / `Utils` / `Tasks` / `UserData` / `FakeData` 这套按职责手工分的组保下来；
   以前没登记过的，按扩展名进 `Source Files`（.c/.cpp/.cc）或 `Header Files`（.h/.hpp）。
3. 仓库外的文件（Include 以 `..` 开头，如 vendored 的 sqlite3.c）—— 统一进
   `third-party` 组，不按它在磁盘上的真实路径建一长串空目录。

非源码条目（Image / Text / None / ResourceCompile）沿用原 .filters 里的归属；
原来没有的，进 `Resource Files`。

用法
----
    cd flamingoclient/tools
    python gen_vcxproj_filters.py            # 生成并覆盖 .filters
    python gen_vcxproj_filters.py --check    # 只比对、不写文件，有差异时退出码为 1

`--check` 适合放进提交前检查：它能在"加了文件却忘了重跑本脚本"时报出来。

注意
----
filter 的 UniqueIdentifier（GUID）按组名做确定性哈希生成，不用随机数 ——
同一个组名每次跑出来的 GUID 都一样，重复生成不会让整个文件产生无谓的 diff。
"""

import hashlib
import os
import re
import sys
import xml.etree.ElementTree as ET

MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"

# 参与"按目录分组"的源码条目类型。
SOURCE_ITEM_TYPES = ("ClCompile", "ClInclude")
# 非源码条目类型：沿用原归属，缺省进 Resource Files。
OTHER_ITEM_TYPES = ("ResourceCompile", "Image", "Text", "None")
# vcxproj 独有、不该出现在 .filters 里的条目类型。
SKIP_ITEM_TYPES = ("ProjectReference", "ProjectConfiguration")

DEFAULT_SOURCE_FILTER = "Source Files"
DEFAULT_HEADER_FILTER = "Header Files"
DEFAULT_OTHER_FILTER = "Resource Files"
THIRD_PARTY_FILTER = "third-party"

SOURCE_EXTS = (".c", ".cpp", ".cc", ".cxx")
HEADER_EXTS = (".h", ".hpp", ".hxx", ".inl")


def script_dir():
    return os.path.dirname(os.path.abspath(__file__))


def project_paths():
    """返回 (vcxproj 路径, filters 路径)。本脚本放在 flamingoclient/tools 下。"""
    source_dir = os.path.normpath(os.path.join(script_dir(), "..", "Source"))
    return (os.path.join(source_dir, "Flamingo.vcxproj"),
            os.path.join(source_dir, "Flamingo.vcxproj.filters"))


def stable_guid(name):
    """按组名生成确定性 GUID —— 同名每次结果一致，避免无谓的 diff。"""
    digest = hashlib.md5(name.encode("utf-8")).hexdigest()
    return "{%s-%s-%s-%s-%s}" % (digest[0:8], digest[8:12], digest[12:16],
                                 digest[16:20], digest[20:32])


def read_existing_filters(filters_path):
    """读旧 .filters，返回 {Include 路径: filter 名}。文件不存在时返回空表。"""
    mapping = {}
    if not os.path.exists(filters_path) or os.path.getsize(filters_path) == 0:
        return mapping
    try:
        tree = ET.parse(filters_path)
    except ET.ParseError:
        # 旧文件损坏（曾出现过被清空成 0 字节的情况）时，当作没有历史归属，
        # 全部按目录规则重建即可。
        return mapping
    for item in tree.getroot().iter():
        tag = item.tag.replace("{%s}" % MSBUILD_NS, "")
        if tag not in SOURCE_ITEM_TYPES + OTHER_ITEM_TYPES:
            continue
        include = item.get("Include")
        if not include:
            continue
        node = item.find("{%s}Filter" % MSBUILD_NS)
        if node is not None and node.text:
            mapping[include.lower()] = node.text.strip()
    return mapping


def read_project_items(proj_path):
    """读 vcxproj，返回 [(条目类型, Include 路径)]，保持原文件里的先后顺序。"""
    items = []
    tree = ET.parse(proj_path)
    for item in tree.getroot().iter():
        tag = item.tag.replace("{%s}" % MSBUILD_NS, "")
        if tag in SKIP_ITEM_TYPES:
            continue
        if tag not in SOURCE_ITEM_TYPES + OTHER_ITEM_TYPES:
            continue
        include = item.get("Include")
        if include:
            items.append((tag, include))
    return items


def decide_filter(item_type, include, existing):
    """按分组规则决定某个条目该进哪个 filter。"""
    # 仓库外的 vendored 文件：不按真实路径建一长串空目录。
    if include.startswith(".."):
        return THIRD_PARTY_FILTER

    directory = os.path.dirname(include)
    if directory:
        # 子目录文件：目录层级原样映射成 filter 层级。
        return directory.replace("/", "\\")

    # 根目录文件：优先沿用历史归属，保住按职责手工分的那几个组。
    prior = existing.get(include.lower())
    if prior:
        return prior

    if item_type in OTHER_ITEM_TYPES:
        return DEFAULT_OTHER_FILTER

    ext = os.path.splitext(include)[1].lower()
    if ext in HEADER_EXTS:
        return DEFAULT_HEADER_FILTER
    if ext in SOURCE_EXTS:
        return DEFAULT_SOURCE_FILTER
    return DEFAULT_SOURCE_FILTER


def expand_filter_tree(names):
    """把 "A\\B\\C" 展开成 A、A\\B、A\\B\\C —— VS 要求每一级都有 Filter 定义。"""
    out = set()
    for name in names:
        parts = name.split("\\")
        for i in range(1, len(parts) + 1):
            out.add("\\".join(parts[:i]))
    return out


def build_filters_xml(items, existing):
    assignments = [(t, inc, decide_filter(t, inc, existing)) for (t, inc) in items]

    lines = []
    lines.append('<?xml version="1.0" encoding="utf-8"?>')
    lines.append('<Project ToolsVersion="4.0" '
                 'xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')

    # 1) filter 定义（含每一级父目录），按名字排序，保证生成结果稳定。
    lines.append("  <ItemGroup>")
    for name in sorted(expand_filter_tree([a[2] for a in assignments])):
        lines.append('    <Filter Include="%s">' % name)
        lines.append("      <UniqueIdentifier>%s</UniqueIdentifier>" % stable_guid(name))
        lines.append("    </Filter>")
    lines.append("  </ItemGroup>")

    # 2) 各类条目分组输出，组内保持 vcxproj 里的原始顺序。
    for item_type in SOURCE_ITEM_TYPES + OTHER_ITEM_TYPES:
        group = [a for a in assignments if a[0] == item_type]
        if not group:
            continue
        lines.append("  <ItemGroup>")
        for (_, include, filter_name) in group:
            lines.append('    <%s Include="%s">' % (item_type, include))
            lines.append("      <Filter>%s</Filter>" % filter_name)
            lines.append("    </%s>" % item_type)
        lines.append("  </ItemGroup>")

    lines.append("</Project>")
    return "\r\n".join(lines) + "\r\n"


def main():
    check_only = "--check" in sys.argv
    proj_path, filters_path = project_paths()

    if not os.path.exists(proj_path):
        sys.stderr.write("找不到工程文件: %s\n" % proj_path)
        return 2

    existing = read_existing_filters(filters_path)
    items = read_project_items(proj_path)
    content = build_filters_xml(items, existing)

    old = ""
    if os.path.exists(filters_path):
        with open(filters_path, "rb") as f:
            old = f.read().decode("utf-8-sig", "replace")

    if check_only:
        if old.replace("\r\n", "\n") == content.replace("\r\n", "\n"):
            print("filters 与 vcxproj 一致，共 %d 个条目。" % len(items))
            return 0
        print("filters 与 vcxproj 不一致：vcxproj 里有 %d 个条目。" % len(items))
        print("请重跑 python gen_vcxproj_filters.py 重新生成。")
        return 1

    # VS 生成的 .filters 是带 BOM 的 UTF-8，保持一致。
    with open(filters_path, "wb") as f:
        f.write(b"\xef\xbb\xbf")
        f.write(content.encode("utf-8"))

    counts = {}
    for (t, _) in items:
        counts[t] = counts.get(t, 0) + 1
    detail = "，".join("%s %d" % (k, counts[k]) for k in sorted(counts))
    print("已生成 %s" % filters_path)
    print("  共 %d 个条目（%s）" % (len(items), detail))
    return 0


if __name__ == "__main__":
    sys.exit(main())
