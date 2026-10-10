/**
 *  控件悬停重画的单元测试（Suite HVR），HoverRepaintTests.cpp
 *
 *  背景：balloonui 的 DuiControl::OnMouseEnter / OnMouseLeave 只改悬停标志、发通知，
 *  不重画；按 IsHover() 绘制悬停效果的控件如果自己不补一次重画，悬停底色 / 描边 / 变色要等别的原因
 *  重画那块区域才出现或消失。2026-10-05 用户发现聊天窗「字幕模式」按钮点过之后浅灰悬停底一直不消失。
 *  用户 2026-10-06 选定的修法：只在会画悬停效果的控件里补重画（DuiButton、DuiComboBox、链接模式的
 *  DuiLabel，以及客户端注册窗的输入框行 RegInput），不在 DuiControl 里统一重画，免得布局容器与大列表
 *  在鼠标进出时整块重画。
 *
 *  覆盖：
 *    HVR1 DuiButton：鼠标移入、移到别的控件、移回后离开整个窗口，三次都请求重画按钮区域；
 *    HVR2 DuiLabel：链接模式移入、移出都请求重画；普通文字模式不画悬停效果，不请求重画；
 *    HVR3 DuiComboBox：移入、移出都请求重画（悬停时边框变色）；
 *    HVR4 布局容器（DuiHBox）的空白处被悬停时不请求重画整个容器，保证没有改成「所有控件都重画」；
 *    HVR5 注册窗实际的控件树：「提交注册」按钮、「服务协议」链接、「公司名称」组合框、「昵称」输入框行
 *         （RegInput，悬停时描边）移入、移出都请求重画。
 *
 *  做法：宿主是放在屏幕外的可见弹出窗口（不可见窗口不维护待重画区域），鼠标消息直接 SendMessage 给宿主，
 *  每步之前先清掉全部待重画区域，返回后用 GetUpdateRect 看被测控件的矩形里有没有待重画区域。
 *  框架 Catch2 v3；全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <objidl.h>    //gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>
#include <memory>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../UI/BalloonRegister/RegisterLayout.h"
#include "../UI/BalloonRegister/RegisterModel.h"   // kRegStepFillInfo：注册的第一步「填写资料」

using balloonwjui::DuiControl;
using balloonwjui::DuiHost;
using balloonwjui::DuiLayout;
using namespace balloonregister;   // HVR5：注册窗的控件树、控件编号与窗口尺寸

namespace {

//宿主窗口左上角坐标：放到屏幕外，测试时不在桌面上出现
const int kHvrOffscreen = -32000;
//单控件用例的宿主尺寸（像素）：左边放被测控件，右边是一块普通控件，供「移到别的控件上」
const int kHvrHostW = 300;
const int kHvrHostH = 60;
//单控件用例里被测控件的宽度（像素）
const int kHvrCtrlW = 120;

//GDI+ 的启动与关闭（注册窗的控件用 GDI+ 绘制），作用域内有效
class HvrGdiplusScope
{
public:
    HvrGdiplusScope()
        : m_token(0)
        , m_ok(false)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    ~HvrGdiplusScope()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，析构时交还
    bool m_ok;           // 是否启动成功
};

//屏幕外的可见宿主窗口，析构时销毁。用 SetRoot 交入控件树后即可发鼠标消息、查待重画区域
class HvrHost
{
public:
    // 建一个宽 w、高 h（像素）的宿主窗口
    HvrHost(int w, int h)
    {
        RECT rc = { kHvrOffscreen, kHvrOffscreen, kHvrOffscreen + w, kHvrOffscreen + h };
        m_host.Create(NULL, rc, _T("HoverRepaintTests"), WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW);
    }

    ~HvrHost()
    {
        if (m_host.IsWindow())
        {
            m_host.DestroyWindow();
        }
    }

    // 窗口是否建好
    bool Ready() const
    {
        return m_host.IsWindow() != FALSE;
    }

    // 交入控件树（所有权转给宿主）并画一遍，之后清掉全部待重画区域
    void SetRoot(std::unique_ptr<DuiControl> root)
    {
        m_host.SetRoot(std::move(root));
        ::UpdateWindow(m_host.m_hWnd);
        ::ValidateRect(m_host.m_hWnd, NULL);
    }

    // 清掉全部待重画区域，再把鼠标移到客户区坐标 pt；返回之后 rc 范围内是否有待重画区域
    bool MoveAndCheckDirty(const POINT& pt, const RECT& rc)
    {
        ::ValidateRect(m_host.m_hWnd, NULL);
        ::SendMessage(m_host.m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
        return DirtyOver(rc);
    }

    // 清掉全部待重画区域，再发 WM_MOUSELEAVE（鼠标离开整个窗口）；返回之后 rc 范围内是否有待重画区域
    bool LeaveAndCheckDirty(const RECT& rc)
    {
        ::ValidateRect(m_host.m_hWnd, NULL);
        ::SendMessage(m_host.m_hWnd, WM_MOUSELEAVE, 0, 0);
        return DirtyOver(rc);
    }

private:
    // 当前待重画区域是否与 rc 相交
    bool DirtyOver(const RECT& rc) const
    {
        RECT dirty;
        ::SetRectEmpty(&dirty);
        if (::GetUpdateRect(m_host.m_hWnd, &dirty, FALSE) == FALSE)
        {
            return false;
        }
        RECT inter;
        return ::IntersectRect(&inter, &dirty, &rc) != FALSE;
    }

    DuiHost m_host;   // 宿主窗口，析构时销毁
};

//矩形中心点
POINT HvrCenter(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

//建「被测控件 + 右侧一块普通控件」的一行，被测控件宽 kHvrCtrlW；经 pCtrl 返回被测控件（借用，宿主持有），
//经 pOther 返回右侧控件
std::unique_ptr<DuiControl> HvrRow(std::unique_ptr<DuiControl> ctrl, DuiControl*& pCtrl, DuiControl*& pOther)
{
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(0);
    row->SetGap(0);
    pCtrl = ctrl.get();
    row->AddChild(std::move(ctrl), DuiLayout::Hint().Fixed(kHvrCtrlW));
    std::unique_ptr<DuiControl> other(new DuiControl());
    pOther = other.get();
    row->AddChild(std::move(other), DuiLayout::Hint().Weight(1));
    return std::unique_ptr<DuiControl>(row.release());
}

//对一个已放进宿主的控件走一遍「移入 → 移到 other → 移回 → 离开窗口」，逐步检查悬停标志与待重画区域。
//expectRepaint 为 false 时检查的是「不请求重画」
void HvrCheckEnterLeave(HvrHost& host, DuiControl* ctrl, DuiControl* other, bool expectRepaint)
{
    const RECT rc = ctrl->GetRect();
    REQUIRE(rc.right > rc.left);
    REQUIRE(rc.bottom > rc.top);
    const POINT on = HvrCenter(rc);
    const POINT away = HvrCenter(other->GetRect());

    const bool enterDirty = host.MoveAndCheckDirty(on, rc);
    CHECK(ctrl->IsHover());
    CHECK(enterDirty == expectRepaint);

    const bool leaveDirty = host.MoveAndCheckDirty(away, rc);
    CHECK_FALSE(ctrl->IsHover());
    CHECK(leaveDirty == expectRepaint);

    host.MoveAndCheckDirty(on, rc);
    CHECK(ctrl->IsHover());
    const bool outDirty = host.LeaveAndCheckDirty(rc);
    CHECK_FALSE(ctrl->IsHover());
    CHECK(outDirty == expectRepaint);
}

} // namespace

TEST_CASE("HVR1 DuiButton 鼠标移入移出都请求重画", "[hover]")
{
    HvrHost host(kHvrHostW, kHvrHostH);
    REQUIRE(host.Ready());
    DuiControl* ctrl = NULL;
    DuiControl* other = NULL;
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetText(_T("Button"));
    host.SetRoot(HvrRow(std::unique_ptr<DuiControl>(btn.release()), ctrl, other));
    HvrCheckEnterLeave(host, ctrl, other, true);
}

TEST_CASE("HVR2 DuiLabel 链接模式移入移出请求重画，普通文字模式不请求", "[hover]")
{
    {
        HvrHost host(kHvrHostW, kHvrHostH);
        REQUIRE(host.Ready());
        DuiControl* ctrl = NULL;
        DuiControl* other = NULL;
        std::unique_ptr<balloonwjui::DuiLabel> link(new balloonwjui::DuiLabel());
        link->SetText(_T("Link"));
        link->SetMode(balloonwjui::DuiLabel::ModeLink);
        host.SetRoot(HvrRow(std::unique_ptr<DuiControl>(link.release()), ctrl, other));
        HvrCheckEnterLeave(host, ctrl, other, true);
    }
    {
        HvrHost host(kHvrHostW, kHvrHostH);
        REQUIRE(host.Ready());
        DuiControl* ctrl = NULL;
        DuiControl* other = NULL;
        std::unique_ptr<balloonwjui::DuiLabel> text(new balloonwjui::DuiLabel());
        text->SetText(_T("Text"));
        host.SetRoot(HvrRow(std::unique_ptr<DuiControl>(text.release()), ctrl, other));
        HvrCheckEnterLeave(host, ctrl, other, false);
    }
}

TEST_CASE("HVR3 DuiComboBox 鼠标移入移出都请求重画", "[hover]")
{
    HvrHost host(kHvrHostW, kHvrHostH);
    REQUIRE(host.Ready());
    DuiControl* ctrl = NULL;
    DuiControl* other = NULL;
    std::unique_ptr<balloonwjui::DuiComboBox> combo(new balloonwjui::DuiComboBox());
    host.SetRoot(HvrRow(std::unique_ptr<DuiControl>(combo.release()), ctrl, other));
    HvrCheckEnterLeave(host, ctrl, other, true);
}

TEST_CASE("HVR4 布局容器的空白处被悬停时不请求重画整个容器", "[hover]")
{
    //外层竖排：上面是被测的横排容器（只在左边放一个按钮，其余是空白），下面是一块普通控件
    HvrHost host(kHvrHostW, kHvrHostH * 2);
    REQUIRE(host.Ready());
    std::unique_ptr<balloonwjui::DuiVBox> outer(new balloonwjui::DuiVBox());
    outer->SetPadding(0);
    outer->SetGap(0);
    std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
    row->SetPadding(0);
    row->SetGap(0);
    row->AddChild(std::unique_ptr<DuiControl>(new balloonwjui::DuiButton()), DuiLayout::Hint().Fixed(kHvrCtrlW));
    DuiControl* pRow = row.get();
    outer->AddChild(std::unique_ptr<DuiControl>(row.release()), DuiLayout::Hint().Fixed(kHvrHostH));
    std::unique_ptr<DuiControl> other(new DuiControl());
    DuiControl* pOther = other.get();
    outer->AddChild(std::move(other), DuiLayout::Hint().Weight(1));
    host.SetRoot(std::unique_ptr<DuiControl>(outer.release()));

    //横排容器右半边是空白：命中测试落在容器本身
    const RECT rcRow = pRow->GetRect();
    RECT rcBlank = rcRow;
    rcBlank.left = rcRow.left + kHvrCtrlW;
    const POINT blank = HvrCenter(rcBlank);
    CHECK_FALSE(host.MoveAndCheckDirty(blank, rcBlank));
    CHECK(pRow->IsHover());
    CHECK_FALSE(host.MoveAndCheckDirty(HvrCenter(pOther->GetRect()), rcBlank));
    CHECK_FALSE(pRow->IsHover());
}

TEST_CASE("HVR5 注册窗的按钮、链接、组合框与输入框行移入移出都请求重画", "[hover][register]")
{
    HvrGdiplusScope gdiplus;
    REQUIRE(gdiplus.Ok());
    //宿主取注册窗标题栏以下的内容区大小
    HvrHost host(kRegWindowWidth, kRegWindowHeight - kRegWindowTitleBarH);
    REQUIRE(host.Ready());
    std::unique_ptr<DuiControl> body = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(body != nullptr);
    DuiControl* root = body.get();
    host.SetRoot(std::move(body));

    DuiControl* submit = root->FindCtrlById(kRegIdBtnSubmit);
    DuiControl* agreement = root->FindCtrlById(kRegIdLinkAgreement);
    DuiControl* company = root->FindCtrlById(kRegIdComboCompany);
    DuiControl* nickEdit = root->FindCtrlById(kRegIdEditNick);
    REQUIRE(submit != NULL);
    REQUIRE(agreement != NULL);
    REQUIRE(company != NULL);
    REQUIRE(nickEdit != NULL);
    //「昵称」输入框行是内嵌输入框的父控件（RegInput）；它的悬停描边只在光标落在左侧图标处时出现
    DuiControl* nickRow = nickEdit->GetParent();
    REQUIRE(nickRow != NULL);

    SECTION("提交注册按钮")
    {
        const RECT rc = submit->GetRect();
        CHECK(host.MoveAndCheckDirty(HvrCenter(rc), rc));
        CHECK(submit->IsHover());
        CHECK(host.LeaveAndCheckDirty(rc));
        CHECK_FALSE(submit->IsHover());
    }
    SECTION("服务协议链接")
    {
        const RECT rc = agreement->GetRect();
        CHECK(host.MoveAndCheckDirty(HvrCenter(rc), rc));
        CHECK(agreement->IsHover());
        CHECK(host.LeaveAndCheckDirty(rc));
        CHECK_FALSE(agreement->IsHover());
    }
    SECTION("公司名称组合框")
    {
        //可编辑组合框中间是内嵌输入框，取右端下拉箭头附近（组合框自身）
        const RECT rc = company->GetRect();
        const POINT arrow = { rc.right - 4, (rc.top + rc.bottom) / 2 };
        CHECK(host.MoveAndCheckDirty(arrow, rc));
        CHECK(company->IsHover());
        CHECK(host.LeaveAndCheckDirty(rc));
        CHECK_FALSE(company->IsHover());
    }
    SECTION("昵称输入框行")
    {
        const RECT rc = nickRow->GetRect();
        const RECT rcEdit = nickEdit->GetRect();
        //输入框左边、行内的图标处
        const POINT icon = { (rc.left + rcEdit.left) / 2, (rc.top + rc.bottom) / 2 };
        CHECK(host.MoveAndCheckDirty(icon, rc));
        CHECK(nickRow->IsHover());
        CHECK(host.LeaveAndCheckDirty(rc));
        CHECK_FALSE(nickRow->IsHover());
    }
}
