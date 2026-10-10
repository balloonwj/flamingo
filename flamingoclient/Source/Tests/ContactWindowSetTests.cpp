/**
 *  联系人资料窗多窗口、本人资料统一与行内编辑按键的单元测试（Suite CWS / CWW），ContactWindowSetTests.cpp
 *
 *  背景（2026-10-07，docs/联系人资料字段扩充方案.md 第 10 节）：
 *    - 联系人资料窗改为一人一窗、可以多个并存：再次打开同一个人时激活已有窗口而不重建界面，新窗口相对
 *      上一个错开摆放，关闭的窗口对象惰性回收（ContactWindowSet）；
 *    - 本人资料统一用联系人资料窗显示，本人资料对话框 CBalloonProfileDlg 删除：资料窗看本人时多一格可就地
 *      编辑的「昵称」（区块第一格），头像显示真实头像、看本人时可点击打开修改头像对话框；
 *    - 资料窗「备注名」与群资料「我在本群的昵称」两处行内编辑补上回车提交、Esc 取消（InlineEditKeys.h）。
 *
 *  覆盖（普通用例，标签 [contactwin]）：
 *    CWS1 新窗口位置的纯函数：居中起点、错开、超出工作区回到起点、比工作区大时对齐左上角；
 *    CWS2 行内编辑通知判定：回车、Esc、失焦三种，必须是同一个输入框且在编辑态；
 *    CWS3 「昵称」格的数据：看本人时在第一格且可编辑，看别人时没有；没填时显示「未填写」；
 *    CWS4 「昵称」格的提交判定：没改动、有改动（去首尾空白）、清空与超长不合格；提交后模型的姓名随之更新；
 *    CWS5 「昵称」格的排版：独占第一行、个性签名第二行，两列格子从第三行起；看本人时按设计高度排得下；
 *    CWS6 头像：看本人时可点击（发出 DUIN_CLICK），看别人时不可点击；模型带头像来源时底色取来源的颜色；
 *    CWS7 群资料「我在本群的昵称」输入框按回车、Esc 发出的通知，经判定分别得到提交与取消。
 *    CWS8 （2026-10-07 补）资料格逐个进入编辑：输入框的行高小于控件高减上下边框（DuiEdit 的单行居中生效，文字不被裁掉），
 *         性别下拉框放得下默认字体一行；群资料的群昵称输入框同样。
 *  覆盖（窗口用例，标签 [.][contact-wnd]，会建真窗口，须在不可见桌面上单独运行 "[contact-wnd]"）：
 *    CWW1 同一个人再次打开：激活已有窗口，正在编辑的备注名内容不丢；「修改备注」入口只让备注名进入编辑态；
 *    CWW2 不同的人并存，第二个窗口相对第一个错开；关闭后对象被回收，再打开时新建；
 *    CWW3 备注名回车提交（回投主面板）、Esc 取消（不回投、恢复原值）；重建界面后编辑态复位；
 *    CWW4 本人「昵称」格回车提交（回投主面板、标题随之更新）、Esc 取消、清空时提示并留在编辑态；
 *    CWW5 头像：看本人时点击回投主面板打开修改头像对话框，看别人时点击没有反应；带头像来源时控件挂上位图。
 *  覆盖（截图用例，标签 [.][contact-shot]，单独运行 "[contact-shot]"）：
 *    CWW6 打开看本人（昵称格在编辑态）与看同事（错开摆放）两个资料窗，用 PrintWindow 存成
 *         %TEMP%\contactwin_self.png、contactwin_peer.png，供人工核对界面。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>
#include <objidl.h>    // gdiplus.h 依赖 IStream 等 COM 声明
#include <gdiplus.h>   // CWW6：截图存成 PNG

#include "../UI/BalloonContact/ContactWindowSet.h"
#include "../UI/BalloonContact/BalloonContactWindow.h"
#include "../UI/BalloonContact/ContactLayout.h"
#include "../UI/BalloonContact/ContactModel.h"
#include "../UI/BalloonMain/InlineEditKeys.h"
#include "../UI/BalloonMain/BalloonGroupInfoLayout.h"
#include "../UI/BalloonMain/AvatarGray.h"          // MakeGrayColor：灰度头像的底色
#include "../Session/ProfileFields.h"
#include "../Session/UserSessionData.h"            // FMG_MSG_SET_CONTACT_REMARK / FMG_MSG_CONTACT_PROFILE_EDIT / FMG_MSG_OPEN_MODIFY_AVATAR
#include "../net/ProfileProtocolClient.h"           // net::ProfileUpdateFields
#include "../net/Protocol.h"                        // enum profile_gender
#include "LayoutFitCheck.h"                         // CWS5：按设计高度排得下
#include "../Common/I18n/I18nLayout.h"              // CWS8：MaxMonitorDpi（写进用例输出）
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   // CWS8：各档 DPI 下的默认字体
#include "TestDpiAwareScope.h"                       // CWS8：按显示器感知 DPI 的线程上下文
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"

namespace {

//被查看的两个同事与本人的 userid（只用来区分窗口，不参与别的断言）
const UINT kCwsPeerA = 70001;
const UINT kCwsPeerB = 70002;
const UINT kCwsSelf = 70009;

//宿主窗口左上角坐标：放到屏幕外，测试时不在桌面上出现
const int kCwsOffscreen = -32000;

//消息泵的轮数与每轮之间的等待（毫秒），让窗口把投给自己的消息处理完
const int kCwsPumpRounds = 3;
const int kCwsPumpSleepMs = 20;

//「昵称」上限（字），与 profilefields::kProfileShortTextMaxChars 一致
const int kCwsNicknameMax = profilefields::kProfileShortTextMaxChars;

//把本线程消息队列里的消息处理完
void CwsPump()
{
    MSG msg;
    for (int i = 0; i < kCwsPumpRounds; ++i)
    {
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(kCwsPumpSleepMs);
    }
}

//看别人的资料模型
ballooncontact::ContactModel MakeCwsPeerModel(const wchar_t* name)
{
    ballooncontact::ContactModel m;
    m.SetBasicInfo(name, _T("10071"), _T("Shanghai"), _T(""), _T(""), _T("Peer signature"),
                   ballooncontact::kContactStatusOnline);
    m.SetProfileFields(profile_gender_male, 0, _T("peer@a.com"), _T(""), _T(""), _T(""));
    m.SetRemark(_T("OldRemark"));
    m.ClearTags();
    m.SetSelf(false);
    return m;
}

//看本人的资料模型：昵称、签名与各字段都填了
ballooncontact::ContactModel MakeCwsSelfModel()
{
    ballooncontact::ContactModel m;
    m.SetBasicInfo(_T("Neo"), _T("10079"), _T("Beijing"), _T("Engineer"), _T("R&D / Client"), _T("My signature"),
                   ballooncontact::kContactStatusOnline);
    m.SetNickname(_T("Neo"));
    m.SetProfileFields(profile_gender_female, 19960812, _T("neo@a.com"), _T("13800000079"), _T("010-7979"),
                       _T("Developer"));
    m.ClearTags();
    m.SetSelf(true);
    return m;
}

//在槽位表里找某个字段所在的槽位；找不到返回 -1
int CwsSlotOf(const std::vector<int>& slots, int field)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i] == field)
        {
            return (int)i;
        }
    }
    return -1;
}

//控件的矩形
RECT CwsRect(balloonwjui::DuiControl* root, UINT id)
{
    balloonwjui::DuiControl* c = root->FindCtrlById(id);
    RECT rc = { 0, 0, 0, 0 };
    if (c != NULL)
    {
        rc = c->GetRect();
    }
    return rc;
}

//矩形中心点
POINT CwsCenter(const RECT& rc)
{
    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    return pt;
}

//记下控件通知的宿主窗口：控件通知（WM_DUI_NOTIFY）发给宿主自己（没有父窗口），在这里逐条记下通知码与控件 id
class CwsNotifyHost : public balloonwjui::DuiHost
{
public:
    //一条记下的通知
    struct Entry
    {
        UINT m_code;     //通知码
        UINT m_ctrlId;   //发出通知的控件 id
    };

    BEGIN_MSG_MAP(CwsNotifyHost)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

    // 在屏幕外建一个宽 w、高 h（像素）的可见宿主窗口
    bool CreateOffscreen(int w, int h)
    {
        RECT rc = { kCwsOffscreen, kCwsOffscreen, kCwsOffscreen + w, kCwsOffscreen + h };
        return Create(NULL, rc, _T("ContactWindowSetTests"), WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW) != NULL;
    }

    // 是否收到过某个控件的某条通知
    bool Has(UINT code, UINT ctrlId) const
    {
        for (size_t i = 0; i < m_entries.size(); ++i)
        {
            if (m_entries[i].m_code == code && m_entries[i].m_ctrlId == ctrlId)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<Entry> m_entries;   //收到的全部控件通知，按到达的先后

private:
    LRESULT OnNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
    {
        const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
        if (n != NULL)
        {
            Entry e;
            e.m_code = n->code;
            e.m_ctrlId = n->ctrlId;
            m_entries.push_back(e);
        }
        return 0;
    }
};

//窗口用例里代替主面板接收回投消息的窗口：只收消息、不显示（HWND_MESSAGE）。投给它的消息由它自己的窗口过程
//记下来：用例里的消息泵（CwsPump）会把本线程的消息全部派发出去，直接从队列里取的话，消息早已被派发掉
class CwsPanelSink
{
public:
    CwsPanelSink()
        : m_oldProc(NULL)
    {
        m_hwnd = ::CreateWindowExW(0, L"STATIC", L"ContactWindowSetTestsSink", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                   NULL, NULL, NULL);
        if (m_hwnd != NULL)
        {
            ::SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
            m_oldProc = reinterpret_cast<WNDPROC>(
                ::SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&CwsPanelSink::SinkProc)));
        }
    }

    ~CwsPanelSink()
    {
        Drain();
        if (m_hwnd != NULL)
        {
            ::SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_oldProc));
            ::DestroyWindow(m_hwnd);
        }
    }

    // 窗口句柄，传给资料窗作宿主主面板
    HWND Hwnd() const
    {
        return m_hwnd;
    }

    // 取出一条投给本窗口的 msgId 消息（先把队列里还没派发的派发掉）；没有时返回 false
    bool Take(UINT msgId, MSG& out)
    {
        Dispatch();
        for (size_t i = 0; i < m_received.size(); ++i)
        {
            if (m_received[i].message == msgId)
            {
                out = m_received[i];
                m_received.erase(m_received.begin() + i);
                return true;
            }
        }
        return false;
    }

    // 丢掉投给本窗口的全部消息，并释放已知消息带的堆负载
    void Drain()
    {
        Dispatch();
        for (size_t i = 0; i < m_received.size(); ++i)
        {
            FreePayload(m_received[i]);
        }
        m_received.clear();
    }

private:
    // 把队列里投给本窗口、还没派发的消息派发掉（窗口过程会记下它们）
    void Dispatch()
    {
        MSG msg;
        while (m_hwnd != NULL && ::PeekMessageW(&msg, m_hwnd, 0, 0, PM_REMOVE))
        {
            ::DispatchMessageW(&msg);
        }
    }

    // 释放已知消息带的堆负载
    static void FreePayload(const MSG& msg)
    {
        if (msg.message == FMG_MSG_SET_CONTACT_REMARK)
        {
            delete reinterpret_cast<std::string*>(msg.lParam);
        }
        else if (msg.message == FMG_MSG_CONTACT_PROFILE_EDIT)
        {
            delete reinterpret_cast<net::ProfileUpdateFields*>(msg.lParam);
        }
    }

    // 窗口过程：记下资料窗回投的消息（WM_USER 以上），其余交回原窗口过程
    static LRESULT CALLBACK SinkProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        CwsPanelSink* self = reinterpret_cast<CwsPanelSink*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self != NULL && uMsg >= WM_USER && uMsg < WM_APP)
        {
            MSG m;
            m.hwnd = hwnd;
            m.message = uMsg;
            m.wParam = wParam;
            m.lParam = lParam;
            m.time = 0;
            m.pt.x = 0;
            m.pt.y = 0;
            self->m_received.push_back(m);
            return 0;
        }
        WNDPROC old = (self != NULL) ? self->m_oldProc : NULL;
        return (old != NULL) ? ::CallWindowProcW(old, hwnd, uMsg, wParam, lParam)
                             : ::DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    HWND m_hwnd;                  //只收消息的窗口
    WNDPROC m_oldProc;            //换掉之前的窗口过程，析构时换回
    std::vector<MSG> m_received;  //窗口过程记下的回投消息，按到达的先后
};

//窗口用例里的提示框替身：记下最近一次提示的文字与次数，不弹模态框（不可见桌面上的模态框没人点）
std::wstring g_cwsLastPrompt;
int g_cwsPromptCount = 0;

void CwsPromptHook(HWND /*owner*/, LPCTSTR text)
{
    g_cwsLastPrompt = (text != NULL) ? text : L"";
    ++g_cwsPromptCount;
}

