/**
 *  屏幕共享控制键鼠「只在明确结束时才收回」的单元测试（Suite SSKM 续，标签 [sharectl]），ShareControlRevokeTests.cpp
 *  背景：第 18 期起画面主人一动本机鼠标键盘就自动收回控制（低级钩子判定）。用户 2026-10-12 要求：被控制期间画面主人可以
 *  照常操作，控制者不会因此失去控制，控制只在画面主人点「收回控制」或控制者点「结束控制」时结束；非主持人的共享者条上
 *  也要有控制状态与「收回控制」按钮。2026-10-12 与用户确认的用例：
 *    - SSKM9  源码守卫：屏幕共享模块不再安装 WH_MOUSE_LL / WH_KEYBOARD_LL 低级钩子，「本机有动静」消息 0x99 作废；
 *    - SSKM10 共享者条：被某人控制时状态文字以「某某 正在控制你的屏幕」开头并出现「收回控制」按钮（在窗口内、不与其他按钮
 *             重叠），点它向控制器发 share_sharer_revoke_control；控制结束后按钮消失，文字恢复「你正在共享屏幕」；
 *    - SSKM11 开始被控制时的提示文字不再说「动一下鼠标即可收回」，改为提示点工具条上的「收回控制」。
 *  SSKM10 会建出真实的置顶小窗，在隐藏桌面上运行。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareMessages.h"
#include "../../FlamingoMedia/Share/ShareRoomPolicy.h"
#include "../../FlamingoMedia/Share/ShareSharerBar.h"
#include "../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//从本文件往上几级是 flamingoclient 目录（Source\Tests\本文件）
const int kSsrDepth = 3;
//读源文件的块大小（字节）
const size_t kSsrReadChunk = 4096;

//读整个文件；读不到返回空串
std::string SsrReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kSsrReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//flamingoclient 目录
std::string SsrClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kSsrDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//收命令的通知窗口
HWND SsrNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

//按 id 找共享者条上的按钮
balloonwjui::DuiButton* SsrButton(CShareSharerBar& bar, UINT id)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

//模拟点一下按钮
void SsrClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

//共享者条所在的参照矩形（屏幕坐标）
const RECT kSsrAnchor = { 0, 0, 1600, 900 };

} // namespace

TEST_CASE("SSKM9 screen share no longer installs low-level hooks to revoke control", "[sharectl]")
{
    const std::string root = SsrClientRoot();
    REQUIRE_FALSE(root.empty());
    const char* const files[] = { "ShareController.cpp", "ShareControllerControl.cpp", "ShareControllerMulti.cpp",
                                  "ShareController.h", "ShareRemoteControl.h", "ShareRemoteControl.cpp" };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
    {
        INFO("file " << files[i]);
        const std::string src = SsrReadFile(root + "\\FlamingoMedia\\Share\\" + files[i]);
        REQUIRE_FALSE(src.empty());
        CHECK(src.find("WH_MOUSE_LL") == std::string::npos);
        CHECK(src.find("WH_KEYBOARD_LL") == std::string::npos);
        CHECK(src.find("ShareLocalActivityDetector") == std::string::npos);
        CHECK(src.find("kMsgShareLocalActivity") == std::string::npos);
    }
    //消息号 0x99 作废：定义删掉，原处留注释，不再复用
    const std::string msgs = SsrReadFile(root + "\\FlamingoMedia\\Share\\ShareMessages.h");
    REQUIRE_FALSE(msgs.empty());
    CHECK(msgs.find("const UINT kMsgShareLocalActivity") == std::string::npos);
    CHECK(msgs.find("WM_APP + 0x99") != std::string::npos);
}

TEST_CASE("SSKM10 sharer bar shows who is controlling and offers revoke", "[sharectl]")
{
    const HWND notify = SsrNotifyWindow();
    CShareSharerBar bar;
    bar.Show(kSsrAnchor, notify);
    REQUIRE(bar.IsWindow());
    CHECK(bar.TipTextForTest() == std::wstring(TR(L"你正在共享屏幕")));
    CHECK(SsrButton(bar, CShareSharerBar::kIdRevokeControl) == NULL);

    //被控制：文字以「某某 正在控制你的屏幕」开头，出现「收回控制」按钮
    const std::wstring name = L"tom";
    bar.SetControlledBy(name);
    const std::wstring prefix = TRF(L"{0} 正在控制你的屏幕", name);
    CHECK(bar.TipTextForTest().compare(0, prefix.size(), prefix) == 0);
    balloonwjui::DuiButton* revoke = SsrButton(bar, CShareSharerBar::kIdRevokeControl);
    REQUIRE(revoke != NULL);
    CHECK(revoke->IsVisible());
    //按钮在窗口里，不与其他按钮重叠
    RECT client;
    ::GetClientRect(bar.m_hWnd, &client);
    const RECT rc = revoke->GetRect();
    CHECK(rc.left >= client.left);
    CHECK(rc.right <= client.right);
    CHECK(rc.right > rc.left);
    const UINT others[] = { CShareSharerBar::kIdAnnotate, CShareSharerBar::kIdMore, CShareSharerBar::kIdStop };
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); ++i)
    {
        balloonwjui::DuiButton* b = SsrButton(bar, others[i]);
        REQUIRE(b != NULL);
        RECT other = b->GetRect();
        RECT both;
        CHECK_FALSE(::IntersectRect(&both, &rc, &other));
    }

    //点「收回控制」：向控制器发 share_sharer_revoke_control
    MSG msg;
    while (::PeekMessageW(&msg, notify, 0, 0, PM_REMOVE))
    {
    }
    SsrClick(bar.m_hWnd, CShareSharerBar::kIdRevokeControl);
    bool got = false;
    while (::PeekMessageW(&msg, notify, kMsgShareSharerCommand, kMsgShareSharerCommand, PM_REMOVE))
    {
        got = true;
        CHECK(msg.wParam == static_cast<WPARAM>(share_sharer_revoke_control));
    }
    CHECK(got);

    //控制结束：按钮消失，文字恢复
    bar.SetControlledBy(std::wstring());
    CHECK(SsrButton(bar, CShareSharerBar::kIdRevokeControl) == NULL);
    CHECK(bar.TipTextForTest() == std::wstring(TR(L"你正在共享屏幕")));
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SSKM11 the control-start notice points to the revoke button", "[sharectl]")
{
    const std::wstring notice = MakeShareControlStartNotice(L"tom");
    CHECK(notice.find(L"tom") != std::wstring::npos);
    CHECK(notice.find(TR(L"收回控制")) != std::wstring::npos);
    CHECK(notice.find(L"动一下鼠标") == std::wstring::npos);
}
