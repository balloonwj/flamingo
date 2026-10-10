/**
 *  CallDlgLayoutTests.cpp
 *
 *  通话对话框文字与画面去向纯逻辑（CallDlgLayout）的测试：
 *    · PickCanvasForFrame —— 默认（对方占大画面）时远端帧进大画面、本端帧进小窗；对调后翻转。
 *    · MakeCallWindowTitle —— 通话中态标题写明"与 XXX 视频 / 语音通话中"；呼叫中 / 来电态
 *      只标通话类型；昵称为空时退回通话类型，不出现"与  视频通话中"这种空档。
 *    · MakeCallPromptText —— 呼叫中 / 来电 / 通话中三种措辞，来电态区分视频 / 语音，
 *      昵称为空时用"对方"兜底。
 *    · MakeCameraUnavailableTip —— 摄像头不可用时贴在本端画面上的提示：没检测到设备 / 打不开
 *      （多为被占用）/ 中途断流三种原因措辞不同，且都要带上"对方看不到你的画面"这句后果。
 *    · ShouldEndCallAfterRelayLoss / MakeCallInterruptedTip —— 通话中中转意外断开：只有通话中态且
 *      会话 id 已知时才在等待 kRelayLossGraceMs 后由本端自行结束；结束时聊天窗提示「通话已中断」，接通过就带上时长。
 *    · PickUserCloseSignal —— 用户主动关窗（三个按钮与标题栏关闭按钮）时按形态发取消 / 拒绝 / 挂断，没有会话 id 时不发；
 *      四条关窗路径统一经 CBalloonCallDlg::CloseByUser 收尾（请求停媒体会话）。
 *    · ComputeCenteredOrigin —— 来电在右下角接听后，通话窗移到该显示器工作区正中（第 10 期 10a）：正常居中、
 *      副屏坐标为负、窗口比工作区大时贴工作区左上角。
 *    · ComputeVoiceArtLayout —— 语音态画面区的大号听筒图标、「语音通话中」与通话时长（第 10 期 10b、第 11 期 11b）：
 *      区域够大时圆底直径 96、三者作为一组在区域里居中；区域矮时圆底按剩余高度缩小；区域有偏移时整体随之平移。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不碰控件。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Call/CallDlgLayout.h"

using namespace call;

TEST_CASE("PickCanvasForFrame：默认对方占大画面时，远端帧进大画面、本端帧进小窗", "[calldlg]")
{
    //localIsBig=false 即通话刚接通的默认状态
    REQUIRE(PickCanvasForFrame(false, false) == kCallCanvasBig);   //远端帧 → 大画面
    REQUIRE(PickCanvasForFrame(false, true)  == kCallCanvasPip);   //本端帧 → 小窗
}

TEST_CASE("PickCanvasForFrame：对调后本端占大画面，两路去向整体翻转", "[calldlg]")
{
    //用户点小窗 / 双击大画面对调后 localIsBig=true
    REQUIRE(PickCanvasForFrame(true, true)  == kCallCanvasBig);    //本端帧 → 大画面
    REQUIRE(PickCanvasForFrame(true, false) == kCallCanvasPip);    //远端帧 → 小窗
}

TEST_CASE("MakeCallWindowTitle：通话中态写明在跟谁通话", "[calldlg]")
{
    REQUIRE(MakeCallWindowTitle(kCallModeConnected, _T("张勇"), true)
            == CString(_T("与 张勇 视频通话中")));
    REQUIRE(MakeCallWindowTitle(kCallModeConnected, _T("张勇"), false)
            == CString(_T("与 张勇 语音通话中")));
}

TEST_CASE("MakeCallWindowTitle：呼叫中 / 来电态只标通话类型", "[calldlg]")
{
    REQUIRE(MakeCallWindowTitle(kCallModeCalling, _T("张勇"), true)
            == CString(_T("视频通话")));
    REQUIRE(MakeCallWindowTitle(kCallModeIncoming, _T("张勇"), false)
            == CString(_T("语音通话")));
}

TEST_CASE("MakeCallWindowTitle：昵称为空时退回通话类型，不留空档", "[calldlg]")
{
    REQUIRE(MakeCallWindowTitle(kCallModeConnected, _T(""), true)
            == CString(_T("视频通话")));
    REQUIRE(MakeCallWindowTitle(kCallModeConnected, _T(""), false)
            == CString(_T("语音通话")));
}

TEST_CASE("MakeCallPromptText：来电态区分视频与语音", "[calldlg]")
{
    REQUIRE(MakeCallPromptText(kCallModeIncoming, _T("张勇"), true)
            == CString(_T("张勇 邀请你视频通话")));
    REQUIRE(MakeCallPromptText(kCallModeIncoming, _T("张勇"), false)
            == CString(_T("张勇 邀请你语音通话")));
}

TEST_CASE("MakeCallPromptText：呼叫中与通话中的措辞", "[calldlg]")
{
    REQUIRE(MakeCallPromptText(kCallModeCalling, _T("韩晓燕888"), true)
            == CString(_T("正在呼叫 韩晓燕888，等待对方接听…")));
    REQUIRE(MakeCallPromptText(kCallModeConnected, _T("韩晓燕888"), false)
            == CString(_T("正在与 韩晓燕888 通话中…")));
}

TEST_CASE("MakeCallPromptText：昵称为空时用「对方」兜底", "[calldlg]")
{
    REQUIRE(MakeCallPromptText(kCallModeCalling, _T(""), false)
            == CString(_T("正在呼叫 对方，等待对方接听…")));
    REQUIRE(MakeCallPromptText(kCallModeIncoming, _T(""), true)
            == CString(_T("对方 邀请你视频通话")));
}

TEST_CASE("MakeCameraUnavailableTip：三种失败原因措辞不同", "[calldlg]")
{
    //机器上没有摄像头（没接 / 被禁用 / 隐私设置不允许）
    REQUIRE(MakeCameraUnavailableTip(kCameraFailNoDevice)
            == CString(_T("未检测到摄像头\n对方看不到你的画面")));
    //有设备但打不开：本机双开测试时最常见，设备被先启动的那个进程占住
    REQUIRE(MakeCameraUnavailableTip(kCameraFailOpen)
            == CString(_T("摄像头打不开，可能已被其它程序占用\n对方看不到你的画面")));
    //通话中途被别的程序抢走 / 设备被拔掉
    REQUIRE(MakeCameraUnavailableTip(kCameraFailStream)
            == CString(_T("摄像头已断开，可能被其它程序占用\n对方看不到你的画面")));
}

TEST_CASE("MakeCameraUnavailableTip：未知原因有兜底措辞，且始终带上后果说明", "[calldlg]")
{
    CString fallback = MakeCameraUnavailableTip(kCameraFailNone);
    REQUIRE(fallback == CString(_T("摄像头不可用\n对方看不到你的画面")));
    //传进来一个没定义过的取值也不能返回空串（界面上要么有话说，要么就别提示）
    REQUIRE(MakeCameraUnavailableTip(9999) == fallback);
}

TEST_CASE("ShouldEndCallAfterRelayLoss：只有通话中态且会话 id 已知时才自行结束", "[calldlg]")
{
    REQUIRE(ShouldEndCallAfterRelayLoss(kCallModeConnected, "5010-5001-1-1"));
    //会话 id 未知（理论上接通后一定有）：不发挂断，也就无从自行结束
    REQUIRE_FALSE(ShouldEndCallAfterRelayLoss(kCallModeConnected, ""));
    //呼叫中 / 来电态还没建中转，不会收到中转断开；即便收到也不处理
    REQUIRE_FALSE(ShouldEndCallAfterRelayLoss(kCallModeCalling, "5010-5001-1-1"));
    REQUIRE_FALSE(ShouldEndCallAfterRelayLoss(kCallModeIncoming, "5010-5001-1-1"));
    //等待正常挂断推送的时长为 5 秒（与用户 2026-10-03 确认的方案一致）
    REQUIRE(kRelayLossGraceMs == 5000u);
}

TEST_CASE("MakeCallInterruptedTip：提示「通话已中断」，接通过就带上时长", "[calldlg]")
{
    REQUIRE(MakeCallInterruptedTip(CString()) == CString(_T("通话已中断")));
    REQUIRE(MakeCallInterruptedTip(CString(_T("05:23"))) == CString(_T("通话已中断 05:23")));
}

TEST_CASE("PickUserCloseSignal：用户关窗按形态发取消 / 拒绝 / 挂断，没有会话 id 时不发", "[calldlg]")
{
    const std::string sessionId = "5127-5308-1791107856000-4";
    REQUIRE(PickUserCloseSignal(kCallModeCalling, sessionId) == kCallCloseSignalCancel);
    REQUIRE(PickUserCloseSignal(kCallModeIncoming, sessionId) == kCallCloseSignalReject);
    //通话中点「挂断」按钮与点标题栏关闭按钮是同一个判定（2026-10-04 联调查出按钮一路曾漏掉停媒体会话）
    REQUIRE(PickUserCloseSignal(kCallModeConnected, sessionId) == kCallCloseSignalHangup);
    //未知形态按通话中处理，与原先标题栏关闭按钮的处理一致
    REQUIRE(PickUserCloseSignal(9999, sessionId) == kCallCloseSignalHangup);
    //呼叫刚发出、服务端还没应答时没有会话 id：哪种形态都不发信令
    REQUIRE(PickUserCloseSignal(kCallModeCalling, "") == kCallCloseSignalNone);
    REQUIRE(PickUserCloseSignal(kCallModeConnected, "") == kCallCloseSignalNone);
}

TEST_CASE("ComputeCenteredOrigin：窗口放在工作区正中，放不下时贴工作区左上角", "[calldlg]")
{
    //主屏 1920×1080，任务栏占底部 40
    const RECT work = { 0, 0, 1920, 1040 };
    const SIZE size = { 720, 569 };
    POINT p = ComputeCenteredOrigin(work, size);
    REQUIRE(p.x == (1920 - 720) / 2);
    REQUIRE(p.y == (1040 - 569) / 2);

    //左侧副屏，坐标为负
    const RECT left = { -1280, 100, 0, 1100 };
    p = ComputeCenteredOrigin(left, size);
    REQUIRE(p.x == -1280 + (1280 - 720) / 2);
    REQUIRE(p.y == 100 + (1000 - 569) / 2);

    //窗口比工作区大：左上角对齐工作区，保证标题栏可见
    const RECT tiny = { 10, 20, 610, 420 };
    p = ComputeCenteredOrigin(tiny, size);
    REQUIRE(p.x == 10);
    REQUIRE(p.y == 20);
}

TEST_CASE("ComputeVoiceArtLayout：圆形图标、文字、时长三行作为一组居中，区域矮时图标缩小", "[calldlg]")
{
    //通话窗默认的画面区 720×405：圆底 96，组高 96 + 12 + 24 + 20 = 152，竖直居中
    const RECT area = { 0, 0, 720, 405 };
    VoiceArtLayout l = ComputeVoiceArtLayout(area);
    REQUIRE(l.m_circle.left == 312);
    REQUIRE(l.m_circle.top == 126);
    REQUIRE(l.m_circle.right == 408);
    REQUIRE(l.m_circle.bottom == 222);
    REQUIRE(l.m_text.left == 0);
    REQUIRE(l.m_text.right == 720);
    REQUIRE(l.m_text.top == 234);
    REQUIRE(l.m_text.bottom == 258);
    //时长在文字下面紧接一行（第 11 期 11b：通话时长从标题栏挪到窗口里）
    REQUIRE(l.m_subText.left == 0);
    REQUIRE(l.m_subText.right == 720);
    REQUIRE(l.m_subText.top == 258);
    REQUIRE(l.m_subText.bottom == 278);

    //只有 100 高：圆底缩到 100 - 12 - 24 - 20 = 44，组贴顶
    const RECT shortArea = { 0, 0, 300, 100 };
    l = ComputeVoiceArtLayout(shortArea);
    REQUIRE(l.m_circle.right - l.m_circle.left == 44);
    REQUIRE(l.m_circle.bottom - l.m_circle.top == 44);
    REQUIRE(l.m_circle.left == 128);
    REQUIRE(l.m_circle.top == 0);
    REQUIRE(l.m_text.top == 56);
    REQUIRE(l.m_subText.bottom == 100);

    //区域有偏移：整体平移
    const RECT moved = { 10, 20, 730, 425 };
    l = ComputeVoiceArtLayout(moved);
    REQUIRE(l.m_circle.left == 322);
    REQUIRE(l.m_circle.top == 146);
}
