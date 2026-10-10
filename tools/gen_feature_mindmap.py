#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 README 用的功能思维导图：中文版 docs/diagrams/flamingo-features.svg（README.md 用），
英文版 docs/diagrams/flamingo-features-en.svg（README_en.md 用），一次运行同时生成两份。
中心节点居中，左右各 6 个功能方向，每个方向下挂若干功能点；坐标全部由脚本计算，
增删功能只需改下面的 LEFT_BRANCHES / RIGHT_BRANCHES 两张表，每一项都同时写中文与英文。
只列已经有实现代码的功能，只有方案文档的（如屏幕墙、投票）不要加进来。

典型用法（在仓库根目录）：
    python tools/gen_feature_mindmap.py
    powershell -ExecutionPolicy Bypass -File tools/render_diagrams.ps1
balloonwj@qq.com   2026-10-10
"""

import os

# 标题区高度：标题与副标题占用的顶部空间，单位：像素（SVG 用户单位，下同）
TITLE_H = 130
# 底部留白
BOTTOM_PAD = 50

# 中心节点的宽高
CENTER_W = 250
CENTER_H = 112

# 分支节点（功能方向）的高度；宽度随语言不同，见 LANG_CONFIG
BRANCH_H = 46
# 分支节点与画布中线的水平距离（节点靠近中心的那条边到中线）
BRANCH_GAP_FROM_CENTER = 260
# 功能点与分支节点之间的水平距离
LEAF_GAP_FROM_BRANCH = 70

# 功能点胶囊的高度与上下间距（相邻两个功能点中心的距离）
LEAF_H = 28
LEAF_STEP = 34
# 功能点胶囊左右的文字内边距
LEAF_PAD_X = 14
# 相邻两个功能方向之间的额外竖直间距
BRANCH_SPACING = 24

# 功能点文字字号
LEAF_FONT = 15
# 15px 字号下各类字符的估算宽度（微软雅黑的拉丁字形取自 Segoe UI，两种字体按同一套经验值估算）
CJK_WIDTH = 15.0       # 汉字与全角标点
NARROW_WIDTH = 4.4     # 空格、间隔点与 i、l、t 一类窄字母和标点
WIDE_WIDTH = 12.0      # m、w、@、& 一类宽字符
UPPER_WIDTH = 9.4      # 其余大写字母
NORMAL_WIDTH = 7.6     # 其余半角字符（小写字母、数字等）
NARROW_CHARS = " ·.,:;!|'()[]/-ijlftrI1"
WIDE_CHARS = "mwMW@&%"

# 两种语言各自的版面参数：
#   canvas_w：画布宽度；branch_w：分支节点宽度；branch_font：分支节点字号；
#   font：字体列表；title / subtitle / center_sub：标题、副标题与中心节点下方的小字；
#   file：输出文件名（放在 docs/diagrams/ 下）
LANG_CONFIG = {
    "zh": {
        "canvas_w": 1640,
        "branch_w": 190,
        "branch_font": 18,
        "font": "'Microsoft YaHei', 'PingFang SC', 'Noto Sans CJK SC', 'Source Han Sans SC', sans-serif",
        "title": "Flamingo IM 功能全景",
        "subtitle": "企业版 · 十二个功能方向 · 只列已实现的功能",
        "center_sub": "企业即时通讯",
        "file": "flamingo-features.svg",
    },
    "en": {
        "canvas_w": 1860,
        "branch_w": 236,
        "branch_font": 17,
        "font": "'Segoe UI', 'Microsoft YaHei', 'Helvetica Neue', Arial, sans-serif",
        "title": "Flamingo IM Feature Overview",
        "subtitle": "Enterprise edition · 12 areas · implemented features only",
        "center_sub": "Enterprise instant messaging",
        "file": "flamingo-features-en.svg",
    },
}

# 右侧的功能方向：(中文名, 英文名, 主色, [(中文功能点, 英文功能点), ...])，自上而下排列
RIGHT_BRANCHES = [
    ("账号与登录", "Account & sign-in", "#EF4444", [
        ("注册 · 登录 · 手机号登录", "Sign up · sign in · mobile number sign-in"),
        ("自动登录 · 断线重连", "Auto sign-in · automatic reconnect"),
        ("个人资料与头像", "Profile & avatar"),
        ("在线状态 · 无操作自动离开", "Presence · auto-away when idle"),
        ("本地数据按账号隔离", "Local data kept per account"),
    ]),
    ("通讯录", "Contacts", "#F97316", [
        ("企业组织架构树", "Organization tree"),
        ("联系人资料卡", "Contact profile card"),
        ("备注名 · 联系人标签", "Remark names · contact tags"),
        ("特别关注 · 黑名单", "Starred contacts · blacklist"),
        ("全局搜索：联系人 · 消息 · 文件", "Global search: contacts · messages · files"),
    ]),
    ("单聊与消息", "Chat & messages", "#EAB308", [
        ("文字 · 表情 · 图片", "Text · emoji · images"),
        ("语音消息", "Voice messages"),
        ("代码片段 · 语法着色 · 批注", "Code snippets · highlighting · annotations"),
        ("Markdown 消息与编辑器", "Markdown messages & editor"),
        ("撤回 · 重新编辑", "Recall · re-edit"),
        ("转发 · 合并转发", "Forward · merged forward"),
    ]),
    ("群聊", "Group chat", "#22C55E", [
        ("建群 · 邀请 · 加群链接", "Create · invite · group links"),
        ("转让群主 · 解散 · 退群", "Transfer ownership · disband · leave"),
        ("群公告 · 入群必读", "Announcements · must-read notice"),
        ("@提醒 · @所有人", "@mentions · @all"),
        ("群文件 · 群备忘", "Group files · group memos"),
        ("群接龙 · 群问答", "Sign-up lists · group Q&A"),
        ("帖子群：发帖 · 评论 · 表情回应", "Thread groups: posts · comments · reactions"),
    ]),
    ("消息管理", "Message management", "#06B6D4", [
        ("聊天记录管理器 · 导出", "Chat history manager · export"),
        ("消息 Pin · 收藏", "Pinned messages · favorites"),
        ("五种提醒模式 · 消息盒子", "Five notification modes · message box"),
        ("会话标为未读", "Mark conversation as unread"),
        ("快捷回复", "Quick replies"),
        ("清理聊天记录", "Clear chat history"),
    ]),
    ("平台与部署", "Platform & deployment", "#64748B", [
        ("多语言：简繁英日韩", "UI in 5 languages (zh-CN/zh-TW/en/ja/ko)"),
        ("客户端自动更新", "Client auto-update"),
        ("崩溃转储", "Crash dumps"),
        ("MySQL / SQLite 双后端", "MySQL or SQLite back end"),
        ("Windows / Linux 部署", "Deploy on Windows or Linux"),
        ("通用 C++11 服务器框架", "Reusable C++11 server framework"),
    ]),
]

# 左侧的功能方向，格式同上
LEFT_BRANCHES = [
    ("音视频通话", "Voice & video calls", "#3B82F6", [
        ("一对一语音 / 视频通话", "One-to-one voice / video calls"),
        ("群音视频通话", "Group voice / video calls"),
        ("来电弹窗 · 通话卡片", "Incoming-call popup · call cards"),
        ("通话网络状况标识", "Call network quality indicator"),
    ]),
    ("屏幕与协作", "Screen & collaboration", "#6366F1", [
        ("远程桌面", "Remote desktop"),
        ("屏幕共享 · 多人共享", "Screen sharing · multiple sharers"),
        ("共享中标注 · 白板", "Annotations · whiteboard"),
        ("申请控制对方键鼠", "Request keyboard & mouse control"),
        ("屏幕录制 · 云端录像", "Screen recording · cloud recordings"),
        ("录制与共享实时字幕", "Live captions while recording / sharing"),
    ]),
    ("文件与传输", "Files & transfer", "#8B5CF6", [
        ("聊天文件 · 群文件 · 秒传", "Chat & group files · instant upload"),
        ("发送文件夹", "Send whole folders"),
        ("文件共享：磁盘 · 文件夹", "File sharing: disks · folders"),
        ("P2P 直传 · UDP 打洞", "P2P transfer · UDP hole punching"),
        ("打不通时经服务器中转", "Server relay as fallback"),
    ]),
    ("安全与隐私", "Security & privacy", "#EC4899", [
        ("协议加密（libsodium）", "Encrypted protocol (libsodium)"),
        ("密聊：端到端加密 · 防截屏", "Secret chat: end-to-end · capture-proof"),
        ("敏感词过滤", "Sensitive-word filtering"),
        ("锁屏", "Screen lock"),
        ("文件共享审计", "File sharing audit"),
    ]),
    ("效率工具", "Productivity", "#84CC16", [
        ("截图（Alt+Z）", "Screenshots (Alt+Z)"),
        ("图片文字识别 OCR", "Image text recognition (OCR)"),
        ("备忘与定时提醒", "Memos & timed reminders"),
        ("聊天字幕模式", "Chat caption mode"),
        ("机器人：企业 · 群 · 个人", "Bots: enterprise · group · personal"),
        ("防止电脑休眠", "Keep the PC awake"),
    ]),
    ("企业管理控制台", "Admin console", "#0F766E", [
        ("企业 · 部门 · 员工", "Enterprises · departments · employees"),
        ("公告投放 · 服务号", "Announcements · service accounts"),
        ("机器人 · 问答库 · 敏感词库", "Bots · Q&A base · sensitive words"),
        ("功能开关 · 客户端版本", "Feature switches · client versions"),
        ("在线用户 · 数据看板", "Online users · dashboard"),
        ("审计日志", "Audit log"),
    ]),
]


def text_width(text):
    """估算一段文字在 LEAF_FONT 字号下的显示宽度（像素）。"""
    width = 0.0
    for ch in text:
        if ord(ch) >= 0x2E80:
            width += CJK_WIDTH
        elif ch in NARROW_CHARS:
            width += NARROW_WIDTH
        elif ch in WIDE_CHARS:
            width += WIDE_WIDTH
        elif "A" <= ch <= "Z":
            width += UPPER_WIDTH
        else:
            width += NORMAL_WIDTH
    return width


def tint(hex_color, ratio):
    """把颜色按 ratio（0~1）与白色混合，得到同色系的浅色底。"""
    r = int(hex_color[1:3], 16)
    g = int(hex_color[3:5], 16)
    b = int(hex_color[5:7], 16)
    r = int(r + (255 - r) * ratio)
    g = int(g + (255 - g) * ratio)
    b = int(b + (255 - b) * ratio)
    return "#%02X%02X%02X" % (r, g, b)


def escape(text):
    """转义 XML 特殊字符。"""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def block_height(leaves):
    """一个功能方向占用的竖直高度：取功能点总高与分支节点高度中较大者。"""
    return max(len(leaves) * LEAF_STEP, BRANCH_H)


def side_height(branches):
    """一侧全部功能方向占用的竖直总高。"""
    total = 0
    for branch in branches:
        total += block_height(branch[3])
    return total + BRANCH_SPACING * (len(branches) - 1)


def draw_side(out, branches, lang, center_x, center_y, is_right, top):
    """
    画一侧的全部功能方向。
    out：输出的 SVG 片段列表，就地追加；branches：LEFT_BRANCHES 或 RIGHT_BRANCHES；
    lang："zh" 或 "en"，决定取哪种语言的文字与版面参数；
    center_x / center_y：中心节点的中心坐标；is_right：True 画右侧、False 画左侧；
    top：这一侧第一个功能方向的上沿 y 坐标。
    """
    config = LANG_CONFIG[lang]
    branch_w = config["branch_w"]
    text_index = 0 if lang == "zh" else 1
    sign = 1 if is_right else -1
    # 中心节点在这一侧的边缘
    center_edge_x = center_x + sign * CENTER_W / 2
    # 分支节点靠近中心的边与远离中心的边
    branch_near_x = center_x + sign * BRANCH_GAP_FROM_CENTER
    branch_far_x = branch_near_x + sign * branch_w
    leaf_x = branch_far_x + sign * LEAF_GAP_FROM_BRANCH

    lines = []
    nodes = []
    y = top
    for branch in branches:
        name = branch[text_index]
        color = branch[2]
        leaves = branch[3]
        height = block_height(leaves)
        branch_cy = y + height / 2

        # 中心 → 分支节点的粗曲线
        mid_x = (center_edge_x + branch_near_x) / 2
        lines.append(
            '<path d="M%.1f,%.1f C%.1f,%.1f %.1f,%.1f %.1f,%.1f" stroke="%s" stroke-width="5" '
            'fill="none" stroke-linecap="round"/>'
            % (center_edge_x, center_y, mid_x, center_y, mid_x, branch_cy, branch_near_x, branch_cy, color))

        # 分支节点
        rect_x = min(branch_near_x, branch_far_x)
        nodes.append(
            '<rect x="%.1f" y="%.1f" width="%d" height="%d" rx="23" fill="%s" filter="url(#sh)"/>'
            % (rect_x, branch_cy - BRANCH_H / 2, branch_w, BRANCH_H, color))
        nodes.append(
            '<text class="branch" x="%.1f" y="%.1f">%s</text>'
            % (rect_x + branch_w / 2, branch_cy + config["branch_font"] * 0.36, escape(name)))

        # 功能点：在本块内竖直居中排列
        first_cy = branch_cy - (len(leaves) - 1) * LEAF_STEP / 2
        light = tint(color, 0.88)
        border = tint(color, 0.45)
        for index, leaf_pair in enumerate(leaves):
            leaf = leaf_pair[text_index]
            leaf_cy = first_cy + index * LEAF_STEP
            width = text_width(leaf) + LEAF_PAD_X * 2
            leaf_mid_x = (branch_far_x + leaf_x) / 2
            lines.append(
                '<path d="M%.1f,%.1f C%.1f,%.1f %.1f,%.1f %.1f,%.1f" stroke="%s" stroke-width="2" '
                'fill="none" stroke-opacity="0.75"/>'
                % (branch_far_x, branch_cy, leaf_mid_x, branch_cy, leaf_mid_x, leaf_cy, leaf_x, leaf_cy, color))
            box_x = leaf_x if is_right else leaf_x - width
            nodes.append(
                '<rect x="%.1f" y="%.1f" width="%.1f" height="%d" rx="14" fill="%s" stroke="%s"/>'
                % (box_x, leaf_cy - LEAF_H / 2, width, LEAF_H, light, border))
            nodes.append(
                '<text class="leaf" x="%.1f" y="%.1f">%s</text>'
                % (box_x + width / 2, leaf_cy + 5.2, escape(leaf)))

        y += height + BRANCH_SPACING

    out.extend(lines)
    out.extend(nodes)


def build_svg(lang):
    """生成一种语言（"zh" 或 "en"）的整张导图 SVG 文本。"""
    config = LANG_CONFIG[lang]
    canvas_w = config["canvas_w"]
    content_h = max(side_height(LEFT_BRANCHES), side_height(RIGHT_BRANCHES))
    canvas_h = int(TITLE_H + content_h + BOTTOM_PAD)
    center_x = canvas_w / 2
    center_y = TITLE_H + content_h / 2

    out = []
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
               % (canvas_w, canvas_h, canvas_w, canvas_h))
    out.append('  <!-- Flamingo IM 功能思维导图（%s）。由 tools/gen_feature_mindmap.py 生成，请勿手工编辑。 -->'
               % ("中文版" if lang == "zh" else "英文版"))
    out.append('  <defs>')
    out.append('    <style>')
    out.append("      text { font-family: %s; }" % config["font"])
    out.append('      .title { font-size: 30px; font-weight: 700; fill: #0F172A; text-anchor: middle; }')
    out.append('      .subtitle { font-size: 15px; fill: #64748B; text-anchor: middle; }')
    out.append('      .branch { font-size: %dpx; font-weight: 700; fill: #FFFFFF; text-anchor: middle; }'
               % config["branch_font"])
    out.append('      .leaf { font-size: %dpx; fill: #1E293B; text-anchor: middle; }' % LEAF_FONT)
    out.append('    </style>')
    out.append('    <filter id="sh" x="-10%" y="-20%" width="120%" height="150%">')
    out.append('      <feDropShadow dx="0" dy="2" stdDeviation="3" flood-color="#0F172A" flood-opacity="0.15"/>')
    out.append('    </filter>')
    out.append('    <linearGradient id="centerFill" x1="0" y1="0" x2="1" y2="1">')
    out.append('      <stop offset="0" stop-color="#F43F5E"/>')
    out.append('      <stop offset="1" stop-color="#FB923C"/>')
    out.append('    </linearGradient>')
    out.append('  </defs>')
    out.append('  <rect x="0" y="0" width="%d" height="%d" fill="#FFFFFF"/>' % (canvas_w, canvas_h))
    out.append('  <text class="title" x="%.1f" y="56">%s</text>' % (center_x, escape(config["title"])))
    out.append('  <text class="subtitle" x="%.1f" y="88">%s</text>' % (center_x, escape(config["subtitle"])))

    body = []
    draw_side(body, RIGHT_BRANCHES, lang, center_x, center_y, True,
              TITLE_H + (content_h - side_height(RIGHT_BRANCHES)) / 2)
    draw_side(body, LEFT_BRANCHES, lang, center_x, center_y, False,
              TITLE_H + (content_h - side_height(LEFT_BRANCHES)) / 2)
    for item in body:
        out.append('  ' + item)

    # 中心节点最后画，压在曲线起点上面
    out.append('  <rect x="%.1f" y="%.1f" width="%d" height="%d" rx="28" fill="url(#centerFill)" filter="url(#sh)"/>'
               % (center_x - CENTER_W / 2, center_y - CENTER_H / 2, CENTER_W, CENTER_H))
    out.append('  <text x="%.1f" y="%.1f" style="font-size:32px;font-weight:700;fill:#FFFFFF;text-anchor:middle">'
               'Flamingo IM</text>' % (center_x, center_y - 4))
    out.append('  <text x="%.1f" y="%.1f" style="font-size:16px;fill:#FFF1F2;text-anchor:middle">%s</text>'
               % (center_x, center_y + 28, escape(config["center_sub"])))
    out.append('</svg>')
    return "\n".join(out) + "\n"


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for lang in ("zh", "en"):
        target = os.path.join(repo_root, "docs", "diagrams", LANG_CONFIG[lang]["file"])
        with open(target, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(build_svg(lang))
        print("written: %s" % target)


if __name__ == "__main__":
    main()