//用例期间换上提示框替身，结束时恢复
struct CwsScopedPromptHook
{
    CwsScopedPromptHook()
    {
        g_cwsLastPrompt.clear();
        g_cwsPromptCount = 0;
        CBalloonContactWindow::SetPromptHook(&CwsPromptHook);
    }
    ~CwsScopedPromptHook()
    {
        CBalloonContactWindow::SetPromptHook(NULL);
    }
};

//资料窗里某个控件
template <typename T>
T* CwsCtrl(CBalloonContactWindow* w, UINT id)
{
    balloonwjui::DuiControl* content = w->GetClientContent();
    return (content != NULL) ? dynamic_cast<T*>(content->FindCtrlById(id)) : NULL;
}

//给资料窗发一条控件点击通知（与控件发出的通知相同）
void CwsClick(CBalloonContactWindow* w, UINT ctrlId)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = ctrlId;
    n.pCtrl = NULL;
    n.extra = 0;
    w->SendMessage(WM_DUI_NOTIFY, static_cast<WPARAM>(ctrlId), reinterpret_cast<LPARAM>(&n));
}

//给资料窗发一次按键（经宿主转给当前的 DUI 焦点控件）
void CwsKey(CBalloonContactWindow* w, UINT vk)
{
    w->SendMessage(WM_KEYDOWN, static_cast<WPARAM>(vk), 0);
    CwsPump();
}

} // namespace

// =====================================================================
// 普通用例
// =====================================================================

