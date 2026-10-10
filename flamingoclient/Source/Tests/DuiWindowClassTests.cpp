/**
 *  balloonui 框架窗口实际注册的窗口类与双击行为（标签 [duiwndclass]），DuiWindowClassTests.cpp
 *
 *  背景：DuiFrameWindow 原先声明的窗口类 __DuiFrameWindow__（样式 0）从未生效：ATL 的 CWindowImpl<T>::Create 按模板参数
 *  T 取窗口类，而 DuiHost 是 CWindowImpl<DuiHost>。修复前本文件先在不改 balloonui 的前提下测出了实际情况（2026-10-06）：
 *  类名 __DuiHost__、样式 0xb（CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS）；快速双击按钮只得到 1 次 DUIN_CLICK 与 1 次 DUIN_DBLCLK，
 *  第二击丢失。用户批准后修复：DuiFrameWindow 声明改为与实际相同的样式并新增内联 Create 使其生效；DuiButton 新增内联
 *  OnLButtonDblClk，把第二击按一次按下处理后再上报双击。现在断言修复后的行为：
 *    - DWC1 类名为 __DuiFrameWindow__，样式仍含 CS_DBLCLKS 与整窗重绘；
 *    - DWC2 单击 + 系统送来的双击：DUIN_CLICK 2 次、DUIN_DBLCLK 1 次。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace {

//被测按钮的 ctrlId（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）
const UINT kDwcButton = 10;
//测试窗口的整窗尺寸（像素）
const int kDwcWidth = 240;
const int kDwcHeight = 160;
//读窗口类名的缓冲长度（字符）
const int kDwcClassNameLen = 128;

//记下按钮通知次数的框架窗口
class DwcFrame : public balloonwjui::DuiFrameWindow
{
public:
    DwcFrame()
        : m_clicks(0)
        , m_dblClicks(0)
    {
    }

    BEGIN_MSG_MAP(DwcFrame)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

    int m_clicks;      //收到被测按钮 DUIN_CLICK 的次数
    int m_dblClicks;   //收到被测按钮 DUIN_DBLCLK 的次数

private:
    LRESULT OnNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
    {
        bHandled = FALSE;
        const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
        if (n == NULL || n->ctrlId != kDwcButton)
            return 0;
        if (n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
            ++m_clicks;
        else if (n->code == static_cast<UINT>(balloonwjui::DUIN_DBLCLK))
            ++m_dblClicks;
        return 0;
    }
};

//创建不显示的测试窗口，客户区放一个按钮；返回按钮中心（客户区坐标）
bool DwcCreate(DwcFrame& frame, POINT& center)
{
    if (frame.Create(NULL, CWindow::rcDefault, _T("dwc"), WS_POPUP, WS_EX_TOOLWINDOW) == NULL)
        return false;
    frame.ResizeClient(kDwcWidth, kDwcHeight);
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetCtrlId(kDwcButton);
    btn->SetText(_T("OK"));
    balloonwjui::DuiButton* raw = btn.get();
    frame.SetClientContent(std::move(btn));
    RECT rc = raw->GetRect();
    if (rc.right <= rc.left || rc.bottom <= rc.top)
    {
        //尚未排版：按客户区排一次
        RECT client;
        ::GetClientRect(frame.m_hWnd, &client);
        frame.GetClientContent()->ForceLayout(client);
        rc = raw->GetRect();
    }
    center.x = (rc.left + rc.right) / 2;
    center.y = (rc.top + rc.bottom) / 2;
    return rc.right > rc.left && rc.bottom > rc.top;
}

} // namespace

TEST_CASE("DWC1 DuiFrameWindow 实际注册的窗口类", "[duiwndclass]")
{
    DwcFrame frame;
    POINT center;
    REQUIRE(DwcCreate(frame, center));

    wchar_t cls[kDwcClassNameLen] = { 0 };
    ::GetClassNameW(frame.m_hWnd, cls, kDwcClassNameLen);
    const ULONG_PTR style = ::GetClassLongPtrW(frame.m_hWnd, GCL_STYLE);
    INFO("class=" << CW2A(cls, CP_UTF8) << " style=0x" << std::hex << style);
    CHECK(std::wstring(cls) == L"__DuiFrameWindow__");
    CHECK((style & CS_DBLCLKS) != 0);
    CHECK((style & CS_HREDRAW) != 0);
    CHECK((style & CS_VREDRAW) != 0);
    frame.DestroyWindow();
}

TEST_CASE("DWC2 快速双击按钮时收到的通知", "[duiwndclass]")
{
    DwcFrame frame;
    POINT center;
    REQUIRE(DwcCreate(frame, center));
    const LPARAM pos = MAKELPARAM(center.x, center.y);

    //第一下：完整的单击
    frame.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, pos);
    frame.SendMessage(WM_LBUTTONUP, 0, pos);
    CHECK(frame.m_clicks == 1);

    //第二下在系统双击时限内：窗口类带 CS_DBLCLKS 时，系统送的是 WM_LBUTTONDBLCLK 而不是 WM_LBUTTONDOWN
    frame.SendMessage(WM_LBUTTONDBLCLK, MK_LBUTTON, pos);
    frame.SendMessage(WM_LBUTTONUP, 0, pos);
    CHECK(frame.m_clicks == 2);
    CHECK(frame.m_dblClicks == 1);
    frame.DestroyWindow();
}
