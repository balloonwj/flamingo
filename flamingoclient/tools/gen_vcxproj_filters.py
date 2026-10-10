#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
按 .vcxproj 重新生成对应的 .vcxproj.filters
balloonwj@qq.com   2026-08-05（2026-08-18 改为纯目录映射，并覆盖测试工程）

用途
----
`.filters` 只决定 VS「解决方案资源管理器」里的目录树长什么样，不参与编译。
它一直是手工维护的，新增文件极容易忘了往里加 —— 2026-08-05 曾出现过 vcxproj
有 773 个源文件、filters 里只有 245 个的局面，三分之二的文件在 VS 里根本看不见。
本脚本把它改成可重复生成，新增文件后重跑一次即可。

分组规则
--------
2026-08-18 把 Source 目录按模块重排成两层结构之后，分组可以完全由磁盘目录推导，
不再需要任何手工维护的归属表：

1. 位于子目录的文件（如 `UI\\BalloonMain\\Xxx.cpp`、`net\\Yyy.h`）—— 按其所在
   目录路径分组，目录层级原样映射成 filter 层级。
2. 位于工程目录下的文件 —— 主工程（Flamingo）只剩 stdafx、resource.h、
   flamingo.rc、ClientVersion.h 这几个构建用的文件，直接挂在工程节点下不分组；
   测试工程（FlamingoUnit）的用例源文件很多，统一归入 `Tests` 组。
3. 路径以 `..` 开头的文件 —— 先去掉全部 `..\\` 前缀再按剩下的目录分组。测试工程
   引用被测代码走的都是这种写法（`..\\UI\\BalloonMain\\Xxx.cpp`），去掉前缀后
   正好落进与主工程同名的分组里。

用法
----
    cd flamingoclient/tools
    python gen_vcxproj_filters.py            # 生成并覆盖两个工程的 .filters
    python gen_vcxproj_filters.py --check    # 只比对、不写文件，有差异时退出码为 1

`--check` 适合放进提交前检查：它能在"加了文件却忘了重跑本脚本"时报出来。

