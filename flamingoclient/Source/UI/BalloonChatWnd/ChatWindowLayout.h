/**
 *  ChatWindowLayout.h
 *
 *  新版单聊窗口的 balloonui 客户区控件树构造种子。把"单聊窗长什么样"
 *  从窗口类里抽出成一个可独立编译、可被单元测试直接调用的纯函数模块
 *  —— CBalloonChatWindow 与 FlamingoUnit 测试工程共用本文件。
 *
 *  设计稿：design/pc-snapshots/07-chat-single.png。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前，调用方必须已引入
 *  ATL/WTL（Flamingo 侧由 stdafx.h 保证，FlamingoUnit 侧由其 stdafx.h
 *  保证）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <memory>
#include <string>
#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonchatwnd {

/**
 *  CtrlId —— 单聊窗里所有"需要被代码引用"的控件的数字 id。
 *
 *  与 BalloonMain / BalloonChat 同约定：XML 里写 id="400"，窗口侧用
 *  DuiControl::FindCtrlById(id) 取回控件指针。集中成 enum 避免 XML
 *  字符串与 C++ 代码各写一份魔数、对不上。纯装饰控件在 XML 里不写 id，
 *  GetCtrlId() 返回 0 —— 0 视为"未命名"，不属于本 enum。
 */
enum CtrlId : UINT
{
    // ---- 主体 ----
    kIdMessageArea   = 400,   // 消息区容器（气泡列表）
    kIdInputEdit     = 401,   // 输入框（ChatInputRichEdit，无窗口富文本，支持内联图片）
    kIdSendButton    = 402,   // 发送按钮
    kIdHeaderInfo    = 403,   // 头部信息块（点击打开对方资料窗）
    kIdHintLabel     = 404,   // 输入区左下角"发送/换行"提示文字（随发送模式联动）
    kIdReplyBar      = 406,   // 输入框上方的"引用回复"条（DuiHBox；默认隐藏，点了
                              // 气泡右键「引用回复」才出现，发送后自动收起）
    kIdReplyName     = 407,   // 引用条里的"回复 张三："（DuiLabel，主题色；宽度按文字实测）
    kIdReplyDigest   = 408,   // 引用条里的被引用内容摘要（DuiLabel，灰色，过长省略）
    kIdReplyClose    = 409,   // 引用条右侧的取消按钮（DuiLabel，ModeLink 的 ✕）
    kIdOfflineBanner = 405,   // 窗口顶部的离线 / 正在连接提示带（ChatBannerLabel，带底色
                              // 与下边框）。平时 SetVisible(false) 不占高度（DuiLayout 会
                              // 跳过不可见子控件）；离线 / 正在连接时显示。只作告知、
                              // <u>不可点击</u> —— 恢复在线走托盘 / 主面板的状态菜单。
                              // 注意显隐变化后须补一次布局才生效，见 ApplyConnectionState
    kIdInputSplitter = 430,   // 消息区(上)/输入区块(下)之间的横向可拖分隔条，
                              //   拖它即可重分两者高度；分隔条落在工具栏上边缘

