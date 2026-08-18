/**
 *  通话对话框文字与画面去向纯逻辑的实现，CallDlgLayout.cpp
 *
 *  全部为纯函数（只做字符串拼接与布尔判定，不碰窗口 / 控件 / GDI），与 FlamingoUnit
 *  测试工程共用、不 include stdafx.h，工程里 PCH 设 NotUsing。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#include "CallDlgLayout.h"

namespace call {

//对端昵称缺失（未取到 / 空串）时提示文字里的兜底称呼，避免出现"正在呼叫 ，等待…"的空档。
static const TCHAR* const kUnknownPeerName = _T("对方");

//通话类型文字：分别用于标题栏与来电提示，两处措辞必须一致，故在此统一。
static const TCHAR* const kVideoCallText = _T("视频通话");
static const TCHAR* const kAudioCallText = _T("语音通话");

CallCanvasTarget PickCanvasForFrame(bool localIsBig, bool frameIsLocal)
{
    //谁占大画面谁的帧就进大画面：本端占大画面时本端帧进大画面、远端帧进小窗；
    //默认（对方占大画面）时正好相反。两个布尔相等即"这一路正占着大画面"。
    if (frameIsLocal == localIsBig)
        return kCallCanvasBig;

    return kCallCanvasPip;
}

CString MakeCallWindowTitle(int mode, const CString& peerName, bool videoCall)
{
    CString kind = videoCall ? kVideoCallText : kAudioCallText;

    //呼叫中 / 来电态尚未接通，标题只标明通话类型；昵称取不到时同样退回通话类型。
    if (mode != kCallModeConnected || peerName.IsEmpty())
        return kind;

    CString title;
    title.Format(_T("与 %s %s中"), (LPCTSTR)peerName, (LPCTSTR)kind);
    return title;
}

CString MakeCallPromptText(int mode, const CString& peerName, bool videoCall)
{
    CString name = peerName.IsEmpty() ? CString(kUnknownPeerName) : peerName;
    CString text;

    switch (mode)
    {
    case kCallModeIncoming:
        //来电态要让被叫在接听前就知道对方约的是视频还是语音（视频会开摄像头）
        text.Format(_T("%s 邀请你%s"), (LPCTSTR)name,
                    videoCall ? kVideoCallText : kAudioCallText);
        break;
    case kCallModeConnected:
        //通话中态只有纯语音布局用得上这行文字（视频布局是满屏画面，没有提示文字）
        text.Format(_T("正在与 %s 通话中…"), (LPCTSTR)name);
        break;
    case kCallModeCalling:
    default:
        text.Format(_T("正在呼叫 %s，等待对方接听…"), (LPCTSTR)name);
        break;
    }

    return text;
}

CString MakeCameraUnavailableTip(int reason)
{
    //第二句统一说明后果，让用户知道通话本身没断、只是自己的画面发不出去
    static const TCHAR* const kConsequence = _T("\n对方看不到你的画面");

    CString text;
    switch (reason)
    {
    case kCameraFailNoDevice:
        //机器上没有摄像头，或者设备被禁用 / 系统隐私设置不允许访问
        text = _T("未检测到摄像头");
        break;
    case kCameraFailOpen:
        //枚举得到设备却打不开：本机双开测试时最常见，摄像头已被先启动的那个进程占住
        text = _T("摄像头打不开，可能已被其它程序占用");
        break;
    case kCameraFailStream:
        //通话中途被别的程序抢走，或者摄像头被拔掉
        text = _T("摄像头已断开，可能被其它程序占用");
        break;
    default:
        text = _T("摄像头不可用");
        break;
    }

    text += kConsequence;
    return text;
}

} // namespace call
