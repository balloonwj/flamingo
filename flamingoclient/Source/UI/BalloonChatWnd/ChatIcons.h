/**
 *  ChatIcons.h
 *
 *  单聊窗用到的一组线性图标的 GDI+ 描边绘制 helper。图标形状取自 design
 *  的 icons.jsx（统一 viewBox 24、圆头圆角描边）。头部按钮、输入工具栏、
 *  气泡回执等处都从这里取图标。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <gdiplus.h>

namespace balloonchatwnd {

/**
 *  图标类型。逐值对应 design icons.jsx 里的同名图标。
 */
enum ChatIconKind
{
    kChatIconPhone   = 0,   // 电话听筒 —— 语音通话
    kChatIconVideo   = 1,   // 摄像机 —— 视频通话
    kChatIconHistory = 2,   // 时钟 —— 消息记录
    kChatIconMore    = 3,   // 三点 —— 更多 / 详情
    kChatIconEmoji   = 4,   // 笑脸 —— 表情
    kChatIconImage   = 5,   // 图片
    kChatIconFile    = 6,   // 文件
    kChatIconCut     = 7,   // 剪刀 —— 截图
    kChatIconMic     = 8,   // 麦克风 —— 语音
    kChatIconPlus    = 9,   // 加号 —— 更多
    kChatIconCheck   = 10,  // 对勾 —— 已读回执
    kChatIconSend    = 11,  // 纸飞机 —— 发送
    kChatIconSettings = 12, // 齿轮 —— 群管理
    kChatIconShake   = 13,  // 抖动的小窗 + 两侧运动线 —— 窗口抖动
    kChatIconFont    = 14,  // 大写字母 A —— 字体设置
    kChatIconRemoteDesktop = 15,  // 显示器 —— 远程桌面
    kChatIconPin     = 16,  // 图钉 —— 被 Pin 住的消息（画在气泡右上角外侧）
    kChatIconFolder  = 17,  // 文件夹 + 向上箭头 —— 发送文件夹
    kChatIconClear   = 18,  // 扫帚 —— 清空聊天记录（与群资料里「退出群聊」的垃圾桶区分开）
    kChatIconRelay   = 19,  // 写字板 + 三行带圆点的条目 —— 群接龙（群聊窗头部「发起接龙」按钮、接龙卡片标题前）
    kChatIconShare   = 20,  // 文件夹 + 夹内三个相连的圆点 —— 文件共享（工具栏「文件共享」按钮、文件夹共享卡片标题前）
    kChatIconDisk    = 21,  // 横置的硬盘 + 指示灯 —— 磁盘共享卡片标题前
    kChatIconScreenShare = 22,  // 显示器 + 屏幕内向上的箭头 —— 共享屏幕（聊天窗头部按钮、屏幕共享卡片标题前）
    kChatIconRecord  = 23,  // 场记板（带斜纹的打板 + 板身里的实心圆点）—— 屏幕录制（工具栏按钮；2026-10-06 之前是圆环 + 圆点）
    kChatIconGroupInvite = 24,  // 人形（头 + 肩）+ 右上角加号 —— 入群邀请卡片标题前（docs/邀请入群方案.md §3.3）
    kChatIconVoiceWave1 = 25,   // 喇叭 + 1 道声波 —— 语音气泡播放中的第 1 帧（docs/语音消息方案.md 第 3.2 节）
    kChatIconVoiceWave2 = 26,   // 喇叭 + 2 道声波 —— 语音气泡播放中的第 2 帧
    kChatIconVoiceWave3 = 27,   // 喇叭 + 3 道声波 —— 语音气泡平时的图标，也是播放中的第 3 帧
    kChatIconSubtitle   = 28,   // 圆角框 + 框内下部一短一长两行横线 —— 字幕模式（聊天窗头部按钮，docs/聊天字幕模式方案.md）
    kChatIconLock       = 29,   // 闭合的挂锁 —— 锁定字幕（字幕工具条上的锁定按钮）
    kChatIconUnlock     = 30,   // 锁梁抬起的挂锁 —— 解锁字幕（字幕锁定后浮出的小锁按钮）
    kChatIconBackToChat = 31,   // 缺一角的方框 + 从框内指向右上角的箭头 —— 回到聊天窗（字幕工具条）
    kChatIconClose      = 32,   // 叉号 —— 退出字幕模式（字幕工具条）
    kChatIconAnnounce   = 33,   // 喇叭（扩音器）+ 手柄 —— 群公告卡片标题前（docs/群公告接入方案.md §13.4）
    kChatIconMemo       = 34,   // 日历（顶部两个挂环）+ 格内对勾 —— 群备忘卡片标题前（docs/备忘与提醒方案.md §7.3）
    kChatIconCaret      = 35,   // 尖朝下的实心小三角 —— 工具栏剪刀右边的截图选项下拉按钮（docs/截图快捷键与隐藏当前窗口方案.md）