    // ---- 头部右侧图标按钮 ----
    kIdHdrVoice      = 410,   // 语音通话
    kIdHdrVideo      = 411,   // 视频通话
    kIdHdrRemoteDesktop = 412,   // 远程桌面（点击向对端发起远程桌面邀请；设计图工具栏第 3 个）
    kIdHdrMore       = 413,   // 更多 / 详情
    kIdHdrHistory    = 414,   // 消息记录（点击打开与对端的历史消息记录窗）
    // 416 原为头部「清空聊天记录」按钮（扫帚），2026-10-03 起「清空聊天记录」从聊天窗移到消息记录窗（底部「导出」左边与行右键菜单），不再使用
    kIdHdrScreenShare = 417,  // 共享屏幕（紧挨在「远程桌面」右边；点击经 FlamingoMedia.dll 弹出共享内容选择窗；模块没有加载时隐藏）
    kIdHdrSubtitle   = 441,   // 字幕模式：紧挨在「消息记录」左边，点一下进入、再点一下退出（docs/聊天字幕模式方案.md）
    kIdVoiceBar      = 432,   // 录音条（语音消息第 3b 步）：输入框后面的横向布局占位，建好后隐藏；录音时代替输入框与
                              // 底部发送行显示，控件由 CVoiceRecordBar::Build 填入
    kIdVoiceCancel   = 433,   // 录音条上的「取消」按钮（DuiButton）
    kIdVoiceSend     = 434,   // 录音条上的「发送」按钮（DuiButton，主色）
    kIdInputBottomRow = 435,  // 输入区底部「提示文字 + 发送按钮」那一行（横向布局）；录音时隐藏
    kIdVoicePause    = 436,   // 录音条上的「暂停 / 继续」按钮（语音消息第 6g 步，文字随状态切换）
    kIdVoiceStop     = 437,   // 录音条上的「停止」按钮：停止录音并进入试听
    kIdVoicePreview  = 438,   // 录音条上的「试听 / 停止试听」按钮（只在停止后的试听状态显示）
    kIdVoiceConfirm  = 439,   // 录音条上的「确认放弃」按钮（点「取消」后的确认状态显示）
    kIdVoiceBack     = 440,   // 录音条上的「返回」按钮：不放弃，回到点「取消」之前的状态
    kIdHdrShare      = 431,   // 文件共享：紧挨在「共享屏幕」右边，弹出本会话的共享列表浮层（FlamingoFileShare.dll 提供），
                              // 模块没有加载或本企业关闭了文件共享时隐藏。2026-10-03 之前在输入框工具栏（发送文件夹之后），
                              // 按用户要求挪到头部；编号沿用原值，没有改成 41x
    kIdPinBanner     = 415,   // 头部信息行下方的「已 Pin N 条」提示带（ChatBannerLabel，
                              // ModeLink 可点）。本会话没有被 Pin 的消息时整条隐藏、不占位；
                              // 点它打开消息记录窗并切到 Pin 分类。
                              // 与 kIdOfflineBanner 分两层：那条讲的是"连接态"、在最顶，
                              // 这条讲的是"本会话内容"、贴着消息区，两者同时出现时层次清楚

    // ---- 输入区上方工具栏图标按钮 ----
    kIdTbEmoji       = 420,   // 表情
    kIdTbImage       = 421,   // 图片
    kIdTbFile        = 422,   // 文件
    kIdTbScreenshot  = 423,   // 截图
    kIdTbScreenshotMenu = 442, // 截图选项下拉按钮（▾）：紧挨在截图右边，点了弹出「截图 / 截取全屏 / 截图时隐藏当前窗口」
                              // 菜单（docs/截图快捷键与隐藏当前窗口方案.md）
    kIdTbVoice       = 424,   // 语音
    kIdTbPlus        = 425,   // 更多
    kIdTbShake       = 426,   // 窗口抖动
    kIdTbFont        = 427,   // 字体设置（AF）
    kIdTbFolder      = 428,   // 发送文件夹（选一个本地目录整体发给对方，走 cmd_folder_* 1810~1817
                              // 那条独立链路；与 kIdTbFile 的单个文件传输不是一回事）
    kIdTbRecord      = 429,   // 屏幕录制：紧挨在截图右边，点了开始独立录屏，录完可一键发到本会话
                              // （FlamingoMedia.dll 提供，模块没有加载时隐藏；docs/屏幕共享与录制方案.md 6.2）
    kIdTbCode        = 448,   // 发送代码：紧挨在 Markdown 开关右边，点了打开「发送代码」窗口（docs/代码片段方案.md 第 3.1 节；
                              // 443~447、450~461 已被 Markdown 模式占用）

    // ---- Markdown（docs/Markdown消息方案.md 第 11.3 节）----
    kIdTbMarkdown    = 443,   // 工具栏的「Markdown 编辑器」按钮：紧挨在字体设置右边，打开独立的 Markdown 编辑器并预选本会话；
                              // FlamingoMarkdown.dll 不可用时隐藏
    // 444 ~ 447、450 ~ 461 已作废（2026-10-06 去掉输入区的 Markdown 模式时删除）：原为格式条占位、预览占位、
    // 「编辑 / 预览」页签与 12 个格式按钮，不得复用

