#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
按 .vcxproj 重新生成服务端 chatserver / fileserver / imgserver 三个工程的 .vcxproj.filters
balloonwj@qq.com   2026-10-12

用途
----
`.filters` 只决定 VS「解决方案资源管理器」里的目录树长什么样，不参与编译。服务端的三个工程原来按源文件所在目录
分组，fileserver 里出现 chatserversrc 组（其实只有共用的 Protocol.h）、imgserver 里出现 chatserversrc 与
fileserversrc 组（与 fileserver 共用的文件传输代码），组名说明不了文件在本工程里的作用；chatserver 的 app
一组就有 165 个文件。2026-10-12 与用户确认改成下面的分组，并由本脚本生成，不再手工维护。

分组规则（只有一层，组都直接放在工程节点下）
--------------------------------------------
1. 服务自身的源码（chatserversrc / fileserversrc / imgserversrc）按功能模块分组，归属写在下面的 GROUPS 表里，
   按「目录 + 文件名主干」查找；同名的 .h 与 .cpp 进同一组。
2. 编进本工程、但属于别的服务的源码单独一组，组名写明与谁共用：
   「Protocol (chatserversrc)」是三个服务共用的协议定义 Protocol.h；
   「FileTransfer (shared with fileserver)」是 imgserver 复用的 fileserver 文件传输代码。
3. 公共库与第三方库按所在目录分组，组名就是目录名（DIR_GROUPS）。

除了生成 .filters，本脚本还会把 GROUPS 表里列出的文件名主干对应的 .h 补进 .vcxproj（ClInclude，只影响 VS 里能否
看到，不影响编译）：原来有不少头文件没有加进工程，在 VS 里看不到。同名的 .cpp 没在工程里时只提示、不补，
因为是否编译它由人决定（例如 imgserver 不编译 fileserversrc/main.cpp）。

用法
----
    cd flamingoserver/tools
    python gen_server_filters.py            # 补齐头文件并重新生成三个工程的 .filters
    python gen_server_filters.py --check    # 只检查、不写文件：有未归组的文件、缺头文件或 .filters 不一致时退出码为 1

新增源文件：先把它加进 .vcxproj（同时别忘了 CMakeLists.txt），再在 GROUPS 表里给它的文件名主干选一个组，
然后重跑本脚本。没在 GROUPS 表里的服务源码会被报出来，脚本以退出码 1 结束、不写任何文件。