TEST_CASE("CWS1 新资料窗的位置：居中起点、错开、超出工作区回到起点、比工作区大时对齐左上角", "[contactwin]")
{
    const RECT work = { 0, 0, 1920, 1040 };
    const SIZE size = { 600, 700 };
    const int step = ballooncontact::kContactWindowCascadeStep;
    CHECK(step == 24);

    //没有上一个窗口：工作区居中
    POINT none = { 0, 0 };
    const POINT origin = ballooncontact::ComputeContactWindowCascade(work, size, false, none, step);
    CHECK(origin.x == (1920 - 600) / 2);
    CHECK(origin.y == (1040 - 700) / 2);

    //有上一个窗口：向右下错开一步
    const POINT next = ballooncontact::ComputeContactWindowCascade(work, size, true, origin, step);
    CHECK(next.x == origin.x + step);
    CHECK(next.y == origin.y + step);

    //错开后会超出下边：回到起点
    POINT lowPrev = { 100, 1040 - 700 - 10 };
    const POINT wrapped = ballooncontact::ComputeContactWindowCascade(work, size, true, lowPrev, step);
    CHECK(wrapped.x == origin.x);
    CHECK(wrapped.y == origin.y);

    //错开后会超出右边：同样回到起点
    POINT rightPrev = { 1920 - 600 - 5, 50 };
    const POINT wrappedRight = ballooncontact::ComputeContactWindowCascade(work, size, true, rightPrev, step);
    CHECK(wrappedRight.x == origin.x);
    CHECK(wrappedRight.y == origin.y);

    //上一个窗口被拖到了工作区外（左上方）：错开后的位置夹回工作区内
    POINT outside = { -500, -400 };
    const POINT clamped = ballooncontact::ComputeContactWindowCascade(work, size, true, outside, step);
    CHECK(clamped.x == 0);
    CHECK(clamped.y == 0);

    //工作区不在原点（第二块显示器）：结果仍在该工作区里
    const RECT second = { 1920, 0, 1920 + 1280, 984 };
    const POINT o2 = ballooncontact::ComputeContactWindowCascade(second, size, false, none, step);
    CHECK(o2.x == 1920 + (1280 - 600) / 2);
    CHECK(o2.x + size.cx <= second.right);
    CHECK(o2.y + size.cy <= second.bottom);

    //窗口比工作区还大：左上角对齐工作区
    const SIZE huge = { 2500, 1200 };
    const POINT big = ballooncontact::ComputeContactWindowCascade(work, huge, true, origin, step);
    CHECK(big.x == work.left);
    CHECK(big.y == work.top);

    //工作区无效：返回工作区左上角，不做除法以外的计算
    const RECT empty = { 10, 20, 10, 20 };
    const POINT e = ballooncontact::ComputeContactWindowCascade(empty, size, true, origin, step);
    CHECK(e.x == 10);
    CHECK(e.y == 20);

    //错开距离不为正：按 0 处理，即与上一个窗口重合（仍在工作区内）
    const POINT zero = ballooncontact::ComputeContactWindowCascade(work, size, true, next, -5);
    CHECK(zero.x == next.x);
    CHECK(zero.y == next.y);
}

TEST_CASE("CWS2 行内编辑通知判定：回车提交、Esc 取消、失焦延后，必须是同一个输入框且在编辑态", "[contactwin]")
{
    const UINT kEdit = 1022;
    const UINT kOther = 1023;
    const UINT kEnter = (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ENTER;
    const UINT kEscape = (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE;
    const UINT kKill = (UINT)balloonwjui::DUIN_KILLFOCUS;

    CHECK(balloonmain::ClassifyInlineEditNotify(kEnter, kEdit, kEdit, true) == balloonmain::inline_edit_commit);
    CHECK(balloonmain::ClassifyInlineEditNotify(kEscape, kEdit, kEdit, true) == balloonmain::inline_edit_cancel);
    CHECK(balloonmain::ClassifyInlineEditNotify(kKill, kEdit, kEdit, true) == balloonmain::inline_edit_blur);

    //别的控件发来的同值通知（自定义通知码按控件各自编号）：不处理
    CHECK(balloonmain::ClassifyInlineEditNotify(kEnter, kOther, kEdit, true) == balloonmain::inline_edit_none);
    CHECK(balloonmain::ClassifyInlineEditNotify(kEscape, kOther, kEdit, true) == balloonmain::inline_edit_none);
    //不在编辑态（例如提交之后输入框随之失焦）：不处理
    CHECK(balloonmain::ClassifyInlineEditNotify(kEnter, kEdit, kEdit, false) == balloonmain::inline_edit_none);
    CHECK(balloonmain::ClassifyInlineEditNotify(kKill, kEdit, kEdit, false) == balloonmain::inline_edit_none);
    //点击等其它通知：不处理
    CHECK(balloonmain::ClassifyInlineEditNotify((UINT)balloonwjui::DUIN_CLICK, kEdit, kEdit, true)
          == balloonmain::inline_edit_none);
}

TEST_CASE("CWS3 昵称格：看本人时在第一格且可编辑，看别人时没有；没填时显示未填写", "[contactwin][contactprofile]")
{
    const std::vector<ballooncontact::ContactProfileCell> selfCells =
        ballooncontact::BuildContactProfileCells(MakeCwsSelfModel());
    REQUIRE(selfCells.size() == (size_t)ballooncontact::kContactProfileFieldCount);
    CHECK(selfCells[0].m_field == ballooncontact::kProfileFieldNickname);
    CHECK(selfCells[0].m_label == CString(_T("昵称")));
    CHECK(selfCells[0].m_value == CString(_T("Neo")));
    CHECK(selfCells[0].m_editable);
    CHECK_FALSE(selfCells[0].m_isEmpty);
    CHECK_FALSE(selfCells[0].m_hasVisibility);
    //个性签名紧随其后
    CHECK(selfCells[1].m_field == ballooncontact::kProfileFieldSignature);
    //昵称、签名各占一行，其余七格两列四行
    CHECK(ballooncontact::ContactProfileRowCount(selfCells) == 6);

    //看别人：姓名已在头部，区块里不列昵称
    const std::vector<ballooncontact::ContactProfileCell> peerCells =
        ballooncontact::BuildContactProfileCells(MakeCwsPeerModel(L"Ann"));
    for (size_t i = 0; i < peerCells.size(); ++i)
    {
        CAPTURE(i);
        CHECK(peerCells[i].m_field != ballooncontact::kProfileFieldNickname);
    }

    //本人没填昵称：弱色「未填写」，进入编辑时输入框为空
    ballooncontact::ContactModel noNick = MakeCwsSelfModel();
    noNick.SetNickname(_T(""));
    const std::vector<ballooncontact::ContactProfileCell> cells = ballooncontact::BuildContactProfileCells(noNick);
    REQUIRE_FALSE(cells.empty());
    CHECK(cells[0].m_field == ballooncontact::kProfileFieldNickname);
    CHECK(cells[0].m_isEmpty);
    CHECK(cells[0].m_value == CString(_T("未填写")));
    CHECK(ballooncontact::ProfileCellEditText(noNick, ballooncontact::kProfileFieldNickname).IsEmpty());
    CHECK(ballooncontact::ProfileCellEditText(MakeCwsSelfModel(), ballooncontact::kProfileFieldNickname)
          == CString(_T("Neo")));
}

TEST_CASE("CWS4 昵称格的提交判定：没改动、有改动、清空与超长不合格；提交后模型的姓名随之更新", "[contactwin][contactprofile]")
{
    const ballooncontact::ContactModel model = MakeCwsSelfModel();
    net::ProfileUpdateFields fields;
    profilefields::ProfileEditError error = profilefields::profile_edit_ok;

    //与原值相同（含首尾空白）：没改动
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldNickname, _T("  Neo "), -1,
                                                  fields, error) == ballooncontact::profile_cell_unchanged);
    //有改动：只标记昵称一项，去掉首尾空白
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldNickname, _T(" Trinity "), -1,
                                                  fields, error) == ballooncontact::profile_cell_submit);
    CHECK(fields.m_hasNickname);
    CHECK(fields.m_values.m_nickname == "Trinity");
    CHECK_FALSE(fields.m_hasSignature);
    CHECK_FALSE(fields.m_hasPost);
    CHECK(net::ProfileProtocolClient::buildUpdateProfileRequest(fields) == "{\"nickname\":\"Trinity\"}");

    //清空（含只有空白）：不合格，昵称不能为空
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldNickname, _T("   "), -1,
                                                  fields, error) == ballooncontact::profile_cell_invalid);
    CHECK(error == profilefields::profile_edit_nickname_empty);
    CHECK_FALSE(profilefields::ProfileEditErrorText(error).empty());

    //超长：上限之内合格，多一个字不合格（中文与英文同算一个字）
    CString atLimit;
    for (int i = 0; i < kCwsNicknameMax; ++i)
    {
        atLimit += _T("昵");
    }
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldNickname, atLimit, -1,
                                                  fields, error) == ballooncontact::profile_cell_submit);
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldNickname, atLimit + _T("x"), -1,
                                                  fields, error) == ballooncontact::profile_cell_invalid);
    CHECK(error == profilefields::profile_edit_nickname_too_long);

    //校验只查提交了的项：没提交昵称时空昵称不拦
    net::ProfileUpdateFields other;
    other.m_hasPost = true;
    other.m_values.m_post = "Lead";
    CHECK(profilefields::ValidateProfileUpdate(other) == profilefields::profile_edit_ok);

    //提交后先写进模型：昵称与头部、标题用的姓名一起更新，其余字段不变
    ballooncontact::ContactModel edited = model;
    net::ProfileUpdateFields rename;
    rename.m_hasNickname = true;
    rename.m_values.m_nickname = "Trinity";
    edited.ApplyProfileUpdate(rename);
    CHECK(edited.Nickname() == CString(_T("Trinity")));
    CHECK(edited.Name() == CString(_T("Trinity")));
    CHECK(edited.Post() == CString(_T("Developer")));
    CHECK(edited.Signature() == CString(_T("My signature")));
}

