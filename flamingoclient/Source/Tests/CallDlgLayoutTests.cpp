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
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不碰控件。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../Call/CallDlgLayout.h"

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
