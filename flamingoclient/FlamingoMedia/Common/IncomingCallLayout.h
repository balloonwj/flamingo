/**
 *  右下角来电弹窗的共用版式（docs/音视频通话优化方案.md 第 1 项），IncomingCallLayout.h
 *
 *  一对一来电（CBalloonCallDlg 的来电态）与群通话来电（CGroupCallIncomingDlg）统一为 420×200（整窗，含标题栏）：
 *  左边 56 像素的圆形头像（一对一放对方头像，群通话放群头像），右边两行文字（第一行深色加粗、第二行灰色），底部右侧
 *  「拒绝」「接听」两个按钮（2026-10-05 起为红、绿圆形图标按钮）。本文件只负责搭控件树，窗口尺寸、标题栏、倒计时、
 *  按钮事件仍由各自的窗口处理。
 *  布局单测见 Source/Tests/CallUxTests.cpp 的 ICL1（按设计尺寸排得下：按钮不被压扁、不越出窗口）。
 *
 *  主叫方的呼叫中态不用本文件：它与接通后是同一个窗口形态，控件树见 Call/CallingLayout.h。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      flamingomedia::IncomingCallBody body = flamingomedia::BuildIncomingCallBody(
 *          m_avatars.MakeUserAvatar(peerId, flamingomedia::kIncomingAvatarPx), name, TR(L"邀请你语音通话"), kIdDecline, kIdAccept,
 *          m_buttonArt);
 *      SetTitleBarHeight(flamingomedia::kIncomingTitleBarH);
 *      ResizeClient(flamingomedia::kIncomingDlgW, flamingomedia::kIncomingDlgH);
 *      SetClientContent(std::move(body.m_root));
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <memory>
#include <string>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"

namespace balloonwjui {
class DuiAvatar;
class DuiLabel;
}

namespace call {
class CallButtonArtCache;
}

namespace flamingomedia {

//来电弹窗的整窗尺寸（含标题栏，逻辑像素）：DuiFrameWindow 的 ResizeClient 设的就是整窗（开发约定文档「UI 约定」第 7 条）
const int kIncomingDlgW = 420;
const int kIncomingDlgH = 200;
//来电弹窗的标题栏高（逻辑像素）
const int kIncomingTitleBarH = 32;
//左侧头像边长（逻辑像素）
const int kIncomingAvatarPx = 56;

//BuildIncomingCallBody 的结果：控件树的根与几个之后还要改的控件（借用，归控件树所有）
struct IncomingCallBody
{
    std::unique_ptr<balloonwjui::DuiControl> m_root;   //客户区内容的根，交给 SetClientContent
    balloonwjui::DuiAvatar* m_avatar;                  //左侧头像；头像更新时就地改
    balloonwjui::DuiLabel*  m_line1;                   //第一行（深色加粗）
    balloonwjui::DuiLabel*  m_line2;                   //第二行（灰色）；群通话里人数变化时就地改

    IncomingCallBody();
};

/**
 *  搭来电弹窗的客户区控件树。
 *  @param avatar    左侧头像控件，所有权交给控件树；为空时放一个同样大小的空白
 *  @param line1     第一行文字（一对一为对方的名字，群通话为「某某 邀请你加入群语音通话」）
 *  @param line2     第二行文字（一对一为「邀请你语音通话」，群通话为「群名 · N 人正在通话」）
 *  @param declineId 「拒绝」按钮的 ctrlId
 *  @param acceptId  「接听」按钮的 ctrlId
 *  @param art       图标按钮的位图缓存（借用；须比返回的控件树活得久，一般是来电窗的成员）。2026-10-05 起「拒绝」「接听」
 *                   是红、绿两个圆形图标按钮（call::CallButtonArt），文字改作悬停提示
 *  @return 控件树与几个控件的指针
 */
IncomingCallBody BuildIncomingCallBody(std::unique_ptr<balloonwjui::DuiAvatar> avatar, const std::wstring& line1,
                                       const std::wstring& line2, UINT declineId, UINT acceptId,
                                       call::CallButtonArtCache& art);

} // namespace flamingomedia
