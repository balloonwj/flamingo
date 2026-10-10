/**
 *  MainPanelSystemMenu.h
 *
 *  登录后主面板左下角"汉堡"按钮弹出的系统菜单的<u>纯数据模型</u>。把"菜单
 *  有哪些项、什么顺序、各项 id / 文本 / 图标 / 是否可用"从对话框类里抽出成
 *  一个不依赖 HWND、不依赖 Flamingo 业务对象的纯函数，便于被 FlamingoUnit
 *  测试工程直接调用断言（弹出 / 路由等带窗口副作用的部分仍留在
 *  CBalloonMainPanel::ShowSystemMenu）。
 *
 *  最初的设计稿：design/主面板系统菜单.png（个人信息 / 修改密码 ── 公告
 *  与通知 ── 系统设置 / 网络设置 ── 关于 FlamingoIM，共 7 项 + 3 条分隔线）。
 *  2026-10-05 起按组加分组标题（账号 / 发起 / 消息与工具 / 设置 / 关于与登出），布局见 BuildSystemMenuModel。
 *  2026-10-07 起最后一组改为「登出与关于」（「关于」在最后一行），「账号」组末尾加「锁屏」。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "MainPanelRecordMenu.h"
#include <windows.h>      // UINT
#include <vector>

namespace balloonmain {

/**
 *  SystemMenuId —— 系统菜单各功能项的命令 id。点击后由 DuiMenu::TrackPopup
 *  返回该值；CBalloonMainPanel::ShowSystemMenu 据此分发到对应动作。
 *
 *  取值<u>必须非 0</u>：0 是 DuiMenu::TrackPopup 的"未选中"（失焦 / ESC）返回值，
 *  不能占用。分隔条不是功能项、没有 id（其 SystemMenuEntry::id 取 0）。
 */
enum SystemMenuId : UINT
{
    kSysMenuProfile      = 1,   // 个人信息 —— 打开本人个人信息窗
    kSysMenuChangePwd    = 2,   // 修改密码 —— 打开修改密码对话框
    // 3 号被一个已下线的功能占用过，号段留空不复用 —— 避免旧版本的截图、日志里
    // 记着的 id=3 被新菜单项顶替，排查历史问题时对不上号
    kSysMenuAnnounce     = 4,   // 公告与通知 —— 打开「公告中心」（平台公告收件箱），与 Banner 底部那一行同一去处
    kSysMenuSettings     = 5,   // 系统设置 —— 打开系统设置对话框
    kSysMenuNetSettings  = 6,   // 网络设置 —— 打开新版网络设置窗
    kSysMenuAbout        = 7,   // 关于 FlamingoIM —— 打开关于对话框
    kSysMenuLogout       = 8,   // 登出 —— 二次确认后断开当前连接、销毁主面板并回到登录界面
                                // （实际动作由 ShowSystemMenu 投递 FMG_MSG_LOGOUT 给会话控制器执行）
    kSysMenuFileShare    = 9,   // 文件共享 —— 打开「文件共享」窗口（FlamingoFileShare.dll 提供）；
                                // 模块没有加载时整项不出现，见 docs/文件共享方案.md 7.3
    kSysMenuRecord       = 10,  // 屏幕录制 / 停止录制 —— 开始独立录屏，录制中则停止并保存（FlamingoMedia.dll 提供）；
                                // 模块没有加载或系统不支持时整项不出现，见 MainPanelRecordMenu.h
    kSysMenuHistoryManager = 11, // 聊天记录管理 —— 打开聊天记录管理器（左侧会话、右侧聊天记录的消息记录窗），
                                 // 见 docs/聊天记录管理器方案.md §3.4
    kSysMenuMemo         = 12,  // 备忘与提醒 —— 打开本人的备忘窗口（左边月历、右边当天的备忘），见 docs/备忘与提醒方案.md
    kSysMenuMsgBox       = 13,  // 消息盒子 —— 打开消息盒子窗口（被收进消息盒子的会话列表），与会话列表里「消息盒子」
                                // 那一行、头部铃铛右侧的消息盒子按钮同一去处（2026-10-04 起）
    kSysMenuStartVoiceCall = 14, // 发起语音通话 —— 打开选会话窗口选一个联系人或群，选联系人直接呼叫，选群再选要呼叫的成员
                                 // （docs/音视频通话优化方案.md 第 4 项）；音视频模块没有加载时整项不出现
    kSysMenuStartVideoCall = 15, // 发起视频通话 —— 同上，发起的是视频通话
    kSysMenuScreenShare  = 16,  // 发起屏幕共享 —— 打开选会话窗口选一个联系人或群，选好后打开它的聊天窗并弹出共享内容选择窗
                                // （docs/屏幕共享与录制方案.md 第 2 节第 42 条）；屏幕共享模块不可用时整项不出现
    kSysMenuRecordManager = 17, // 录制管理 —— 打开录制管理窗口，列出本账号录过的录像（FlamingoMedia.dll 提供，
                                // docs/屏幕共享与录制方案.md 第 11.6 节 F9）；窗口不可用时整项不出现
    kSysMenuOcr          = 18,  // 图片文字识别 —— 打开识别窗口，用窗口里的「打开图片」选一张本地图片识别（FlamingoOcr.dll 提供，
                                // docs/图片文字识别方案.md 第 14 节）；识别模块没有加载时整项不出现
    kSysMenuMarkdownEditor = 19, // Markdown 编辑器 —— 打开一个空白的 Markdown 编辑器（docs/Markdown消息方案.md 第 11.3 节）；
                                 // FlamingoMarkdown.dll 不可用时整项不出现
    kSysMenuLockScreen   = 20,  // 锁屏 —— 弹出锁屏的临时密码窗口，与托盘菜单「锁屏」同一去处（CLockHost::RequestLock，
                                // 2026-10-07 起）；FlamingoLock.dll 没有加载时整项不出现
    kSysMenuMyBots       = 21,  // 我的机器人 —— 打开「我的机器人」窗口（FlamingoBot.dll 提供，docs/机器人方案.md §4.3）；
                                // 插件不可用或企业没有允许个人机器人时整项不出现
};