注意
----
filter 的 UniqueIdentifier（GUID）按组名做确定性哈希生成，不用随机数 ——
同一个组名每次跑出来的 GUID 都一样，重复生成不会让整个文件产生无谓的 diff。
"""

import hashlib
import io
import os
import sys
import xml.etree.ElementTree as ET

MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"

# 会出现在 .filters 里的条目类型。ProjectReference / ProjectConfiguration 是
# vcxproj 独有的，不参与分组。
ITEM_TYPES = ("ClCompile", "ClInclude", "ResourceCompile", "Image", "Text", "None")

# 要生成的工程。第三项是"工程目录下的文件"该进哪个组，None 表示不分组、
# 直接挂在工程节点下。
#   路径相对本脚本所在的 flamingoclient/tools 目录。
TARGETS = (
    (os.path.join("..", "Source", "Flamingo.vcxproj"), None),
    (os.path.join("..", "Source", "Tests", "FlamingoUnit.vcxproj"), "Tests"),
    # 2026-10-02 起新增的 DLL 工程（屏幕共享 / 录制 / 音视频）。工程目录下只有
    # DllMain.cpp 与 .def 两个构建用文件，直接挂在工程节点下不分组。
    (os.path.join("..", "FlamingoMedia", "FlamingoMedia.vcxproj"), None),
    # 2026-10-02 起新增的文件共享 DLL 工程（docs/文件共享方案.md 7.1）。工程目录下的
    # DllMain.cpp、FileShareModule.*、FileShareApi.h 等直接挂在工程节点下不分组，
    # Engine/、UI/ 两个子目录各成一组。
    (os.path.join("..", "FlamingoFileShare", "FlamingoFileShare.vcxproj"), None),
    # 2026-10-03 起新增的 P2P 通信 DLL 工程（docs/P2P通信方案.md 4.2）。工程目录下的 DllMain.cpp、
    # FlamingoP2PApi.h 等直接挂在工程节点下不分组，Host/ 子目录成一组。
    (os.path.join("..", "FlamingoP2P", "FlamingoP2P.vcxproj"), None),
    # 2026-10-03 起新增的界面语言包 DLL 工程（docs/多语言方案.md 第 6 节）。工程目录下的源文件、
    # 头文件、.rc 与 .def 直接挂在工程节点下不分组，lang/ 子目录（四个 .po）成一组。
    (os.path.join("..", "FlamingoLang", "FlamingoLang.vcxproj"), None),
    # 2026-10-05 起新增的图片文字识别 DLL 工程（docs/图片文字识别方案.md 第 9 节）。工程目录下的
    # DllMain.cpp、FlamingoOcrApi.h、函数表等直接挂在工程节点下不分组，Core/、WinOcr/ 两个子目录各成一组。
    (os.path.join("..", "FlamingoOcr", "FlamingoOcr.vcxproj"), None),
    # 2026-10-06 起新增的锁屏 DLL 工程（docs/锁屏方案.md 第 4.1 节）。工程目录下的 DllMain.cpp、
    # FlamingoLockApi.h 等直接挂在工程节点下不分组，Host/、Core/、Render/、Session/、Scene/ 各成一组。
    (os.path.join("..", "FlamingoLock", "FlamingoLock.vcxproj"), None),
    # 2026-10-06 起新增的语音字幕 DLL 工程与模型资源 DLL 工程（docs/语音字幕方案.md 第 11 节）。工程目录下的
    # DllMain.cpp、CaptionApi.h、.def 直接挂在工程节点下不分组，Model/、Engine/、Record/、Render/、Host/ 等各成一组。
    (os.path.join("..", "FlamingoCaption", "FlamingoCaption.vcxproj"), None),
    (os.path.join("..", "FlamingoCaptionModel", "FlamingoCaptionModel.vcxproj"), None),
    # 2026-10-07 起新增的单聊密聊 DLL 工程（docs/密聊方案.md 第 4.1 节）。工程目录下的 DllMain.cpp、
    # SecretChatApi.h、.def 直接挂在工程节点下不分组，Host/、Core/、Tests/ 各成一组。
    (os.path.join("..", "FlamingoSecretChat", "FlamingoSecretChat.vcxproj"), None),
    # 2026-10-07 起新增的帖子群 DLL 工程（docs/帖子群方案.md 第 7 节）。工程目录下的 DllMain.cpp、
    # ThreadGroupApi.h、.def 直接挂在工程节点下不分组，Host/、Core/、UI/ 各成一组。
    (os.path.join("..", "FlamingoThreadGroup", "FlamingoThreadGroup.vcxproj"), None),
    # 2026-10-07 起新增的截图 DLL 工程（docs/截图独立模块方案.md 第 3.1 节）。工程目录下的 DllMain.cpp、
    # ScreenshotApi.h、.def 直接挂在工程节点下不分组，Host/ 等子目录各成一组。
    (os.path.join("..", "FlamingoScreenshot", "FlamingoScreenshot.vcxproj"), None),
    # 2026-10-08 起新增的机器人管理界面 DLL 工程（docs/机器人方案.md 8.2）。工程目录下的 DllMain.cpp、
    # BotApi.h、.def 直接挂在工程节点下不分组，Host/、Core/、UI/ 各成一组。
    (os.path.join("..", "FlamingoBot", "FlamingoBot.vcxproj"), None),
)


def script_dir():
    return os.path.dirname(os.path.abspath(__file__))


def stable_guid(name):
    """按组名生成确定性 GUID —— 同名每次结果一致，避免无谓的 diff。"""
    digest = hashlib.md5(name.encode("utf-8")).hexdigest()
    return "{%s-%s-%s-%s-%s}" % (digest[0:8], digest[8:12], digest[12:16],
                                 digest[16:20], digest[20:32])


def read_project_items(proj_path):
    """读 vcxproj，返回 [(条目类型, Include 路径)]，保持原文件里的先后顺序。"""
    items = []
    tree = ET.parse(proj_path)
    for item in tree.getroot().iter():
        tag = item.tag.replace("{%s}" % MSBUILD_NS, "")
        if tag not in ITEM_TYPES:
            continue
        include = item.get("Include")
        if include:
            items.append((tag, include))
    return items


def decide_filter(include, root_filter):
    """
    按分组规则决定某个条目该进哪个 filter。返回 None 表示不分组。

      include     条目在 vcxproj 里的 Include 路径
      root_filter 工程目录下的文件该进的组名，None 表示不分组
    """
    path = include.replace("/", "\\")

    # 去掉全部 ..\ 前缀：测试工程引用被测代码用的就是这种写法，
    # 去掉之后正好与主工程里的分组同名。
    while path.startswith("..\\"):
        path = path[3:]

    directory = os.path.dirname(path)
    if directory:
        return directory
    return root_filter


def expand_filter_tree(names):
    """把 "A\\B\\C" 展开成 A、A\\B、A\\B\\C —— VS 要求每一级都有 Filter 定义。"""
    out = set()
    for name in names:
        if not name:
            continue
        parts = name.split("\\")
        for i in range(1, len(parts) + 1):
            out.add("\\".join(parts[:i]))
    return out


def build_filters_xml(items, root_filter):
    assignments = []
    for (item_type, include) in items:
        assignments.append((item_type, include, decide_filter(include, root_filter)))

    lines = []
    lines.append('<?xml version="1.0" encoding="utf-8"?>')
    lines.append('<Project ToolsVersion="4.0" '
                 'xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')

    # 1) filter 定义（含每一级父目录），按名字排序，保证生成结果稳定。
    names = expand_filter_tree([a[2] for a in assignments if a[2]])
    if names:
        lines.append("  <ItemGroup>")
        for name in sorted(names):
            lines.append('    <Filter Include="%s">' % name)
            lines.append("      <UniqueIdentifier>%s</UniqueIdentifier>" % stable_guid(name))
            lines.append("    </Filter>")
        lines.append("  </ItemGroup>")

    # 2) 各类条目分组输出，组内保持 vcxproj 里的原始顺序。
    for item_type in ITEM_TYPES:
        group = [a for a in assignments if a[0] == item_type]
        if not group:
            continue
        lines.append("  <ItemGroup>")
        for (_, include, filter_name) in group:
            if filter_name:
                lines.append('    <%s Include="%s">' % (item_type, include))
                lines.append("      <Filter>%s</Filter>" % filter_name)
                lines.append("    </%s>" % item_type)
            else:
                # 不分组的条目直接挂在工程节点下。
                lines.append('    <%s Include="%s" />' % (item_type, include))
        lines.append("  </ItemGroup>")

    lines.append("</Project>")
    # 换行符用 LF：同目录的 Flamingo.vcxproj / FlamingoUnit.vcxproj 都是 BOM + LF，
    # 生成的 .filters 跟着一致，重新生成时才不会整个文件每一行都算作改动。
    return "\n".join(lines) + "\n"


def process(proj_rel, root_filter, check_only):
    """生成或校验一个工程的 .filters。返回 (是否一致, 条目数)。"""
    proj_path = os.path.normpath(os.path.join(script_dir(), proj_rel))
    filters_path = proj_path + ".filters"

    if not os.path.exists(proj_path):
        sys.stderr.write("找不到工程文件: %s\n" % proj_path)
        return (False, 0)

    items = read_project_items(proj_path)
    content = build_filters_xml(items, root_filter)

    old = ""
    if os.path.exists(filters_path):
        with open(filters_path, "rb") as f:
            old = f.read().decode("utf-8-sig", "replace")

    same = old.replace("\r\n", "\n") == content.replace("\r\n", "\n")

    if check_only:
        if same:
            print("  %-24s 一致，共 %d 个条目。"
                  % (os.path.basename(proj_path), len(items)))
        else:
            print("  %-24s 不一致：vcxproj 里有 %d 个条目，请重跑本脚本。"
                  % (os.path.basename(proj_path), len(items)))
        return (same, len(items))

    # VS 生成的 .filters 是带 BOM 的 UTF-8，保持一致。
    with open(filters_path, "wb") as f:
        f.write(b"\xef\xbb\xbf")
        f.write(content.encode("utf-8"))

    counts = {}
    for (t, _) in items:
        counts[t] = counts.get(t, 0) + 1
    detail = "，".join("%s %d" % (k, counts[k]) for k in sorted(counts))
    print("  已生成 %s" % filters_path)
    print("    共 %d 个条目（%s）" % (len(items), detail))
    return (True, len(items))


def main():
    # 脚本里有中文提示，而 Windows 控制台默认不是 UTF-8，不显式指定会输出乱码。
    if hasattr(sys.stdout, "buffer"):
        sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8",
                                      errors="replace")

    check_only = "--check" in sys.argv
    all_same = True
    for (proj_rel, root_filter) in TARGETS:
        same, _ = process(proj_rel, root_filter, check_only)
        all_same = all_same and same

    if check_only and not all_same:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