    // ---- 粘贴识别提示条（docs/代码片段方案.md 第 3.1 节）----
    kIdCodePasteBar     = 462,   // 引用条与输入框之间的「看起来是一段代码」提示条（DuiVBox 套一行 DuiHBox；默认隐藏，往空输入框里
                                 // 粘贴像代码的文字时出现，继续输入、发送或点「不用了」时收起）
    kIdCodePasteText    = 463,   // 提示条左侧的说明文字
    kIdCodePasteSend    = 464,   // 提示条上的「以代码片段发送」链接：把输入框里的文字移进发送代码窗口
    kIdCodePasteDismiss = 465,   // 提示条上的「不用了」链接：只收起提示条

    // ---- 单聊密聊（docs/密聊方案.md 4.7、6.2）----
    kIdHdrSecret        = 466,   // 头部「密聊」按钮（锁形图标），放在「更多」左边；密聊 DLL 不可用、企业关闭开关、
                                 // 服务端没有确认开启或对方是本人时隐藏；密聊状态下也隐藏
    kIdSecretBanner     = 467,   // 窗口顶部的深色密聊头部条（DuiVBox，默认隐藏）：密聊中、只能查看两种状态下显示
    kIdSecretBannerText = 468,   // 头部条左侧的状态文字（「密聊中 · 消息不会被记录，退出后全部清除」等）
    kIdSecretBannerWarn = 469,   // 头部条第二行的防截图提示（醒目的橙色文字），双方都能防截图时隐藏
    kIdSecretSafety     = 470,   // 头部条上的「安全码」链接：密聊中才显示
    kIdSecretReinvite   = 471,   // 头部条上的「重新邀请」链接：只能查看、且入口可用时才显示
    kIdSecretExit       = 472,   // 头部条上的「退出密聊」链接
};

/**
 *  构造单聊窗的客户区控件树（不含窗口边框 —— 边框由 DuiFrameWindow
 *  自身负责）。
 *
 *  返回：客户区根控件（一个 vbox），调用方拿到所有权；解析失败返回
 *        nullptr（内嵌 XML 为编译期常量、形态固定，理论上不会失败）。
 */
//   loadMockMessages：是否用 mock 会话消息预填消息列表。真实会话传 false
//                     （窗口起始为空，由真实收发填充）；--chatwindow 调试入口
//                     与群聊视觉核对传 true。默认 true 保持既有调用不变。
std::unique_ptr<balloonwjui::DuiControl> BuildChatWindowBody(bool loadMockMessages = true);

// 输入区块（工具栏 + 输入框 + 提示/发送行）的默认高度（像素）。用户从未拖动
// 分隔条时聊天窗用它布局；与改造前输入区固定高度保持一致。须与各窗口 XML 里
// splitter 的 split-fraction 默认值大致对应（默认窗口尺寸下约为本值）。
// 2026-10-06 按用户要求由 172 调高为 212（加了「代码」入口，输入区也调大一些，见 docs/代码片段方案.md 第 3.1 节）。
const int kChatInputBlockDefaultPx = 212;

// 单聊窗允许缩放到的最小整窗尺寸（逻辑像素，含标题栏；见 UI 约定第 7 条）。CBalloonChatWindow 据此调用
// SetMinSize；布局单测按这个宽度检查 Markdown 格式条的两个页签仍完整显示（2026-10-06 从 BalloonChatWindow.cpp 移来）。
const int kChatWindowMinWidth  = 460;
const int kChatWindowMinHeight = 420;

/**
 *  会话内"输入区块高度"的进程级记忆（像素）。
 *
 *  用户拖动聊天窗中间工具栏上边缘的分隔条（kIdInputSplitter）后，记下"输入区块
 *  应有多高"；之后新开的单聊 / 群聊窗，以及当前窗口缩放时，都把输入区块稳定在
 *  这个高度、让消息区吃掉窗口高度的变化（与改造前"输入区固定、消息区撑满"的
 *  手感一致）。不落盘 —— 进程重启复位为 kChatInputBlockDefaultPx。单聊与群聊
 *  共用同一值（都是"我习惯的输入区高度"），群聊侧也复用这里的访问声明。
 *
 *  返回：已记住的输入区块高度（像素）；用户从未拖过时返回 kChatInputBlockDefaultPx。
 */
