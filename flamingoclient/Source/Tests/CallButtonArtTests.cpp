/**
 *  通话窗圆形图标按钮的单元测试（标签 [callbtn]），CallButtonArtTests.cpp
 *  覆盖 FlamingoMedia/Call/CallButtonArt.h：
 *    - CBA1 由按钮种类与状态算出的外观：图标、斜杠、听筒倾斜、配色、直径；
 *    - CBA2 各按钮在两种状态下的悬停提示文字；
 *    - CBA3 位图：32 位、边长等于直径、四角完全透明、圆内为底色；不同配色与状态的底色不同；
 *    - CBA4 缓存：装到 DuiButton 上后有背景位图、文字清空、不再接收焦点（点击后不画虚线焦点框）；同一外观只缓存一份；
 *    - CBA9（隐藏，人工核对用）：把全部按钮的四种状态拼成一张 PNG，运行 FlamingoUnit.exe "[callbtn-dump]" 生成。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <string.h>
#include <string>
#include <vector>

#include "../../FlamingoMedia/Call/CallButtonArt.h"
#include "../UI/BalloonChatWnd/ChatIcons.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

using namespace call;

namespace {

//DIB 像素里各通道的字节偏移（BGRA）
const int kCbaChB = 0;
const int kCbaChG = 1;
const int kCbaChR = 2;
const int kCbaChA = 3;
//完全不透明
const int kCbaOpaque = 255;
//圆内靠上边缘的取样点：横向居中，离顶边这么多像素（在圆内、在图标区域之外）
const int kCbaRingProbeY = 4;
//颜色比较的容差（抗锯齿与预乘带来的舍入）
const int kCbaTolerance = 3;
//红底按钮的红色通道至少比绿色通道高出这么多
const int kCbaRedMargin = 80;

//进程内 GDI+ 的启停；多次 GdiplusStartup 是安全的
class CbaGdiplus
{
public:
    CbaGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }

    ~CbaGdiplus()
    {
        if (m_token != 0)
            Gdiplus::GdiplusShutdown(m_token);
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌
};

//读 32 位自上而下 DIB 在 (x, y) 处某个通道的值；读不到时返回 -1
int CbaChannel(HBITMAP bmp, int x, int y, int channel)
{
    DIBSECTION ds;
    if (::GetObject(bmp, sizeof(ds), &ds) != sizeof(ds) || ds.dsBm.bmBits == NULL || ds.dsBm.bmBitsPixel != 32)
        return -1;
    const BYTE* row = static_cast<const BYTE*>(ds.dsBm.bmBits) + static_cast<size_t>(y) * ds.dsBm.bmWidthBytes;
    return row[x * 4 + channel];
}

bool CbaNear(int a, int b)
{
    return abs(a - b) <= kCbaTolerance;
}

} // namespace

TEST_CASE("CBA1 外观：图标、斜杠、听筒倾斜、配色与直径随按钮种类与状态变化", "[callbtn]")
{
    //静音：开着麦克风为常态，关了画斜杠、换深灰底
    CallButtonLook micOn = LookForAction(kActionMute, false, false);
    CallButtonLook micOff = LookForAction(kActionMute, true, false);
    CHECK(micOn.m_icon == balloonchatwnd::kChatIconMic);
    CHECK_FALSE(micOn.m_slash);
    CHECK(micOn.m_tone == call_tone_normal);
    CHECK(micOff.m_icon == balloonchatwnd::kChatIconMic);
    CHECK(micOff.m_slash);
    CHECK(micOff.m_tone == call_tone_active);
    CHECK(micOn.m_diameter == kCallRoundBtnPx);

    //摄像头：同上
    CHECK_FALSE(LookForAction(kActionCamera, false, true).m_slash);
    CHECK(LookForAction(kActionCamera, true, true).m_slash);
    CHECK(LookForAction(kActionCamera, true, true).m_tone == call_tone_active);
    CHECK(LookForAction(kActionCamera, false, true).m_icon == balloonchatwnd::kChatIconVideo);

    //切换媒体：图标是切换的目标
    CHECK(LookForAction(kActionSwitchMedia, false, false).m_icon == balloonchatwnd::kChatIconVideo);
    CHECK(LookForAction(kActionSwitchMedia, false, true).m_icon == balloonchatwnd::kChatIconPhone);
    CHECK(LookForAction(kActionSwitchMedia, false, true).m_tone == call_tone_normal);

    //共享屏幕：正在共享换深灰底，不画斜杠
    CHECK(LookForAction(kActionShare, false, false).m_tone == call_tone_normal);
    CHECK(LookForAction(kActionShare, true, false).m_tone == call_tone_active);
    CHECK_FALSE(LookForAction(kActionShare, true, false).m_slash);
    CHECK(LookForAction(kActionShare, true, false).m_icon == balloonchatwnd::kChatIconScreenShare);

    //音量：调到 0 时画斜杠，配色不变
    CHECK(LookForAction(kActionVolume, false, false).m_icon == balloonchatwnd::kChatIconVoiceWave3);
    CHECK_FALSE(LookForAction(kActionVolume, false, false).m_slash);
    CHECK(LookForAction(kActionVolume, true, false).m_slash);
    CHECK(LookForAction(kActionVolume, true, false).m_tone == call_tone_normal);

    //挂断、取消：红底、倾斜的听筒、大一圈
    const int dangerIds[] = { kActionHangup, kActionCancel };
    for (size_t i = 0; i < sizeof(dangerIds) / sizeof(dangerIds[0]); ++i)
    {
        INFO("action=" << dangerIds[i]);
        CallButtonLook look = LookForAction(dangerIds[i], false, false);
        CHECK(look.m_icon == balloonchatwnd::kChatIconPhone);
        CHECK(look.m_hangupTilt);
        CHECK(look.m_tone == call_tone_danger);
        CHECK(look.m_diameter == kCallRoundBtnLargePx);
    }
    //来电弹窗：拒绝红底倾斜听筒、接听绿底正放听筒，都是普通直径
    CHECK(LookForAction(kActionReject, false, false).m_tone == call_tone_danger);
    CHECK(LookForAction(kActionReject, false, false).m_hangupTilt);
    CHECK(LookForAction(kActionReject, false, false).m_diameter == kCallRoundBtnPx);
    CHECK(LookForAction(kActionAccept, false, false).m_tone == call_tone_accept);
    CHECK_FALSE(LookForAction(kActionAccept, false, false).m_hangupTilt);
    CHECK(LookForAction(kActionAccept, false, false).m_diameter == kCallRoundBtnPx);

    //未知种类：不画图标
    CHECK(LookForAction(0, false, false).m_icon == -1);
    CHECK(LookForAction(999, true, true).m_icon == -1);
}

TEST_CASE("CBA2 提示文字：说明点击之后会怎样，随状态变化", "[callbtn]")
{
    CHECK(TooltipForAction(kActionMute, false, false) == L"关闭麦克风");
    CHECK(TooltipForAction(kActionMute, true, false) == L"打开麦克风");
    CHECK(TooltipForAction(kActionCamera, false, true) == L"关闭摄像头");
    CHECK(TooltipForAction(kActionCamera, true, true) == L"打开摄像头");
    CHECK(TooltipForAction(kActionSwitchMedia, false, false) == L"切到视频");
    CHECK(TooltipForAction(kActionSwitchMedia, false, true) == L"切回语音");
    CHECK(TooltipForAction(kActionShare, false, false) == L"共享屏幕");
    CHECK(TooltipForAction(kActionShare, true, false) == L"停止共享");
    CHECK(TooltipForAction(kActionVolume, false, false) == L"调节音量");
    CHECK(TooltipForAction(kActionHangup, false, false) == L"挂断");
    CHECK(TooltipForAction(kActionCancel, false, false) == L"取消");
    CHECK(TooltipForAction(kActionReject, false, false) == L"拒绝");
    CHECK(TooltipForAction(kActionAccept, false, false) == L"接听");
    CHECK(TooltipForAction(999, false, false).empty());
}

TEST_CASE("CBA3 位图：32 位、边长等于直径、四角透明、圆内为底色；配色与状态改变底色", "[callbtn]")
{
    CbaGdiplus gdiplus;
    const CallButtonLook normalLook = LookForAction(kActionMute, false, false);
    const CallButtonLook dangerLook = LookForAction(kActionHangup, false, false);
    HBITMAP normal = MakeCallButtonBitmap(normalLook, call_btn_normal);
    HBITMAP hover = MakeCallButtonBitmap(normalLook, call_btn_hover);
    HBITMAP danger = MakeCallButtonBitmap(dangerLook, call_btn_normal);
    REQUIRE(normal != NULL);
    REQUIRE(hover != NULL);
    REQUIRE(danger != NULL);

    BITMAP bm;
    memset(&bm, 0, sizeof(bm));
    REQUIRE(::GetObject(normal, sizeof(bm), &bm) != 0);
    CHECK(bm.bmWidth == kCallRoundBtnPx);
    CHECK(bm.bmHeight == kCallRoundBtnPx);
    CHECK(bm.bmBitsPixel == 32);
    REQUIRE(::GetObject(danger, sizeof(bm), &bm) != 0);
    CHECK(bm.bmWidth == kCallRoundBtnLargePx);

    //四角在圆外：完全透明
    const int last = kCallRoundBtnPx - 1;
    CHECK(CbaChannel(normal, 0, 0, kCbaChA) == 0);
    CHECK(CbaChannel(normal, last, 0, kCbaChA) == 0);
    CHECK(CbaChannel(normal, 0, last, kCbaChA) == 0);
    CHECK(CbaChannel(normal, last, last, kCbaChA) == 0);

    //圆内靠上边缘（图标之外）：不透明，颜色为常态底色 #DDE1E6
    const int cx = kCallRoundBtnPx / 2;
    CHECK(CbaChannel(normal, cx, kCbaRingProbeY, kCbaChA) == kCbaOpaque);
    CHECK(CbaNear(CbaChannel(normal, cx, kCbaRingProbeY, kCbaChR), 0xDD));
    CHECK(CbaNear(CbaChannel(normal, cx, kCbaRingProbeY, kCbaChG), 0xE1));
    CHECK(CbaNear(CbaChannel(normal, cx, kCbaRingProbeY, kCbaChB), 0xE6));
    //悬停底色更深
    CHECK(CbaChannel(hover, cx, kCbaRingProbeY, kCbaChR) < CbaChannel(normal, cx, kCbaRingProbeY, kCbaChR));

    //挂断：红底
    const int dcx = kCallRoundBtnLargePx / 2;
    CHECK(CbaChannel(danger, dcx, kCbaRingProbeY, kCbaChR)
          > CbaChannel(danger, dcx, kCbaRingProbeY, kCbaChG) + kCbaRedMargin);

    //非法参数
    CallButtonLook bad;
    bad.m_diameter = 0;
    CHECK(MakeCallButtonBitmap(bad, call_btn_normal) == NULL);
    CHECK(MakeCallButtonBitmap(normalLook, call_btn_state_count) == NULL);

    ::DeleteObject(normal);
    ::DeleteObject(hover);
    ::DeleteObject(danger);
}

TEST_CASE("CBA4 缓存：装上后有背景位图、文字清空；同一外观只缓存一份", "[callbtn]")
{
    CbaGdiplus gdiplus;
    {
        CallButtonArtCache cache;
        balloonwjui::DuiButton a;
        balloonwjui::DuiButton b;
        a.SetText(L"挂断");
        const CallButtonLook look = LookForAction(kActionHangup, false, false);
        //DuiButton 默认接收焦点，点击后会在圆形按钮外画方形虚线焦点框；装上图标后不再接收焦点
        CHECK(a.IsTabStop());
        REQUIRE(cache.Apply(&a, look, TooltipForAction(kActionHangup, false, false)));
        CHECK(a.GetBgBitmapForCurrentState() != NULL);
        CHECK(a.GetText().IsEmpty());
        CHECK_FALSE(a.IsTabStop());
        CHECK(cache.LookCount() == 1);

        //同一外观再装一个按钮：复用位图
        REQUIRE(cache.Apply(&b, look, std::wstring()));
        CHECK(cache.LookCount() == 1);
        CHECK(b.GetBgBitmapForCurrentState() == a.GetBgBitmapForCurrentState());

        //不同外观：新增一份
        REQUIRE(cache.Apply(&b, LookForAction(kActionMute, true, false), std::wstring()));
        CHECK(cache.LookCount() == 2);
        CHECK(b.GetBgBitmapForCurrentState() != a.GetBgBitmapForCurrentState());

        CHECK_FALSE(cache.Apply(NULL, look, std::wstring()));
        //按钮先于缓存析构（与窗口里控件树先于缓存释放的次序一致）
    }
}

namespace {

//CBA9 的输出：每个按钮占一格，格子边长与格间距（像素）
const int kCbaSheetCell = 64;
//CBA9 拼图的底色：通话窗底部栏的白底
const BYTE kCbaSheetBg = 0xFF;

//在 GDI+ 已注册的编码器里找 PNG 的那个
bool CbaPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("CBA9 （人工核对用）把全部按钮的四种状态拼成一张 PNG 写到 %TEMP%\\call_buttons.png", "[.][callbtn-dump]")
{
    CbaGdiplus gdiplus;
    struct Row
    {
        int  m_action;   //按钮种类
        bool m_active;   //激活态
        bool m_video;    //是否视频态
    };
    const Row rows[] =
    {
        { kActionMute, false, false }, { kActionMute, true, false },
        { kActionVolume, false, false }, { kActionVolume, true, false },
        { kActionCamera, false, true }, { kActionCamera, true, true },
        { kActionSwitchMedia, false, false }, { kActionSwitchMedia, false, true },
        { kActionShare, false, false }, { kActionShare, true, false },
        { kActionHangup, false, false }, { kActionReject, false, false }, { kActionAccept, false, false },
    };
    const int rowCount = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
    Gdiplus::Bitmap sheet(kCbaSheetCell * call_btn_state_count, kCbaSheetCell * rowCount, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&sheet);
        g.Clear(Gdiplus::Color(kCbaOpaque, kCbaSheetBg, kCbaSheetBg, kCbaSheetBg));
        for (int r = 0; r < rowCount; ++r)
        {
            const CallButtonLook look = LookForAction(rows[r].m_action, rows[r].m_active, rows[r].m_video);
            for (int s = 0; s < call_btn_state_count; ++s)
            {
                HBITMAP bmp = MakeCallButtonBitmap(look, s);
                REQUIRE(bmp != NULL);
                DIBSECTION ds;
                REQUIRE(::GetObject(bmp, sizeof(ds), &ds) == sizeof(ds));
                Gdiplus::Bitmap cell(look.m_diameter, look.m_diameter, ds.dsBm.bmWidthBytes, PixelFormat32bppPARGB,
                                     static_cast<BYTE*>(ds.dsBm.bmBits));
                const int offset = (kCbaSheetCell - look.m_diameter) / 2;
                g.DrawImage(&cell, s * kCbaSheetCell + offset, r * kCbaSheetCell + offset);
                ::DeleteObject(bmp);
            }
        }
    }
    wchar_t dir[MAX_PATH] = { 0 };
    REQUIRE(::GetTempPathW(MAX_PATH, dir) > 0);
    const std::wstring path = std::wstring(dir) + L"call_buttons.png";
    CLSID png;
    REQUIRE(CbaPngEncoder(png));
    CHECK(sheet.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);
}