filter 的 UniqueIdentifier（GUID）按「工程名 + 组名」做确定性哈希生成，重复生成不会产生无谓的 diff。
写回文件时保留原文件的 BOM 与换行符。
"""

import hashlib
import io
import os
import re
import sys
import xml.etree.ElementTree as ET

MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"

# 会出现在 .filters 里的条目类型
ITEM_TYPES = ("ClCompile", "ClInclude", "ResourceCompile", "Image", "Text", "None")

# 服务源码所在的目录
SERVICE_DIRS = ("chatserversrc", "fileserversrc", "imgserversrc")

# 公共库与第三方库：目录名 → 组名（组名就是目录名）
DIR_GROUPS = {
    "base": "base",
    "net": "net",
    "utils": "utils",
    "dbapi": "dbapi",
    "mysqlapi": "mysqlapi",
    "mysqlmgr": "mysqlmgr",
    "sqliteapi": "sqliteapi",
    "jsoncpp1.9.0": "jsoncpp1.9.0",
    "sqlite3": "sqlite3",
    "zlib1.3.2": "zlib1.3.2",
}

# 三个服务共用的协议定义
PROTOCOL_GROUP = ("Protocol (chatserversrc)", "chatserversrc", ["Protocol"])

# fileserver 的文件传输代码（imgserver 复用，见 CMakeLists.txt）：fileserver 里按模块分组，imgserver 里合成一组
FILE_TRANSFER_STEMS = ["FileServer", "FileSession", "TcpSession", "FileManager", "FileMsg", "UploadPartPolicy",
                       "FileSessionRemoval"]
FOLDER_RELAY_STEMS = ["FolderRelayEndpoint", "FolderRelayHub"]
UPDATE_STEMS = ["UpdatePackageIndex"]
CLOUD_RECORD_SCENE_STEMS = ["CloudRecordScene"]

# 各工程的服务源码分组：(组名, 目录, [文件名主干])，组的先后只影响本表的阅读顺序，生成时按组名排序
GROUPS = {
    "chatserver": [
        ("Core", "chatserversrc", ["main", "ChatServer", "ChatSession", "ChatSessionBot", "ChatSessionMemo",
                                   "ChatSessionSecret", "ChatSessionThread", "TcpSession", "BussinessLogic",
                                   "Protocol", "MonitorServer", "MonitorSession",
                                   "FeatureSwitchManager"]),
        ("Account", "chatserversrc", ["LoginProtocol", "RegisterProtocol", "RegisterService", "AccountName",
                                      "SmsCodeManager", "UserManager", "ProfileProtocol"]),
        ("Contacts", "chatserversrc", ["ContactsProtocol", "ContactRemarkManager", "ContactTagManager",
                                       "ContactTagProtocol", "FavoriteManager", "FavoriteProtocol", "FriendProtocol"]),
        ("Groups", "chatserversrc", ["GroupManager", "GroupProtocol", "GroupPermission", "GroupAutoName",
                                     "GroupFollowManager", "GroupInviteManager", "GroupInviteProtocol",
                                     "GroupLinkManager", "GroupLinkProtocol", "GroupQaManager", "GroupQaProtocol"]),
        ("Announcement", "chatserversrc", ["AnnouncementManager", "AnnouncementProtocol"]),
        ("Messages", "chatserversrc", ["MessageManager", "MessageProtocol", "MessageDeleteRange", "MessagePinManager",
                                       "MessageSearchText", "GlobalSearchProtocol", "CardManager", "CardProtocol",
                                       "MemoManager", "MemoProtocol"]),
        ("Files", "chatserversrc", ["FileProtocol", "FileStoreManager", "GroupFileManager", "GroupFileProtocol",
                                    "FolderTransferManager", "FolderTransferProtocol"]),
        ("FileShare", "chatserversrc", ["FileShareManager", "FileShareProtocol", "FileSharePolicy",
                                        "FileShareSessionManager", "FileShareAdminProtocol", "FileShareAdminService"]),
        ("Call", "chatserversrc", ["CallManager", "CallProtocol", "CallCardWriter", "GroupCallManager",
                                   "GroupCallProtocol", "GroupCallService", "GroupCallServerEnv"]),
        ("RemoteDesktop", "chatserversrc", ["RemoteDesktopManager", "RemoteDesktopProtocol"]),
        ("ScreenShare", "chatserversrc", ["ScreenShareManager", "ScreenShareProtocol", "ScreenShareRules",
                                          "ScreenShareService", "ScreenShareLinkTable", "CloudRecordLink",
                                          "CloudRecordProtocol", "CloudRecordRules", "CloudRecordServerEnv",
                                          "CloudRecordService", "CloudRecordStore"]),
        ("P2P", "chatserversrc", ["P2PProtocol", "P2PServerSetup", "P2PSessionManager", "P2PSignalService"]),
        ("ThreadGroup", "chatserversrc", ["ThreadManager", "ThreadProtocol", "ThreadWatchRegistry"]),
        ("SecretChat", "chatserversrc", ["SecretChatManager"]),
        ("Bot", "chatserversrc", ["BotDefs", "BotEngine", "BotManager", "BotProtocol", "BotRules", "BotServerEnv"]),
        ("Sensitive", "chatserversrc", ["SensitiveWordDefs", "SensitiveWordManager", "SensitiveWordMatcher",
                                        "SensitiveAdminProtocol"]),
        ("Enterprise", "chatserversrc", ["EnterpriseManager", "DepartmentManager", "DepartmentMsg", "DepartmentPath",
                                         "EmployeeManager", "ServiceAccountManager", "AdminManager", "AdminApiCodes",
                                         "QaAdminProtocol"]),
        ("Http", "chatserversrc", ["HttpServer", "HttpSession", "HttpMsg", "HttpRequestParse"]),
        ("AdminLink", "chatserversrc", ["AdminLinkProtocol", "AdminLinkServer", "AdminLinkSession"]),
        ("Update", "chatserversrc", ["ClientVersionManager", "UpdatePackageScanner", "UpdateProtocol"]),
    ],
    "fileserver": [
        ("Entry", "fileserversrc", ["main"]),
        ("Transfer", "fileserversrc", FILE_TRANSFER_STEMS),
        ("FolderRelay", "fileserversrc", FOLDER_RELAY_STEMS),
        ("Update", "fileserversrc", UPDATE_STEMS),
        ("CloudRecord", "fileserversrc", CLOUD_RECORD_SCENE_STEMS),
        PROTOCOL_GROUP,
    ],
    "imgserver": [
        ("Entry", "imgserversrc", ["main"]),
        ("Relay", "imgserversrc", ["RelayServer", "RelaySession", "RelayHub", "RelayRoomHub", "RelayHelloPolicy",
                                   "RoomRelayFrame", "RoomForwardPolicy", "ScreenShareRoomRelay"]),
        ("CloudRecord", "imgserversrc", ["CloudRecorder", "CloudRecordFile", "CloudRecordControl"]),
        ("P2PProbe", "imgserversrc", ["P2PAddressProbe", "P2PProbeCore"]),
        ("FileTransfer (shared with fileserver)", "fileserversrc",
         FILE_TRANSFER_STEMS + FOLDER_RELAY_STEMS + UPDATE_STEMS + CLOUD_RECORD_SCENE_STEMS),
        PROTOCOL_GROUP,
    ],
}

# 各工程自己的服务目录：磁盘上有、GROUPS 表里却没有的文件只在这个目录里提示（别的服务目录只编了其中几个文件）
OWN_DIR = {"chatserver": "chatserversrc", "fileserver": "fileserversrc", "imgserver": "imgserversrc"}


def script_dir():
    return os.path.dirname(os.path.abspath(__file__))


def server_root():
    return os.path.normpath(os.path.join(script_dir(), ".."))


def project_path(name):
    return os.path.join(server_root(), "windowsproj", name + ".vcxproj")


def stable_guid(name):
    """按名字生成确定性 GUID：同名每次结果一致，避免无谓的 diff。"""
    digest = hashlib.md5(name.encode("utf-8")).hexdigest()
    return "{%s-%s-%s-%s-%s}" % (digest[0:8], digest[8:12], digest[12:16], digest[16:20], digest[20:32])


def read_text(path):
    """读文本文件，返回 (内容（换行统一为 LF）, 是否带 BOM, 原换行符)。"""
    with open(path, "rb") as f:
        raw = f.read()
    bom = raw.startswith(b"\xef\xbb\xbf")
    newline = "\r\n" if b"\r\n" in raw else "\n"
    text = raw.decode("utf-8-sig").replace("\r\n", "\n")
    return text, bom, newline


def write_text(path, text, bom, newline):
    """按给定的 BOM 与换行符写回文本文件。"""
    data = text.replace("\n", newline).encode("utf-8")
    with open(path, "wb") as f:
        if bom:
            f.write(b"\xef\xbb\xbf")
        f.write(data)


def read_project_items(path):
    """读 vcxproj，返回 [(条目类型, Include 路径)]，保持原文件里的先后顺序。"""
    items = []
    tree = ET.parse(path)
    for item in tree.getroot().iter():
        tag = item.tag.replace("{%s}" % MSBUILD_NS, "")
        if tag not in ITEM_TYPES:
            continue
        include = item.get("Include")
        if include:
            items.append((tag, include))
    return items


def split_include(include):
    """把 ..\\dir\\Name.ext 拆成 (dir, Name, .ext)；不在子目录里的文件 dir 为空串。"""
    path = include.replace("/", "\\")
    while path.startswith("..\\"):
        path = path[3:]
    parts = path.split("\\")
    directory = parts[0] if len(parts) > 1 else ""
    stem, ext = os.path.splitext(parts[-1])
    return directory, stem, ext.lower()


def stem_table(project):
    """(目录, 文件名主干) → 组名。"""
    table = {}
    for (group, directory, stems) in GROUPS[project]:
        for stem in stems:
            table[(directory, stem)] = group
    return table


def decide_filter(project, include, table):
    """
    决定某个条目进哪个组。返回 (组名, 错误说明)：组名为 None 且错误说明为空表示不分组（挂在工程节点下）；
    错误说明非空表示这个服务源码没在 GROUPS 表里。
    """
    directory, stem, _ = split_include(include)
    if directory in SERVICE_DIRS:
        group = table.get((directory, stem))
        if group is None:
            return None, "%s 没在 GROUPS[%r] 里，请给它选一个组" % (include, project)
        return group, ""
    if directory in DIR_GROUPS:
        return DIR_GROUPS[directory], ""
    if directory == "":
        return None, ""
    return None, "%s 所在的目录 %s 没有对应的组，请加进 DIR_GROUPS" % (include, directory)


def missing_headers(project, items):
    """
    GROUPS 表里列出的文件名主干有 .h、却没加进工程的，返回要补的 Include 路径列表；同时返回只提示的说明：
    同名 .cpp 没在工程里、本服务目录里有文件没在 GROUPS 表里。
    """
    root = server_root()
    present = set(include.replace("/", "\\").lower() for (_, include) in items)
    add = []
    notes = []
    for (group, directory, stems) in GROUPS[project]:
        for stem in stems:
            header = "..\\%s\\%s.h" % (directory, stem)
            source = "..\\%s\\%s.cpp" % (directory, stem)
            if os.path.exists(os.path.join(root, directory, stem + ".h")) and header.lower() not in present:
                add.append(header)
            if os.path.exists(os.path.join(root, directory, stem + ".cpp")) and source.lower() not in present:
                notes.append("%s 在磁盘上、没在工程里（只提示，不补）" % source)
    own = OWN_DIR[project]
    table = stem_table(project)
    for name in sorted(os.listdir(os.path.join(root, own))):
        stem, ext = os.path.splitext(name)
        if ext.lower() in (".h", ".cpp") and (own, stem) not in table:
            notes.append("..\\%s\\%s 在磁盘上、没在 GROUPS 表里（只提示）" % (own, name))
    return add, notes


def add_headers_to_project(path, headers):
    """把头文件以 ClInclude 补进 vcxproj：放在同目录最后一个 ClInclude 之后，没有同目录的就放在最后一个 ClInclude 之后。"""
    text, bom, newline = read_text(path)
    lines = text.split("\n")
    for header in headers:
        directory, _, _ = split_include(header)
        last_any = -1
        last_same = -1
        indent = "    "
        for i, line in enumerate(lines):
            m = re.match(r'^(\s*)<ClInclude Include="([^"]+)"', line)
            if not m:
                continue
            # 带子元素的条目（<ClInclude ...>...</ClInclude>）要插在它的结束标签之后
            end = i
            if not line.rstrip().endswith("/>"):
                while end < len(lines) and "</ClInclude>" not in lines[end]:
                    end += 1
            last_any = end
            indent = m.group(1)
            if split_include(m.group(2))[0] == directory:
                last_same = end
        if last_any < 0:
            raise RuntimeError("%s 里没有 ClInclude 条目，无法确定插入位置" % path)
        pos = (last_same if last_same >= 0 else last_any) + 1
        lines.insert(pos, '%s<ClInclude Include="%s" />' % (indent, header))
    write_text(path, "\n".join(lines), bom, newline)


def build_filters_xml(project, items, table):
    """生成 .filters 的内容（换行为 LF）。返回 (内容, 错误说明列表)。"""
    errors = []
    assignments = []
    for (item_type, include) in items:
        group, error = decide_filter(project, include, table)
        if error:
            errors.append(error)
        assignments.append((item_type, include, group))

    lines = ['<?xml version="1.0" encoding="utf-8"?>',
             '<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">']
    names = sorted(set(a[2] for a in assignments if a[2]), key=lambda s: s.lower())
    if names:
        lines.append("  <ItemGroup>")
        for name in names:
            lines.append('    <Filter Include="%s">' % name)
            lines.append("      <UniqueIdentifier>%s</UniqueIdentifier>" % stable_guid(project + "|" + name))
            lines.append("    </Filter>")
        lines.append("  </ItemGroup>")
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
                lines.append('    <%s Include="%s" />' % (item_type, include))
        lines.append("  </ItemGroup>")
    lines.append("</Project>")
    return "\n".join(lines) + "\n", errors


def process(project, check_only):
    """补齐头文件并生成（或检查）一个工程的 .filters。返回是否一致 / 成功。"""
    proj = project_path(project)
    filters = proj + ".filters"
    items = read_project_items(proj)
    add, notes = missing_headers(project, items)
    for note in notes:
        print("  [%s] 提示：%s" % (project, note))

    table = stem_table(project)
    planned = items + [("ClInclude", h) for h in add]
    content, errors = build_filters_xml(project, planned, table)
    if errors:
        for error in errors:
            print("  [%s] 错误：%s" % (project, error))
        return False

    old_text, bom, newline = read_text(filters) if os.path.exists(filters) else ("", True, "\r\n")
    same = (old_text == content) and not add
    if check_only:
        if add:
            print("  [%s] 缺少头文件：%s" % (project, "、".join(add)))
        print("  [%s] %s" % (project, "一致" if same else "不一致，请重跑本脚本"))
        return same

    if add:
        add_headers_to_project(proj, add)
        print("  [%s] 已补进工程的头文件 %d 个：%s" % (project, len(add), "、".join(add)))
        #头文件插在工程文件里同目录条目的后面，不在末尾：重新读一次，.filters 的条目顺序才与工程文件一致，
        #下一次 --check 不会报不一致
        planned = read_project_items(proj)
        content, errors = build_filters_xml(project, planned, table)
    write_text(filters, content, bom, newline)
    print("  [%s] 已生成 %s，共 %d 个条目、%d 个组" % (project, os.path.basename(filters), len(planned),
                                                content.count("<Filter Include=")))
    return True


def main():
    # 脚本里有中文提示，而 Windows 控制台默认不是 UTF-8，不显式指定会输出乱码
    if hasattr(sys.stdout, "buffer"):
        sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    check_only = "--check" in sys.argv
    ok = True
    for project in ("chatserver", "fileserver", "imgserver"):
        ok = process(project, check_only) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
