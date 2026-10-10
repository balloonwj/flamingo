/**
 *  批注栏与左右分栏的单测（UI/BalloonCode/CodeNotesPane，docs/代码片段方案.md 第 10.5 节），CodeNotesPaneTests.cpp
 *
 *  [codenotespane]
 *    NP1 卡片按批注顺序排列、互不重叠；顶部写「批注 (N)」；可编辑时有「修改」「删除」；长说明折成多行，卡片更高
 *    NP2 只读模式没有「修改」「删除」，也不能进入编辑
 *    NP3 新增批注：编辑卡片按起始行插在对应位置，结束编辑后恢复
 *    NP4 修改批注：编辑卡片代替原来那一张，输入框预填原来的说明
 *    NP5 批注栏变窄后长说明折出更多行，卡片随之变高
 *    NP6 分栏：右栏保持固定宽度，窗口变宽时代码区变宽；收起后代码区占满；再展开时右栏回到原宽（隐藏后再显示的排版）
 *    NP7 控件编号反查第几条批注、哪个部位
 *    NP8 没有批注时显示提示文字
 *    NP9 展开状态下装入两栏、排版不递归；拖动分隔条后右栏宽度被记住，窗口缩放后保持；收起时不接受拖动
 *    NP10 放进真实宿主窗口：编辑卡片的「确定」在可见范围内；点「确定」「取消」与卡片上的行范围链接，窗口都收到对应的点击通知
 *    NP11 批注栏先排过版、再进入编辑或增加批注（发送代码窗口的实际顺序）：新出现的编辑卡片「确定」、第三张卡片的链接都能被命中
 *    NP12 追加批注（第 11.3 节）：卡片写出作者名（名字表里没有的写 userid）与「新」；只有本人的卡片有「修改」「删除」，
 *         别人的批注不能进入修改；没设名字表时不写作者；同一行上两位作者的卡片都在、互不重叠
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../UI/BalloonCode/CodeNotesPane.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"     // NP10：真实宿主窗口
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // NP10：截获点击通知

using namespace ballooncode;
using namespace codesnippet;

namespace {

//批注栏排版用的尺寸（像素）
const int kPaneW = kCodeNotesDefaultWidth;
const int kNarrowPaneW = kCodeNotesMinWidth;
const int kPaneH = 600;

//分栏的两种宽度（像素）
const int kSplitW = 800;
const int kWiderSplitW = 1000;
const int kSplitH = 400;

//一段很长的说明：在默认宽度下也要折成几行
const std::wstring kLongText(120, L'长');

//第 i 张卡片上部位 part 的控件
balloonwjui::DuiControl* CardPart(balloonwjui::DuiControl* root, int index, int part)
{
    return root->FindCtrlById(kCodeNotesIdCardBase + (UINT)index * kCodeNotesIdStride + (UINT)part);
}

//三条批注
std::vector<CodeNote> ThreeNotes()
{
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(2, 3, L"短说明"));
    notes.push_back(CodeNote(8, 8, kLongText));
    notes.push_back(CodeNote(12, 14, L"第三条"));
    return notes;
}

//建一个排好版的批注栏
std::unique_ptr<CodeNotesPane> MakePane(bool readOnly, int width)
{
    std::unique_ptr<CodeNotesPane> pane(new CodeNotesPane());
    pane->SetReadOnly(readOnly);
    pane->SetNotes(ThreeNotes());
    pane->ForceLayout(RECT{ 0, 0, width, kPaneH });
    return pane;
}

//控件高度
int HeightOf(balloonwjui::DuiControl* c)
{
    const RECT& rc = c->GetRect();
    return rc.bottom - rc.top;
}

} // anonymous namespace

TEST_CASE("NP1 cards are listed in order", "[codenotespane]")
{
    std::unique_ptr<CodeNotesPane> pane = MakePane(false, kPaneW);
    balloonwjui::DuiLabel* header = dynamic_cast<balloonwjui::DuiLabel*>(pane->FindCtrlById(kCodeNotesIdHeader));
    REQUIRE(header != NULL);
    CHECK(std::wstring((LPCWSTR)header->GetText()) == L"批注 (3)");

    for (int i = 0; i < 3; ++i)
    {
        INFO("card " << i);
        REQUIRE(CardPart(pane.get(), i, kCodeNotesPartCard) != NULL);
        CHECK(CardPart(pane.get(), i, kCodeNotesPartRange) != NULL);
        CHECK(CardPart(pane.get(), i, kCodeNotesPartEdit) != NULL);
        CHECK(CardPart(pane.get(), i, kCodeNotesPartDelete) != NULL);
    }
    const RECT c0 = CardPart(pane.get(), 0, kCodeNotesPartCard)->GetRect();
    const RECT c1 = CardPart(pane.get(), 1, kCodeNotesPartCard)->GetRect();
    const RECT c2 = CardPart(pane.get(), 2, kCodeNotesPartCard)->GetRect();
    CHECK(c1.top >= c0.bottom);
    CHECK(c2.top >= c1.bottom);
    //长说明折成多行：第二张卡片比第一张高
    CHECK(c1.bottom - c1.top > c0.bottom - c0.top);
    //卡片不越出批注栏
    CHECK(c0.right <= kPaneW);
    //行范围的写法
    balloonwjui::DuiLabel* range = dynamic_cast<balloonwjui::DuiLabel*>(CardPart(pane.get(), 0, kCodeNotesPartRange));
    REQUIRE(range != NULL);
    CHECK(std::wstring((LPCWSTR)range->GetText()) == L"第 2～3 行");
    balloonwjui::DuiLabel* single = dynamic_cast<balloonwjui::DuiLabel*>(CardPart(pane.get(), 1, kCodeNotesPartRange));
    REQUIRE(single != NULL);
    CHECK(std::wstring((LPCWSTR)single->GetText()) == L"第 8 行");
}

TEST_CASE("NP2 read-only panes have no edit links", "[codenotespane]")
{
    std::unique_ptr<CodeNotesPane> pane = MakePane(true, kPaneW);
    CHECK(CardPart(pane.get(), 0, kCodeNotesPartRange) != NULL);
    CHECK(CardPart(pane.get(), 0, kCodeNotesPartEdit) == NULL);
    CHECK(CardPart(pane.get(), 0, kCodeNotesPartDelete) == NULL);
    pane->BeginEdit(4, 5, L"x", -1);
    CHECK_FALSE(pane->IsEditing());
    CHECK(pane->FindCtrlById(kCodeNotesIdEditBox) == NULL);
}

TEST_CASE("NP3 a new note is edited at its position", "[codenotespane]")
{
    std::unique_ptr<CodeNotesPane> pane = MakePane(false, kPaneW);
    pane->BeginEdit(5, 6, L"", -1);
    REQUIRE(pane->IsEditing());
    CHECK(pane->EditingIndex() == -1);
    CHECK(pane->EditingFrom() == 5);
    CHECK(pane->EditingTo() == 6);
    balloonwjui::DuiControl* edit = pane->FindCtrlById(kCodeNotesIdEditBox);
    REQUIRE(edit != NULL);
    CHECK(pane->FindCtrlById(kCodeNotesIdEditOk) != NULL);
    CHECK(pane->FindCtrlById(kCodeNotesIdEditCancel) != NULL);
    //第 5～6 行排在第 2～3 行与第 8 行之间
    CHECK(edit->GetRect().top > CardPart(pane.get(), 0, kCodeNotesPartCard)->GetRect().bottom);
    CHECK(edit->GetRect().bottom < CardPart(pane.get(), 1, kCodeNotesPartCard)->GetRect().top);

    pane->EndEdit();
    CHECK_FALSE(pane->IsEditing());
    CHECK(pane->FindCtrlById(kCodeNotesIdEditBox) == NULL);
    CHECK(CardPart(pane.get(), 1, kCodeNotesPartCard) != NULL);
}

TEST_CASE("NP4 editing an existing note replaces its card", "[codenotespane]")
{
    std::unique_ptr<CodeNotesPane> pane = MakePane(false, kPaneW);
    pane->BeginEdit(12, 14, L"第三条", 2);
    REQUIRE(pane->IsEditing());
    CHECK(pane->EditingIndex() == 2);
    CHECK(CardPart(pane.get(), 2, kCodeNotesPartCard) == NULL);
    CHECK(CardPart(pane.get(), 1, kCodeNotesPartCard) != NULL);
    REQUIRE(pane->EditBox() != NULL);
    CHECK(pane->EditingText() == L"第三条");
}

TEST_CASE("NP5 narrower panes wrap long notes into more lines", "[codenotespane]")
{
    std::unique_ptr<CodeNotesPane> pane = MakePane(false, kPaneW);
    const int wideH = HeightOf(CardPart(pane.get(), 1, kCodeNotesPartCard));
    pane->ForceLayout(RECT{ 0, 0, kNarrowPaneW, kPaneH });
    const int narrowH = HeightOf(CardPart(pane.get(), 1, kCodeNotesPartCard));
    CHECK(narrowH > wideH);
}

TEST_CASE("NP6 the splitter keeps the notes pane width and can collapse", "[codenotespane]")
{
    CodeNotesSplitter split;
    split.SetPane(0, std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiVBox()));
    split.SetPane(1, std::unique_ptr<balloonwjui::DuiControl>(new CodeNotesPane()));
    balloonwjui::DuiControl* left = split.GetPane(0);
    balloonwjui::DuiControl* right = split.GetPane(1);

    split.ForceLayout(RECT{ 0, 0, kSplitW, kSplitH });
    CHECK(right->GetRect().right - right->GetRect().left == kCodeNotesDefaultWidth);
    const int leftW = left->GetRect().right - left->GetRect().left;

    //窗口变宽：右栏不变，代码区变宽
    split.ForceLayout(RECT{ 0, 0, kWiderSplitW, kSplitH });
    CHECK(right->GetRect().right - right->GetRect().left == kCodeNotesDefaultWidth);
    CHECK(left->GetRect().right - left->GetRect().left == leftW + (kWiderSplitW - kSplitW));

    //收起：代码区占满，右栏隐藏
    split.SetCollapsed(true);
    split.ForceLayout(RECT{ 0, 0, kSplitW, kSplitH });
    CHECK(split.IsCollapsed());
    CHECK_FALSE(right->IsVisible());
    CHECK(left->GetRect().right - left->GetRect().left == kSplitW);

    //再展开：右栏回到原宽
    split.SetCollapsed(false);
    split.ForceLayout(RECT{ 0, 0, kSplitW, kSplitH });
    CHECK(right->IsVisible());
    CHECK(right->GetRect().right - right->GetRect().left == kCodeNotesDefaultWidth);
    CHECK(left->GetRect().right - left->GetRect().left == leftW);
}

TEST_CASE("NP7 control ids map back to notes", "[codenotespane]")
{
    int index = -1;
    int part = -1;
    REQUIRE(CodeNotesIdToNote(kCodeNotesIdCardBase + 2 * kCodeNotesIdStride + kCodeNotesPartDelete, index, part));
    CHECK(index == 2);
    CHECK(part == kCodeNotesPartDelete);
    CHECK_FALSE(CodeNotesIdToNote(kCodeNotesIdCollapse, index, part));
    CHECK_FALSE(CodeNotesIdToNote(kCodeNotesIdCardBase + (UINT)kCodeNotesMax * kCodeNotesIdStride, index, part));
}

TEST_CASE("NP8 an empty pane shows a hint", "[codenotespane]")
{
    std::unique_ptr<CodeNotesPane> pane(new CodeNotesPane());
    pane->SetNotes(std::vector<CodeNote>());
    pane->ForceLayout(RECT{ 0, 0, kPaneW, kPaneH });
    balloonwjui::DuiControl* empty = pane->FindCtrlById(kCodeNotesIdEmpty);
    REQUIRE(empty != NULL);
    CHECK(HeightOf(empty) > 0);
    balloonwjui::DuiLabel* header = dynamic_cast<balloonwjui::DuiLabel*>(pane->FindCtrlById(kCodeNotesIdHeader));
    REQUIRE(header != NULL);
    CHECK(std::wstring((LPCWSTR)header->GetText()) == L"批注 (0)");
}

TEST_CASE("NP9 dragging the bar keeps the new notes pane width", "[codenotespane]")
{
    //展开状态下装入两栏（NP6 也是这样）：排版不得递归；拖动分隔条向左 kDragPx，右栏变宽并在窗口缩放后保持
    const int kDragPx = 50;
    CodeNotesSplitter split;
    split.SetPane(0, std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiVBox()));
    split.SetPane(1, std::unique_ptr<balloonwjui::DuiControl>(new CodeNotesPane()));
    split.ForceLayout(RECT{ 0, 0, kSplitW, kSplitH });
    const RECT bar = split.GetBarRect();
    POINT down = { (bar.left + bar.right) / 2, (bar.top + bar.bottom) / 2 };
    REQUIRE(split.OnLButtonDown(down, MK_LBUTTON));
    POINT move = { down.x - kDragPx, down.y };
    split.OnMouseMove(move, MK_LBUTTON);
    CHECK(split.GetPane(1)->GetRect().right - split.GetPane(1)->GetRect().left == kCodeNotesDefaultWidth + kDragPx);
    REQUIRE(split.OnLButtonUp(move, 0));
    CHECK(split.RightWidth() == kCodeNotesDefaultWidth + kDragPx);

    split.ForceLayout(RECT{ 0, 0, kWiderSplitW, kSplitH });
    CHECK(split.GetPane(1)->GetRect().right - split.GetPane(1)->GetRect().left == kCodeNotesDefaultWidth + kDragPx);

    //收起时按在原分隔条的位置不开始拖动
    split.SetCollapsed(true);
    split.ForceLayout(RECT{ 0, 0, kSplitW, kSplitH });
    CHECK_FALSE(split.OnLButtonDown(down, MK_LBUTTON));
}

namespace {

//NP10：宿主窗口放到屏幕外（可见窗口才会正常处理鼠标消息），尺寸取批注栏默认宽度与 kPaneH
const int kNpOffscreen = -32000;

//截获点击通知的宿主：DuiHost 没有父窗口时把通知发给自己，这里记下每个 DUIN_CLICK 的控件编号
class NoteClickHost : public balloonwjui::DuiHost
{
public:
    BEGIN_MSG_MAP(NoteClickHost)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

    std::vector<UINT> m_clicks;   //收到的点击通知的控件编号，按先后顺序

private:
    LRESULT OnNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
    {
        const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
        if (n != NULL && n->code == balloonwjui::DUIN_CLICK)
            m_clicks.push_back(n->ctrlId);
        bHandled = FALSE;
        return 0;
    }
};

//在控件中心按下再松开鼠标左键（客户区坐标，直接 SendMessage 给宿主）
void ClickCenter(HWND hwnd, balloonwjui::DuiControl* ctrl)
{
    const RECT& rc = ctrl->GetRect();
    const LPARAM pt = MAKELPARAM((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    ::SendMessage(hwnd, WM_MOUSEMOVE, 0, pt);
    ::SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    ::SendMessage(hwnd, WM_LBUTTONUP, 0, pt);
}

} // anonymous namespace

TEST_CASE("NP10 clicks on the edit card and the cards reach the window", "[codenotespane]")
{
    NoteClickHost host;
    RECT rc = { kNpOffscreen, kNpOffscreen, kNpOffscreen + kPaneW, kNpOffscreen + kPaneH };
    host.Create(NULL, rc, _T("CodeNotesPaneTests"), WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW);
    REQUIRE(host.IsWindow());
    std::unique_ptr<CodeNotesPane> owned(new CodeNotesPane());
    CodeNotesPane* pane = owned.get();
    pane->SetNotes(ThreeNotes());
    pane->BeginEdit(5, 6, L"", -1);
    host.SetRoot(std::unique_ptr<balloonwjui::DuiControl>(owned.release()));
    ::UpdateWindow(host.m_hWnd);

    balloonwjui::DuiControl* ok = pane->FindCtrlById(kCodeNotesIdEditOk);
    balloonwjui::DuiControl* cancel = pane->FindCtrlById(kCodeNotesIdEditCancel);
    balloonwjui::DuiControl* range = CardPart(pane, 0, kCodeNotesPartRange);
    REQUIRE(ok != NULL);
    REQUIRE(cancel != NULL);
    REQUIRE(range != NULL);
    INFO("ok rect " << ok->GetRect().left << "," << ok->GetRect().top << "," << ok->GetRect().right << ","
         << ok->GetRect().bottom);
    //「确定」在批注栏可见的范围之内
    CHECK(ok->GetRect().bottom <= kPaneH);
    CHECK(ok->GetRect().right <= kPaneW);
    CHECK(ok->GetRect().right > ok->GetRect().left);

    ClickCenter(host.m_hWnd, ok);
    ClickCenter(host.m_hWnd, cancel);
    ClickCenter(host.m_hWnd, range);
    REQUIRE(host.m_clicks.size() == 3);
    CHECK(host.m_clicks[0] == kCodeNotesIdEditOk);
    CHECK(host.m_clicks[1] == kCodeNotesIdEditCancel);
    CHECK(host.m_clicks[2] == CardPart(pane, 0, kCodeNotesPartRange)->GetCtrlId());
    host.DestroyWindow();
}

TEST_CASE("NP11 cards added after the first layout can still be clicked", "[codenotespane]")
{
    //与发送代码窗口相同的顺序：批注栏先按两条批注排过版，再进入编辑、确定后多出第三条
    std::vector<CodeNote> two;
    two.push_back(CodeNote(2, 3, L"first"));
    two.push_back(CodeNote(8, 8, L"second"));
    std::unique_ptr<CodeNotesPane> pane(new CodeNotesPane());
    pane->SetNotes(two);
    pane->ForceLayout(RECT{ 0, 0, kPaneW, kPaneH });

    //新增一条（排在最后）：编辑卡片的「确定」要能被命中，卡片列表的矩形要包住编辑卡片
    pane->BeginEdit(12, 12, L"", -1);
    balloonwjui::DuiControl* ok = pane->FindCtrlById(kCodeNotesIdEditOk);
    REQUIRE(ok != NULL);
    const RECT okRc = ok->GetRect();
    const POINT okPt = { (okRc.left + okRc.right) / 2, (okRc.top + okRc.bottom) / 2 };
    CHECK(pane->HitTest(okPt) == ok);
    CHECK(::PtInRect(&ok->GetParent()->GetParent()->GetParent()->GetRect(), okPt));

    //确定之后（换成三条批注）：第三张卡片的行范围链接与「删除」都能被命中
    std::vector<CodeNote> three = two;
    three.push_back(CodeNote(12, 12, L"third"));
    pane->SetNotes(three);
    balloonwjui::DuiControl* range = CardPart(pane.get(), 2, kCodeNotesPartRange);
    balloonwjui::DuiControl* del = CardPart(pane.get(), 2, kCodeNotesPartDelete);
    REQUIRE(range != NULL);
    REQUIRE(del != NULL);
    const RECT rr = range->GetRect();
    const RECT dr = del->GetRect();
    const POINT rangePt = { (rr.left + rr.right) / 2, (rr.top + rr.bottom) / 2 };
    const POINT delPt = { (dr.left + dr.right) / 2, (dr.top + dr.bottom) / 2 };
    CHECK(pane->HitTest(rangePt) == range);
    CHECK(pane->HitTest(delPt) == del);
}

TEST_CASE("NP12 cards by several authors", "[codenotespane]")
{
    const long long kSelf = 100;
    const long long kOther = 200;
    const long long kUnnamed = 300;
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(1, 1, L"other's note", kOther));
    notes.push_back(CodeNote(1, 1, L"my new note", kSelf, true));
    notes.push_back(CodeNote(3, 3, L"by someone without a name", kUnnamed, true));
    std::map<long long, std::wstring> names;
    names[kSelf] = L"我自己";
    names[kOther] = L"李四";

    std::unique_ptr<CodeNotesPane> pane(new CodeNotesPane());
    pane->SetSelfId(kSelf);
    pane->SetAuthorNames(names);
    pane->SetNotes(notes);
    pane->ForceLayout(RECT{ 0, 0, kPaneW, kPaneH });

    //作者名与「新」
    balloonwjui::DuiLabel* a0 = dynamic_cast<balloonwjui::DuiLabel*>(CardPart(pane.get(), 0, kCodeNotesPartAuthor));
    balloonwjui::DuiLabel* a2 = dynamic_cast<balloonwjui::DuiLabel*>(CardPart(pane.get(), 2, kCodeNotesPartAuthor));
    REQUIRE(a0 != NULL);
    REQUIRE(a2 != NULL);
    CHECK(std::wstring((LPCWSTR)a0->GetText()) == L"李四");
    CHECK(std::wstring((LPCWSTR)a2->GetText()) == L"300");
    CHECK(CardPart(pane.get(), 0, kCodeNotesPartNew) == NULL);
    CHECK(CardPart(pane.get(), 1, kCodeNotesPartNew) != NULL);
    CHECK(CardPart(pane.get(), 2, kCodeNotesPartNew) != NULL);

    //只有本人的卡片有「修改」「删除」
    CHECK(CardPart(pane.get(), 0, kCodeNotesPartEdit) == NULL);
    CHECK(CardPart(pane.get(), 0, kCodeNotesPartDelete) == NULL);
    CHECK(CardPart(pane.get(), 1, kCodeNotesPartEdit) != NULL);
    CHECK(CardPart(pane.get(), 1, kCodeNotesPartDelete) != NULL);
    CHECK(CardPart(pane.get(), 2, kCodeNotesPartEdit) == NULL);
    CHECK_FALSE(pane->CanEditNote(0));
    CHECK(pane->CanEditNote(1));

    //同一行上两位作者的卡片都在，互不重叠；行范围与作者都落在卡片里
    const RECT c0 = CardPart(pane.get(), 0, kCodeNotesPartCard)->GetRect();
    const RECT c1 = CardPart(pane.get(), 1, kCodeNotesPartCard)->GetRect();
    CHECK(c1.top >= c0.bottom);
    const RECT r0 = CardPart(pane.get(), 0, kCodeNotesPartRange)->GetRect();
    const RECT n0 = a0->GetRect();
    CHECK(n0.left >= r0.right);
    CHECK(n0.right <= c0.right);

    //别人的批注不能进入修改；本人的可以
    pane->BeginEdit(1, 1, L"x", 0);
    CHECK_FALSE(pane->IsEditing());
    pane->BeginEdit(1, 1, L"x", 1);
    CHECK(pane->IsEditing());
    CHECK(pane->EditingIndex() == 1);
    pane->EndEdit();

    //没设名字表：不写作者
    std::unique_ptr<CodeNotesPane> plain(new CodeNotesPane());
    plain->SetSelfId(kSelf);
    plain->SetNotes(notes);
    plain->ForceLayout(RECT{ 0, 0, kPaneW, kPaneH });
    CHECK(CardPart(plain.get(), 0, kCodeNotesPartAuthor) == NULL);
    CHECK(CardPart(plain.get(), 1, kCodeNotesPartNew) != NULL);
}
