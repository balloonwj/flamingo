/**
 *  屏幕共享 / 录制选择窗 CSharePickerDialog 的窗口用例（标签 [sharepicker-wnd]，隐藏用例：会在屏幕中央弹出选择窗，
 *  须单独运行 "[sharepicker-wnd]"），SharePickerWindowTests.cpp
 *  背景：2026-10-05 用户反馈录制选择窗右下角的按钮显示不全，要求窗口能拖动改大小、能最大化与最小化，
 *  重复打开时激活前一个窗口。
 *  覆盖：
 *    - SPW1 可改大小，有最小化、最大化按钮所需的窗口样式，有任务栏按钮（最小化后能从任务栏找回）；
 *    - SPW2 录制模式按设计尺寸打开时，三个勾选项与四个按钮都完整落在窗口内、互不重叠，勾选项放得下自己的文字；
 *    - SPW3 窗口变宽后缩略图网格的列数增加，缩回设计宽度恢复 3 列；不能缩到最小尺寸以下；
 *    - SPW4 最小化后再次以同一用途打开：恢复成原来那个窗口（句柄不变、不再最小化），已选的页签保留。
 *    - SPW5 语音字幕可用时录制模式多「生成字幕」勾选项与它后面的提示文字（2026-10-06）：四个勾选项依次排在同一行、都在窗口内、
 *      互不重叠、放得下文字，默认勾选与 SetCaptionOptions 给的一致；两路声音都不勾时提示文字出现，勾回一路后消失；
 *      不提供时没有这两项。
 *    - SPW6 录制模式在按钮一行左端有「保存到云端」勾选项（第 27 期）：默认不勾、放得下文字、与按钮互不重叠；企业没有开通时
 *      不可勾，开通时可勾；发起共享时没有这一项。
 *    - SPW7 发起共享时字幕可用：按钮一行多「为我的发言生成字幕」（2026-10-07），默认勾选与 SetCaptionOptions 给的一致，
 *      放得下文字、在窗口内、与密码、取消、开始共享互不重叠；通话中发起时同样提供；不提供时没有；切换内容时没有；
 *    - SPW8 窗口开着时换了用途：标题与最小尺寸按新用途重设（2026-10-07 修正：原先只在新建窗口时设置）。
 *    - SPW9 切换模式的确认按钮（2026-10-13）：本人已在共享时为「切换」；多人共享里经主持人同意后第一次选择时为「开始共享」，
 *      仍不显示发起共享才有的选项。
 *  缩略图只在内存里，不写盘。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../../FlamingoMedia/Share/SharePickerDialog.h"
#include "../../FlamingoMedia/Share/SharePickerPolicy.h"
#include "../Common/I18n/I18n.h"
#include "../Common/I18n/I18nLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//勾选项上方框与文字间距等占的宽度（像素）：文字宽度加这个数才放得下
const int kSpwCheckExtra = 24;
//SPW3 拉宽后的窗口尺寸与预期的列数：内容区 1200 - 2 × 16 = 1168，每格 208 + 2 × 6 = 220，放得下 5 列
const int kSpwWideW = 1200;
const int kSpwWideH = 800;
const int kSpwWideCols = 5;
const int kSpwDesignCols = 3;
//SPW3 试图缩到的过小尺寸
const int kSpwTinyW = 300;
const int kSpwTinyH = 200;
//设计宽度（与 SharePickerDialog.cpp 的 kDialogW 一致）
const int kSpwDesignW = 740;
//SPW8 读窗口标题的缓冲区长度（字符）
const int kSpwTitleChars = 128;

void SpwPump()
{
    MSG msg;
    while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

RECT SpwWindowRect(CSharePickerDialog& dlg)
{
    RECT rc;
    dlg.GetWindowRect(&rc);
    return rc;
}

//模拟某个勾选项的勾选状态变化通知（SPW5）：勾选状态由调用方先用 SetCheck 设好
void SpwValueChanged(CSharePickerDialog& dlg, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_VALUECHANGED);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    dlg.SendMessage(WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

} // namespace

TEST_CASE("SPW1 picker window is resizable with minimize and maximize", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    REQUIRE(dlg.IsWindow());
    CHECK(dlg.IsResizable());
    const LONG style = dlg.GetStyle();
    CHECK((style & WS_MINIMIZEBOX) != 0);
    CHECK((style & WS_MAXIMIZEBOX) != 0);
    CHECK((dlg.GetExStyle() & WS_EX_APPWINDOW) != 0);
    dlg.CloseDialog();
}

TEST_CASE("SPW2 record picker bottom rows fit the window", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    REQUIRE(dlg.IsWindow());
    const RECT rcWnd = SpwWindowRect(dlg);
    const int winW = rcWnd.right - rcWnd.left;
    const int winH = rcWnd.bottom - rcWnd.top;
    balloonwjui::DuiControl* content = dlg.GetClientContent();
    REQUIRE(content != NULL);
    const UINT ids[] = { CSharePickerDialog::kCtrlMicCheck, CSharePickerDialog::kCtrlSysAudioCheck,
                         CSharePickerDialog::kCtrlCameraCheck, CSharePickerDialog::kCtrlCancel,
                         CSharePickerDialog::kCtrlRegion, CSharePickerDialog::kCtrlWhiteboard,
                         CSharePickerDialog::kCtrlOk };
    const size_t count = sizeof(ids) / sizeof(ids[0]);
    RECT rects[sizeof(ids) / sizeof(ids[0])];
    for (size_t i = 0; i < count; ++i)
    {
        INFO("control " << ids[i]);
        balloonwjui::DuiButton* b = dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(ids[i]));
        REQUIRE(b != NULL);
        rects[i] = b->GetRect();
        CHECK(rects[i].left >= 0);
        CHECK(rects[i].right <= winW);
        CHECK(rects[i].bottom <= winH);
        CHECK(rects[i].right > rects[i].left);
        //勾选项放得下自己的文字
        if (i < 3)
        {
            const int need = i18n::MeasureUiTextWidth(b->GetText().GetString(), i18n::DefaultFontPixelHeight(), false) +
                             kSpwCheckExtra;
            CHECK(rects[i].right - rects[i].left >= need);
        }
    }
    //同一行里的控件互不重叠
    for (size_t i = 0; i < count; ++i)
    {
        for (size_t k = i + 1; k < count; ++k)
        {
            RECT overlap;
            const bool hit = ::IntersectRect(&overlap, &rects[i], &rects[k]) != FALSE;
            INFO("controls " << ids[i] << " and " << ids[k]);
            CHECK_FALSE(hit);
        }
    }
    dlg.CloseDialog();
}

TEST_CASE("SPW3 grid columns follow the window width", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    REQUIRE(dlg.IsWindow());
    const RECT design = SpwWindowRect(dlg);
    CHECK(dlg.GridColumns() == kSpwDesignCols);
    dlg.ResizeClient(kSpwWideW, kSpwWideH);
    SpwPump();
    CHECK(dlg.GridColumns() == kSpwWideCols);
    dlg.ResizeClient(design.right - design.left, design.bottom - design.top);
    SpwPump();
    CHECK(dlg.GridColumns() == kSpwDesignCols);
    //不能缩到最小尺寸（设计尺寸）以下
    dlg.ResizeClient(kSpwTinyW, kSpwTinyH);
    SpwPump();
    const RECT tiny = SpwWindowRect(dlg);
    CHECK(tiny.right - tiny.left >= kSpwDesignW);
    CHECK(tiny.bottom - tiny.top >= design.bottom - design.top);
    dlg.CloseDialog();
}

TEST_CASE("SPW4 reopening restores the same picker window", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    REQUIRE(dlg.IsWindow());
    const HWND first = dlg.m_hWnd;
    dlg.ShowWindow(SW_MINIMIZE);
    SpwPump();
    REQUIRE(::IsIconic(first) != FALSE);
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    SpwPump();
    CHECK(dlg.m_hWnd == first);
    CHECK(::IsIconic(first) == FALSE);
    CHECK(::IsWindowVisible(first) != FALSE);
    dlg.CloseDialog();
}

TEST_CASE("SPW5 record picker offers the caption check and its hint", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    //不提供字幕：没有这两项
    dlg.SetCaptionOptions(false, true);
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    REQUIRE(dlg.IsWindow());
    CHECK(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlCaptionCheck) == NULL);
    CHECK(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlCaptionHint) == NULL);
    dlg.CloseDialog();

    dlg.SetCaptionOptions(true, true);
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    REQUIRE(dlg.IsWindow());
    const RECT rcWnd = SpwWindowRect(dlg);
    const int winW = rcWnd.right - rcWnd.left;
    balloonwjui::DuiControl* content = dlg.GetClientContent();
    REQUIRE(content != NULL);
    const UINT ids[] = { CSharePickerDialog::kCtrlMicCheck, CSharePickerDialog::kCtrlSysAudioCheck,
                         CSharePickerDialog::kCtrlCameraCheck, CSharePickerDialog::kCtrlCaptionCheck };
    const size_t count = sizeof(ids) / sizeof(ids[0]);
    RECT prev = { 0, 0, 0, 0 };
    for (size_t i = 0; i < count; ++i)
    {
        INFO("control " << ids[i]);
        balloonwjui::DuiButton* b = dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(ids[i]));
        REQUIRE(b != NULL);
        const RECT rc = b->GetRect();
        CHECK(rc.left >= 0);
        CHECK(rc.right <= winW);
        const int need = i18n::MeasureUiTextWidth(b->GetText().GetString(), i18n::DefaultFontPixelHeight(), false) +
                         kSpwCheckExtra;
        CHECK(rc.right - rc.left >= need);
        if (i > 0)
        {
            CHECK(rc.top == prev.top);
            CHECK(rc.left >= prev.right);
        }
        prev = rc;
    }
    balloonwjui::DuiButton* caption =
        dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CSharePickerDialog::kCtrlCaptionCheck));
    balloonwjui::DuiLabel* hint =
        dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(CSharePickerDialog::kCtrlCaptionHint));
    REQUIRE(caption != NULL);
    REQUIRE(hint != NULL);
    CHECK(caption->IsChecked());
    //默认勾着「录制电脑声音」：有声音可识别，没有提示
    CHECK(std::wstring(hint->GetText().GetString()).empty());
    //两路声音都取消：出现提示
    balloonwjui::DuiButton* mic =
        dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CSharePickerDialog::kCtrlMicCheck));
    balloonwjui::DuiButton* sys =
        dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CSharePickerDialog::kCtrlSysAudioCheck));
    REQUIRE(mic != NULL);
    REQUIRE(sys != NULL);
    mic->SetCheck(false, false);
    sys->SetCheck(false, false);
    SpwValueChanged(dlg, CSharePickerDialog::kCtrlSysAudioCheck);
    CHECK_FALSE(std::wstring(hint->GetText().GetString()).empty());
    //勾回麦克风：提示消失
    mic->SetCheck(true, false);
    SpwValueChanged(dlg, CSharePickerDialog::kCtrlMicCheck);
    CHECK(std::wstring(hint->GetText().GetString()).empty());
    dlg.CloseDialog();
}

TEST_CASE("SPW6 record picker offers save to cloud in the button row", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    //企业没有开通：有这一项，但不可勾
    dlg.SetCloudSaveOption(false);
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    REQUIRE(dlg.IsWindow());
    balloonwjui::DuiButton* cloud =
        dynamic_cast<balloonwjui::DuiButton*>(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlCloudSave));
    REQUIRE(cloud != NULL);
    CHECK_FALSE(cloud->IsEnabled());
    CHECK_FALSE(cloud->IsChecked());
    dlg.CloseDialog();

    //开通：可勾，默认不勾；在按钮一行的左端，放得下文字，与按钮互不重叠
    dlg.SetCloudSaveOption(true);
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    REQUIRE(dlg.IsWindow());
    const RECT rcWnd = SpwWindowRect(dlg);
    const int winW = rcWnd.right - rcWnd.left;
    balloonwjui::DuiControl* content = dlg.GetClientContent();
    REQUIRE(content != NULL);
    cloud = dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CSharePickerDialog::kCtrlCloudSave));
    REQUIRE(cloud != NULL);
    CHECK(cloud->IsEnabled());
    CHECK_FALSE(cloud->IsChecked());
    const RECT rcCloud = cloud->GetRect();
    CHECK(rcCloud.left >= 0);
    const int need = i18n::MeasureUiTextWidth(cloud->GetText().GetString(), i18n::DefaultFontPixelHeight(), false) +
                     kSpwCheckExtra;
    CHECK(rcCloud.right - rcCloud.left >= need);
    const UINT ids[] = { CSharePickerDialog::kCtrlCancel, CSharePickerDialog::kCtrlRegion,
                         CSharePickerDialog::kCtrlWhiteboard, CSharePickerDialog::kCtrlAudioOnly,
                         CSharePickerDialog::kCtrlOk };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("control " << ids[i]);
        balloonwjui::DuiControl* b = content->FindCtrlById(ids[i]);
        REQUIRE(b != NULL);
        const RECT rc = b->GetRect();
        CHECK(rc.top == rcCloud.top);
        CHECK(rc.left >= rcCloud.right);
        CHECK(rc.right <= winW);
    }
    dlg.CloseDialog();

    //发起共享：没有这一项
    dlg.Open(NULL, NULL, share_picker_start, std::wstring());
    REQUIRE(dlg.IsWindow());
    CHECK(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlCloudSave) == NULL);
    dlg.CloseDialog();
}

TEST_CASE("SPW7 share picker offers self captions in the button row", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    //不提供字幕：没有这一项
    dlg.SetCaptionOptions(false, true);
    dlg.Open(NULL, NULL, share_picker_start, std::wstring());
    REQUIRE(dlg.IsWindow());
    CHECK(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlCaptionCheck) == NULL);
    dlg.CloseDialog();

    //发起共享：在按钮一行，密码之后、取消之前
    dlg.SetCaptionOptions(true, true);
    dlg.Open(NULL, NULL, share_picker_start, std::wstring());
    REQUIRE(dlg.IsWindow());
    const RECT rcWnd = SpwWindowRect(dlg);
    const int winW = rcWnd.right - rcWnd.left;
    const int winH = rcWnd.bottom - rcWnd.top;
    balloonwjui::DuiControl* content = dlg.GetClientContent();
    balloonwjui::DuiButton* caption =
        dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CSharePickerDialog::kCtrlCaptionCheck));
    REQUIRE(caption != NULL);
    CHECK(caption->IsChecked());
    CHECK(std::wstring(caption->GetText().GetString()) == std::wstring(TR(L"为我的发言生成字幕")));
    const RECT rcCap = caption->GetRect();
    CHECK(rcCap.left >= 0);
    CHECK(rcCap.right <= winW);
    CHECK(rcCap.bottom <= winH);
    const int need = i18n::MeasureUiTextWidth(caption->GetText().GetString(), i18n::DefaultFontPixelHeight(), false) +
                     kSpwCheckExtra;
    CHECK(rcCap.right - rcCap.left >= need);
    const UINT ids[] = { CSharePickerDialog::kCtrlPasswordCheck, CSharePickerDialog::kCtrlPasswordEdit,
                         CSharePickerDialog::kCtrlCancel, CSharePickerDialog::kCtrlOk };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("control " << ids[i]);
        balloonwjui::DuiControl* c = content->FindCtrlById(ids[i]);
        REQUIRE(c != NULL);
        const RECT rc = c->GetRect();
        RECT overlap;
        CHECK_FALSE(::IntersectRect(&overlap, &rc, &rcCap));
        CHECK(rc.top == rcCap.top);
        CHECK(rc.right <= winW);
    }
    dlg.CloseDialog();

    //通话中发起：同样提供，默认不勾时不勾，与电脑声音不重叠
    dlg.SetCaptionOptions(true, false);
    dlg.Open(NULL, NULL, share_picker_start_in_call, std::wstring());
    REQUIRE(dlg.IsWindow());
    content = dlg.GetClientContent();
    caption = dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(CSharePickerDialog::kCtrlCaptionCheck));
    REQUIRE(caption != NULL);
    CHECK_FALSE(caption->IsChecked());
    balloonwjui::DuiControl* sys = content->FindCtrlById(CSharePickerDialog::kCtrlSysAudioCheck);
    REQUIRE(sys != NULL);
    RECT overlap;
    const RECT rcSys = sys->GetRect();
    const RECT rcCall = caption->GetRect();
    CHECK_FALSE(::IntersectRect(&overlap, &rcSys, &rcCall));
    dlg.CloseDialog();

    //切换内容：没有
    dlg.SetCaptionOptions(true, true);
    dlg.Open(NULL, NULL, share_picker_switch, std::wstring());
    REQUIRE(dlg.IsWindow());
    CHECK(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlCaptionCheck) == NULL);
    dlg.CloseDialog();
}

TEST_CASE("SPW8 an open picker adopts the title and minimum size of a new mode", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    dlg.Open(NULL, NULL, share_picker_switch, std::wstring());
    REQUIRE(dlg.IsWindow());
    const HWND first = dlg.m_hWnd;
    RECT rc = SpwWindowRect(dlg);
    CHECK(rc.bottom - rc.top == SharePickerDialogHeight(share_picker_switch));
    //不关窗换成发起共享：同一个窗口，高度撑到发起共享的设计高度
    dlg.Open(NULL, NULL, share_picker_start, std::wstring());
    SpwPump();
    CHECK(dlg.m_hWnd == first);
    rc = SpwWindowRect(dlg);
    CHECK(rc.bottom - rc.top >= SharePickerDialogHeight(share_picker_start));
    //最小尺寸也按新用途：缩不到发起共享的设计高度以下
    dlg.ResizeClient(kSpwTinyW, kSpwTinyH);
    SpwPump();
    rc = SpwWindowRect(dlg);
    CHECK(rc.bottom - rc.top >= SharePickerDialogHeight(share_picker_start));
    CHECK(rc.right - rc.left >= SharePickerDialogWidth());
    //换成录制：标题改成录制的说法
    dlg.Open(NULL, NULL, share_picker_record, std::wstring());
    SpwPump();
    wchar_t title[kSpwTitleChars] = { 0 };
    ::GetWindowTextW(dlg.m_hWnd, title, kSpwTitleChars);
    CHECK(std::wstring(title) == std::wstring(TR(L"选择要录制的内容")));
    dlg.CloseDialog();
}

TEST_CASE("SPW9 the confirm button says start when an approved sharer picks for the first time", "[.][sharepicker-wnd]")
{
    CSharePickerDialog dlg;
    //共享过程中切换内容：「切换」
    dlg.Open(NULL, NULL, share_picker_switch, std::wstring());
    REQUIRE(dlg.IsWindow());
    balloonwjui::DuiButton* ok =
        dynamic_cast<balloonwjui::DuiButton*>(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlOk));
    REQUIRE(ok != NULL);
    CHECK(std::wstring(ok->GetText().GetString()) == std::wstring(TR(L"切换")));
    dlg.CloseDialog();
    //多人共享里经主持人同意后第一次选择（share_picker_switch_to_start）：界面同切换内容（没有标题与选项），按钮为「开始共享」
    dlg.Open(NULL, NULL, share_picker_switch_to_start, std::wstring(L"approved"));
    REQUIRE(dlg.IsWindow());
    ok = dynamic_cast<balloonwjui::DuiButton*>(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlOk));
    REQUIRE(ok != NULL);
    CHECK(std::wstring(ok->GetText().GetString()) == std::wstring(TR(L"开始共享")));
    CHECK(dlg.GetClientContent()->FindCtrlById(CSharePickerDialog::kCtrlPasswordCheck) == NULL);
    dlg.CloseDialog();
}
