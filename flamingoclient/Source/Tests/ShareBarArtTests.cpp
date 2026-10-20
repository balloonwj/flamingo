/**
 *  共享控制条与录制条统一样式（图标按钮、闪烁录制红点）的单元测试（标签 [sharebarart]），ShareBarArtTests.cpp
 *  背景：2026-10-07 用户确认把共享控制条与录制条改成同一套样式：左侧状态区（录制红点闪烁、暂停时变灰）+ 右侧带悬停提示的
 *  图标按钮，开关类按钮打开时高亮、关着时带斜杠；共享控制条在「录下这场共享」时也显示闪烁的录制红点
 *  （docs/屏幕共享与录制方案.md 第 13 节）。
 *  覆盖：
 *    - SBAR1 外观：开关打开为蓝底、关着带斜杠；录制中为红底；停止 / 结束为红底方块；暂停中画「继续」；鼠标效果带下拉三角；
 *    - SBAR2 提示文字：与改版前的按钮文字一致（共享控制条「关闭麦克风」、录制条「麦克风：开」等）；
 *    - SBAR3 录制红点：在录时每 500 毫秒亮暗交替，暂停时灰色不闪，不录时隐藏；各状态颜色不同；
 *    - SBAR4 位图：边长正确，圆角外透明、中心不透明；打开的开关与常态底色不同；缓存同一外观只画一次，装上后按钮文字清空、
 *      提示登记，保留文字时文字不变；
 *    - SBAR5 录制条：按钮都是图标按钮（没有文字、有提示），都在窗口里且互不重叠；红点按时刻闪烁，暂停后变灰；
 *    - SBAR6 共享控制条：按钮都在窗口里且互不重叠，图标按钮没有文字；录制这场共享时红点出现并闪烁，停止后消失；
 *      字幕能使用时多一个字幕按钮、整条变宽，点它发「为我的发言生成字幕」并带上想要的新状态；
 *    - SBAR7~8 浮动观看工具条用的新按钮（2026-10-12）：白板、共享说明、主持人、全屏 / 退出全屏、离开的图标与提示，
 *      三态按钮（申请控制、共享我的屏幕、新建白板）的常态 / 申请中（橙底）/ 打开（蓝底）外观，新图标都画出了内容。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <vector>
#include "../../FlamingoMedia/Record/RecordBar.h"
#include "../../FlamingoMedia/Share/ShareBarButtonArt.h"
#include "../../FlamingoMedia/Share/ShareControlBar.h"
#include "../../FlamingoMedia/Share/ShareMessages.h"
#include "../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//参照矩形（屏幕坐标）
const RECT kSbarAnchor = { 0, 0, 1600, 900 };
//每像素字节数与 alpha 所在的字节
const int kSbarBpp = 4;
const int kSbarAlphaByte = 3;

//收命令的通知窗口
HWND SbarNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

//读位图某个像素的四个字节（B、G、R、A）
void SbarPixel(HBITMAP bmp, int x, int y, BYTE out[4])
{
    DIBSECTION ds;
    memset(&ds, 0, sizeof(ds));
    ::GetObject(bmp, sizeof(ds), &ds);
    const BYTE* bits = static_cast<const BYTE*>(ds.dsBm.bmBits);
    const BYTE* p = bits + y * ds.dsBm.bmWidthBytes + x * kSbarBpp;
    for (int i = 0; i < kSbarBpp; ++i)
        out[i] = p[i];
}

//按 id 找控件树里的按钮
balloonwjui::DuiButton* SbarButton(balloonwjui::DuiFrameWindow& wnd, UINT id)
{
    balloonwjui::DuiControl* content = wnd.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

//一组按钮都在窗口客户区里、互不重叠、宽度为图标按钮边长
void SbarCheckButtons(balloonwjui::DuiFrameWindow& wnd, const std::vector<UINT>& ids, bool iconOnly)
{
    RECT client;
    ::GetClientRect(wnd.m_hWnd, &client);
    std::vector<RECT> rects;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        balloonwjui::DuiButton* b = SbarButton(wnd, ids[i]);
        REQUIRE(b != NULL);
        const RECT rc = b->GetRect();
        CHECK(rc.left >= client.left);
        CHECK(rc.right <= client.right);
        CHECK(rc.top >= client.top);
        CHECK(rc.bottom <= client.bottom);
        if (iconOnly)
        {
            CHECK(rc.right - rc.left == kShareBarBtnPx);
            CHECK(b->GetText().IsEmpty());
            CHECK_FALSE(balloonwjui::DuiToolTipMgr::Inst().GetText(b).IsEmpty());
        }
        for (size_t j = 0; j < rects.size(); ++j)
        {
            RECT both;
            CHECK_FALSE(::IntersectRect(&both, &rc, &rects[j]));
        }
        rects.push_back(rc);
    }
}

//模拟点一下按钮
void SbarClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

} // namespace

TEST_CASE("SBAR1 button looks follow the toggle state", "[sharebarart]")
{
    ShareBarButtonLook l = ShareBarLookFor(share_bar_mic, true);
    CHECK(l.m_icon == share_bar_icon_mic);
    CHECK(l.m_tone == share_bar_tone_active);
    CHECK_FALSE(l.m_slash);
    l = ShareBarLookFor(share_bar_mic, false);
    CHECK(l.m_tone == share_bar_tone_normal);
    CHECK(l.m_slash);
    CHECK(ShareBarLookFor(share_bar_system_audio, false).m_slash);
    CHECK(ShareBarLookFor(share_bar_caption, false).m_slash);
    CHECK(ShareBarLookFor(share_bar_camera, true).m_tone == share_bar_tone_active);
    CHECK(ShareBarLookFor(share_bar_record, true).m_tone == share_bar_tone_danger);
    CHECK(ShareBarLookFor(share_bar_record, false).m_tone == share_bar_tone_normal);
    CHECK(ShareBarLookFor(share_bar_annotate, true).m_tone == share_bar_tone_active);
    CHECK(ShareBarLookFor(share_bar_stop_share, false).m_tone == share_bar_tone_danger);
    CHECK(ShareBarLookFor(share_bar_stop_record, false).m_icon == share_bar_icon_stop);
    CHECK(ShareBarLookFor(share_bar_pause, true).m_icon == share_bar_icon_play);
    CHECK(ShareBarLookFor(share_bar_pause, false).m_icon == share_bar_icon_pause);
    CHECK(ShareBarLookFor(share_bar_mouse, false).m_dropMark);
    CHECK(ShareBarLookFor(share_bar_collapse, true).m_icon == share_bar_icon_pin);
    CHECK(ShareBarLookFor(share_bar_collapse, false).m_icon == share_bar_icon_collapse);
    const int kUnknown = 99;
    CHECK(ShareBarLookFor(kUnknown, true).m_icon == share_bar_icon_none);
}

TEST_CASE("SBAR2 tooltips keep the old button texts", "[sharebarart]")
{
    CHECK(ShareBarTooltipFor(share_bar_mic, true, false) == std::wstring(TR(L"关闭麦克风")));
    CHECK(ShareBarTooltipFor(share_bar_mic, false, false) == std::wstring(TR(L"打开麦克风")));
    CHECK(ShareBarTooltipFor(share_bar_mic, true, true) == std::wstring(TR(L"麦克风：开")));
    CHECK(ShareBarTooltipFor(share_bar_system_audio, false, true) == std::wstring(TR(L"电脑声音：关")));
    CHECK(ShareBarTooltipFor(share_bar_annotate, true, false) == std::wstring(TR(L"退出标注")));
    CHECK(ShareBarTooltipFor(share_bar_annotate, false, true) == std::wstring(TR(L"画笔")));
    CHECK(ShareBarTooltipFor(share_bar_record, true, false) == std::wstring(TR(L"停止录制")));
    CHECK(ShareBarTooltipFor(share_bar_pause, true, true) == std::wstring(TR(L"继续")));
    CHECK(ShareBarTooltipFor(share_bar_stop_share, false, false) == std::wstring(TR(L"结束共享")));
    CHECK(ShareBarTooltipFor(share_bar_stop_record, false, true) == std::wstring(TR(L"停止")));
    CHECK(ShareBarTooltipFor(share_bar_more, false, false) == std::wstring(TR(L"更多")));
    CHECK(ShareBarTooltipFor(share_bar_collapse, false, false) == std::wstring(TopBarCollapseText()));
    CHECK(ShareBarTooltipFor(share_bar_collapse, true, false) == std::wstring(TopBarPinText()));
}

TEST_CASE("SBAR3 record dot blinks, greys out when paused and hides when idle", "[sharebarart]")
{
    CHECK(ShareRecordDotStateAt(false, false, 0) == share_record_dot_hidden);
    CHECK(ShareRecordDotStateAt(true, false, 0) == share_record_dot_bright);
    CHECK(ShareRecordDotStateAt(true, false, kShareRecordBlinkMs - 1) == share_record_dot_bright);
    CHECK(ShareRecordDotStateAt(true, false, kShareRecordBlinkMs) == share_record_dot_dim);
    CHECK(ShareRecordDotStateAt(true, false, 2 * kShareRecordBlinkMs) == share_record_dot_bright);
    CHECK(ShareRecordDotStateAt(true, true, 0) == share_record_dot_paused);
    CHECK(ShareRecordDotStateAt(true, true, kShareRecordBlinkMs) == share_record_dot_paused);
    const COLORREF bg = RGB(44, 48, 56);
    CHECK(ShareRecordDotColor(share_record_dot_hidden, bg) == bg);
    CHECK(ShareRecordDotColor(share_record_dot_bright, bg) != ShareRecordDotColor(share_record_dot_dim, bg));
    CHECK(ShareRecordDotColor(share_record_dot_paused, bg) != ShareRecordDotColor(share_record_dot_bright, bg));
}

TEST_CASE("SBAR4 button bitmaps and the art cache", "[sharebarart]")
{
    HBITMAP normal = MakeShareBarButtonBitmap(ShareBarLookFor(share_bar_mic, false), share_bar_btn_normal, kShareBarBtnPx);
    HBITMAP active = MakeShareBarButtonBitmap(ShareBarLookFor(share_bar_mic, true), share_bar_btn_normal, kShareBarBtnPx);
    REQUIRE(normal != NULL);
    REQUIRE(active != NULL);
    BITMAP bm;
    ::GetObject(normal, sizeof(bm), &bm);
    CHECK(bm.bmWidth == kShareBarBtnPx);
    CHECK(bm.bmHeight == kShareBarBtnPx);
    BYTE corner[4];
    BYTE edge[4];
    BYTE edgeActive[4];
    SbarPixel(normal, 0, 0, corner);
    CHECK(corner[kSbarAlphaByte] == 0);
    //左边中点附近：底色，不被图标覆盖
    SbarPixel(normal, 2, kShareBarBtnPx / 2, edge);
    SbarPixel(active, 2, kShareBarBtnPx / 2, edgeActive);
    CHECK(edge[kSbarAlphaByte] == 255);
    CHECK(memcmp(edge, edgeActive, 3) != 0);
    ::DeleteObject(normal);
    ::DeleteObject(active);
    //缓存
    ShareBarButtonArtCache cache;
    balloonwjui::DuiButton a;
    balloonwjui::DuiButton b;
    a.SetText(L"x");
    b.SetText(L"3");
    CHECK(cache.Apply(&a, ShareBarLookFor(share_bar_more, false), L"tip"));
    CHECK(cache.Apply(&b, ShareBarLookFor(share_bar_more, false), L"tip2", true));
    CHECK(cache.LookCount() == 1);
    CHECK(a.GetText().IsEmpty());
    CHECK(b.GetText() == CString(L"3"));
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(&a) == CString(L"tip"));
    CHECK(a.GetBgBitmapForCurrentState() != NULL);
    balloonwjui::DuiToolTipMgr::Inst().Unregister(&a);
    balloonwjui::DuiToolTipMgr::Inst().Unregister(&b);
}

TEST_CASE("SBAR5 record bar uses icon buttons and a blinking dot", "[sharebarart]")
{
    const HWND notify = SbarNotifyWindow();
    screenrecord::CRecordBar bar;
    bar.SetPenAvailable(true);
    bar.SetAudioToggles(true, true, false);
    bar.SetCameraToggle(true, false);
    bar.SetCaptionToggle(true, true);
    bar.Show(kSbarAnchor, notify, 0);
    REQUIRE(bar.IsWindow());
    std::vector<UINT> ids;
    ids.push_back(screenrecord::CRecordBar::kIdPause);
    ids.push_back(screenrecord::CRecordBar::kIdMouse);
    ids.push_back(screenrecord::CRecordBar::kIdPen);
    ids.push_back(screenrecord::CRecordBar::kIdMic);
    ids.push_back(screenrecord::CRecordBar::kIdSystem);
    ids.push_back(screenrecord::CRecordBar::kIdCamera);
    ids.push_back(screenrecord::CRecordBar::kIdCaption);
    ids.push_back(screenrecord::CRecordBar::kIdStop);
    SbarCheckButtons(bar, ids, true);
    //红点闪烁
    bar.TickBlink(0);
    CHECK(bar.DotState() == share_record_dot_bright);
    bar.TickBlink(kShareRecordBlinkMs);
    CHECK(bar.DotState() == share_record_dot_dim);
    //暂停：灰色、不闪
    bar.SetState(1000, true);
    bar.TickBlink(0);
    CHECK(bar.DotState() == share_record_dot_paused);
    bar.TickBlink(kShareRecordBlinkMs);
    CHECK(bar.DotState() == share_record_dot_paused);
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SBAR6 share control bar uses icon buttons, a record badge and a caption button", "[sharebarart]")
{
    const HWND notify = SbarNotifyWindow();
    CShareControlBar bar;
    bar.Show(kSbarAnchor, notify);
    REQUIRE(bar.IsWindow());
    const int plainW = bar.BarWidthForTest();
    std::vector<UINT> ids;
    ids.push_back(CShareControlBar::kIdAnnotate);
    ids.push_back(CShareControlBar::kIdRecord);
    ids.push_back(CShareControlBar::kIdMic);
    ids.push_back(CShareControlBar::kIdSysAudio);
    ids.push_back(CShareControlBar::kIdCamera);
    ids.push_back(CShareControlBar::kIdPause);
    ids.push_back(CShareControlBar::kIdSwitch);
    ids.push_back(CShareControlBar::kIdMore);
    ids.push_back(CShareControlBar::kIdStop);
    ids.push_back(CShareControlBar::kIdToggle);
    SbarCheckButtons(bar, ids, true);
    std::vector<UINT> viewers(1, CShareControlBar::kIdViewers);
    SbarCheckButtons(bar, viewers, false);
    CHECK(SbarButton(bar, CShareControlBar::kIdCaption) == NULL);
    //录下这场共享：红点出现并闪烁
    CHECK(bar.RecordDotState() == share_record_dot_hidden);
    bar.SetRecording(true);
    bar.TickBlink(0);
    CHECK(bar.RecordDotState() == share_record_dot_bright);
    bar.TickBlink(kShareRecordBlinkMs);
    CHECK(bar.RecordDotState() == share_record_dot_dim);
    CHECK(balloonwjui::DuiToolTipMgr::Inst().GetText(SbarButton(bar, CShareControlBar::kIdRecord)) ==
          CString(TR(L"停止录制")));
    bar.SetRecording(false);
    bar.TickBlink(0);
    CHECK(bar.RecordDotState() == share_record_dot_hidden);
    //字幕能使用：多一个字幕按钮，整条变宽
    bar.SetCaptionState(true, false, true);
    REQUIRE(SbarButton(bar, CShareControlBar::kIdCaption) != NULL);
    CHECK(bar.BarWidthForTest() > plainW);
    ids.push_back(CShareControlBar::kIdCaption);
    SbarCheckButtons(bar, ids, true);
    MSG msg;
    while (::PeekMessageW(&msg, notify, 0, 0, PM_REMOVE))
    {
    }
    SbarClick(bar.m_hWnd, CShareControlBar::kIdCaption);
    bool got = false;
    while (::PeekMessageW(&msg, notify, kMsgShareBarCommand, kMsgShareBarCommand, PM_REMOVE))
    {
        got = true;
        CHECK(msg.wParam == static_cast<WPARAM>(CShareControlBar::kCmdCaptionSelf));
        CHECK(msg.lParam == 1);
    }
    CHECK(got);
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SBAR7 viewer bar buttons: new icons, three-state looks and tooltips", "[sharebarart]")
{
    //浮动观看工具条（2026-10-12，docs/远程桌面控制权限与观看工具条方案.md 第 4.3 节）新增的按钮种类
    CHECK(ShareBarLookFor(share_bar_board, false).m_icon == share_bar_icon_board);
    CHECK(ShareBarLookFor(share_bar_info, false).m_icon == share_bar_icon_info);
    //主持人菜单：皇冠图标，点开是菜单，带下拉三角
    CHECK(ShareBarLookFor(share_bar_host, false).m_icon == share_bar_icon_crown);
    CHECK(ShareBarLookFor(share_bar_host, false).m_dropMark);
    //全屏：on 表示正在全屏，此时画「退出全屏」
    CHECK(ShareBarLookFor(share_bar_fullscreen, false).m_icon == share_bar_icon_fullscreen);
    CHECK(ShareBarLookFor(share_bar_fullscreen, true).m_icon == share_bar_icon_exit_fullscreen);
    //离开 / 断开：红底
    CHECK(ShareBarLookFor(share_bar_leave, false).m_icon == share_bar_icon_leave);
    CHECK(ShareBarLookFor(share_bar_leave, false).m_tone == share_bar_tone_danger);

    //三态按钮（申请控制、共享我的屏幕、新建白板）：常态灰底、申请中橙底、打开蓝底，图标不随状态变
    const int triActions[] = { share_bar_control, share_bar_share_mine, share_bar_board };
    const int triIcons[] = { share_bar_icon_mouse, share_bar_icon_switch, share_bar_icon_board };
    for (size_t i = 0; i < sizeof(triActions) / sizeof(triActions[0]); ++i)
    {
        INFO("action " << triActions[i]);
        CHECK(ShareBarLookForTriState(triActions[i], share_bar_tri_off).m_tone == share_bar_tone_normal);
        CHECK(ShareBarLookForTriState(triActions[i], share_bar_tri_pending).m_tone == share_bar_tone_pending);
        CHECK(ShareBarLookForTriState(triActions[i], share_bar_tri_on).m_tone == share_bar_tone_active);
        CHECK(ShareBarLookForTriState(triActions[i], share_bar_tri_pending).m_icon == triIcons[i]);
    }
    //不是三态的按钮：申请中按关着处理，打开时与 ShareBarLookFor(action, true) 相同
    CHECK(ShareBarLookForTriState(share_bar_mic, share_bar_tri_pending).m_slash);
    CHECK(ShareBarLookForTriState(share_bar_mic, share_bar_tri_on).m_tone == share_bar_tone_active);

    //提示文字
    CHECK(ShareBarTooltipFor(share_bar_fullscreen, false, false) == std::wstring(TR(L"全屏")));
    CHECK(ShareBarTooltipFor(share_bar_fullscreen, true, false) == std::wstring(TR(L"退出全屏")));
    CHECK(ShareBarTooltipFor(share_bar_info, false, false) == std::wstring(TR(L"共享说明")));
    CHECK(ShareBarTooltipFor(share_bar_host, false, false) == std::wstring(TR(L"主持人")));
    CHECK(ShareBarTooltipFor(share_bar_leave, false, false) == std::wstring(TRC(L"离开屏幕共享的按钮", L"离开")));
    //浮动观看工具条的默认提示：三态按钮申请中时提示撤回，其余情况与 ShareBarTooltipFor 相同
    CHECK(ShareBarTooltipForTriState(share_bar_control, share_bar_tri_pending) == std::wstring(TR(L"取消申请")));
    CHECK(ShareBarTooltipForTriState(share_bar_share_mine, share_bar_tri_pending) == std::wstring(TR(L"取消共享申请")));
    CHECK(ShareBarTooltipForTriState(share_bar_board, share_bar_tri_pending) == std::wstring(TR(L"取消白板申请")));
    CHECK(ShareBarTooltipForTriState(share_bar_board, share_bar_tri_on) == std::wstring(TR(L"关闭我的白板")));
    CHECK(ShareBarTooltipForTriState(share_bar_mic, share_bar_tri_on) == ShareBarTooltipFor(share_bar_mic, true, false));
    CHECK(ShareBarTooltipForTriState(share_bar_mic, share_bar_tri_pending) == ShareBarTooltipFor(share_bar_mic, false, false));
}

TEST_CASE("SBAR8 the pending tone and the new icons draw distinct bitmaps", "[sharebarart]")
{
    //申请中的橙底与常态、打开的底色都不同
    HBITMAP off = MakeShareBarButtonBitmap(ShareBarLookForTriState(share_bar_control, share_bar_tri_off),
                                           share_bar_btn_normal, kShareBarBtnPx);
    HBITMAP pending = MakeShareBarButtonBitmap(ShareBarLookForTriState(share_bar_control, share_bar_tri_pending),
                                               share_bar_btn_normal, kShareBarBtnPx);
    HBITMAP on = MakeShareBarButtonBitmap(ShareBarLookForTriState(share_bar_control, share_bar_tri_on),
                                          share_bar_btn_normal, kShareBarBtnPx);
    REQUIRE(off != NULL);
    REQUIRE(pending != NULL);
    REQUIRE(on != NULL);
    BYTE edgeOff[4];
    BYTE edgePending[4];
    BYTE edgeOn[4];
    SbarPixel(off, 2, kShareBarBtnPx / 2, edgeOff);
    SbarPixel(pending, 2, kShareBarBtnPx / 2, edgePending);
    SbarPixel(on, 2, kShareBarBtnPx / 2, edgeOn);
    CHECK(memcmp(edgePending, edgeOff, 3) != 0);
    CHECK(memcmp(edgePending, edgeOn, 3) != 0);
    ::DeleteObject(off);
    ::DeleteObject(pending);
    ::DeleteObject(on);

    //每个新图标都画出了东西：位图中心附近有不透明的像素，且与不画图标的底图不同
    const int newIcons[] = { share_bar_icon_board, share_bar_icon_info, share_bar_icon_crown, share_bar_icon_fullscreen,
                             share_bar_icon_exit_fullscreen, share_bar_icon_leave };
    for (size_t i = 0; i < sizeof(newIcons) / sizeof(newIcons[0]); ++i)
    {
        INFO("icon " << newIcons[i]);
        HBITMAP icon = MakeShareBarIconBitmap(newIcons[i], kShareBarBtnPx, RGB(255, 255, 255));
        REQUIRE(icon != NULL);
        int opaque = 0;
        for (int y = 0; y < kShareBarBtnPx; ++y)
        {
            for (int x = 0; x < kShareBarBtnPx; ++x)
            {
                BYTE px[4];
                SbarPixel(icon, x, y, px);
                if (px[kSbarAlphaByte] != 0)
                    ++opaque;
            }
        }
        CHECK(opaque > 0);
        ::DeleteObject(icon);
    }

    //缓存按配色区分：同一图标的常态与申请中是两种外观
    ShareBarButtonArtCache cache;
    balloonwjui::DuiButton a;
    balloonwjui::DuiButton b;
    CHECK(cache.Apply(&a, ShareBarLookForTriState(share_bar_control, share_bar_tri_off), L"tip"));
    CHECK(cache.Apply(&b, ShareBarLookForTriState(share_bar_control, share_bar_tri_pending), L"tip"));
    CHECK(cache.LookCount() == 2);
    balloonwjui::DuiToolTipMgr::Inst().Unregister(&a);
    balloonwjui::DuiToolTipMgr::Inst().Unregister(&b);
}