TEST_CASE("CWS5 昵称格独占第一行、个性签名第二行，两列格子从第三行起；看本人时按设计高度排得下", "[contactwin][contactprofile][layoutfit]")
{
    const std::vector<ballooncontact::ContactProfileCell> cells =
        ballooncontact::BuildContactProfileCells(MakeCwsSelfModel());
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    ballooncontact::ApplyContactSelfView(root.get(), true);
    std::vector<int> slots;
    const int rows = ballooncontact::ConfigureContactProfile(root.get(), cells, false, false, slots);
    REQUIRE(rows == 6);
    REQUIRE(slots.size() == (size_t)ballooncontact::kContactProfileSlotCount);
    CHECK(slots[0] == ballooncontact::kProfileFieldNickname);
    CHECK(slots[1] == ballooncontact::kProfileFieldSignature);
    CHECK(slots[2] == ballooncontact::kProfileFieldGender);
    CHECK(CwsSlotOf(slots, ballooncontact::kProfileFieldMobile) == ballooncontact::kContactProfileSlotCount - 1);

    const int height = ballooncontact::ContactBodyHeight(0, 1, rows, true);
    RECT rc = { 0, 0, ballooncontact::kContactBodyWidth, height };
    root->ForceLayout(rc);
    ballooncontact::RelayoutContactBody(root.get());

    const int left = ballooncontact::kContactBodyPaddingH;
    const int right = ballooncontact::kContactBodyWidth - ballooncontact::kContactBodyPaddingH;
    const RECT nick = CwsRect(root.get(), ballooncontact::ContactProfileCtrlId(0, ballooncontact::kProfilePartCell));
    const RECT sig = CwsRect(root.get(), ballooncontact::ContactProfileCtrlId(1, ballooncontact::kProfilePartCell));
    const RECT gender = CwsRect(root.get(), ballooncontact::ContactProfileCtrlId(2, ballooncontact::kProfilePartCell));
    const RECT birthday = CwsRect(root.get(), ballooncontact::ContactProfileCtrlId(3, ballooncontact::kProfilePartCell));
    //昵称、签名各占满一行，昵称在最上面
    CHECK(nick.left == left);
    CHECK(nick.right == right);
    CHECK(sig.left == left);
    CHECK(sig.right == right);
    CHECK(nick.bottom <= sig.top);
    //两列格子从签名下面开始，同一行上沿对齐、左右排开
    CHECK(sig.bottom <= gender.top);
    CHECK(gender.left == left);
    CHECK(birthday.top == gender.top);
    CHECK(birthday.left > gender.right);
    CHECK(birthday.right <= right);
    //昵称格的编辑按钮可见
    balloonwjui::DuiControl* pencil =
        root->FindCtrlById(ballooncontact::ContactProfileCtrlId(0, ballooncontact::kProfilePartPencil));
    REQUIRE(pencil != nullptr);
    CHECK(pencil->IsVisible());

    //按看本人的高度排得下：关闭按钮没有被压扁、下方留白与空间充裕时相同；最后一行在关闭按钮之上
    const layoutfit::FitResult close =
        layoutfit::MeasureAt(root.get(), ballooncontact::kIdBtnClose, ballooncontact::kContactBodyWidth, height);
    REQUIRE(close.m_found);
    CHECK(close.m_bottom <= height);
    CHECK(close.m_height == close.m_roomyHeight);
    CHECK(close.m_bottomGap == close.m_roomyBottomGap);
    root->ForceLayout(rc);
    ballooncontact::RelayoutContactBody(root.get());
    const RECT mobile = CwsRect(root.get(), ballooncontact::ContactProfileCtrlId(
                                                ballooncontact::kContactProfileSlotCount - 1,
                                                ballooncontact::kProfilePartCell));
    CHECK(mobile.bottom <= CwsRect(root.get(), ballooncontact::kIdBtnClose).top);
}

TEST_CASE("CWS6 头像：看本人时可点击、看别人时不可点击；带头像来源时底色取来源的颜色", "[contactwin]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    balloonwjui::DuiAvatar* avatar = dynamic_cast<balloonwjui::DuiAvatar*>(root->FindCtrlById(ballooncontact::kIdAvatar));
    REQUIRE(avatar != nullptr);
    //出场时不可点击
    CHECK_FALSE(ballooncontact::IsContactAvatarClickable(avatar));

    //放进屏幕外的宿主里点一下：不可点击时没有点击通知，可点击时有
    CwsNotifyHost host;
    const int height = ballooncontact::ContactBodyHeight(0, 1);
    REQUIRE(host.CreateOffscreen(ballooncontact::kContactBodyWidth, height));
    host.SetRoot(std::move(root));
    RECT rc = { 0, 0, ballooncontact::kContactBodyWidth, height };
    host.GetRoot()->ForceLayout(rc);
    const POINT pt = CwsCenter(avatar->GetRect());
    REQUIRE(avatar->GetRect().right > avatar->GetRect().left);

    host.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    host.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
    CHECK_FALSE(host.Has((UINT)balloonwjui::DUIN_CLICK, (UINT)ballooncontact::kIdAvatar));

    ballooncontact::SetContactAvatarClickable(avatar, true, _T("修改头像"));
    CHECK(ballooncontact::IsContactAvatarClickable(avatar));
    host.m_entries.clear();
    host.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    host.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
    CHECK(host.Has((UINT)balloonwjui::DUIN_CLICK, (UINT)ballooncontact::kIdAvatar));

    ballooncontact::SetContactAvatarClickable(avatar, false, NULL);
    CHECK_FALSE(ballooncontact::IsContactAvatarClickable(avatar));
    host.DestroyWindow();

    //头像来源：没有来源时仍是原来的固定底色；带来源时取来源的颜色，离线时为它的灰色
    ballooncontact::ContactModel model = MakeCwsSelfModel();
    balloonwjui::DuiAvatar plain;
    ballooncontact::ApplyContactAvatar(&plain, model);
    CHECK(plain.GetFallbackBgColor() == ballooncontact::kContactAvatarBgColor);

    ballooncontact::ContactAvatarSource source;
    source.m_valid = true;
    source.m_glyph = _T("N");
    source.m_color = RGB(0x4A, 0x90, 0xE2);
    model.SetAvatarSource(source);
    CHECK(ballooncontact::SameAvatarSource(model.AvatarSource(), source));
    ballooncontact::ApplyContactAvatar(&plain, model);
    CHECK(plain.GetFallbackBgColor() == RGB(0x4A, 0x90, 0xE2));
    model.SetAvatarGray(true);
    ballooncontact::ApplyContactAvatar(&plain, model);
    CHECK(plain.GetFallbackBgColor() == balloonmain::MakeGrayColor(RGB(0x4A, 0x90, 0xE2)));

    //来源比较：任何一项不同都算不同
    ballooncontact::ContactAvatarSource changed = source;
    changed.m_imagePath = _T("C:\\avatar.png");
    CHECK_FALSE(ballooncontact::SameAvatarSource(changed, source));
}