int GetRememberedChatInputHeightPx();

/**
 *  记下新的输入区块高度（像素）。由聊天窗在收到分隔条拖动完成通知
 *  （DUIN_VALUECHANGED）时调用。
 *    px：输入区块期望高度（像素）。<= 0 视为复位为默认高度。
 */
void RememberChatInputHeightPx(int px);

// ---------------------------------------------------------------------
// 「已 Pin N 条」提示带（kIdPinBanner / kGIdPinBanner）
//
// 单聊窗与群聊窗是两棵独立控件树，但这条提示带的外观与行为必须一模一样，
// 所以样式与文案都收在这里由两边共用，避免各写一份慢慢走样。
// ---------------------------------------------------------------------

/**
 *  Pin 提示带上应显示的文字。抽成纯函数便于单元测试直接断言文案。
 *    count：本会话当前被 Pin 的消息条数。<= 0 表示一条都没有。
 *  返回：提示带文字（形如"已 Pin 3 条 ›"）；count <= 0 时返回空串
 *        —— 此时整条提示带应当隐藏、不占位，见 UpdatePinBanner。
 */
std::wstring MakePinBannerText(int count);

/**
 *  把 Pin 提示带调成统一样式：淡琥珀底 + 下边框、可点击的链接文字、初始隐藏。
 *  由两个窗口的 Build*Body 在建好控件树后各调一次。
 *    banner：Pin 提示带控件（实际类型须为 ChatBannerLabel）。传 NULL 或类型
 *            不符时直接返回，调用方不必先判空。所有权仍在控件树，本函数不持有。
 *  副作用：修改该控件的可见性与配色。
 */
void ApplyPinBannerStyle(balloonwjui::DuiControl* banner);

/**
 *  把粘贴识别提示条（docs/代码片段方案.md 第 3.1 节）调成统一样式：浅蓝底圆角、灰色说明文字、两个可点击的链接，
 *  初始隐藏；非简体中文界面下链接按译文宽度加宽。由两个窗口的 Build*Body 在建好控件树后各调一次。
 *    root：客户区根控件；传 NULL 直接返回。所有权不转移。
 *    barId / textId / sendId / dismissId：提示条、说明文字、「以代码片段发送」「不用了」两个链接的控件 id
 *      （单聊传 kIdCodePaste*，群聊传 kGIdCodePaste*）。找不到的控件跳过。
 *  副作用：修改这几个控件的可见性、配色、链接模式与宽度。
 */
void ApplyCodePasteBarStyle(balloonwjui::DuiControl* root, UINT barId, UINT textId, UINT sendId, UINT dismissId);

/**
 *  把密聊头部条（kIdSecretBanner）调成统一样式并收起：深色底、浅色状态文字、橙色防截图提示、三个可点击的链接；
 *  头部的「密聊」按钮（kIdHdrSecret）同时收起。由 BuildChatWindowBody 在建好控件树后调用一次（docs/密聊方案.md 4.7）。
 *    root：客户区根控件；传 NULL 直接返回。所有权不转移。
 *  副作用：修改这几个控件的可见性、配色、链接模式与宽度。
 */
void ApplySecretBannerStyle(balloonwjui::DuiControl* root);

/**
 *  按最新的 Pin 条数刷新提示带：有 Pin 就显示并写上条数，一条没有就整条收起。
 *    root：窗口客户区根控件；传 NULL 直接返回 false。所有权不转移。
 *    bannerId：提示带控件 id（单聊传 kIdPinBanner，群聊传 kGIdPinBanner）。
 *    count：本会话当前被 Pin 的消息条数，<= 0 视为 0。
 *  返回：提示带的<u>显隐</u>是否发生了变化。为 true 时调用方必须补一次布局
 *        —— SetVisible 只触发重绘不重新布局，不补的话新显示的提示带拿不到
 *        高度（画出来是空的），刚隐藏的那条空白也不会自动收掉。
 */
bool UpdatePinBanner(balloonwjui::DuiControl* root, UINT bannerId, int count);

} // namespace balloonchatwnd
