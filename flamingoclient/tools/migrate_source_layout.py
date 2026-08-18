#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
把 flamingoclient/Source 下的文件按模块重排到子目录，并同步改写所有引用
balloonwj@qq.com   2026-08-18

用途
----
Source 目录长期是"根目录堆着 77 个文件 + 三十来个平铺子目录"的形态，解决方案
资源管理器里的分组只能靠 .filters 手工维护，越维护越乱。本脚本一次性把磁盘
目录调整成两层结构，让分组树可以完全由目录推导出来：

    Source/
        stdafx.h / stdafx.cpp / resource.h / flamingo.rc / ClientVersion.h
        App/            程序骨架：入口、代理窗、隐藏主窗口、崩溃转储、托盘图标
        Session/        账号与会话数据
        Common/Util     通用工具
        Common/Log      日志
        Common/Graphics 绘图与图片
        Threading/      线程基础设施与后台任务
        net/            网络层（原地不动）
        res/            图标资源（原地不动）
        UI/             17 个界面模块目录
        Feature/        音视频通话、远程桌面、文件夹传输、自动更新
        third-party/    随工程携带的 jsoncpp / zlib / WTL
        Tests/          单元测试（原地不动）

脚本做四件事：搬文件、改写源码里的 #include、改写两个 .vcxproj 里的条目路径与
包含目录、清理搬空的目录。

用法
----
    cd flamingoclient/tools
    python migrate_source_layout.py --dry-run   # 只输出改动清单，不动任何文件
    python migrate_source_layout.py --apply     # 真正执行

--dry-run 会把完整的文件去向表和 #include 改写表写到 tools/migrate_report.txt，
供人工核对；终端上只打印统计数字。

注意
----
1. `#include "stdafx.h"` 一律不改写。工程默认开着预编译头（/Yu"stdafx.h"），
   编译器是按 #include 的字面文本去匹配的，改成带路径的写法会报 C1010。
2. 源文件按字节读写，不做编码转换 —— 本仓库的源文件 BOM 与换行符是混着的
   （有 UTF-8 带 BOM 的，也有不带的；有 CRLF 也有 LF），逐字节替换才不会把
   这些属性改坏。