TEST_CASE("CWS7 群资料「我在本群的昵称」：回车发出提交、Esc 发出取消", "[contactwin][groupinfo]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = balloonmain::BuildGroupInfoBody();
    REQUIRE(root != nullptr);
    balloonmain::GroupInfoViewData data;
    data.groupid = 900001;
    data.groupName = _T("Team");
    data.memberCount = 3;
    data.myCard = _T("OldCard");
    REQUIRE(balloonmain::ApplyGroupInfoDataToView(root.get(), data));

    CwsNotifyHost host;
    const int kHostW = 460;
    const int kHostH = 408;
    REQUIRE(host.CreateOffscreen(kHostW, kHostH));
    balloonwjui::DuiControl* rawRoot = root.get();
    host.SetRoot(std::move(root));
    REQUIRE(balloonmain::SetGroupInfoCardEditMode(rawRoot, true));
    balloonwjui::DuiEdit* edit =
        dynamic_cast<balloonwjui::DuiEdit*>(rawRoot->FindCtrlById(balloonmain::kIdGroupCardEdit));
    REQUIRE(edit != nullptr);
    CHECK(edit->IsVisible());
    edit->SetFocus();

    //回车：输入框发出 DUIN_EDIT_ENTER，判定为提交
    host.SendMessage(WM_KEYDOWN, VK_RETURN, 0);
    REQUIRE(host.Has((UINT)balloonwjui::DuiEdit::DUIN_EDIT_ENTER, (UINT)balloonmain::kIdGroupCardEdit));
    CHECK(balloonmain::ClassifyInlineEditNotify((UINT)balloonwjui::DuiEdit::DUIN_EDIT_ENTER,
                                                (UINT)balloonmain::kIdGroupCardEdit,
                                                (UINT)balloonmain::kIdGroupCardEdit, true)
          == balloonmain::inline_edit_commit);
    //回车没有往单行输入框里插入换行
    CHECK(edit->GetText().Find(_T('\r')) < 0);
    CHECK(edit->GetText().Find(_T('\n')) < 0);

    //Esc：输入框发出 DUIN_EDIT_ESCAPE，判定为取消
    host.m_entries.clear();
    host.SendMessage(WM_KEYDOWN, VK_ESCAPE, 0);
    REQUIRE(host.Has((UINT)balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE, (UINT)balloonmain::kIdGroupCardEdit));
    CHECK(balloonmain::ClassifyInlineEditNotify((UINT)balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE,
                                                (UINT)balloonmain::kIdGroupCardEdit,
                                                (UINT)balloonmain::kIdGroupCardEdit, true)
          == balloonmain::inline_edit_cancel);
    host.DestroyWindow();
}

namespace {

//DuiEdit 的边框宽度（像素），与 balloonui DuiEdit.cpp 的 kBorderPx 相同：单行居中的条件是「行高 < 控件高 - 上下边框」
const int kCwsEditBorderPx = 1;
//DuiComboBox 的边框宽度（像素）：文字在去掉边框后的区域里垂直居中
const int kCwsComboBorderPx = 1;
//CWS8 进入编辑时填进输入框的文字：带下伸部分的英文字母
const TCHAR kCwsEditSample[] = _T("Ag");
//群资料窗口客户区的尺寸（像素），与 CWS7 相同
const int kCwsGroupInfoW = 460;
const int kCwsGroupInfoH = 408;

//默认字体一行的高度（像素）：控件绘制时用的字体（按控件所在窗口的 DPI 取）的 tmHeight
int CwsDefaultFontLineHeight(const balloonwjui::DuiControl* ctrl)
{
    HDC hdc = ::GetDC(NULL);
    HFONT old = (HFONT)::SelectObject(hdc, ctrl->GetDefaultFont());
    TEXTMETRICW tm;
    memset(&tm, 0, sizeof(tm));
    ::GetTextMetricsW(hdc, &tm);
    ::SelectObject(hdc, old);
    ::ReleaseDC(NULL, hdc);
    return tm.tmHeight;
}

} // namespace

