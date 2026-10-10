/**
 *  帖子群详情里点「@某人」弹成员菜单的单元测试（标签 [threaddll]，源码编进 FlamingoUnit；docs/帖子群方案.md §15.1 第 4 条）：
 *    - TGD34 @ 命中：
 *        · 纯函数：显示区间带上被 @ 的人，按字符位置查到对应的 userid，落在片段之外、或落在「@所有人」上时查不到；
 *          点 @ 时对方的群角色（名单里的角色、已退群、名单未到）；
 *        · 真实的只读富文本（CThreadAtRichEdit，挂在不显示的 DuiFrameWindow 上）：给「@名字」上色时记下各段位置，
 *          左键、右键点在 @XXX 上回调；点在其他文字、「@所有人」、行尾空白上不回调；右键点在其他文字上交给基类
 *          （复制菜单，用例里关掉了菜单，以免弹出模态菜单）；没有登记回调时与 DuiRichEdit 相同。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../Core/ThreadMention.h"
#include "../Core/ThreadMembers.h"
#include "../ThreadGroupApi.h"     // kMemberRoleLeft / kMemberRoleUnknown
#include "../UI/ThreadRichText.h"  // CThreadAtRichEdit / MakeReadOnlyRichText / FillWireText / HighlightAtSpans
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

using namespace threadgroup;

namespace {

//测试窗口的尺寸（像素）：一行放得下用例里的整段正文
const int kFrameWidth = 800;
const int kFrameHeight = 300;
//点击位置相对字左上角的偏移（像素）：落在字的内部
const int kInsideOffset = 2;
//行尾空白处的点击位置相对最后一个字左上角的偏移（像素）：远在这一行文字的右边
const int kFarRightOffset = 400;

const int64_t kSelf = 4101;    //本人
const int64_t kZhang = 4103;   //张三

//OLE 初始化范围：富文本引擎需要
class OleScope
{
public:
    OleScope()
    {
        m_hr = ::OleInitialize(NULL);
    }

    ~OleScope()
    {
        if (SUCCEEDED(m_hr))
            ::OleUninitialize();
    }

    //OLE 是否初始化成功
    bool Ok() const
    {
        return SUCCEEDED(m_hr);
    }

private:
    HRESULT m_hr;   //OleInitialize 的结果
};

//记录回调：被调了几次、最后一次的 userid
struct AtClickRecord
{
    int     m_calls;    //回调次数
    int64_t m_userId;   //最后一次回调的 userid

    AtClickRecord()
        : m_calls(0)
        , m_userId(0)
    {
    }
};

//点 @XXX 的回调：记下来
void RecordAtClick(void* user, int64_t userId, POINT /*screenPt*/)
{
    AtClickRecord* record = static_cast<AtClickRecord*>(user);
    ++record->m_calls;
    record->m_userId = userId;
}

//第 index 个字内部的一点（宿主窗口客户区坐标）
POINT PointInChar(CThreadAtRichEdit* edit, int index)
{
    POINTL pos;
    pos.x = 0;
    pos.y = 0;
    edit->SendMessageToEngine(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pos), index);
    POINT pt;
    pt.x = pos.x + kInsideOffset;
    pt.y = pos.y + kInsideOffset;
    return pt;
}

//第 index 个字右半边的一点（宿主窗口客户区坐标）：下一个字左边往回 kInsideOffset 像素；第 index 个字须不是一行的最后一个字，
//或者是全文最后一个字（此时「下一个字」的位置是全文末尾的插入点，在同一行）
POINT PointInCharRight(CThreadAtRichEdit* edit, int index)
{
    POINTL pos;
    pos.x = 0;
    pos.y = 0;
    edit->SendMessageToEngine(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pos), index);
    POINTL next;
    next.x = 0;
    next.y = 0;
    edit->SendMessageToEngine(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&next), index + 1);
    POINT pt;
    pt.x = next.x - kInsideOffset;
    pt.y = pos.y + kInsideOffset;
    return pt;
}

//一段 @ 片段（UTF-8 字节位置）
ThreadAtSpan Span(int64_t userId, int off, int len)
{
    ThreadAtSpan s;
    s.m_userId = userId;
    s.m_off = off;
    s.m_len = len;
    return s;
}

} // namespace

