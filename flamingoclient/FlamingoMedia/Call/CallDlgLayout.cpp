/**
 *  通话对话框文字与画面去向纯逻辑的实现，CallDlgLayout.cpp
 *
 *  全部为纯函数（只做字符串拼接与布尔判定，不碰窗口 / 控件 / GDI），与 FlamingoUnit
 *  测试工程共用、不 include stdafx.h，工程里 PCH 设 NotUsing。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#include "CallDlgLayout.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace call {

//对端昵称缺失（未取到 / 空串）时提示文字里的兜底称呼，避免出现"正在呼叫 ，等待…"的空档。
static const TCHAR* const kUnknownPeerName = TR_NOOP(L"对方");

//通话类型文字：分别用于标题栏与来电提示，两处措辞必须一致，故在此统一。
static const TCHAR* const kVideoCallText = TR_NOOP(L"视频通话");
static const TCHAR* const kAudioCallText = TR_NOOP(L"语音通话");

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
    CString kind = videoCall ? TR(kVideoCallText) : TR(kAudioCallText);

    //呼叫中 / 来电态尚未接通，标题只标明通话类型；昵称取不到时同样退回通话类型。
    if (mode != kCallModeConnected || peerName.IsEmpty())
        return kind;

    CString title;
    title.Format(TR(L"与 %s %s中"), (LPCTSTR)peerName, (LPCTSTR)kind);
    return title;
}

CString MakeCallPromptText(int mode, const CString& peerName, bool videoCall)
{
    CString name = peerName.IsEmpty() ? CString(TR(kUnknownPeerName)) : peerName;
    CString text;

    switch (mode)
    {
    case kCallModeIncoming:
        //来电态要让被叫在接听前就知道对方约的是视频还是语音（视频会开摄像头）
        text.Format(TR(L"%s 邀请你%s"), (LPCTSTR)name,
                    videoCall ? TR(kVideoCallText) : TR(kAudioCallText));
        break;
    case kCallModeConnected:
        //通话中态只有纯语音布局用得上这行文字（视频布局是满屏画面，没有提示文字）
        text.Format(TR(L"正在与 %s 通话中…"), (LPCTSTR)name);
        break;
    case kCallModeCalling:
    default:
        text.Format(TR(L"正在呼叫 %s，等待对方接听…"), (LPCTSTR)name);
        break;
    }

    return text;
}

bool IsRemoteVideoStale(int64_t nowMs, int64_t lastFrameMs, int64_t staleMs)
{
    if (lastFrameMs <= 0)
        return false;
    return nowMs - lastFrameMs > staleMs;
}

CString MakeIncomingSubText(bool videoCall)
{
    return videoCall ? CString(TR(L"邀请你视频通话")) : CString(TR(L"邀请你语音通话"));
}

CString MakeOutgoingStatusText()
{
    return TR(L"正在呼叫，等待对方接听…");
}

CString MakeCameraUnavailableTip(int reason)
{
    //第二句统一说明后果，让用户知道通话本身没断、只是自己的画面发不出去
    static const TCHAR* const kConsequence = TR(L"\n对方看不到你的画面");

    CString text;
    switch (reason)
    {
    case kCameraFailNoDevice:
        //机器上没有摄像头，或者设备被禁用 / 系统隐私设置不允许访问
        text = TR(L"未检测到摄像头");
        break;
    case kCameraFailOpen:
        //枚举得到设备却打不开：本机双开测试时最常见，摄像头已被先启动的那个进程占住
        text = TR(L"摄像头打不开，可能已被其它程序占用");
        break;
    case kCameraFailStream:
        //通话中途被别的程序抢走，或者摄像头被拔掉
        text = TR(L"摄像头已断开，可能被其它程序占用");
        break;
    default:
        text = TR(L"摄像头不可用");
        break;
    }

    text += kConsequence;
    return text;
}

bool ShouldEndCallAfterRelayLoss(int mode, const std::string& sessionId)
{
    return mode == kCallModeConnected && !sessionId.empty();
}

int PickUserCloseSignal(int mode, const std::string& sessionId)
{
    if (sessionId.empty())
        return kCallCloseSignalNone;
    switch (mode)
    {
    //呼叫中：对方还没接听，主叫取消
    case kCallModeCalling:
        return kCallCloseSignalCancel;
    //来电中：被叫拒绝
    case kCallModeIncoming:
        return kCallCloseSignalReject;
    //通话中与未知形态：挂断（与原先标题栏关闭按钮的处理一致）
    default:
        return kCallCloseSignalHangup;
    }
}

POINT ComputeCenteredOrigin(const RECT& work, const SIZE& size)
{
    POINT p;
    p.x = work.left + ((work.right - work.left) - size.cx) / 2;
    p.y = work.top + ((work.bottom - work.top) - size.cy) / 2;
    //窗口比工作区大时贴工作区的左 / 上边：宁可右下角超出，也要保证标题栏与关闭按钮可见
    if (p.x < work.left)
        p.x = work.left;
    if (p.y < work.top)
        p.y = work.top;
    return p;
}

VoiceArtLayout ComputeVoiceArtLayout(const RECT& area)
{
    const int areaW = area.right - area.left;
    const int areaH = area.bottom - area.top;
    //圆底直径：不超过 kVoiceArtCircle，也不超过扣掉文字行、时长行与间距后的剩余高度和区域宽度
    int d = kVoiceArtCircle;
    if (d > areaH - kVoiceArtGap - kVoiceArtTextH - kVoiceArtSubTextH)
        d = areaH - kVoiceArtGap - kVoiceArtTextH - kVoiceArtSubTextH;
    if (d > areaW)
        d = areaW;
    if (d < 0)
        d = 0;
    //圆底、文字、时长作为一组在区域里竖直居中
    const int groupH = d + kVoiceArtGap + kVoiceArtTextH + kVoiceArtSubTextH;
    int top = area.top + (areaH - groupH) / 2;
    if (top < area.top)
        top = area.top;

    VoiceArtLayout l;
    l.m_circle.left = area.left + (areaW - d) / 2;
    l.m_circle.top = top;
    l.m_circle.right = l.m_circle.left + d;
    l.m_circle.bottom = top + d;
    l.m_text.left = area.left;
    l.m_text.right = area.right;
    l.m_text.top = l.m_circle.bottom + kVoiceArtGap;
    l.m_text.bottom = l.m_text.top + kVoiceArtTextH;
    l.m_subText.left = area.left;
    l.m_subText.right = area.right;
    l.m_subText.top = l.m_text.bottom;
    l.m_subText.bottom = l.m_subText.top + kVoiceArtSubTextH;
    return l;
}

VoiceArtLayout ComputeAvatarTipLayout(const RECT& area)
{
    const int areaW = area.right - area.left;
    const int areaH = area.bottom - area.top;
    //头像直径：不超过 kVoiceArtCircle，也不超过扣掉提示行与间距后的剩余高度和区域宽度
    int d = kVoiceArtCircle;
    if (d > areaH - kAvatarTipGap - kVoiceArtTextH)
        d = areaH - kAvatarTipGap - kVoiceArtTextH;
    if (d > areaW)
        d = areaW;
    if (d < 0)
        d = 0;
    //头像与提示作为一组在区域里竖直居中
    const int groupH = d + kAvatarTipGap + kVoiceArtTextH;
    int top = area.top + (areaH - groupH) / 2;
    if (top < area.top)
        top = area.top;

    VoiceArtLayout l;
    l.m_circle.left = area.left + (areaW - d) / 2;
    l.m_circle.top = top;
    l.m_circle.right = l.m_circle.left + d;
    l.m_circle.bottom = top + d;
    l.m_text.left = area.left;
    l.m_text.right = area.right;
    l.m_text.top = l.m_circle.bottom + kAvatarTipGap;
    l.m_text.bottom = l.m_text.top + kVoiceArtTextH;
    l.m_subText.left = area.left;
    l.m_subText.right = area.right;
    l.m_subText.top = l.m_text.bottom;
    l.m_subText.bottom = l.m_text.bottom;
    return l;
}

CString MakeCallInterruptedTip(const CString& durationText)
{
    CString tip = TR(L"通话已中断");
    if (!durationText.IsEmpty())
    {
        tip += _T(" ");
        tip += durationText;
    }
    return tip;
}

} // namespace call