//背景：2026-10-07 用户截图（snapshots/201141.png）资料窗看本人时昵称格进入编辑，输入框里的文字下半截被裁掉，其他几个格子一样。
//资料格的输入框与性别下拉框原先固定为 22 像素高；DuiEdit 只在「行高 < 控件高 - 上下边框」时把单行文字垂直居中
//（DuiEdit::ApplyTextInsets），否则从顶部排、超出部分在底部被裁掉。
//本机显示器为 120 DPI 时（用户截图时的环境），9 磅默认字体按 120 DPI 一行高 20 像素，与 22 像素输入框去掉边框后的 20 像素相等，
//不满足「小于」，文字从顶部排、底部被裁掉。用例在按显示器感知 DPI 的线程上下文里建窗口与控件树，与真实客户端一致。
TEST_CASE("CWS8 资料格进入编辑时输入框与下拉框放得下一行文字；群资料的群昵称输入框同样", "[contactwin][contactprofile]")
{
    TestDpiAwareScope dpiAware;
    const std::vector<ballooncontact::ContactProfileCell> cells =
        ballooncontact::BuildContactProfileCells(MakeCwsSelfModel());
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    ballooncontact::ApplyContactSelfView(root.get(), true);
    std::vector<int> slots;
    const int rows = ballooncontact::ConfigureContactProfile(root.get(), cells, false, false, slots);
    REQUIRE(rows > 0);
    const int height = ballooncontact::ContactBodyHeight(0, 1, rows, true);

    CwsNotifyHost host;
    REQUIRE(host.CreateOffscreen(ballooncontact::kContactBodyWidth, height));
    balloonwjui::DuiControl* raw = root.get();
    host.SetRoot(std::move(root));
    const RECT body = { 0, 0, ballooncontact::kContactBodyWidth, height };
    WARN("host dpi " << host.GetDpi() << ", max monitor dpi " << i18n::MaxMonitorDpi());

    for (size_t slot = 0; slot < slots.size(); ++slot)
    {
        const int field = slots[slot];
        if (field < 0)
            continue;
        INFO("slot " << slot << ", field " << field);
        ballooncontact::SetContactProfileSlotEditing(raw, (int)slot, field, true, kCwsEditSample, 0);
        raw->ForceLayout(body);
        ballooncontact::RelayoutContactBody(raw);
        if (field == ballooncontact::kProfileFieldGender)
        {
            //性别格编辑时是下拉框：去掉上下边框后放得下默认字体的一行
            balloonwjui::DuiControl* combo =
                raw->FindCtrlById(ballooncontact::ContactProfileCtrlId((int)slot, ballooncontact::kProfilePartCombo));
            REQUIRE(combo != nullptr);
            REQUIRE(combo->IsVisible());
            const RECT cr = combo->GetRect();
            const int lineH = CwsDefaultFontLineHeight(combo);
            INFO("combo height " << (cr.bottom - cr.top) << ", font line height " << lineH);
            CHECK(lineH <= (cr.bottom - cr.top) - 2 * kCwsComboBorderPx);
        }
        else
        {
            balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(
                raw->FindCtrlById(ballooncontact::ContactProfileCtrlId((int)slot, ballooncontact::kProfilePartEdit)));
            REQUIRE(edit != nullptr);
            REQUIRE(edit->IsVisible());
            const RECT er = edit->GetRect();
            const int lineH = edit->GetLineHeight();
            INFO("edit height " << (er.bottom - er.top) << ", line height " << lineH);
            CHECK(lineH > 0);
            CHECK(lineH < (er.bottom - er.top) - 2 * kCwsEditBorderPx);
        }
        ballooncontact::SetContactProfileSlotEditing(raw, (int)slot, field, false, NULL, 0);
    }
    host.DestroyWindow();

    //群资料的「我在本群的昵称」输入框（按行高拉满一行）
    std::unique_ptr<balloonwjui::DuiControl> groupRoot = balloonmain::BuildGroupInfoBody();
    REQUIRE(groupRoot != nullptr);
    balloonmain::GroupInfoViewData data;
    data.groupid = 900002;
    data.groupName = _T("Team");
    data.memberCount = 3;
    data.myCard = kCwsEditSample;
    REQUIRE(balloonmain::ApplyGroupInfoDataToView(groupRoot.get(), data));
    CwsNotifyHost groupHost;
    REQUIRE(groupHost.CreateOffscreen(kCwsGroupInfoW, kCwsGroupInfoH));
    balloonwjui::DuiControl* rawGroup = groupRoot.get();
    groupHost.SetRoot(std::move(groupRoot));
    REQUIRE(balloonmain::SetGroupInfoCardEditMode(rawGroup, true));
    const RECT groupRc = { 0, 0, kCwsGroupInfoW, kCwsGroupInfoH };
    rawGroup->ForceLayout(groupRc);
    balloonwjui::DuiEdit* cardEdit =
        dynamic_cast<balloonwjui::DuiEdit*>(rawGroup->FindCtrlById(balloonmain::kIdGroupCardEdit));
    REQUIRE(cardEdit != nullptr);
    const RECT ge = cardEdit->GetRect();
    INFO("group card edit height " << (ge.bottom - ge.top) << ", line height " << cardEdit->GetLineHeight());
    CHECK(cardEdit->GetLineHeight() < (ge.bottom - ge.top) - 2 * kCwsEditBorderPx);
    groupHost.DestroyWindow();

    //按各档显示器 DPI 算出的值行高度，都放得下该 DPI 下默认字体的一行（与 DuiRichEdit::GetLineHeight 同一算法）
    const int kDpis[] = { 96, 120, 144, 168, 192 };
    for (size_t i = 0; i < sizeof(kDpis) / sizeof(kDpis[0]); ++i)
    {
        HFONT font = balloonwjui::DuiResMgr::Inst().GetDefaultFontForDpi(kDpis[i]);
        REQUIRE(font != NULL);
        HDC hdc = ::GetDC(NULL);
        HFONT old = (HFONT)::SelectObject(hdc, font);
        TEXTMETRICW tm;
        memset(&tm, 0, sizeof(tm));
        ::GetTextMetricsW(hdc, &tm);
        ::SelectObject(hdc, old);
        ::ReleaseDC(NULL, hdc);
        const int lineH = tm.tmHeight + tm.tmExternalLeading;
        const int valueH = ballooncontact::ContactProfileValueHeightForDpi(kDpis[i]);
        INFO("dpi " << kDpis[i] << ", line height " << lineH << ", value height " << valueH);
        CHECK(lineH < valueH - 2 * kCwsEditBorderPx);
    }
    //实际使用的高度按本机最高的显示器 DPI 算
    CHECK(ballooncontact::ContactProfileValueHeight()
          == ballooncontact::ContactProfileValueHeightForDpi(i18n::MaxMonitorDpi()));
}

// =====================================================================
// 窗口用例（[.][contact-wnd]：会建真窗口，须在不可见桌面上单独运行）
// =====================================================================

TEST_CASE("CWW1 同一个人再次打开：激活已有窗口，正在编辑的备注名不丢；修改备注入口只进入备注编辑", "[.][contact-wnd]")
{
    CwsScopedPromptHook hook;
    CwsPanelSink sink;
    ballooncontact::CContactWindowSet set(NULL);

    CBalloonContactWindow* a = set.Open(sink.Hwnd(), kCwsPeerA, MakeCwsPeerModel(L"Ann"), false);
    REQUIRE(a != NULL);
    REQUIRE(a->IsWindow());
    CHECK(set.OpenCount() == 1);
    CHECK_FALSE(a->IsRemarkEditing());

    //「修改备注」入口打开已开着的窗口：同一个对象，只进入备注编辑
    CBalloonContactWindow* again = set.Open(sink.Hwnd(), kCwsPeerA, MakeCwsPeerModel(L"Changed"), true);
    CHECK(again == a);
    CHECK(set.OpenCount() == 1);
    CHECK(a->IsRemarkEditing());
    //激活不重建界面：模型仍是原来的（标题没有变成新模型的名字）
    CHECK(a->GetModel().Name() == CString(_T("Ann")));

    //正在编辑时再打开一次：输入框里的内容保留
    balloonwjui::DuiEdit* edit = CwsCtrl<balloonwjui::DuiEdit>(a, ballooncontact::kIdRemarkEdit);
    REQUIRE(edit != NULL);
    edit->SetText(_T("Typing..."));
    CBalloonContactWindow* third = set.Open(sink.Hwnd(), kCwsPeerA, MakeCwsPeerModel(L"Ann"), false);
    CHECK(third == a);
    CHECK(a->IsRemarkEditing());
    CHECK(edit == CwsCtrl<balloonwjui::DuiEdit>(a, ballooncontact::kIdRemarkEdit));
    CHECK(edit->GetText() == CString(_T("Typing...")));

    //最小化后再打开：还原
    a->ShowWindow(SW_MINIMIZE);
    CwsPump();
    set.Open(sink.Hwnd(), kCwsPeerA, MakeCwsPeerModel(L"Ann"), false);
    CwsPump();
    CHECK_FALSE(a->IsIconic());

    set.DestroyAll();
    CHECK(set.OpenCount() == 0);
}

