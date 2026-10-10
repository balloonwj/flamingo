/**
 *  MarkdownEditorLayout.h
 *
 *  Markdown 编辑器窗口（docs/Markdown消息方案.md 第 11.3 节）的客户区控件树：
 *    - 顶部一行：「打开」「保存」「导出 ▾」，15 个格式按钮（ChatMarkdownFormatBar，窗口太窄时从末尾隐藏），右侧「预览」开关；
 *    - 查找条（默认隐藏，第 13 节）：查找行与替换行，Ctrl+F 显示查找行、Ctrl+H 两行都显示；
 *    - 中间：DuiSplitter 左右分栏，左边纯文字编辑框，右边 DuiScrollView + ChatMarkdownPreview 实时预览；
 *    - 底部一行：发送对象说明、「选择…」「发送」。
 *  控件树单独成文件，FlamingoUnit 可以按设计尺寸检查排得下（Tests/MarkdownEditorTests.cpp）。
 *
 *  典型用法（MarkdownEditorWindow.cpp）：
 *      SetClientContent(BuildMarkdownEditorBody());
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <memory>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../BalloonChatWnd/MarkdownEditCommands.h"   // kMarkdownEditCommandCount

namespace balloonmd {

// ---- 控件 id（避开标题栏按钮固定使用的 1~3）----
const UINT kMdEdIdOpen          = 101;   //「打开」：打开本地 .md 文件
const UINT kMdEdIdSave          = 102;   //「保存」：保存到当前文件，没有文件时另存为
const UINT kMdEdIdExport        = 103;   //「导出 ▾」：弹出导出格式菜单
const UINT kMdEdIdFormatBar     = 104;   //格式按钮所在的横向布局（ChatMarkdownFormatBar）
const UINT kMdEdIdPreviewToggle = 105;   //「预览」开关：收起或展开右侧预览
const UINT kMdEdIdSplitter      = 106;   //编辑区与预览区之间的分隔条（左右分栏）
const UINT kMdEdIdEditor        = 107;   //编辑框（DuiRichEdit，纯文字）
const UINT kMdEdIdPreviewScroll = 108;   //预览的滚动视图（DuiScrollView）
const UINT kMdEdIdPreview       = 109;   //预览控件（ChatMarkdownPreview）
const UINT kMdEdIdTargets       = 110;   //底部的发送对象说明（DuiLabel）
const UINT kMdEdIdPickTargets   = 111;   //「选择…」：打开发送对象选择窗
const UINT kMdEdIdSend          = 112;   //「发送」
const UINT kMdEdIdStatus        = 113;   //顶部右侧的状态文字（已保存、已发送等短提示）
const UINT kMdEdIdFormatFirst   = 120;   //第一个格式按钮；120 ~ 134 依次为标题、加粗……警告框（顺序同 MarkdownEditCommand）

// ---- 查找条（docs/Markdown消息方案.md 第 13 节）：顶部一行与编辑区之间的两行，默认隐藏 ----
const UINT kMdEdIdFindRow       = 140;   //查找行：查找框、「上一个」「下一个」「区分大小写」、匹配个数、关闭
const UINT kMdEdIdFindEdit      = 141;   //查找框（DuiEdit，单行）
const UINT kMdEdIdFindPrev      = 142;   //「上一个」（Shift+F3）
const UINT kMdEdIdFindNext      = 143;   //「下一个」（F3）
const UINT kMdEdIdFindMatchCase = 144;   //「区分大小写」勾选框
const UINT kMdEdIdFindCount     = 145;   //匹配个数或查找结果的短提示（DuiLabel）
const UINT kMdEdIdFindClose     = 146;   //关闭查找条（Esc）
const UINT kMdEdIdReplaceRow    = 147;   //替换行：替换框、「替换」「全部替换」（Ctrl+H 时与查找行一起显示）
const UINT kMdEdIdReplaceEdit   = 148;   //替换框（DuiEdit，单行）
const UINT kMdEdIdReplaceOne    = 149;   //「替换」：替换当前选中的匹配并找下一个
const UINT kMdEdIdReplaceAll    = 150;   //「全部替换」

// ---- 窗口尺寸（整窗尺寸，含标题栏，见开发约定文档 UI 约定第 7 条）----
const int kMdEdTitleBarH    = 32;    //标题栏高
const int kMdEdWindowWidth  = 980;   //默认宽
const int kMdEdWindowHeight = 660;   //默认高
const int kMdEdMinWidth     = 640;   //最小宽
const int kMdEdMinHeight    = 420;   //最小高

// ---- 分栏 ----
const int kMdEdPaneMinPx    = 160;   //编辑区、预览区各自的最小宽度（像素）
const double kMdEdInitialSplitFraction = 0.5;   //打开窗口与展开预览时编辑区所占的比例：左右各一半

// 编辑器与预览窗口根控件的底色。两个窗口的标题栏都是透明的，标题栏显示的也是这个颜色，
// 标题栏程序图标按它合成底色（开发约定文档 UI 约定第 10 条）
const COLORREF kMdWindowBackColor = RGB(0xFF, 0xFF, 0xFF);

/**
 *  搭 Markdown 编辑器的客户区控件树。编辑框的字体由窗口另行设置。
 *  @return 控件树的根
 */