TEST_CASE("TGD34 点 @XXX 弹成员菜单：命中判断与回调", "[threaddll]")
{
    //「请看 」3 个字（7 字节），「@张三」字符 3~6（字节 7 起 7 个），「 和 」3 个字（5 字节），「@所有人」字符 9~13（字节 19 起 10 个）
    const std::wstring wire = L"请看 @张三 和 @所有人 好的";
    std::vector<ThreadAtSpan> at;
    at.push_back(Span(kZhang, 7, 7));
    at.push_back(Span(kThreadAtAllUserId, 19, 10));

    SECTION("纯函数：显示区间带上被 @ 的人；按字符位置查人，片段之外与「@所有人」查不到")
    {
        const std::vector<ThreadAtDisplay> ranges = AtSpansToDisplay(wire, at, kSelf, std::set<int>(), 0);
        REQUIRE(ranges.size() == 2);
        CHECK(ranges[0].m_userId == kZhang);
        CHECK(ranges[0].m_start == 3);
        CHECK(ranges[0].m_end == 6);
        CHECK(ranges[1].m_userId == kThreadAtAllUserId);
        CHECK(AtUserAtChar(ranges, 3) == kZhang);
        CHECK(AtUserAtChar(ranges, 5) == kZhang);
        CHECK(AtUserAtChar(ranges, 6) == 0);
        CHECK(AtUserAtChar(ranges, 0) == 0);
        CHECK(AtUserAtChar(ranges, 10) == 0);
        CHECK(AtUserAtChar(std::vector<ThreadAtDisplay>(), 3) == 0);
    }

    SECTION("点 @ 时对方的群角色：名单里的角色、已退群、名单未到")
    {
        CThreadMemberBook book;
        CHECK(book.MenuRoleOf(kZhang) == kMemberRoleUnknown);
        std::vector<ThreadMember> members;
        ThreadMember zhang;
        zhang.m_userId = kZhang;
        zhang.m_role = thread_member_role_admin;
        members.push_back(zhang);
        book.Set(members);
        CHECK(book.MenuRoleOf(kZhang) == thread_member_role_admin);
        CHECK(book.MenuRoleOf(kSelf) == kMemberRoleLeft);
    }

    SECTION("真实的只读富文本：左键、右键点在 @XXX 上回调；其他地方不回调")
    {
        OleScope ole;
        REQUIRE(ole.Ok());
        balloonwjui::DuiFrameWindow frame;
        RECT rc;
        rc.left = 0;
        rc.top = 0;
        rc.right = kFrameWidth;
        rc.bottom = kFrameHeight;
        //不加 WS_VISIBLE：用例跑起来不在屏幕上闪过窗口
        REQUIRE(frame.Create(NULL, rc, L"ThreadAtClickTests", WS_OVERLAPPEDWINDOW) != NULL);
        std::unique_ptr<CThreadAtRichEdit> owned = MakeReadOnlyRichText(NULL, RGB(0, 0, 0), RGB(0xFF, 0xFF, 0xFF));
        CThreadAtRichEdit* edit = owned.get();
        //右键点在其他文字上时交给基类：关掉复制菜单，免得用例里弹出模态菜单
        edit->SetContextMenuEnabled(false);
        frame.SetClientContent(std::unique_ptr<balloonwjui::DuiControl>(owned.release()));
        FillWireText(edit, wire, std::vector<ThreadFaceEntry>(), L"[?]", 0);
        CHECK(HighlightAtSpans(edit, 0, wire, at, kSelf, std::vector<ThreadFaceEntry>(), L"[?]") == 2);
        //上色时记下了两段的位置与被 @ 的人
        REQUIRE(edit->AtRanges().size() == 2);
        CHECK(edit->AtRanges()[0].m_userId == kZhang);
        CHECK(edit->AtRanges()[0].m_start == 3);

        AtClickRecord record;
        edit->SetAtClickHandler(&RecordAtClick, &record);
        //「张」（第 4 个字）上：命中张三
        const POINT onZhang = PointInChar(edit, 4);
        CHECK(edit->HitTestAt(onZhang) == kZhang);
        //点在字的右半边（第 6h 步联调发现：EM_CHARFROMPOS 给的是离点最近的字边界，右半边得到的是下一个字）：
        //「张」「三」的右半边仍是张三；「@」前面那个空格的右半边不算
        CHECK(edit->HitTestAt(PointInCharRight(edit, 4)) == kZhang);
        CHECK(edit->HitTestAt(PointInCharRight(edit, 5)) == kZhang);
        CHECK(edit->HitTestAt(PointInCharRight(edit, 2)) == 0);
        //「@所有人」最后一个字的右半边：查到的是「@所有人」，不弹菜单
        CHECK(edit->HitTestAt(PointInCharRight(edit, 12)) == 0);
        //右键：回调、不交给基类
        CHECK(edit->OnRButtonDown(onZhang, 0));
        CHECK(record.m_calls == 1);
        CHECK(record.m_userId == kZhang);
        //左键按下、松开（没有拖选）：回调
        edit->OnLButtonDown(onZhang, MK_LBUTTON);
        edit->OnLButtonUp(onZhang, 0);
        CHECK(record.m_calls == 2);
        //「请」上：不命中；右键交给基类（菜单关掉时基类返回 false）
        const POINT onPlain = PointInChar(edit, 0);
        CHECK(edit->HitTestAt(onPlain) == 0);
        CHECK_FALSE(edit->OnRButtonDown(onPlain, 0));
        edit->OnLButtonDown(onPlain, MK_LBUTTON);
        edit->OnLButtonUp(onPlain, 0);
        CHECK(record.m_calls == 2);
        //「@所有人」上：不弹菜单
        CHECK(edit->HitTestAt(PointInChar(edit, 10)) == 0);
        edit->OnRButtonDown(PointInChar(edit, 10), 0);
        CHECK(record.m_calls == 2);

        //经宿主窗口派发的真实鼠标消息（第 6h 步联调时右键没有到达富文本，在这里复现）：左键按下、松开与右键按下
        const LPARAM lpZhang = MAKELPARAM(onZhang.x, onZhang.y);
        ::SendMessage(frame.m_hWnd, WM_MOUSEMOVE, 0, lpZhang);
        ::SendMessage(frame.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, lpZhang);
        ::SendMessage(frame.m_hWnd, WM_LBUTTONUP, 0, lpZhang);
        CHECK(record.m_calls == 3);
        ::SendMessage(frame.m_hWnd, WM_MOUSEMOVE, 0, lpZhang);
        ::SendMessage(frame.m_hWnd, WM_RBUTTONDOWN, MK_RBUTTON, lpZhang);
        CHECK(record.m_calls == 4);

        //没有登记回调：与 DuiRichEdit 相同，右键交给基类
        edit->SetAtClickHandler(NULL, NULL);
        CHECK_FALSE(edit->OnRButtonDown(onZhang, 0));
        CHECK(record.m_calls == 4);
        frame.DestroyWindow();
    }

    SECTION("正文以「@张三」结尾：点在这一行右边的空白处不算点了它")
    {
        OleScope ole;
        REQUIRE(ole.Ok());
        balloonwjui::DuiFrameWindow frame;
        RECT rc;
        rc.left = 0;
        rc.top = 0;
        rc.right = kFrameWidth;
        rc.bottom = kFrameHeight;
        REQUIRE(frame.Create(NULL, rc, L"ThreadAtClickTests", WS_OVERLAPPEDWINDOW) != NULL);
        std::unique_ptr<CThreadAtRichEdit> owned = MakeReadOnlyRichText(NULL, RGB(0, 0, 0), RGB(0xFF, 0xFF, 0xFF));
        CThreadAtRichEdit* edit = owned.get();
        edit->SetContextMenuEnabled(false);
        frame.SetClientContent(std::unique_ptr<balloonwjui::DuiControl>(owned.release()));
        const std::wstring tail = L"请看 @张三";
        FillWireText(edit, tail, std::vector<ThreadFaceEntry>(), L"[?]", 0);
        HighlightAtSpans(edit, 0, tail, std::vector<ThreadAtSpan>(1, Span(kZhang, 7, 7)), kSelf,
                         std::vector<ThreadFaceEntry>(), L"[?]");
        AtClickRecord record;
        edit->SetAtClickHandler(&RecordAtClick, &record);
        //最后一个字「三」上：命中
        const POINT onLast = PointInChar(edit, 5);
        CHECK(edit->HitTestAt(onLast) == kZhang);
        //全文最后一个字的右半边同样命中
        CHECK(edit->HitTestAt(PointInCharRight(edit, 5)) == kZhang);
        //同一行、远在右边的空白处：不命中
        POINT farRight = onLast;
        farRight.x += kFarRightOffset;
        CHECK(edit->HitTestAt(farRight) == 0);
        CHECK_FALSE(edit->OnRButtonDown(farRight, 0));
        CHECK(record.m_calls == 0);
        frame.DestroyWindow();
    }
}