TEST_CASE("CWW2 不同的人并存且错开摆放；关闭后对象被回收，再打开时新建", "[.][contact-wnd]")
{
    CwsScopedPromptHook hook;
    CwsPanelSink sink;
    ballooncontact::CContactWindowSet set(NULL);

    CBalloonContactWindow* a = set.Open(sink.Hwnd(), kCwsPeerA, MakeCwsPeerModel(L"Ann"), false);
    REQUIRE(a != NULL);
    CBalloonContactWindow* b = set.Open(sink.Hwnd(), kCwsPeerB, MakeCwsPeerModel(L"Bob"), false);
    REQUIRE(b != NULL);
    CHECK(a != b);
    CHECK(set.OpenCount() == 2);
    CHECK(set.Find(kCwsPeerA) == a);
    CHECK(set.Find(kCwsPeerB) == b);
    std::vector<CBalloonContactWindow*> open;
    set.GetOpenWindows(open);
    REQUIRE(open.size() == 2);
    CHECK(open[0] == a);
    CHECK(open[1] == b);

    //第二个窗口的位置：按上一个窗口错开（与纯函数的结果一致），且在工作区内
    RECT ra;
    RECT rb;
    a->GetWindowRect(&ra);
    b->GetWindowRect(&rb);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    REQUIRE(::GetMonitorInfoW(::MonitorFromPoint(CwsCenter(ra), MONITOR_DEFAULTTONEAREST), &mi));
    const SIZE sb = { rb.right - rb.left, rb.bottom - rb.top };
    const POINT prev = { ra.left, ra.top };
    const POINT expected = ballooncontact::ComputeContactWindowCascade(mi.rcWork, sb, true, prev,
                                                                       ballooncontact::kContactWindowCascadeStep);
    CHECK(rb.left == expected.x);
    CHECK(rb.top == expected.y);
    CHECK(rb.left >= mi.rcWork.left);
    CHECK(rb.top >= mi.rcWork.top);
    CHECK(rb.right <= mi.rcWork.right);
    CHECK(rb.bottom <= mi.rcWork.bottom);

    //关闭 A：对象被回收，查不到了；B 不受影响
    a->SendMessage(WM_CLOSE, 0, 0);
    CwsPump();
    CHECK(set.Find(kCwsPeerA) == NULL);
    CHECK(set.OpenCount() == 1);
    CHECK(set.Find(kCwsPeerB) == b);

    //再打开 A：新建窗口
    CBalloonContactWindow* a2 = set.Open(sink.Hwnd(), kCwsPeerA, MakeCwsPeerModel(L"Ann"), false);
    REQUIRE(a2 != NULL);
    CHECK(a2->IsWindow());
    CHECK(set.OpenCount() == 2);

    //userid 为 0：不建窗口
    CHECK(set.Open(sink.Hwnd(), 0, MakeCwsPeerModel(L"Nobody"), false) == NULL);
    CHECK(set.OpenCount() == 2);

    set.DestroyAll();
    CHECK(set.OpenCount() == 0);
    CHECK(set.Find(kCwsPeerB) == NULL);
}

TEST_CASE("CWW3 备注名：回车提交并回投主面板，Esc 取消不回投；重建界面后编辑态复位", "[.][contact-wnd]")
{
    CwsScopedPromptHook hook;
    CwsPanelSink sink;
    ballooncontact::CContactWindowSet set(NULL);

    CBalloonContactWindow* w = set.Open(sink.Hwnd(), kCwsPeerA, MakeCwsPeerModel(L"Ann"), true);
    REQUIRE(w != NULL);
    REQUIRE(w->IsRemarkEditing());
    balloonwjui::DuiEdit* edit = CwsCtrl<balloonwjui::DuiEdit>(w, ballooncontact::kIdRemarkEdit);
    REQUIRE(edit != NULL);
    CHECK(edit->GetText() == CString(_T("OldRemark")));

    //Esc：退出编辑，不回投；再进入编辑时输入框是原值
    edit->SetText(_T("Discarded"));
    CwsKey(w, VK_ESCAPE);
    CHECK_FALSE(w->IsRemarkEditing());
    CHECK_FALSE(edit->IsVisible());
    MSG msg;
    CHECK_FALSE(sink.Take(FMG_MSG_SET_CONTACT_REMARK, msg));
    CHECK(w->GetModel().Remark() == CString(_T("OldRemark")));

    //回车：退出编辑，把输入框里的原始文字回投主面板
    CwsClick(w, ballooncontact::kIdRemarkEditBtn);
    REQUIRE(w->IsRemarkEditing());
    CHECK(edit->GetText() == CString(_T("OldRemark")));
    edit->SetText(_T("NewRemark"));
    CwsKey(w, VK_RETURN);
    CHECK_FALSE(w->IsRemarkEditing());
    REQUIRE(sink.Take(FMG_MSG_SET_CONTACT_REMARK, msg));
    std::unique_ptr<std::string> remark(reinterpret_cast<std::string*>(msg.lParam));
    CHECK(msg.wParam == (WPARAM)kCwsPeerA);
    REQUIRE(remark.get() != NULL);
    CHECK(*remark == "NewRemark");
    //提交之后输入框随之失焦，不会再提交第二次
    CwsPump();
    CHECK_FALSE(sink.Take(FMG_MSG_SET_CONTACT_REMARK, msg));

    //编辑中途收到资料刷新（重建界面）：编辑态复位，点铅笔仍能再次进入编辑
    CwsClick(w, ballooncontact::kIdRemarkEditBtn);
    REQUIRE(w->IsRemarkEditing());
    w->RefreshContactData(MakeCwsPeerModel(L"Ann"));
    CHECK_FALSE(w->IsRemarkEditing());
    CwsClick(w, ballooncontact::kIdRemarkEditBtn);
    CHECK(w->IsRemarkEditing());
    balloonwjui::DuiEdit* rebuilt = CwsCtrl<balloonwjui::DuiEdit>(w, ballooncontact::kIdRemarkEdit);
    REQUIRE(rebuilt != NULL);
    CHECK(rebuilt->IsVisible());

    set.DestroyAll();
    sink.Drain();
}

TEST_CASE("CWW4 本人昵称格：回车提交并更新标题，Esc 取消，清空时提示并留在编辑态", "[.][contact-wnd]")
{
    CwsScopedPromptHook hook;
    CwsPanelSink sink;
    ballooncontact::CContactWindowSet set(NULL);

    CBalloonContactWindow* w = set.Open(sink.Hwnd(), kCwsSelf, MakeCwsSelfModel(), false);
    REQUIRE(w != NULL);
    CHECK(w->GetTitle() == CString(_T("Neo的资料")));
    const UINT pencil = ballooncontact::ContactProfileCtrlId(0, ballooncontact::kProfilePartPencil);
    const UINT editId = ballooncontact::ContactProfileCtrlId(0, ballooncontact::kProfilePartEdit);

    //点昵称格的编辑按钮：进入编辑，输入框是原昵称
    CwsClick(w, pencil);
    REQUIRE(w->GetEditingProfileSlot() == 0);
    balloonwjui::DuiEdit* edit = CwsCtrl<balloonwjui::DuiEdit>(w, editId);
    REQUIRE(edit != NULL);
    CHECK(edit->IsVisible());
    CHECK(edit->GetText() == CString(_T("Neo")));

    //Esc：退出编辑、不回投、昵称不变
    edit->SetText(_T("Discarded"));
    CwsKey(w, VK_ESCAPE);
    CHECK(w->GetEditingProfileSlot() == -1);
    MSG msg;
    CHECK_FALSE(sink.Take(FMG_MSG_CONTACT_PROFILE_EDIT, msg));
    CHECK(w->GetModel().Nickname() == CString(_T("Neo")));

    //清空后回车：提示「昵称不能为空」并留在编辑态，不回投
    CwsClick(w, pencil);
    REQUIRE(w->GetEditingProfileSlot() == 0);
    edit = CwsCtrl<balloonwjui::DuiEdit>(w, editId);
    REQUIRE(edit != NULL);
    edit->SetText(_T("  "));
    CwsKey(w, VK_RETURN);
    CHECK(g_cwsPromptCount == 1);
    CHECK(g_cwsLastPrompt == profilefields::ProfileEditErrorText(profilefields::profile_edit_nickname_empty));
    CHECK(w->GetEditingProfileSlot() == 0);
    CHECK_FALSE(sink.Take(FMG_MSG_CONTACT_PROFILE_EDIT, msg));

    //改成新昵称后回车：退出编辑，回投只含昵称一项的改资料请求；头部与标题立即换成新昵称
    edit->SetText(_T("Trinity"));
    CwsKey(w, VK_RETURN);
    CHECK(w->GetEditingProfileSlot() == -1);
    REQUIRE(sink.Take(FMG_MSG_CONTACT_PROFILE_EDIT, msg));
    std::unique_ptr<net::ProfileUpdateFields> fields(reinterpret_cast<net::ProfileUpdateFields*>(msg.lParam));
    REQUIRE(fields.get() != NULL);
    CHECK(fields->m_hasNickname);
    CHECK(fields->m_values.m_nickname == "Trinity");
    CHECK_FALSE(fields->m_hasSignature);
    CHECK(w->GetModel().Nickname() == CString(_T("Trinity")));
    CHECK(w->GetModel().Name() == CString(_T("Trinity")));
    CHECK(w->GetTitle() == CString(_T("Trinity的资料")));

    set.DestroyAll();
    sink.Drain();
}

