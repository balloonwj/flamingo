/**
 *  BalloonChatLayout.h
 *
 *  单聊 / 群聊对话框的 balloonui 声明式布局种子。把"控件树长什么样"
 *  从对话框类里抽出来，单独成一个可独立编译、可被单元测试直接调用的
 *  纯函数模块 —— BalloonBuddyChatDlg / BalloonGroupChatDlg 与
 *  FlamingoUnit 测试工程共用本文件。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方必须已经把 ATL/WTL
 *  （<atlbase.h> 等）引入（Flamingo 侧由 stdafx.h 保证，FlamingoUnit 侧
 *  由其 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026.05.21
 */

#pragma once

#include <memory>
#include <vector>
#include "../../balloonui/balloonui/DuiControl.h"

namespace balloonchat {

/**
 *  CtrlId —— 单聊 / 群聊布局里所有"需要被代码引用"的控件的数字 id。
 *
 *  balloonui 的 DuiXmlBuilder 用数字 UINT 作 ctrlId（XML 里写 id="100"），
 *  对话框侧再用 DuiControl::FindCtrlById(id) 把控件指针取回来。把这些数字
 *  集中成一个 enum，避免 XML 字符串与 C++ 代码各写一份魔数、对不上。
 *
 *  单聊与群聊是两棵<u>独立</u>的控件树，FindCtrlById 各查各的子树，所以
 *  群聊左半区可以原样复用单聊的全部 id；群聊右侧栏另用 110/111 段。
 *
 *  纯装饰、不需要代码引用的控件（分隔线、占位 spacer）在 XML 里不写 id，
 *  GetCtrlId() 返回 0 —— 0 视为"未命名"，不属于本 enum。
 */
enum CtrlId : UINT
{
    // ---- 消息收发富文本区（单聊 / 群聊左半区共用）----

    kIdRecvEdit        = 100,   // 消息接收区：只读 richedit，显示历史 + 新到消息
    kIdSendEdit        = 101,   // 消息输入区：可编辑 richedit，用户在此打字

    // ---- 群聊右侧栏（仅群聊布局出现）----

    kIdGroupAnnounce   = 110,   // 群公告：只读 richedit，显示群主设置的公告文本
    kIdMemberList      = 111,   // 群成员列表：单列 treeview，每行一名成员
    kIdMemberCountLabel= 112,   // 群成员计数标签，形如 "(1/2)"（在线/总数）

    // ---- 单聊右侧"消息记录"面板（仅单聊布局出现）----

    kIdHistoryEdit     = 120,   // 消息记录：只读 richedit，展示历史聊天记录
    kIdHistoryPanel    = 130,   // 消息记录面板容器（vbox），默认隐藏、可切换
    kIdHistorySep      = 131,   // 消息记录面板左侧的竖分隔条，与面板同步显隐

    // ---- 顶部工具栏（单聊 / 群聊左半区共用前两个）----

    kIdTbFile          = 201,   // "文件"按钮：发起文件传输
    kIdTbRemoteDesktop = 202,   // "远程"按钮：发起远程桌面协助

    // ---- 中部工具栏（位于输入区上方，单聊 / 群聊左半区共用）----

    kIdTbFont          = 301,   // "字"按钮：弹出字体 / 字号 / 颜色设置
    kIdTbEmoji         = 302,   // "表情"按钮：弹出 emoji 选择面板
    kIdTbShake         = 303,   // "抖动"按钮：给对方发送窗口抖动
    kIdTbImage         = 304,   // "图片"按钮：选择本地图片插入输入区
    kIdTbScreenshot    = 305,   // "截图"按钮：调起截图工具
    kIdTbMsgHistory    = 306,   // "消息记录"按钮：开关右侧消息记录面板

    // ---- 底部按钮栏（单聊 / 群聊左半区共用）----

    kIdBtnClose        = 401,   // "关闭(C)"按钮：关闭聊天窗口
    kIdBtnSend         = 402,   // "发送(S)"按钮：把输入区内容作为消息发出
    kIdBtnSendArrow    = 403,   // 发送按钮右侧 "▾"：弹出发送方式菜单
};

/**
 *  构造单聊对话框的客户区控件树（不含窗口边框 —— 边框由 DuiFrameWindow
 *  自身负责）。
 *
 *  返回：客户区根控件（一个 vbox），调用方拿到所有权；解析失败返回
 *        nullptr（理论上不会发生，内嵌 XML 是编译期常量、形态固定）。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildBuddyChatBody();

/**
 *  构造群聊对话框的客户区控件树。结构 = [单聊左半区] + [竖分隔条] +
 *  [群公告 / 群成员右侧栏]，左半区与单聊完全同构（同一份内嵌 XML）。
 *
 *  返回：客户区根控件（一个 hbox），调用方拿到所有权；解析失败返回
 *        nullptr。
 */
std::unique_ptr<balloonwjui::DuiControl> BuildGroupChatBody();

/**
 *  给聊天对话框套上 9-grid 皮肤背景，背景图为 Skins/Skin0/ChatDlgBg.png
 *  （以 exe 目录为基解析相对路径）。9-grid 内距依据该图的像素分带确定，
 *  让顶部渐变标题区 / 底部色条保持固定高度、中部正文区随窗口纵向拉伸。
 *
 *  须在窗口 Create 之后调用 —— LoadBgImageFromFile 会 Invalidate 宿主。
 *    host：聊天对话框自身（DuiFrameWindow 派生自 DuiHost），传 this；
 *          为 NULL 时直接返回。
 */
void ApplyChatWindowBackground(balloonwjui::DuiHost* host);

/**
 *  加载聊天对象（好友 / 群）的头像位图，供标题栏 DuiFrameWindow::SetIcon
 *  使用。头像 PNG 约定在 Image\UserThumbs\<faceId>.png（相对 exe 目录）。
 *
 *    faceId：头像编号（好友 / 群信息里的 m_nFace 字段）。
 *  返回：成功 → 新建的 HBITMAP，<u>调用方负责 DeleteObject</u>；
 *        文件缺失 / 解码失败 / GDI+ 未就绪 → NULL（调用方据此跳过设图标）。
 */
HBITMAP LoadChatAvatar(int faceId);

/**
 *  给客户区控件树里的按钮换肤，并给工具栏按钮注册悬停 tooltip：
 *   · 工具栏图标按钮 —— 贴真实图标 PNG、清空文字、注册 tooltip 提示；
 *   · 底部"关闭 / 发送 / ▾" —— 贴各自的按钮皮肤。
 *
 *  须在 DuiXmlBuilder 构建出控件树、且窗口已 Create 之后调用。
 *    root：           客户区根控件，遍历其整棵子树。NULL 安全。
 *    outTooltipCtrls：本次注册了 tooltip 的控件指针会追加到此 vector，
 *                     调用方须在控件树销毁前 / 对话框析构时把它们传给
 *                     UnregisterTooltips 取消注册（否则 DuiToolTipMgr
 *                     会留下悬空指针）。
 */
void ApplyToolbarButtonStyles(balloonwjui::DuiControl* root,
                              std::vector<balloonwjui::DuiControl*>& outTooltipCtrls);

/**
 *  从 DuiToolTipMgr 取消注册一批控件的 tooltip。
 *    ctrls：先前由 ApplyToolbarButtonStyles 填充的控件指针集合。
 *  在控件树被重建 / 对话框析构前调用，避免管理器持有悬空指针。
 */
void UnregisterTooltips(const std::vector<balloonwjui::DuiControl*>& ctrls);

} // namespace balloonchat
