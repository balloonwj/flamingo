/**
 *  主持人以外的共享者也能「屏幕当白板」（标签 [sharerboard]），ShareSharerBoardTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.1 节 ③（2026-10-07 用户确认）。原先「屏幕当白板」只给主持人；改为多人共享里其他
 *  共享者也能用，只切换他自己那一块画面：只共享一个窗口时，共享者控制条上多一个「屏幕当白板」，点了先提示「观看者将看到
 *  整块屏幕」，确认后共享内容换成该窗口所在的整块屏幕并进入标注；再点「退出屏幕白板」恢复只共享原窗口。本文件覆盖：
 *    - SSBD1 共享者控制条：共享的是单个窗口时有「屏幕当白板」按钮，点了向控制器发 share_sharer_screen_board；进入后按钮
 *            文字为「退出屏幕白板」；共享整块屏幕时没有这个按钮；按钮出现与消失时整条宽度随之变化，按钮都在窗口里；
 *    - SSBD2 源文件守卫：控制器处理这条命令时调用 toggleSharerScreenBoard；它按窗口取所在显示器的共享源、经
 *            switchPublishSource 切换共享者自己的推流，退出时切回原窗口；switchPublishSource 调用 m_publisher.SwitchSource。
 *  SSBD2 在运行时读取源文件，位置取自 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>
#include "../../FlamingoMedia/Share/ShareMessages.h"
#include "../../FlamingoMedia/Share/ShareSharerBar.h"
#include "../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

using namespace screenshare;

namespace {

//参照矩形（屏幕坐标）
const RECT kSsbdAnchor = { 0, 0, 1600, 900 };
//读文件时每次读的字节数
const size_t kSsbdReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）
const int kSsbdDepth = 3;

std::string SsbdReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kSsbdReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

std::string SsbdClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kSsbdDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//取一个函数的函数体：从定义开始到下一个以 nextPrefix 开头的行（或文件末尾）
std::string SsbdFunctionBody(const std::string& source, const std::string& signature, const std::string& nextPrefix)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t next = source.find("\n" + nextPrefix, begin + signature.size());
    return source.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

HWND SsbdNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

balloonwjui::DuiButton* SsbdButton(CShareSharerBar& bar, UINT id)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    return content != NULL ? dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id)) : NULL;
}

int SsbdWidth(CShareSharerBar& bar)
{
    RECT rc = { 0, 0, 0, 0 };
    ::GetWindowRect(bar.m_hWnd, &rc);
    return rc.right - rc.left;
}

void SsbdClick(HWND wnd, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    ::SendMessageW(wnd, WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

} // namespace

TEST_CASE("SSBD1 sharer bar offers screen-as-whiteboard when sharing a window", "[sharerboard]")
{
    const HWND notify = SsbdNotifyWindow();
    CShareSharerBar bar;
    //共享整块屏幕：没有这个按钮
    bar.SetScreenBoard(false, false);
    bar.Show(kSsbdAnchor, notify);
    REQUIRE(bar.IsWindow());
    CHECK(SsbdButton(bar, CShareSharerBar::kIdScreenBoard) == NULL);
    const int plainW = SsbdWidth(bar);
    //共享单个窗口：有按钮，整条变宽
    bar.SetScreenBoard(true, false);
    balloonwjui::DuiButton* b = SsbdButton(bar, CShareSharerBar::kIdScreenBoard);
    REQUIRE(b != NULL);
    CHECK(b->GetText() == CString(TR(L"屏幕当白板")));
    CHECK(SsbdWidth(bar) > plainW);
    RECT client;
    ::GetClientRect(bar.m_hWnd, &client);
    CHECK(b->GetRect().right <= client.right);
    //点了：向控制器发命令
    MSG msg;
    while (::PeekMessageW(&msg, notify, 0, 0, PM_REMOVE))
    {
    }
    SsbdClick(bar.m_hWnd, CShareSharerBar::kIdScreenBoard);
    bool got = false;
    while (::PeekMessageW(&msg, notify, kMsgShareSharerCommand, kMsgShareSharerCommand, PM_REMOVE))
    {
        got = true;
        CHECK(msg.wParam == static_cast<WPARAM>(share_sharer_screen_board));
    }
    CHECK(got);
    //进入之后：按钮文字为「退出屏幕白板」
    bar.SetScreenBoard(true, true);
    b = SsbdButton(bar, CShareSharerBar::kIdScreenBoard);
    REQUIRE(b != NULL);
    CHECK(b->GetText() == CString(TR(L"退出屏幕白板")));
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("SSBD2 controller switches the sharer's own stream to the monitor and back", "[sharerboard]")
{
    const std::string root = SsbdClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string multi = SsbdReadFile(root + "\\FlamingoMedia\\Share\\ShareControllerMulti.cpp");
    REQUIRE_FALSE(multi.empty());
    const std::string handler =
        SsbdFunctionBody(multi, "LRESULT CShareController::OnSharerCommandMsg(", "LRESULT CShareController::");
    REQUIRE_FALSE(handler.empty());
    CHECK(handler.find("case share_sharer_screen_board:") != std::string::npos);
    CHECK(handler.find("toggleSharerScreenBoard();") != std::string::npos);
    const std::string switchFn =
        SsbdFunctionBody(multi, "void CShareController::switchPublishSource(", "void CShareController::");
    REQUIRE_FALSE(switchFn.empty());
    CHECK(switchFn.find("m_publisher.SwitchSource(") != std::string::npos);

    const std::string board = SsbdReadFile(root + "\\FlamingoMedia\\Share\\ShareControllerBoard.cpp");
    REQUIRE_FALSE(board.empty());
    const std::string toggle =
        SsbdFunctionBody(board, "void CShareController::toggleSharerScreenBoard()", "void CShareController::");
    REQUIRE_FALSE(toggle.empty());
    CHECK(toggle.find("MonitorSourceForWindow(m_publishSource.m_hwnd") != std::string::npos);
    CHECK(toggle.find("switchPublishSource(m_publishScreenBoardSaved)") != std::string::npos);
    CHECK(toggle.find("switchPublishSource(monitor)") != std::string::npos);
}