std::unique_ptr<balloonwjui::DuiControl> BuildMarkdownEditorBody();

/**
 *  按分隔条当前的矩形，立即把左右宽度重新分为 kMdEdInitialSplitFraction。
 *  DuiSplitter 只在第一次排版时把比例换算成像素，之后容器尺寸变化也保持像素不变；窗口建好时先按建窗的默认尺寸
 *  排过一次，ResizeClient 到设计尺寸之后左右就不再各占一半。SetSplitFraction 在控件排过版之后会立即按当前矩形
 *  换算（DuiSplitter.cpp），所以窗口要在 ResizeClient 之后（WM_SIZE 已同步排版）调用本函数。
 *  @param root BuildMarkdownEditorBody 返回的控件树（借用，不转移所有权）；为 NULL 或找不到分隔条时不做任何事
 *  @return 找到分隔条并已设置时返回 true
 */
bool ResetMarkdownEditorSplit(balloonwjui::DuiControl* root);

/**
 *  显示或隐藏查找条。查找行与替换行都是根容器的直接子控件，显隐变化之后对根容器按它当前的矩形 ForceLayout
 *  （开发约定文档 UI 约定第 5 条：SetVisible 不会重新排版），编辑区随之让出或收回高度。
 *  @param root    BuildMarkdownEditorBody 返回的控件树（借用，不转移所有权）；为 NULL 时不做任何事
 *  @param find    是否显示查找行（Ctrl+F）
 *  @param replace 是否同时显示替换行（Ctrl+H）；find 为 false 时忽略，两行都隐藏
 */
void ShowMarkdownFindBar(balloonwjui::DuiControl* root, bool find, bool replace);

// ---- Markdown 预览窗口（点聊天窗里的 Markdown 卡片打开）的控件 id ----
const UINT kMdPvIdMeta       = 201;   //顶部左侧「发送者 · 时间」（DuiLabel）
const UINT kMdPvIdCopy       = 202;   //「复制 ▾」：弹出复制方式菜单
const UINT kMdPvIdExport     = 203;   //「导出 ▾」：弹出导出格式菜单
const UINT kMdPvIdOpenEditor = 204;   //「在编辑器中打开」：把源文本放进一个新的编辑器
const UINT kMdPvIdScroll     = 205;   //正文的滚动视图（DuiScrollView）
const UINT kMdPvIdPreview    = 206;   //正文（ChatMarkdownPreview）

// ---- 预览窗口尺寸（整窗尺寸，含标题栏）----
const int kMdPvTitleBarH    = 32;    //标题栏高
const int kMdPvWindowWidth  = 760;   //默认宽
const int kMdPvWindowHeight = 640;   //默认高
const int kMdPvMinWidth     = 480;   //最小宽
const int kMdPvMinHeight    = 360;   //最小高

/**
 *  搭 Markdown 预览窗口的客户区控件树：顶部一行「发送者 · 时间」与三个按钮，下面是可滚动的正文。
 *  @return 控件树的根
 */
std::unique_ptr<balloonwjui::DuiControl> BuildMarkdownPreviewBody();

/**
 *  格式按钮的提示文字（未翻译的原文，使用处再经 TR 翻译）。
 *  @param command 格式命令，取值见 MarkdownEditCommand
 *  @return 提示原文；command 越界时返回空串
 */
const wchar_t* MarkdownFormatTipSource(int command);

} // namespace balloonmd