/**
 *  SystemMenuEntry —— 系统菜单里的一行（功能项、分隔条或分组标题）。
 *  按聚合初始化时可以只写前五个成员，isHeader 随之取 false（功能项与分隔条都这样写）。
 */
struct SystemMenuEntry
{
    UINT           id;            // 功能项命令 id（见 SystemMenuId）；分隔条与分组标题取 0
    bool           isSeparator;   // 是否为分隔条；true 时 label/iconKind 无意义
    const wchar_t* label;         // 显示文本（宽字符常量串）；分隔条取 nullptr，分组标题为标题文字
    int            iconKind;      // 左侧图标类型（MainPanelIcons.h 的 IconKind）；分隔条与分组标题取 -1
    bool           enabled;       // 是否可点击；false 为灰显 no-op（功能项均 true，分隔条与分组标题为 false）
    bool           isHeader;      // 是否为分组标题（2026-10-05 起）：不可点击的一行灰色小字，交给 DuiMenu::AppendHeader
};

/**
 *  构造系统菜单的行模型（2026-10-05 起按组加分组标题，docs/屏幕共享与录制方案.md 第 11.6 节 F8）：
 *      账号（个人信息、修改密码、锁屏）─ 发起（发起语音通话、发起视频通话、发起屏幕共享、发起录制）
 *      ─ 消息与工具（聊天记录管理、公告与通知、消息盒子、备忘与提醒、文件共享、录制管理）
 *      ─ 设置（系统设置、网络设置）─ 登出与关于（登出、关于 <产品名称>）
 *  2026-10-07 起「关于」排在整张菜单的最后一行、「登出」在它之前，并在「账号」组末尾加「锁屏」。
 *  每组开头一行分组标题（isHeader 为真），组与组之间一条分隔线。默认参数下没有「发起」组，也没有「文件共享」
 *  「录制管理」，共 10 个功能项 + 3 条分隔线 + 4 个分组标题 = 17 行。
 *  纯函数、无副作用、不创建窗口；调用方（ShowSystemMenu）据此逐行喂给 DuiMenu。
 *
 *    withFileShare：文件共享模块是否已加载（CFileShareHost::IsModuleReady）；为假时不出现「文件共享」。
 *    recordState：  「发起录制」一项的状态（RecordMenuState）：不出现、「发起录制」或「停止录制」；
 *                   出现时排在「发起」组的最后。
 *    aboutLabel：   「关于 <产品名称>」的文字（主面板用 BuildAboutCaption(g_strAppTitle) 拼好
 *                   传入）；NULL 或空串时为「关于 FlamingoIM」。借用调用方的字符串，不复制。
 *    withCalls：    音视频模块是否已加载；为真时「发起」组里有「发起语音通话」「发起视频通话」
 *                   （docs/音视频通话优化方案.md 第 4 项）。
 *    withScreenShare：屏幕共享模块是否可用（CScreenShareHub::IsModuleReady）；为真时「发起」组里在「发起录制」
 *                   所在位置之前多一项「发起屏幕共享」（docs/屏幕共享与录制方案.md 第 2 节第 42 条）。
 *    withRecordManager：录制管理窗口是否可用；为真时「消息与工具」组的最后多一项「录制管理」。
 *    withOcr：      图片文字识别模块是否可用（ocr::IsOcrEntryVisible）；为真时「消息与工具」组里紧跟「备忘与提醒」
 *                   多一项「图片文字识别」（docs/图片文字识别方案.md 第 14 节）。
 *    withMarkdown： FlamingoMarkdown.dll 是否可用（markdown::IsMarkdownAvailable）；为真时「消息与工具」组里紧跟
 *                   「图片文字识别」（没有它时紧跟「备忘与提醒」）多一项「Markdown 编辑器」（docs/Markdown消息方案.md 第 11.3 节）。
 *    withLock：     锁屏模块是否可用（CLockHost::IsModuleReady）；为真时「账号」组的最后多一项「锁屏」。
 *    withBots：     「我的机器人」是否可用（FlamingoBot.dll 就绪且企业允许个人机器人，2026-10-08 起）；为真时「消息与工具」组里
 *                   紧跟「备忘与提醒」多一项「我的机器人」。
 *  withCalls、withScreenShare 为假且 recordState 为不出现时，整个「发起」组（连同它前面的分隔线）不出现。
 *  返回：行模型 vector（值语义，调用方拥有）。「关于」项的 label 指向 aboutLabel，调用方须保证
 *        它在使用模型期间有效；其余各项的 label 指向静态常量串，生命周期为进程级。
 */
std::vector<SystemMenuEntry> BuildSystemMenuModel(bool withFileShare = false, int recordState = kRecordMenuHidden,
                                                  const wchar_t* aboutLabel = NULL, bool withCalls = false,
                                                  bool withScreenShare = false, bool withRecordManager = false,
                                                  bool withOcr = false, bool withMarkdown = false,
                                                  bool withLock = false, bool withBots = false);

} // namespace balloonmain