    // ---- Markdown 消息（docs/Markdown消息方案.md 第 11.3 节），绘制在 ChatMarkdownIcons.cpp ----
    kChatIconMarkdown       = 36,   // 圆角框 + 框内「M↓」—— 聊天工具栏上打开 Markdown 编辑器的按钮，以及 Markdown 卡片的图标
    kChatIconMdHeading      = 37,   // 字母「H」—— 格式条：标题
    kChatIconMdBold         = 38,   // 粗体字母「B」—— 格式条：加粗
    kChatIconMdItalic       = 39,   // 斜体字母「I」—— 格式条：斜体
    kChatIconMdStrike       = 40,   // 字母「S」+ 穿过中部的横线 —— 格式条：删除线
    kChatIconMdCode         = 41,   // 一对尖括号「< >」—— 格式条：行内代码
    kChatIconMdCodeBlock    = 42,   // 圆角框 + 框内尖括号 —— 格式条：代码块
    kChatIconMdLink         = 43,   // 两个斜扣的圆角链环 —— 格式条：链接
    kChatIconMdQuote        = 44,   // 左侧竖条 + 右侧两行横线 —— 格式条：引用
    kChatIconMdBulletList   = 45,   // 三个圆点 + 三行横线 —— 格式条：无序列表
    kChatIconMdNumberList   = 46,   // 左侧「1 2 3」+ 三行横线 —— 格式条：有序列表
    kChatIconMdTaskList     = 47,   // 带勾的小方框 + 空方框，各跟一行横线 —— 格式条：任务列表
    kChatIconMdTable        = 48,   // 三列两行的表格框 —— 格式条：表格
    kChatIconCode           = 49,   // 两个尖括号夹一条斜线「</>」 —— 输入区工具栏：发送代码（docs/代码片段方案.md 第 3.1 节）
    kChatIconSystemAudio    = 50,   // 显示器 + 屏幕内一个音符 —— 通话窗操作栏的「共享电脑声音」（docs/音视频通话方案.md 第 10 节）
    kChatIconMdUnderline    = 51,   // 字母「U」+ 下方一条横线 —— Markdown 编辑器格式条：下划线（docs/Markdown消息方案.md 第 13 节）
    kChatIconMdRule         = 52,   // 中间一条长横线，上下各一条短横线 —— Markdown 编辑器格式条：分割线
    kChatIconMdAlert        = 53,   // 三角形 + 框内感叹号 —— Markdown 编辑器格式条：警告框
    kChatIconOrgTree        = 54,   // 组织架构图：上方一个框，经连接线分出下方两个框 —— 群聊窗成员搜索框右边的
                                    // 「按部门 / 平铺」切换按钮（部门树视图下高亮；2026-10-07 用户选定，取代原来的文字链接）
    kChatIconChatRecord     = 55,   // 对话气泡 + 右侧一道弧线（表示后面还叠着一条）—— 合并转发的聊天记录卡片（docs/聊天卡片美化方案.md 3.1）
    kChatIconCloudRecord    = 56,   // 圆角框 + 框内向右的播放三角 —— 云端录像卡片（同上）
    kChatIconPhoneLine      = 57,   // 电话听筒的轮廓线（不填充）—— 语音通话、群语音通话卡片的图标块（同上）。与 kChatIconPhone 的实心
                                    // 听筒不同：卡片里与其他线条图标并排，实心听筒显得偏重；聊天窗头部的语音通话按钮仍用 kChatIconPhone
    kChatIconCopy           = 58,   // 两张错开叠放的圆角纸 —— 代码片段卡片头部的「复制」（docs/聊天卡片美化方案.md 3.7）
};

/**
 *  在指定区域内绘制一个图标。
 *
 *    g：          目标 Graphics（调用方应已开启抗锯齿）。
 *    kind：       图标类型，取值见 ChatIconKind；越界值不绘制。
 *    x / y：      绘制区域左上角（设备坐标）。
 *    size：       绘制区域边长（图标按 viewBox 24 等比缩放到此尺寸）。
 *    color：      图标颜色。
 *    strokeWidth：描边宽度，以 viewBox 24 为基准的单位（一般取 2.0）。
 */
void DrawChatIcon(Gdiplus::Graphics& g, int kind,
                  Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                  const Gdiplus::Color& color, Gdiplus::REAL strokeWidth);

} // namespace balloonchatwnd
