/**
 *  MemoCardTests.cpp
 *
 *  「备忘与提醒」卡片的测试（2026-10-03 用户反馈后改为四行卡片、悬停显示「修改」「删除」、显示创建人）：
 *    MEM-K1 群「备忘」页签的卡片文字：下一次提醒时间及其提醒时刻（到点重画用）、已结束、规则 · 提醒对象、创建人、能否修改；
 *    MEM-K2 本人备忘窗口的卡片文字：当天的钟点、规则 · 提前量 · 群名、群备忘只能查看；
 *    MEM-K3 创建人一行、备注压成一行、提醒对象、保存 / 删除超时的提示文字；
 *    MEM-K4 卡片控件树：各控件 ctrlId 落在本卡片的号段内、能修改的卡片才有两个按钮且起初隐藏、
 *           卡片高度随备注与创建人变化、悬停显示按钮（按钮拿到矩形）而离开后隐藏、视口外不算悬停；
 *    MEM-K5 由 ctrlId 反推卡片序号与部分的边界；
 *    MEM-K6 卡片视口的滚动条是 balloonui 统一的细滑块（与聊天消息区一致、可拖动，2026-10-04 起），内容超出时卡片右侧让出窄条，
 *           悬停前后卡片都不伸到滑块下面（2026-10-04 用户反馈 snapshots/011810.png）。
 *
 *  标签 [memo]。框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../UI/BalloonMemo/MemoViewModel.h"
#include "../UI/BalloonMemo/MemoCardList.h"
#include "../UI/BalloonHistory/HistoryRow.h"
#include "../UI/BalloonChatWnd/ChatScrollThumb.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace balloonmemo;
using memo::LocalMinute;
using memo::LocalMinuteFromParts;

namespace {

//群与群主（与 MemoViewModelTests 的取值无关，只要非 0）
const int64_t kGroupId = 268435463;
const int64_t kCreator = 5127;

//边栏宽、视口高（逻辑像素）：与群聊窗右侧边栏同宽
const int kViewW = 240;
const int kViewH = 400;

//卡片外面的底色
const COLORREF kOuterBg = RGB(0xFF, 0xFF, 0xFF);

//现在：2026-10-03 22:00
LocalMinute Now()
{
    return LocalMinuteFromParts(2026, 10, 3, 22, 0);
}

//一条每天重复、不限次数、提醒 3 人的群备忘：首次 2026-10-04 10:30
net::MemoItem GroupMemo()
{
    net::MemoItem item;
    item.m_memoid = 7;
    item.m_ctype = net::kMemoCtypeGroup;
    item.m_groupid = kGroupId;
    item.m_creator = kCreator;
    item.m_title = "周会";
    item.m_start = 202610041030LL;
    item.m_repeat = 1;
    item.m_count = 0;
    item.m_targetAll = false;
    item.m_targets.push_back(1);
    item.m_targets.push_back(2);
    item.m_targets.push_back(3);
    return item;
}

//一条不重复、提前 10 分钟的个人备忘：2026-10-05 17:30，备注两行
net::MemoItem PersonalMemo()
{
    net::MemoItem item;
    item.m_memoid = 9;
    item.m_ctype = net::kMemoCtypePersonal;
    item.m_title = "交周报";
    item.m_note = "第一行\r\n第二行";
    item.m_start = 202610051730LL;
    item.m_advance = 10;
    item.m_repeat = 0;
    return item;
}

//卡片内某部分的 ctrlId
UINT CardId(int index, int part)
{
    return kMemoCardIdBase + (UINT)index * kMemoCardIdStride + (UINT)part;
}

} // namespace

TEST_CASE("MEM-K1 群页签卡片：下一次提醒、规则与提醒对象、创建人", "[memo]")
{
    const LocalMinute now = Now();
    net::MemoItem item = GroupMemo();
    MemoCardText card = BuildGroupTabCard(item, now, L"张勇", LocalMinuteFromParts(2026, 10, 3, 21, 5), true);
    CHECK(card.m_memoid == 7);
    CHECK(card.m_title == L"周会");
    CHECK(card.m_note.empty());
    CHECK(card.m_when == L"10月4日 10:30");
    CHECK_FALSE(card.m_ended);
    CHECK(card.m_rule == L"每天 · 提醒 3 人");
    CHECK(card.m_creator == L"张勇 · 今天 21:05 创建");
    CHECK(card.m_editable);
    //到了这次事件的提醒时刻，卡片上的时间就该换成下一次：窗口据 m_remindAt 安排重画
    CHECK(card.m_remindAt == LocalMinuteFromParts(2026, 10, 4, 10, 30));

    //提醒时刻已过的那次事件不算：今天 22:05 的事件提前 10 分钟，21:55 就该提醒了，下一次是明天 22:05
    item.m_start = 202610032205LL;
    item.m_advance = 10;
    card = BuildGroupTabCard(item, now, L"张勇", 0, true);
    CHECK(card.m_when == L"10月4日 22:05");
    CHECK(card.m_remindAt == LocalMinuteFromParts(2026, 10, 4, 21, 55));
    CHECK(card.m_creator == L"张勇 创建");

    //不重复且已经过去：已结束
    item.m_start = 202610031000LL;
    item.m_advance = 0;
    item.m_repeat = 0;
    card = BuildGroupTabCard(item, now, L"张勇", 0, true);
    CHECK(card.m_ended);
    CHECK(card.m_when == L"已结束");
    CHECK(card.m_remindAt == 0);
    CHECK(card.m_rule == L"不重复 · 提醒 3 人");

    //提醒全体成员；普通成员不能修改
    item.m_targetAll = true;
    card = BuildGroupTabCard(item, now, L"张勇", 0, false);
    CHECK(card.m_rule == L"不重复 · 提醒全体成员");
    CHECK_FALSE(card.m_editable);
}

TEST_CASE("MEM-K2 本人备忘窗口卡片：当天钟点、规则与提前量、群备忘只能查看", "[memo]")
{
    const LocalMinute now = Now();
    const LocalMinute event = LocalMinuteFromParts(2026, 10, 5, 17, 30);

    //个人备忘：没有创建人一行，可以修改；备注里的换行换成空格
    MemoCardText card = BuildDayCard(PersonalMemo(), event, L"", L"", 0, now);
    CHECK(card.m_memoid == 9);
    CHECK(card.m_title == L"交周报");
    CHECK(card.m_note == L"第一行  第二行");
    CHECK(card.m_when == L"17:30");
    CHECK_FALSE(card.m_ended);
    CHECK(card.m_rule == L"不重复 · 提前 10 分钟");
    CHECK(card.m_creator.empty());
    CHECK(card.m_editable);
    CHECK(card.m_remindAt == 0);

    //群备忘：带所属群名与创建人，只能查看
    net::MemoItem group = GroupMemo();
    card = BuildDayCard(group, LocalMinuteFromParts(2026, 10, 4, 10, 30), L"产品群", L"张勇",
                        LocalMinuteFromParts(2026, 10, 1, 9, 0), now);
    CHECK(card.m_when == L"10:30");
    CHECK(card.m_rule == L"每天 · 到点提醒 · 群：产品群");
    CHECK(card.m_creator == L"张勇 · 10月1日 09:00 创建");
    CHECK_FALSE(card.m_editable);
}

TEST_CASE("MEM-K3 创建人一行、备注压成一行、提醒对象与超时提示", "[memo]")
{
    const LocalMinute now = Now();
    CHECK(CreatorText(L"张勇", 0, now) == L"张勇 创建");
    CHECK(CreatorText(L"张勇", LocalMinuteFromParts(2026, 10, 3, 8, 7), now) == L"张勇 · 今天 08:07 创建");
    CHECK(CreatorText(L"张勇", LocalMinuteFromParts(2025, 12, 31, 9, 0), now) == L"张勇 · 2025年12月31日 09:00 创建");

    CHECK(NoteOneLine("").empty());
    CHECK(NoteOneLine("a\nb\rc") == L"a b c");

    CHECK(TargetsText(PersonalMemo()).empty());
    CHECK(TargetsText(GroupMemo()) == L"提醒 3 人");

    CHECK(MemoFailureText(kMemoCodeTimeout, false) == L"服务器没有应答，备忘没有保存，请稍后重试");
    CHECK(MemoFailureText(kMemoCodeTimeout, true) == L"服务器没有应答，备忘没有删除，请稍后重试");
}

TEST_CASE("MEM-K4 卡片控件树：号段、按钮、高度与悬停", "[memo][layoutfit]")
{
    const LocalMinute now = Now();
    std::vector<MemoCardText> cards;
    //0：能修改、没有备注、有创建人；1：不能修改、有备注、有创建人；2：能修改、有备注、没有创建人
    cards.push_back(BuildGroupTabCard(GroupMemo(), now, L"张勇", 0, true));
    net::MemoItem withNote = GroupMemo();
    withNote.m_memoid = 8;
    withNote.m_note = "带一行备注";
    cards.push_back(BuildGroupTabCard(withNote, now, L"张勇", 0, false));
    cards.push_back(BuildDayCard(PersonalMemo(), LocalMinuteFromParts(2026, 10, 5, 17, 30), L"", L"", 0, now));

    std::unique_ptr<balloonwjui::DuiScrollView> scroll = CreateMemoCardScroll(1000);
    REQUIRE(scroll != nullptr);
    scroll->ForceLayout(RECT{ 0, 0, kViewW, kViewH });
    SetMemoCards(scroll.get(), cards, kOuterBg);
    REQUIRE(scroll->GetContent() != nullptr);
    REQUIRE(scroll->GetContent()->Children().size() == cards.size());

    //各卡片外层是 HistoryRow，号段按序号排开；高度与 MemoCardHeight 一致，备注与创建人各多一行
    for (size_t i = 0; i < cards.size(); ++i)
    {
        INFO("card " << i);
        balloonhistory::HistoryRow* row =
            dynamic_cast<balloonhistory::HistoryRow*>(scroll->FindCtrlById(CardId((int)i, kMemoCardPartRow)));
        REQUIRE(row != nullptr);
        const RECT rc = row->GetRect();
        CHECK(rc.bottom - rc.top == MemoCardHeight(cards[i]));
        CHECK(scroll->FindCtrlById(CardId((int)i, kMemoCardPartTitle)) != nullptr);
        CHECK(scroll->FindCtrlById(CardId((int)i, kMemoCardPartWhen)) != nullptr);
        CHECK(scroll->FindCtrlById(CardId((int)i, kMemoCardPartRule)) != nullptr);
        CHECK((scroll->FindCtrlById(CardId((int)i, kMemoCardPartNote)) != nullptr) == !cards[i].m_note.empty());
        CHECK((scroll->FindCtrlById(CardId((int)i, kMemoCardPartCreator)) != nullptr) == !cards[i].m_creator.empty());
    }
    CHECK(MemoCardHeight(cards[1]) > MemoCardHeight(cards[0]));
    CHECK(MemoCardHeight(cards[0]) == MemoCardHeight(cards[2]));

    //能修改的卡片才有两个按钮，起初隐藏
    balloonwjui::DuiButton* edit0 = dynamic_cast<balloonwjui::DuiButton*>(scroll->FindCtrlById(CardId(0, kMemoCardPartEdit)));
    balloonwjui::DuiButton* del0 = dynamic_cast<balloonwjui::DuiButton*>(scroll->FindCtrlById(CardId(0, kMemoCardPartDelete)));
    REQUIRE(edit0 != nullptr);
    REQUIRE(del0 != nullptr);
    CHECK_FALSE(edit0->IsVisible());
    CHECK_FALSE(del0->IsVisible());
    CHECK(scroll->FindCtrlById(CardId(1, kMemoCardPartEdit)) == nullptr);
    CHECK(scroll->FindCtrlById(CardId(1, kMemoCardPartDelete)) == nullptr);
    CHECK(scroll->FindCtrlById(CardId(2, kMemoCardPartEdit)) != nullptr);

    //悬停在第 0 张卡片上：两个按钮显示出来并拿到矩形（标题一行重排过），标题让出宽度；别的卡片不受影响
    balloonwjui::DuiControl* title0 = scroll->FindCtrlById(CardId(0, kMemoCardPartTitle));
    REQUIRE(title0 != nullptr);
    const RECT titleBefore = title0->GetRect();
    const RECT rc0 = scroll->FindCtrlById(CardId(0, kMemoCardPartRow))->GetRect();
    POINT inside;
    inside.x = (rc0.left + rc0.right) / 2;
    inside.y = (rc0.top + rc0.bottom) / 2;
    UpdateMemoCardHover(scroll.get(), inside);
    CHECK(edit0->IsVisible());
    CHECK(del0->IsVisible());
    const RECT rcEdit = edit0->GetRect();
    const RECT rcDel = del0->GetRect();
    CHECK(rcEdit.right - rcEdit.left > 0);
    CHECK(rcDel.left >= rcEdit.right);
    CHECK(rcDel.right <= rc0.right);
    CHECK(title0->GetRect().right < titleBefore.right);
    balloonhistory::HistoryRow* row2 =
        dynamic_cast<balloonhistory::HistoryRow*>(scroll->FindCtrlById(CardId(2, kMemoCardPartRow)));
    REQUIRE(row2 != nullptr);
    CHECK_FALSE(row2->IsHovered());
    CHECK_FALSE(scroll->FindCtrlById(CardId(2, kMemoCardPartEdit))->IsVisible());

    //移到第 2 张卡片：第 0 张收起按钮，第 2 张显示
    const RECT rc2 = row2->GetRect();
    POINT onSecond;
    onSecond.x = (rc2.left + rc2.right) / 2;
    onSecond.y = (rc2.top + rc2.bottom) / 2;
    UpdateMemoCardHover(scroll.get(), onSecond);
    CHECK_FALSE(edit0->IsVisible());
    CHECK(scroll->FindCtrlById(CardId(2, kMemoCardPartEdit))->IsVisible());

    //离开窗口：全部收起
    POINT none;
    none.x = -1;
    none.y = -1;
    UpdateMemoCardHover(scroll.get(), none);
    CHECK_FALSE(row2->IsHovered());
    CHECK_FALSE(scroll->FindCtrlById(CardId(2, kMemoCardPartEdit))->IsVisible());

    //视口外不算悬停：视口只露出第 0 张卡片的上半截时，落在第 0 张卡片下半截（视口外）的光标不算
    scroll->ForceLayout(RECT{ 0, 0, kViewW, MemoCardHeight(cards[0]) / 2 });
    const RECT rcLow = scroll->FindCtrlById(CardId(0, kMemoCardPartRow))->GetRect();
    POINT below;
    below.x = (rcLow.left + rcLow.right) / 2;
    below.y = rcLow.bottom - 1;
    UpdateMemoCardHover(scroll.get(), below);
    CHECK_FALSE(edit0->IsVisible());

    //替换为空列表：没有卡片
    SetMemoCards(scroll.get(), std::vector<MemoCardText>(), kOuterBg);
    CHECK(scroll->GetContent()->Children().empty());
}

TEST_CASE("MEM-K5 由 ctrlId 反推卡片序号与部分", "[memo]")
{
    int index = -1;
    int part = -1;
    CHECK(MemoCardFromCtrlId(kMemoCardIdBase, index, part));
    CHECK(index == 0);
    CHECK(part == kMemoCardPartRow);
    CHECK(MemoCardFromCtrlId(CardId(2, kMemoCardPartDelete), index, part));
    CHECK(index == 2);
    CHECK(part == kMemoCardPartDelete);
    CHECK(MemoCardFromCtrlId(CardId(kMemoCardMaxCount - 1, kMemoCardPartCreator), index, part));
    CHECK(index == kMemoCardMaxCount - 1);

    //号段之外：出参不变
    index = -1;
    part = -1;
    CHECK_FALSE(MemoCardFromCtrlId(kMemoCardIdBase - 1, index, part));
    CHECK_FALSE(MemoCardFromCtrlId(kMemoCardIdBase + (UINT)kMemoCardMaxCount * kMemoCardIdStride, index, part));
    CHECK_FALSE(MemoCardFromCtrlId(0, index, part));
    CHECK(index == -1);
    CHECK(part == -1);
}

TEST_CASE("MEM-K6 卡片视口：聊天同款细滚动条，滑块不压在卡片上", "[memo][layoutfit]")
{
    const LocalMinute now = Now();
    //六张带备注与创建人的卡片，在 240 × 300 的视口里放不下
    std::vector<MemoCardText> cards;
    const int kManyCards = 6;
    for (int i = 0; i < kManyCards; ++i)
    {
        net::MemoItem item = GroupMemo();
        item.m_memoid = 100 + i;
        item.m_note = "备注";
        cards.push_back(BuildGroupTabCard(item, now, L"张勇", 0, true));
    }
    const int kShortViewH = 300;
    std::unique_ptr<balloonwjui::DuiScrollView> scroll = CreateMemoCardScroll(1000);
    CMemoCardScrollView* view = dynamic_cast<CMemoCardScrollView*>(scroll.get());
    REQUIRE(view != nullptr);
    scroll->ForceLayout(RECT{ 0, 0, kViewW, kShortViewH });
    SetMemoCards(scroll.get(), cards, kOuterBg);
    REQUIRE(scroll->GetContentHeight() > kShortViewH);

    //滑块是 balloonui 统一的细滑块（与聊天消息区同一尺寸），贴着视口右缘，在窄条之内
    const RECT thumb = view->ThumbRect();
    REQUIRE(thumb.bottom > thumb.top);
    CHECK(thumb.right - thumb.left == balloonchatwnd::kChatScrollBarWidth);
    CHECK(thumb.right - thumb.left == (int)balloonwjui::DuiScrollBar::kOverlayThumbPx);
    CHECK(thumb.right == kViewW - balloonchatwnd::kChatScrollBarMargin);
    CHECK(thumb.left >= kViewW - kMemoScrollGutter);
    CHECK(thumb.top == 0);

    //每张卡片的右缘都在滑块左边、留有空隙；悬停之后也一样
    for (int i = 0; i < kManyCards; ++i)
    {
        INFO("card " << i);
        balloonwjui::DuiControl* row = scroll->FindCtrlById(CardId(i, kMemoCardPartRow));
        REQUIRE(row != nullptr);
        CHECK(row->GetRect().right <= kViewW - kMemoScrollGutter);
        CHECK(row->GetRect().right < thumb.left);
    }
    const RECT rc0 = scroll->FindCtrlById(CardId(0, kMemoCardPartRow))->GetRect();
    POINT inside;
    inside.x = (rc0.left + rc0.right) / 2;
    inside.y = (rc0.top + rc0.bottom) / 2;
    UpdateMemoCardHover(scroll.get(), inside);
    CHECK(scroll->FindCtrlById(CardId(0, kMemoCardPartEdit))->IsVisible());
    CHECK(scroll->FindCtrlById(CardId(0, kMemoCardPartRow))->GetRect().right == rc0.right);
    CHECK(scroll->FindCtrlById(CardId(0, kMemoCardPartBody))->GetRect().right <= rc0.right);
    CHECK(scroll->FindCtrlById(CardId(0, kMemoCardPartDelete))->GetRect().right <= rc0.right);

    //落在滑块上的点由视口的滚动条接下（悬浮在窄条之上），卡片里的点照常交给卡片
    POINT onThumb;
    onThumb.x = (thumb.left + thumb.right) / 2;
    onThumb.y = (thumb.top + thumb.bottom) / 2;
    balloonwjui::DuiScrollBar* bar = scroll->GetScrollBar();
    REQUIRE(bar != nullptr);
    CHECK(scroll->HitTest(onThumb) == bar);
    CHECK(scroll->HitTest(inside) != bar);

    //按住滑块往下拖：内容随之滚动；松开后不再跟随
    REQUIRE(bar->OnLButtonDown(onThumb, MK_LBUTTON));
    const int kDragPx = 60;
    POINT dragged = onThumb;
    dragged.y += kDragPx;
    bar->OnMouseMove(dragged, MK_LBUTTON);
    const int pos = scroll->GetScrollPos();
    CHECK(pos > 0);
    //滚动位置是整数，由它反推的滑块位置可能差 1 像素
    const int movedTop = view->ThumbRect().top;
    CHECK(movedTop >= thumb.top + kDragPx - 1);
    CHECK(movedTop <= thumb.top + kDragPx + 1);
    bar->OnLButtonUp(dragged, 0);
    POINT further = dragged;
    further.y += kDragPx;
    bar->OnMouseMove(further, 0);
    CHECK(scroll->GetScrollPos() == pos);

    //内容放得下时没有滑块，卡片用满整宽
    std::vector<MemoCardText> one(1, cards[0]);
    SetMemoCards(scroll.get(), one, kOuterBg);
    scroll->SetScrollPos(0);
    const RECT none = view->ThumbRect();
    CHECK(none.bottom <= none.top);
    CHECK(scroll->FindCtrlById(CardId(0, kMemoCardPartRow))->GetRect().right == kViewW);
}