TEST_CASE("CWW5 头像：看本人时点击回投打开修改头像对话框，看别人时没有反应；带头像来源时挂上位图", "[.][contact-wnd]")
{
    CwsScopedPromptHook hook;
    CwsPanelSink sink;
    ballooncontact::CContactWindowSet set(NULL);

    //看本人：头像可点击，点击后回投主面板
    ballooncontact::ContactModel selfModel = MakeCwsSelfModel();
    ballooncontact::ContactAvatarSource source;
    source.m_valid = true;
    source.m_glyph = _T("N");
    source.m_color = RGB(0x4A, 0x90, 0xE2);
    selfModel.SetAvatarSource(source);
    CBalloonContactWindow* self = set.Open(sink.Hwnd(), kCwsSelf, selfModel, false);
    REQUIRE(self != NULL);
    balloonwjui::DuiAvatar* avatar = CwsCtrl<balloonwjui::DuiAvatar>(self, ballooncontact::kIdAvatar);
    REQUIRE(avatar != NULL);
    CHECK(ballooncontact::IsContactAvatarClickable(avatar));
    //带头像来源：控件挂上生成的位图（字形头像），离线置灰时换成另一张灰度位图
    const HBITMAP colorBmp = avatar->GetBitmap();
    CHECK(colorBmp != NULL);
    self->ApplyPeerPresence(ballooncontact::kContactStatusOffline, true);
    CHECK(avatar->GetBitmap() != NULL);
    CHECK(avatar->GetBitmap() != colorBmp);
    self->ApplyPeerPresence(ballooncontact::kContactStatusOnline, false);
    CHECK(avatar->GetBitmap() == colorBmp);

    CwsClick(self, ballooncontact::kIdAvatar);
    MSG msg;
    CHECK(sink.Take(FMG_MSG_OPEN_MODIFY_AVATAR, msg));

    //改头像之后只换头像：来源变了换一张新位图，界面不重建
    balloonwjui::DuiEdit* anyEdit = CwsCtrl<balloonwjui::DuiEdit>(
        self, ballooncontact::ContactProfileCtrlId(0, ballooncontact::kProfilePartEdit));
    ballooncontact::ContactAvatarSource changed = source;
    changed.m_glyph = _T("T");
    self->ApplyAvatarSource(changed);
    CHECK(ballooncontact::SameAvatarSource(self->GetModel().AvatarSource(), changed));
    CHECK(avatar == CwsCtrl<balloonwjui::DuiAvatar>(self, ballooncontact::kIdAvatar));
    CHECK(anyEdit == CwsCtrl<balloonwjui::DuiEdit>(
        self, ballooncontact::ContactProfileCtrlId(0, ballooncontact::kProfilePartEdit)));
    CHECK(avatar->GetBitmap() != NULL);

    //看别人：头像不可点击，点击后什么都不回投；没有头像来源时不挂位图
    CBalloonContactWindow* peer = set.Open(sink.Hwnd(), kCwsPeerA, MakeCwsPeerModel(L"Ann"), false);
    REQUIRE(peer != NULL);
    balloonwjui::DuiAvatar* peerAvatar = CwsCtrl<balloonwjui::DuiAvatar>(peer, ballooncontact::kIdAvatar);
    REQUIRE(peerAvatar != NULL);
    CHECK_FALSE(ballooncontact::IsContactAvatarClickable(peerAvatar));
    CHECK(peerAvatar->GetBitmap() == NULL);
    CwsClick(peer, ballooncontact::kIdAvatar);
    CHECK_FALSE(sink.Take(FMG_MSG_OPEN_MODIFY_AVATAR, msg));

    set.DestroyAll();
    sink.Drain();
}

namespace {

//PrintWindow 的「按合成后的完整内容绘制」标志（Windows 8.1 起；老 SDK 头文件里没有这个宏）
const UINT kCwsPrintFullContent = 0x00000002;

//取 GDI+ 的 PNG 编码器
bool CwsPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
    {
        return false;
    }
    std::vector<BYTE> buffer(bytes);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buffer[0]);
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
    {
        return false;
    }
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

//把窗口自身的内容（PrintWindow，与遮挡无关）存成 %TEMP%\contactwin_<name>.png；返回是否存成
bool CwsShot(HWND hwnd, const wchar_t* name)
{
    CwsPump();
    RECT rc;
    ::GetWindowRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    ::PrintWindow(hwnd, mem, kCwsPrintFullContent);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    bool saved = false;
    {
        Gdiplus::Bitmap image(bmp, NULL);
        wchar_t dir[MAX_PATH] = { 0 };
        ::GetTempPathW(MAX_PATH, dir);
        const std::wstring path = std::wstring(dir) + L"contactwin_" + name + L".png";
        CLSID png;
        if (CwsPngEncoder(png))
        {
            saved = (image.Save(path.c_str(), &png, NULL) == Gdiplus::Ok);
        }
    }
    ::DeleteObject(bmp);
    return saved;
}

//进程内 GDI+ 的启停（截图存 PNG 用）
class CwsGdiplus
{
public:
    CwsGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&m_token, &input, NULL);
    }

    ~CwsGdiplus()
    {
        if (m_token != 0)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

private:
    ULONG_PTR m_token;   //GdiplusStartup 返回的令牌
};

} // namespace

TEST_CASE("CWW6 截图：看本人（昵称格在编辑态）与看同事两个资料窗", "[.][contact-shot]")
{
    CwsGdiplus gdiplus;
    CwsScopedPromptHook hook;
    CwsPanelSink sink;
    ballooncontact::CContactWindowSet set(NULL);

    //看本人：带字形头像来源，昵称格进入编辑
    ballooncontact::ContactModel selfModel = MakeCwsSelfModel();
    ballooncontact::ContactAvatarSource source;
    source.m_valid = true;
    source.m_glyph = _T("N");
    source.m_color = RGB(0x4A, 0x90, 0xE2);
    selfModel.SetAvatarSource(source);
    CBalloonContactWindow* self = set.Open(sink.Hwnd(), kCwsSelf, selfModel, false);
    REQUIRE(self != NULL);
    CwsClick(self, ballooncontact::ContactProfileCtrlId(0, ballooncontact::kProfilePartPencil));
    CHECK(self->GetEditingProfileSlot() == 0);
    CHECK(CwsShot(self->m_hWnd, L"self"));

    //看同事：错开摆放，备注名在编辑态
    ballooncontact::ContactModel peerModel = MakeCwsPeerModel(L"Ann");
    ballooncontact::ContactAvatarSource peerSource;
    peerSource.m_valid = true;
    peerSource.m_glyph = _T("A");
    peerSource.m_color = RGB(0xFF, 0x7A, 0x45);
    peerModel.SetAvatarSource(peerSource);
    CBalloonContactWindow* peer = set.Open(sink.Hwnd(), kCwsPeerA, peerModel, true);
    REQUIRE(peer != NULL);
    CHECK(CwsShot(peer->m_hWnd, L"peer"));

    set.DestroyAll();
    sink.Drain();
}
