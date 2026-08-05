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
#include "../../balloonui/balloonui/DuiControl.h"

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
    kIdInputEdit     = 401,   // 输入框（DuiRichEditHost，富文本，支持内联图片）
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
    kIdTbVoice       = 424,   // 语音
    kIdTbPlus        = 425,   // 更多
    kIdTbShake       = 426,   // 窗口抖动
    kIdTbFont        = 427,   // 字体设置（AF）
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
const int kChatInputBlockDefaultPx = 172;

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
