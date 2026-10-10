/**
 *  屏幕共享右下角提示 CShareCornerToast 的单元测试（标签 [sharetoast]），ShareCornerToastTests.cpp
 *  覆盖（2026-10-05 联调反馈：提示框太小，125% 缩放下「发送到本会话」被截去第一个字）：
 *    - SCT1 按钮宽度按实际字宽算：放得下文字加左右留白，不小于设计宽度；
 *    - SCT2 弹出后的提示框不小于 400×152，两个按钮的宽度与 SCT1 一致、整体落在窗口内，主按钮靠右；
 *    - SCT3 按钮文字很长（英文译文）时窗口随之加宽，按钮仍完整落在窗口内。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../../FlamingoMedia/Share/ShareCornerToast.h"
#include "../Common/I18n/I18n.h"
#include "../Common/I18n/I18nLayout.h"

namespace {

//用户要求加大后的提示框最小整窗尺寸（像素）
const int kSctMinWindowW = 400;
const int kSctMinWindowH = 152;
//按钮文字左右至少要留的空白合计（像素），与 balloonui 按钮的默认留白一致
const int kSctTextPadPx = i18n::kDefaultButtonTextPadPx;
//设计上的按钮最小宽度（像素）
const int kSctMinButtonW = 84;

//按钮文字在默认字体下的宽度
int SctTextWidth(const std::wstring& text)
{
    return i18n::MeasureUiTextWidth(text.c_str(), i18n::DefaultFontPixelHeight(), false);
}

//弹出一个提示并核对按钮宽度与位置
void SctCheckToast(const std::wstring& accept, const std::wstring& ignore)
{
    screenshare::ShareToastTexts texts;
    texts.m_title = L"Share";
    texts.m_accept = accept;
    texts.m_ignore = ignore;
    screenshare::CShareCornerToast toast;
    toast.Show(L"saved as board.png", 1, NULL, 1, texts);
    REQUIRE(toast.IsWindow());

    RECT rcWnd;
    toast.GetWindowRect(&rcWnd);
    const int winW = rcWnd.right - rcWnd.left;
    const int winH = rcWnd.bottom - rcWnd.top;
    CHECK(winW >= kSctMinWindowW);
    CHECK(winH >= kSctMinWindowH);

    balloonwjui::DuiControl* content = toast.GetClientContent();
    REQUIRE(content != NULL);
    balloonwjui::DuiControl* join = content->FindCtrlById(screenshare::CShareCornerToast::kIdJoin);
    balloonwjui::DuiControl* skip = content->FindCtrlById(screenshare::CShareCornerToast::kIdIgnore);
    REQUIRE(join != NULL);
    REQUIRE(skip != NULL);
    const RECT rcJoin = join->GetRect();
    const RECT rcSkip = skip->GetRect();
    //两个按钮都放得下自己的文字
    CHECK(rcJoin.right - rcJoin.left >= SctTextWidth(accept) + kSctTextPadPx);
    CHECK(rcSkip.right - rcSkip.left >= SctTextWidth(ignore) + kSctTextPadPx);
    //按钮整体落在窗口内，次按钮在左、主按钮在右，互不重叠
    CHECK(rcSkip.left >= 0);
    CHECK(rcJoin.right <= winW);
    CHECK(rcSkip.right <= rcJoin.left);
    CHECK(rcJoin.bottom <= winH);
    toast.Hide();
}

} // namespace

TEST_CASE("SCT1 corner toast button width follows its text", "[sharetoast]")
{
    const std::wstring shortText = TR(L"同意");
    const std::wstring longText = TR(L"发送到本会话");
    CHECK(screenshare::ShareToastButtonWidth(shortText) >= kSctMinButtonW);
    CHECK(screenshare::ShareToastButtonWidth(longText) >= SctTextWidth(longText) + kSctTextPadPx);
    CHECK(screenshare::ShareToastButtonWidth(L"") == kSctMinButtonW);
    //两个窄按钮时窗口取设计宽度；按钮很宽时窗口加宽
    CHECK(screenshare::ShareToastWindowWidth(kSctMinButtonW, kSctMinButtonW) == kSctMinWindowW);
    CHECK(screenshare::ShareToastWindowWidth(300, 300) > 600);
}

TEST_CASE("SCT2 corner toast is larger and fits its buttons", "[sharetoast]")
{
    SctCheckToast(TR(L"发送到本会话"), TR(L"关闭"));
    SctCheckToast(TR(L"同意"), TR(L"拒绝"));
}

TEST_CASE("SCT3 corner toast grows for long button texts", "[sharetoast]")
{
    SctCheckToast(L"Send to this conversation now", L"Close this notification");
}
