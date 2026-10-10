/**
 *  通话窗圆形图标按钮的外观与位图，说明见 CallButtonArt.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "CallButtonArt.h"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <string.h>

#include "../../Source/UI/BalloonChatWnd/ChatIcons.h"   //麦克风、喇叭、摄像机、听筒、共享屏幕的线性图标
#include "../../Source/Common/I18n/I18n.h"              //TR：提示文字的多语言查找
#include "CallSystemAudioSender.h"                      //CallSystemAudioResult：打开共享电脑声音失败的原因
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   //DuiToolTipMgr

namespace call {

namespace {

//图标边长占按钮直径的比例：48 像素的按钮画 22 像素的图标，与主流通话界面的观感相当
const float kIconRatio = 0.46f;
//听筒图标的比例：ChatIcons 的听筒在 24 格画布里留白较多，同样比例下比麦克风、摄像机小一圈，单独放大
const float kPhoneIconRatio = 0.56f;
//图标描边宽度（以图标 viewBox 24 为基准，与聊天窗工具栏一致）
const float kIconStroke = 2.0f;
//挂断听筒的顺时针旋转角度（度）：ChatIcons 的听筒斜放、弧背朝左上，转 45° 后听筒水平、弧背朝上、两端朝下，
//即「放下的听筒」，是挂断按钮的通用画法
const float kHangupTiltDegrees = 45.0f;
//斜杠下面先垫一道底色描边，把斜杠与图标线条隔开，粗细是斜杠本身的这个倍数
const float kSlashGapRatio = 2.2f;
//斜杠占图标区域的比例：从左上到右下，两端各缩进图标边长的这个比例
const float kSlashInsetRatio = 0.08f;
//斜杠的描边宽度（像素，按 48 像素按钮定；更大的按钮按直径等比放大）
const float kSlashStrokePx = 2.0f;
//斜杠描边宽度换算的基准直径（像素）
const float kSlashBaseDiameter = 48.0f;
//圆形向内缩进的像素数：抗锯齿边缘落在位图之内，不被裁掉
const float kCircleInset = 0.5f;
//一个像素的字节数（32 位位图）
const int kBytesPerPixel = 4;
//颜色完全不透明
const BYTE kOpaque = 255;

//每种配色在常态、悬停、按下三种状态下的底色，以及图标颜色
struct TonePalette
{
    COLORREF m_fill[3];   //下标取 call_btn_normal / call_btn_hover / call_btn_pressed
    COLORREF m_icon;      //图标颜色
};

//四种配色（下标取 CallButtonTone）：浅色主题，与通话窗画面区的浅灰底协调
const TonePalette kPalettes[] =
{
    //常态：浅灰底、深灰图标。底色须比底部栏背景（约 #F0F0F0）明显深一档，否则圆形轮廓看不出来
    //（2026-10-05 截图核对时 #EEF0F3 与背景几乎一样，图标像是直接浮在背景上）
    { { RGB(0xDD, 0xE1, 0xE6), RGB(0xD0, 0xD5, 0xDC), RGB(0xC3, 0xC9, 0xD1) }, RGB(0x3E, 0x43, 0x4C) },
    //激活态：深灰底、白色图标
    { { RGB(0x4A, 0x4F, 0x59), RGB(0x58, 0x5E, 0x69), RGB(0x3C, 0x40, 0x48) }, RGB(0xFF, 0xFF, 0xFF) },
    //危险操作：红底、白色图标
    { { RGB(0xE5, 0x48, 0x4D), RGB(0xEC, 0x60, 0x64), RGB(0xC9, 0x3A, 0x3F) }, RGB(0xFF, 0xFF, 0xFF) },
    //接听：绿底、白色图标
    { { RGB(0x2F, 0xB4, 0x5A), RGB(0x3D, 0xC4, 0x69), RGB(0x27, 0x9A, 0x4C) }, RGB(0xFF, 0xFF, 0xFF) },
};
const int kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

//禁用态不分配色，一律淡灰底、浅灰图标（底色同样比底部栏背景略深，圆形轮廓仍可辨认）
const COLORREF kDisabledFill = RGB(0xE8, 0xEA, 0xED);
const COLORREF kDisabledIcon = RGB(0xB5, 0xB9, 0xC1);

//缓存键各字段的位置：图标 8 位、斜杠 1 位、倾斜 1 位、配色 4 位、直径放在高 16 位
const int kKeyIconMask = 0xFF;
const int kKeySlashBit = 8;
const int kKeyTiltBit = 9;
const int kKeyToneShift = 10;
const int kKeyToneMask = 0xF;
const int kKeyDiameterShift = 16;

//GDI+ 在本文件惰性初始化一次（进程内多次 GdiplusStartup 是安全的，进程退出时由系统回收）：
//群通话来电窗可能先于一对一通话窗用到按钮位图，不能依赖别的模块先启动 GDI+
void EnsureGdiplus()
{
    static ULONG_PTR token = 0;
    if (token != 0)
        return;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);
}

Gdiplus::Color ToGdiColor(COLORREF c)
{
    return Gdiplus::Color(kOpaque, GetRValue(c), GetGValue(c), GetBValue(c));
}

//构造一个外观（LookForAction 内部用）
CallButtonLook MakeLook(int icon, bool slash, bool tilt, int tone, int diameter)
{
    CallButtonLook look;
    look.m_icon = icon;
    look.m_slash = slash;
    look.m_hangupTilt = tilt;
    look.m_tone = tone;
    look.m_diameter = diameter;
    return look;
}

} // namespace

CallButtonLook::CallButtonLook()
    : m_icon(-1)
    , m_slash(false)
    , m_hangupTilt(false)
    , m_tone(call_tone_normal)
    , m_diameter(kCallRoundBtnPx)
{
}

CallButtonLook LookForAction(int actionId, bool active, bool video)
{
    switch (actionId)
    {
    //静音键：麦克风；已关麦时画斜杠、深灰底
    case kActionMute:
        return MakeLook(balloonchatwnd::kChatIconMic, active, false, active ? call_tone_active : call_tone_normal,
                        kCallRoundBtnPx);

    //音量键：喇叭加声波；音量调到 0 时画斜杠，配色不变（它是弹出滑块的入口，没有开关两态）
    case kActionVolume:
        return MakeLook(balloonchatwnd::kChatIconVoiceWave3, active, false, call_tone_normal, kCallRoundBtnPx);

    //摄像头键：摄像机；摄像头已关时画斜杠、深灰底
    case kActionCamera:
        return MakeLook(balloonchatwnd::kChatIconVideo, active, false, active ? call_tone_active : call_tone_normal,
                        kCallRoundBtnPx);

    //切换媒体键：图标表示切换的目标，语音态画摄像机（切到视频）、视频态画听筒（切回语音）
    case kActionSwitchMedia:
        return MakeLook(video ? balloonchatwnd::kChatIconPhone : balloonchatwnd::kChatIconVideo, false, false,
                        call_tone_normal, kCallRoundBtnPx);

    //共享屏幕键：正在共享时深灰底（点击即停止共享），不画斜杠
    case kActionShare:
        return MakeLook(balloonchatwnd::kChatIconScreenShare, false, false,
                        active ? call_tone_active : call_tone_normal, kCallRoundBtnPx);

    //共享电脑声音键（2026-10-06）：显示器里一个音符；正在共享时深灰底（点击即停止），不画斜杠
    case kActionSystemAudio:
        return MakeLook(balloonchatwnd::kChatIconSystemAudio, false, false,
                        active ? call_tone_active : call_tone_normal, kCallRoundBtnPx);

    //挂断与取消：红底、倾斜的听筒，比普通按钮大一圈
    case kActionHangup:
    case kActionCancel:
        return MakeLook(balloonchatwnd::kChatIconPhone, false, true, call_tone_danger, kCallRoundBtnLargePx);

    //来电弹窗的拒绝：红底、倾斜的听筒；弹窗较小，用普通直径
    case kActionReject:
        return MakeLook(balloonchatwnd::kChatIconPhone, false, true, call_tone_danger, kCallRoundBtnPx);

    //来电弹窗的接听：绿底、正放的听筒
    case kActionAccept:
        return MakeLook(balloonchatwnd::kChatIconPhone, false, false, call_tone_accept, kCallRoundBtnPx);

    //未知的按钮种类：常态配色、不画图标
    default:
        return CallButtonLook();
    }
}

std::wstring TooltipForAction(int actionId, bool active, bool video)
{
    switch (actionId)
    {
    //静音键：说明点击之后会怎样
    case kActionMute:
        return active ? TR(L"打开麦克风") : TR(L"关闭麦克风");

    //音量键：点击弹出音量滑块
    case kActionVolume:
        return TR(L"调节音量");

    //摄像头键：同静音键，说明点击之后会怎样
    case kActionCamera:
        return active ? TR(L"打开摄像头") : TR(L"关闭摄像头");

    //切换媒体键：按切换目标措辞
    case kActionSwitchMedia:
        return video ? TR(L"切回语音") : TR(L"切到视频");

    //共享屏幕键：正在共享时点击即停止
    case kActionShare:
        return active ? TR(L"停止共享") : TR(L"共享屏幕");

    //共享电脑声音（2026-10-06）：外放时电脑声音会经麦克风再传一遍，提示里建议戴耳机
    case kActionSystemAudio:
        return active ? TR(L"停止共享电脑声音") : TR(L"共享电脑声音（建议佩戴耳机）");

    //挂断
    case kActionHangup:
        return TR(L"挂断");

    //呼叫中撤回呼叫
    case kActionCancel:
        return TR(L"取消");

    //来电拒绝
    case kActionReject:
        return TR(L"拒绝");

    //来电接听
    case kActionAccept:
        return TR(L"接听");

    //未知的按钮种类：没有提示
    default:
        return std::wstring();
    }
}

std::wstring SystemAudioFailureText(int result)
{
    switch (result)
    {
    //通话还没接通（媒体通道没建好）：按钮只在接通后出现，一般是接通后的头一两秒里点的
    case call_sysaudio_not_connected:
        return TR(L"通话接通后才能共享电脑声音");
    //对方是旧版本，或还没收到对方的第一份质量报告（接通约 1 秒后才有）
    case call_sysaudio_peer_unsupported:
        return TR(L"对方的版本不支持接收电脑声音");
    //两种回环采集方式都打不开：与屏幕共享打不开电脑声音时的说法一致
    case call_sysaudio_capture_failed:
        return TR(L"无法采集电脑声音，请检查播放设备。");
    //成功或未知取值：不提示
    default:
        return std::wstring();
    }
}

std::wstring PeerSystemAudioText(bool sharing)
{
    return sharing ? TR(L"对方开始共享电脑声音") : TR(L"对方已停止共享电脑声音");
}

HBITMAP MakeCallButtonBitmap(const CallButtonLook& look, int state)
{
    const int d = look.m_diameter;
    if (d <= 0 || state < 0 || state >= call_btn_state_count)
        return NULL;
    EnsureGdiplus();

    //32 位自上而下的 DIB，创建时全部为 0（完全透明）
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = d;
    bi.bmiHeader.biHeight = -d;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        if (bmp != NULL)
            ::DeleteObject(bmp);
        return NULL;
    }

    //配色：禁用态统一淡灰，其余按配色与状态取
    const int tone = (look.m_tone >= 0 && look.m_tone < kPaletteCount) ? look.m_tone : call_tone_normal;
    const COLORREF fill = (state == call_btn_disabled) ? kDisabledFill : kPalettes[tone].m_fill[state];
    const COLORREF iconColor = (state == call_btn_disabled) ? kDisabledIcon : kPalettes[tone].m_icon;

    {
        //GDI+ 直接画进 DIB 的像素：预乘透明度格式，与 AlphaBlend 要求的格式一致
        Gdiplus::Bitmap canvas(d, d, d * kBytesPerPixel, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        Gdiplus::SolidBrush brush(ToGdiColor(fill));
        const Gdiplus::REAL side = static_cast<Gdiplus::REAL>(d) - 2 * kCircleInset;
        g.FillEllipse(&brush, kCircleInset, kCircleInset, side, side);

        //图标居中
        const float ratio = (look.m_icon == balloonchatwnd::kChatIconPhone) ? kPhoneIconRatio : kIconRatio;
        const Gdiplus::REAL iconSize = static_cast<Gdiplus::REAL>(d) * ratio;
        const Gdiplus::REAL iconX = (static_cast<Gdiplus::REAL>(d) - iconSize) / 2;
        const Gdiplus::REAL iconY = iconX;
        if (look.m_icon >= 0)
        {
            if (look.m_hangupTilt)
            {
                //绕按钮中心旋转，画完复位
                const Gdiplus::REAL c = static_cast<Gdiplus::REAL>(d) / 2;
                g.TranslateTransform(c, c);
                g.RotateTransform(kHangupTiltDegrees);
                g.TranslateTransform(-c, -c);
            }
            balloonchatwnd::DrawChatIcon(g, look.m_icon, iconX, iconY, iconSize, ToGdiColor(iconColor), kIconStroke);
            g.ResetTransform();
        }

        if (look.m_slash)
        {
            //斜杠：先用底色画一道更粗的线把图标线条隔开，再用图标颜色画斜杠本身
            const Gdiplus::REAL stroke = kSlashStrokePx * static_cast<Gdiplus::REAL>(d) / kSlashBaseDiameter;
            const Gdiplus::REAL inset = iconSize * kSlashInsetRatio;
            const Gdiplus::REAL x1 = iconX + inset;
            const Gdiplus::REAL y1 = iconY + inset;
            const Gdiplus::REAL x2 = iconX + iconSize - inset;
            const Gdiplus::REAL y2 = iconY + iconSize - inset;
            Gdiplus::Pen gap(ToGdiColor(fill), stroke * kSlashGapRatio);
            gap.SetStartCap(Gdiplus::LineCapRound);
            gap.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&gap, x1, y1, x2, y2);
            Gdiplus::Pen slash(ToGdiColor(iconColor), stroke);
            slash.SetStartCap(Gdiplus::LineCapRound);
            slash.SetEndCap(Gdiplus::LineCapRound);
            g.DrawLine(&slash, x1, y1, x2, y2);
        }
    }
    ::GdiFlush();
    return bmp;
}

CallButtonArtCache::StateBitmaps::StateBitmaps()
{
    for (int i = 0; i < call_btn_state_count; ++i)
        m_states[i] = NULL;
}

CallButtonArtCache::CallButtonArtCache()
{
}

CallButtonArtCache::~CallButtonArtCache()
{
    for (std::map<int, StateBitmaps>::iterator it = m_bitmaps.begin(); it != m_bitmaps.end(); ++it)
    {
        for (int i = 0; i < call_btn_state_count; ++i)
        {
            if (it->second.m_states[i] != NULL)
                ::DeleteObject(it->second.m_states[i]);
        }
    }
    m_bitmaps.clear();
}

int CallButtonArtCache::KeyOf(const CallButtonLook& look)
{
    return (look.m_icon & kKeyIconMask)
           | ((look.m_slash ? 1 : 0) << kKeySlashBit)
           | ((look.m_hangupTilt ? 1 : 0) << kKeyTiltBit)
           | ((look.m_tone & kKeyToneMask) << kKeyToneShift)
           | (look.m_diameter << kKeyDiameterShift);
}

bool CallButtonArtCache::Apply(balloonwjui::DuiButton* button, const CallButtonLook& look, const std::wstring& tooltip)
{
    if (button == NULL)
        return false;

    const int key = KeyOf(look);
    std::map<int, StateBitmaps>::iterator it = m_bitmaps.find(key);
    if (it == m_bitmaps.end())
    {
        //第一次用到这种外观：四张位图一起画，任何一张失败都不缓存，下次再试
        StateBitmaps made;
        bool ok = true;
        for (int i = 0; i < call_btn_state_count; ++i)
        {
            made.m_states[i] = MakeCallButtonBitmap(look, i);
            if (made.m_states[i] == NULL)
                ok = false;
        }
        if (!ok)
        {
            for (int i = 0; i < call_btn_state_count; ++i)
            {
                if (made.m_states[i] != NULL)
                    ::DeleteObject(made.m_states[i]);
            }
            return false;
        }
        it = m_bitmaps.insert(std::make_pair(key, made)).first;
    }

    const StateBitmaps& bmps = it->second;
    //圆形图标按钮不接收焦点：DuiButton 默认接收焦点（构造函数里 SetTabStop(true)），鼠标点击后会在按钮矩形内侧画一圈
    //方形虚线焦点框，套在圆形按钮外面很突兀（2026-10-05 真机核对时发现）。通话窗与来电弹窗本来就不靠键盘操作
    button->SetTabStop(false);
    button->SetBgBitmap(bmps.m_states[call_btn_normal], bmps.m_states[call_btn_hover],
                        bmps.m_states[call_btn_pressed], bmps.m_states[call_btn_disabled]);
    button->SetText(_T(""));
    if (tooltip.empty())
        balloonwjui::DuiToolTipMgr::Inst().Unregister(button);
    else
        balloonwjui::DuiToolTipMgr::Inst().Register(button, tooltip.c_str());
    button->Invalidate();
    return true;
}

} // namespace call
