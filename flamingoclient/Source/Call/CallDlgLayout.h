/**
 *  通话对话框的文字与画面去向纯逻辑，CallDlgLayout.h
 *
 *  把音视频通话对话框（CBalloonCallDlg）里三处"只依赖当前形态 + 对端昵称 + 是否视频通话"
 *  的判定抽成不依赖 HWND / 控件的纯函数，便于 FlamingoUnit 测试工程直接断言：
 *    · MakeCallWindowTitle —— 标题栏文字。通话中要写明"在跟谁通话"（如"与 张勇 视频通话中"），
 *      呼叫中 / 来电态还没接通，只标明通话类型。
 *    · MakeCallPromptText  —— 客户区提示文字（呼叫中 / 来电 / 通话中三种措辞；来电态区分
 *      "邀请你视频通话" / "邀请你语音通话"）。
 *    · PickCanvasForFrame  —— 视频通话时一路画面（本端采集 / 远端解码）该画到大画面控件还是
 *      小画面（画中画）控件上。用户可以把自己和对方对调显示，故去向随"本端是否占大画面"翻转。
 *
 *  带窗口副作用的部分（建控件树、SetTitle、窗口置顶、重绘）仍留在 CBalloonCallDlg 里。
 *
 *  典型用法：
 *      SetTitle(call::MakeCallWindowTitle(m_mode, m_peerName, m_videoCall));
 *      m_msgLabel->SetText(call::MakeCallPromptText(m_mode, m_peerName, m_videoCall));
 *      if (call::PickCanvasForFrame(m_localIsBig, true) == call::kCallCanvasBig) ...
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <atlstr.h>          //CString（与 FlamingoUnit 测试工程共用，故不 include stdafx.h）
#include "MfCameraCapture.h" //CameraFailReason（摄像头失败原因，只用其枚举；该头仅依赖 stdint）

namespace call {

//通话对话框形态。取值与 CBalloonCallDlg::Mode 一一对应，抽到本模块是为了让纯函数不依赖对话框类。
enum CallDlgMode
{
    kCallModeCalling   = 0,   //主叫：已发出呼叫，正在等待对方接听
    kCallModeIncoming  = 1,   //被叫：收到来电，等待本人点接听 / 拒绝
    kCallModeConnected = 2,   //通话中：双方已接通，媒体通道已建立
};

//视频通话时一路画面的去向：窗口上半部铺满的大画面，还是底部栏左下角的小画面（画中画）。
enum CallCanvasTarget
{
    kCallCanvasBig = 0,   //大画面：视频通话窗上半部铺满的那块
    kCallCanvasPip = 1,   //小画面：底部栏左下角的画中画小窗
};

/**
 *  判定一路视频帧该画到大画面还是小画面控件上。
 *
 *  规则：谁占大画面，谁的帧就进大画面，另一路进小画面。默认对方占大画面（localIsBig=false），
 *  用户点小窗对调后本端占大画面（localIsBig=true）。
 *
 *  @param localIsBig   当前是否本端（自己的摄像头画面）占大画面；false=对方占大画面
 *  @param frameIsLocal 本帧是否本端采集帧；false=远端解码帧
 *  @return 本帧应画到的画面控件（kCallCanvasBig / kCallCanvasPip）
 */
CallCanvasTarget PickCanvasForFrame(bool localIsBig, bool frameIsLocal);

/**
 *  组装通话窗标题栏文字。
 *
 *  通话中（mode=kCallModeConnected）且拿得到对端昵称时写成"与 XXX 视频通话中" /
 *  "与 XXX 语音通话中"，让用户一眼看出在跟谁通话；呼叫中 / 来电态，或昵称为空时，
 *  退回"视频通话" / "语音通话"。
 *
 *  @param mode      当前对话框形态，取 CallDlgMode 之一
 *  @param peerName  对端昵称；空串表示未取到
 *  @param videoCall 本次通话是否含视频；true=视频通话，false=纯语音
 *  @return 标题栏文字（非空）
 */
CString MakeCallWindowTitle(int mode, const CString& peerName, bool videoCall);

/**
 *  组装客户区提示文字（纯语音布局与呼叫 / 来电对话框用；视频通话中态没有这行文字）。
 *
 *  @param mode      当前对话框形态，取 CallDlgMode 之一
 *  @param peerName  对端昵称；空串时用"对方"兜底，避免出现"正在呼叫 ，等待…"这种空档
 *  @param videoCall 本次通话是否含视频：来电态据此区分"邀请你视频通话" / "邀请你语音通话"
 *  @return 提示文字（非空）
 */
CString MakeCallPromptText(int mode, const CString& peerName, bool videoCall);

/**
 *  组装"本端摄像头不可用"的提示文字，显示在视频通话窗里本端那一路画面上（代替一块纯黑）。
 *  分两句：第一句说清是什么毛病（没检测到设备 / 打不开 / 中途断了），第二句说清后果
 *  （对方看不到你），中间用 '\n' 分行。
 *
 *  @param reason 失败原因，取 call::CameraFailReason；未知取值按"摄像头不可用"兜底
 *  @return 提示文字（非空，含一个换行）
 */
CString MakeCameraUnavailableTip(int reason);

} // namespace call