3. 搬文件优先用 git mv，让 git 记录为重命名；未纳入版本管理的文件退回普通移动。
"""

import os
import re
import subprocess
import sys

# ---------------------------------------------------------------------------
# 一、迁移映射表
# ---------------------------------------------------------------------------

# 留在 Source 根目录的文件（小写比较）。
#   stdafx.h / stdafx.cpp   预编译头，VS 惯例放工程根
#   resource.h / flamingo.rc 资源脚本及其头文件
#   ClientVersion.h         被 flamingo.rc 引用，rc.exe 对包含路径敏感，不挪
KEEP_AT_ROOT = set([
    "stdafx.h", "stdafx.cpp", "resource.h", "flamingo.rc", "clientversion.h",
])

# 根目录文件的去向：文件名（小写）-> 目标目录（相对 Source，正斜杠）。
ROOT_DEST = {}


def _assign(dest, names):
    """把一组根目录文件登记到同一个目标目录。"""
    for name in names:
        ROOT_DEST[name.lower()] = dest


# 程序骨架：进程入口、代理窗、隐藏主窗口、崩溃转储、窗口层叠、托盘图标、
# 以及全局的消息号 / 文案常量定义。
_assign("App", [
    "FlamingoClient.cpp", "FlamingoClient.h",
    "Startup.cpp", "Startup.h",
    "MainDlg.cpp", "MainDlg.h",
    "Crash.cpp", "Crash.h",
    "CascadeWinManager.cpp", "CascadeWinManager.h",
    "TrayIcon.cpp", "TrayIcon.h",
    "CustomMsgDef.h", "UIText.h",
])

# 账号与会话数据：当前登录用户、本地账号列表、用户配置、群角色规则。
_assign("Session", [
    "UserMgr.cpp", "UserMgr.h",
    "UserConfig.cpp", "UserConfig.h",
    "UserSessionData.cpp", "UserSessionData.h",
    "LoginAccountList.cpp", "LoginAccountList.h",
    "GroupRoleRules.cpp", "GroupRoleRules.h",
])

# 通用工具：缓冲区、编码转换、文件与路径、配置文件、摘要算法、压缩、XML、剪贴板。
_assign("Common/Util", [
    "Buffer.cpp", "Buffer.h",
    "MiniBuffer.h", "MinuBuffer.cpp",
    "EncodeUtil.cpp", "EncodeUtil.h",
    "File2.cpp", "File2.h",
    "IniFile.cpp", "IniFile.h",
    "Path.cpp", "Path.h",
    "Utils.cpp", "Utils.h",
    "MD5.cpp", "MD5.h", "MD5Core.h", "MD5Sum.cpp",
    "ZlibUtil.cpp", "ZlibUtil.h",
    "XmlDocument.cpp", "XmlDocument.h",
    "ClipboardImage.h", "ClipboardText.h",
])

# 日志：日志器本体、日志文件清理策略、聊天消息落盘记录。
_assign("Common/Log", [
    "IULog.cpp", "IULog.h",
    "LogCleanupPolicy.cpp", "LogCleanupPolicy.h",
    "MessageLogger.cpp", "MessageLogger.h",
])

# 绘图与图片：字体缓存工厂、图片加载与绘制、表情图片清单。
_assign("Common/Graphics", [
    "GDIFactory.cpp", "GDIFactory.h",
    "ImageEx.cpp", "ImageEx.h",
    "FaceList.cpp", "FaceList.h",
])

# 线程基础设施与后台任务。
_assign("Threading", [
    "Thread.cpp", "Thread.h",
    "ThreadPool.cpp", "ThreadPool.h",
    "SendMsgThread.cpp", "SendMsgThread.h",
    "FileTaskThread.cpp", "FileTaskThread.h",
    "ImageTaskThread.cpp", "ImageTaskThread.h",
    "CheckNetworkStatusTask.cpp", "CheckNetworkStatusTask.h",
])

# 顺带纠正的文件名拼写错误：该文件实现的是 MiniBuffer.h 里的 CMiniBuffer。
RENAME = {
    "minubuffer.cpp": "MiniBuffer.cpp",
}

# 顶层目录的去向：目录名 -> 新的相对路径。未列出的目录（net、res、Tests 等）原地不动。
DIR_DEST = {}

# 17 个界面模块目录收进 UI/。
for _name in ("BalloonAnnounce", "BalloonAuth", "BalloonChat", "BalloonChatWnd",
              "BalloonContact", "BalloonGroupManage", "BalloonHistory",
              "BalloonImageViewer", "BalloonMain", "BalloonNetSettings",
              "BalloonProfile", "BalloonRegister", "BalloonRemoteDesktop",
              "BalloonScreenshot", "BalloonSearch", "BalloonSettings",
              "BalloonUpdate"):
    DIR_DEST[_name] = "UI/" + _name

# 四个功能引擎收进 Feature/。BalloonUpdate 是升级窗口、Update 是升级逻辑，
# 两者分属 UI 与 Feature，保持现有的窗口 / 逻辑分工不变。
for _name in ("Call", "RemoteDesktop", "FolderTransfer", "Update"):
    DIR_DEST[_name] = "Feature/" + _name

# 随工程携带的三方库收进 third-party/。
for _name in ("jsoncpp-1.9.0", "zlib1.2.11", "wtl10.1"):
    DIR_DEST[_name] = "third-party/" + _name

# 扫描源文件时要跳过的目录（构建产物与测试数据）。
SKIP_DIRS = set([
    "debug", "release", "x64", "ipch", ".vs",
    "unit_debug_win32", "unit_debug_x64",
    "unit_release_win32", "unit_release_x64",
    "flamingo_updatepkg_test", "flamingo_updatescan_test",
])

# 参与 #include 改写的源文件后缀。
SOURCE_EXTS = (".h", ".hpp", ".hxx", ".inl", ".c", ".cpp", ".cc", ".cxx")

# 需要同步改写条目路径的工程文件：路径相对仓库里的 Source 目录。
PROJECTS = ("Flamingo.vcxproj", "Tests/FlamingoUnit.vcxproj")


def source_root():
    """返回 Source 目录的绝对路径。本脚本放在 flamingoclient/tools 下。"""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "Source"))


def new_relpath(old_rel):
    """给出某个文件迁移后的相对路径（相对 Source，正斜杠）。不搬的原样返回。"""
    parts = old_rel.split("/")
    if len(parts) == 1:
        base = parts[0]
        if base.lower() in KEEP_AT_ROOT:
            return old_rel
        dest = ROOT_DEST.get(base.lower())
        if dest is None:
            return old_rel
        return dest + "/" + RENAME.get(base.lower(), base)

    top = parts[0]
    if top in DIR_DEST:
        return DIR_DEST[top] + "/" + "/".join(parts[1:])
    return old_rel


def collect_files(root):
    """列出 Source 下所有文件（相对路径、正斜杠），跳过构建产物目录。"""
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d.lower() not in SKIP_DIRS]
        for name in filenames:
            full = os.path.join(dirpath, name)
            out.append(os.path.relpath(full, root).replace("\\", "/"))
    return out


def to_include_form(path, like):
    """
    把正斜杠的相对路径转成与原写法一致的分隔符风格。

      path 计算出来的新路径，内部一律用正斜杠
      like 原来的写法，用来判断该文件习惯用哪种分隔符

    本仓库里两种写法都有（`..\\IULog.h` 与 `../jsoncpp-1.9.0/json.h`），保持各自
    原样可以避免产生一批与本次调整无关的改动。
    """
    if "/" in like and "\\" not in like:
        return path
    return path.replace("/", "\\")


# 匹配带引号的 #include。分三段捕获，替换时只动中间的路径，前后原样保留。
INCLUDE_RE = re.compile(rb'(#[ \t]*include[ \t]+")([^"\r\n]+)(")')


def rewrite_includes_in(data, src_old_rel, src_new_rel, file_index, base_index):
    """
    改写一个源文件里的 #include 路径。

      data          源文件字节内容
      src_old_rel   该文件迁移前的相对路径
      src_new_rel   该文件迁移后的相对路径
      file_index    {小写相对路径: 真实相对路径}，用于判断引用是否指向本工程内的文件
      base_index    {小写文件名: [相对路径]}，用于兜底修正写错的相对路径

    返回 (新字节内容, [(旧写法, 新写法)])。
    """
    old_dir = os.path.dirname(src_old_rel)
    new_dir = os.path.dirname(src_new_rel)
    changes = []

    def repl(match):
        raw = match.group(2).decode("ascii", "replace")
        # 预编译头那一行不能动：编译器按字面文本匹配 /Yu 的值。
        if os.path.basename(raw.replace("\\", "/")).lower() == "stdafx.h":
            return match.group(0)

        target_old = os.path.normpath(
            os.path.join(old_dir, raw.replace("\\", "/"))).replace("\\", "/")
        real = file_index.get(target_old.lower())
        if real is None:
            # 相对路径解析不到。若工程内恰好只有一个同名文件，说明这行引用的
            # 相对路径本来就写错了（现在能编过是因为某个包含目录恰好兜住了），
            # 按真实位置重算一份正确的写法。多于一个同名文件时不猜，保持原样。
            candidates = base_index.get(
                os.path.basename(raw.replace("\\", "/")).lower(), [])
            if len(candidates) != 1:
                # 指向三方库或系统头文件，不归本脚本管。
                return match.group(0)
            real = candidates[0]

        target_new = new_relpath(real)
        want = os.path.relpath(
            target_new, new_dir if new_dir else ".").replace("\\", "/")
        want = to_include_form(want, raw)
        if want == raw:
            return match.group(0)

        changes.append((raw, want))
        return match.group(1) + want.encode("ascii") + match.group(3)

    return INCLUDE_RE.sub(repl, data), changes


# 匹配工程文件里的条目路径。
PROJ_INCLUDE_RE = re.compile(rb'(Include=")([^"\r\n]+)(")')


def rewrite_project(data, proj_rel, file_index):
    """
    改写一个 .vcxproj 里的条目路径。

      data       工程文件字节内容
      proj_rel   工程文件相对 Source 的路径，用于确定条目路径的基准目录
      file_index {小写相对路径: 真实相对路径}

    返回 (新字节内容, [(旧写法, 新写法)])。
    """
    proj_dir = os.path.dirname(proj_rel)
    changes = []

    def repl(match):
        raw = match.group(2).decode("ascii", "replace")
        target_old = os.path.normpath(
            os.path.join(proj_dir, raw.replace("\\", "/"))).replace("\\", "/")
        real = file_index.get(target_old.lower())
        if real is None:
            # ProjectConfiguration（"Debug|Win32"）、指向别的工程的引用等，不动。
            return match.group(0)

        target_new = new_relpath(real)
        want = os.path.relpath(
            target_new, proj_dir if proj_dir else ".").replace("\\", "/")
        want = to_include_form(want, raw)
        if want == raw:
            return match.group(0)

        changes.append((raw, want))
        return match.group(1) + want.encode("ascii") + match.group(3)

    return PROJ_INCLUDE_RE.sub(repl, data), changes


def rewrite_include_path_property(data):
    """
    改写 Flamingo.vcxproj 里指向 WTL 的 <IncludePath>。

    工程的四个配置各有一行 <IncludePath>wtl10.1\\;$(IncludePath)</IncludePath>，
    全工程的 <atlapp.h> 一类尖括号引用都靠它。目录挪进 third-party 后必须同步改，
    否则 WTL 头文件一个都找不到。

    返回 (新字节内容, 替换次数)。
    """
    old = b"<IncludePath>wtl10.1\\;"
    new = b"<IncludePath>third-party\\wtl10.1\\;"
    count = data.count(old)
    if count:
        data = data.replace(old, new)
    return data, count


def git_move(root, old_rel, new_rel):
    """搬一个文件。优先 git mv 以便 git 记录为重命名，失败则退回普通移动。"""
    old_abs = os.path.join(root, old_rel.replace("/", os.sep))
    new_abs = os.path.join(root, new_rel.replace("/", os.sep))
    new_dir = os.path.dirname(new_abs)
    if not os.path.isdir(new_dir):
        os.makedirs(new_dir)

    result = subprocess.call(
        ["git", "mv", "-f", old_abs, new_abs],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    if result != 0:
        os.rename(old_abs, new_abs)


def prune_empty_dirs(root):
    """删掉搬空的目录，返回被删目录的相对路径列表。"""
    removed = []
    for dirpath, dirnames, filenames in os.walk(root, topdown=False):
        if os.path.abspath(dirpath) == os.path.abspath(root):
            continue
        if dirnames or filenames:
            continue
        rel = os.path.relpath(dirpath, root).replace("\\", "/")
        if rel.split("/")[0].lower() in SKIP_DIRS:
            continue
        os.rmdir(dirpath)
        removed.append(rel)
    return removed


def main():
    apply_mode = "--apply" in sys.argv
    if not apply_mode and "--dry-run" not in sys.argv:
        sys.stderr.write("用法: migrate_source_layout.py --dry-run | --apply\n")
        return 2

    root = source_root()
    if not os.path.isdir(root):
        sys.stderr.write("找不到 Source 目录: %s\n" % root)
        return 2

    all_files = collect_files(root)
    file_index = {}
    base_index = {}
    for rel in all_files:
        file_index[rel.lower()] = rel
        base_index.setdefault(os.path.basename(rel).lower(), []).append(rel)

    moves = []
    for rel in all_files:
        dst = new_relpath(rel)
        if dst != rel:
            moves.append((rel, dst))
    moves.sort()

    # 1) 改写源码里的 #include。
    include_edits = []          # [(文件, 旧写法, 新写法)]
    touched_sources = {}        # {相对路径: 新字节内容}
    for rel in all_files:
        if not rel.lower().endswith(SOURCE_EXTS):
            continue
        abspath = os.path.join(root, rel.replace("/", os.sep))
        with open(abspath, "rb") as f:
            data = f.read()
        newdata, changes = rewrite_includes_in(
            data, rel, new_relpath(rel), file_index, base_index)
        if changes:
            touched_sources[rel] = newdata
            for old, new in changes:
                include_edits.append((rel, old, new))

    # 2) 改写工程文件里的条目路径与包含目录。
    project_edits = []          # [(工程, 旧写法, 新写法)]
    touched_projects = {}
    include_path_fix = 0
    for proj_rel in PROJECTS:
        abspath = os.path.join(root, proj_rel.replace("/", os.sep))
        if not os.path.exists(abspath):
            continue
        with open(abspath, "rb") as f:
            data = f.read()
        newdata, changes = rewrite_project(data, proj_rel, file_index)
        if proj_rel == "Flamingo.vcxproj":
            newdata, include_path_fix = rewrite_include_path_property(newdata)
        if newdata != data:
            touched_projects[proj_rel] = newdata
            for old, new in changes:
                project_edits.append((proj_rel, old, new))

    # 3) 输出清单。
    report_path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "migrate_report.txt")
    with open(report_path, "wb") as f:
        def w(line):
            f.write((line + "\r\n").encode("utf-8"))

        w("=" * 78)
        w("文件搬迁清单（共 %d 个）" % len(moves))
        w("=" * 78)
        for old, new in moves:
            w("  %-56s -> %s" % (old, new))
        w("")
        w("=" * 78)
        w("#include 改写清单（共 %d 行，分布在 %d 个文件）"
          % (len(include_edits), len(touched_sources)))
        w("=" * 78)
        for rel, old, new in include_edits:
            w("  %-52s %-38s -> %s" % (rel, old, new))
        w("")
        w("=" * 78)
        w("工程文件条目路径改写（共 %d 处）" % len(project_edits))
        w("=" * 78)
        for proj, old, new in project_edits:
            w("  %-26s %-46s -> %s" % (proj, old, new))
        w("")
        w("Flamingo.vcxproj 的 <IncludePath> 改写次数: %d" % include_path_fix)

    print("待搬迁文件: %d 个" % len(moves))
    print("待改写 #include: %d 行，分布在 %d 个文件"
          % (len(include_edits), len(touched_sources)))
    print("待改写工程条目路径: %d 处，涉及 %d 个工程文件"
          % (len(project_edits), len(touched_projects)))
    print("待改写 <IncludePath>: %d 处" % include_path_fix)
    print("完整清单已写入: %s" % report_path)

    if not apply_mode:
        print("（当前是 --dry-run，未改动任何文件）")
        return 0

    # 4) 真正执行：先落盘改写后的内容，再搬文件。
    for rel, data in touched_sources.items():
        with open(os.path.join(root, rel.replace("/", os.sep)), "wb") as f:
            f.write(data)
    for rel, data in touched_projects.items():
        with open(os.path.join(root, rel.replace("/", os.sep)), "wb") as f:
            f.write(data)
    for old, new in moves:
        git_move(root, old, new)
    removed = prune_empty_dirs(root)

    print("已执行。清空后删除的目录: %d 个" % len(removed))
    for rel in removed:
        print("   ", rel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
