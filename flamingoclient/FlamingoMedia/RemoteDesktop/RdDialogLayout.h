/**
 *  远程桌面会话提示对话框（CBalloonRemoteDesktopDlg）的提示文字与控件树，RdDialogLayout.h
 *
 *  把对话框里不依赖窗口句柄的部分抽出来，供 FlamingoUnit 直接建树、排版、断言：
 *    · MakeRdDialogText  —— 两种形态的提示文字，对端名字过长时截断；
 *    · BuildRdDialogBody —— 标题栏以下的控件树：上方提示区（可折行、左右留白、垂直居中），下方按钮栏；
 *    · ApplyRdDialogText —— 设置提示文字，并按提示区当前宽度量出折行后的高度，让文字在提示区里垂直居中。
 *  2026-10-10 之前提示文字是单行居中、没有左右留白，对端带备注名时「正在呼叫 赵卓轩（卓轩（桌面端）），等待对方接受…」
 *  两端都超出窗口被截掉。
 *
 *  典型用法（对话框里）：
 *      SetClientContent(rd::BuildRdDialogBody(m_mode));
 *      rd::ApplyRdDialogText(GetClientContent(), rd::MakeRdDialogText(m_mode, m_peerName));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *  balloonwj@qq.com   2026-10-10
 */
#pragma once

#include <memory>
#include <atlstr.h>   // CString

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace rd {

// ---- 固定尺寸（逻辑像素）----
const int kRdDlgWidth         = 340;   // 整窗宽
const int kRdDlgContentHeight = 128;   // 标题栏以下的内容区高；整窗高 = 它 + kRdDlgTitleBarH
const int kRdDlgTitleBarH     = 44;    // 标题栏高
const int kRdMsgPadH          = 20;    // 提示区左右内边距，提示文字不贴窗口边缘
const int kRdBottomBarH       = 34;    // 底部按钮栏高
const int kRdBottomPad        = 16;    // 按钮栏下方到窗口下缘的留白
const int kRdBtnWidth         = 88;    // 按钮宽
const int kRdBtnGap           = 12;    // 「拒绝」「接受」两按钮的间距

// 提示文字里对端名字最多显示的字符数；超出部分截掉并加「…」，保证提示文字在提示区里最多占三行
const int kRdMaxPeerNameChars = 20;

// ---- 控件 ctrlId（避开 DuiFrameWindow 保留的 1 / 2 / 3）----
const UINT kRdIdBtnLeft  = 10;   // 次按钮：授权形态的「拒绝」；呼叫形态没有
const UINT kRdIdBtnRight = 11;   // 主按钮：授权形态的「接受」，呼叫形态的「取消」
const UINT kRdIdMsgBox   = 12;   // 提示区（DuiVBox）：提示文字上下各一块等权重的空白，把文字夹在垂直正中
const UINT kRdIdMsg      = 13;   // 提示文字（折行的 DuiLabel）

// 对话框形态，取值与 CBalloonRemoteDesktopDlg::Mode 一一对应
enum RdDialogMode
{
    kRdModeCalling = 0,   // 发起方：已发出邀请，正在等待对方接受
    kRdModeAuth    = 1,   // 被控端：收到邀请，等待本人授权
};

/**
 *  对端名字超过 kRdMaxPeerNameChars 个字符时截断，末尾加「…」。
 *    name：对端显示名，可能带备注，如「赵卓轩（卓轩（桌面端））」。
 *  @return 不超过 kRdMaxPeerNameChars 个字符的名字（截断时含末尾的「…」）；空名字原样返回
 */
CString ShortenRdPeerName(const CString& name);

/**
 *  组装提示文字。
 *    mode：    对话框形态，取 RdDialogMode 之一；
 *    peerName：对端显示名；为空时用「对方」代替。
 *  @return 呼叫形态为「正在呼叫 XXX，等待对方接受…」，授权形态为「XXX 请求查看你的桌面。接受后对方只能查看，
 *          要控制你的电脑还需你另外同意」（2026-10-12 前为「XXX 请求远程控制你的电脑」）
 */
CString MakeRdDialogText(int mode, const CString& peerName);

/**
 *  建标题栏以下的控件树：提示区（kRdIdMsgBox，内含提示文字 kRdIdMsg）在上、按钮栏在下。
 *  呼叫形态只有「取消」（kRdIdBtnRight）；授权形态有「拒绝」（kRdIdBtnLeft）与「接受」（kRdIdBtnRight）。
 *  提示文字为空，由 ApplyRdDialogText 填入。
 *    mode：对话框形态，取 RdDialogMode 之一。
 *  @return 控件树根，所有权交给调用方
 */
std::unique_ptr<balloonwjui::DuiControl> BuildRdDialogBody(int mode);

/**
 *  把 text 设为提示文字，按提示区当前的内容宽度量出折行后的高度（不超过提示区高度），重设文字的高度并重排提示区，
 *  文字因此在提示区里垂直居中。须在控件树排过版（提示区已有宽度）之后调用；提示区宽度为 0 时只设文字。
 *    root：BuildRdDialogBody 建出的控件树根（借用）；为 NULL 或找不到提示区时什么都不做；
 *    text：提示文字，一般取自 MakeRdDialogText。
 */
void ApplyRdDialogText(balloonwjui::DuiControl* root, const CString& text);

} // namespace rd
