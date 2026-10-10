/**
 *  聊天窗工具栏「Markdown 编辑器」按钮的单元测试，用例 MT1 ~ MT3（docs/Markdown消息方案.md 第 11.3 节）：
 *
 *    - MT1 DLL 不可用：按钮隐藏，工具栏不留空位（「字体」右边紧接着下一个按钮）
 *    - MT2 DLL 可用：按钮显示，排在「字体」右边
 *    - MT3 最小窗口宽度下工具栏最右边的按钮不越过右边缘（加按钮时窗口最小宽度要跟着核对）
 *
 *  每个用例都分单聊窗与群聊窗两节。DLL 的可用与否用 markdown::SetMarkdownApiOverrideForTest 模拟，每个用例结束时
 *  恢复。前身是聊天输入区 Markdown 模式的 [mdcomposer] 用例（2026-10-06 输入区的 Markdown 模式改为独立编辑器后删除），
 *  MT3 即原 MC8。框架 Catch2 v3，标签 [mdtoolbar]。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatWindowLayout.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"
#include "../UI/BalloonChatWnd/ChatMarkdown.h"
#include "../Feature/Markdown/MarkdownDocument.h"
#include "../../FlamingoMarkdown/MdApiTable.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiSplitter.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

//单聊窗与群聊窗的标题栏高度（像素，与 BalloonChatWindow.cpp / BalloonGroupChatWindow.cpp 相同）；
//客户区控件树排在标题栏下方，内容区高度 = 整窗高度 - 标题栏高度
const int kMtTitleBarPx = 32;

//工具栏按钮之间的间距（像素，与两份布局 XML 里工具栏的 gap 相同）
const int kMtToolbarGap = 2;

/**
 *  在作用域内替换 Markdown 函数表，析构时恢复为宿主的函数表。
 */
class MtApiOverride
{
public:
    explicit MtApiOverride(bool available)
    {
        markdown::SetMarkdownApiOverrideForTest(true, available ? flamingomd::GetMdApiTable() : NULL);
    }

    ~MtApiOverride()
    {
        markdown::SetMarkdownApiOverrideForTest(false, NULL);
    }
};

/**
 *  一种聊天窗（单聊或群聊）测试要用到的控件编号与尺寸。
 */
struct MtWindow
{
    bool m_group;              //是否为群聊窗
    UINT m_button;             //Markdown 编辑器按钮
    UINT m_fontButton;         //工具栏「字体」按钮：Markdown 编辑器按钮紧挨在它右边
    UINT m_lastToolbarButton;  //工具栏最右边的按钮（单聊为「更多」，群聊为「@」）
    UINT m_splitter;           //消息区与输入区块之间的分隔条
    int  m_minWidth;           //最小整窗宽度（像素）
    int  m_minHeight;          //最小整窗高度（像素）
};

MtWindow singleWindow()
{
    MtWindow w;
    w.m_group = false;
    w.m_button = kIdTbMarkdown;
    w.m_fontButton = kIdTbFont;
    w.m_lastToolbarButton = kIdTbPlus;
    w.m_splitter = kIdInputSplitter;
    w.m_minWidth = kChatWindowMinWidth;
    w.m_minHeight = kChatWindowMinHeight;
    return w;
}

MtWindow groupWindow()
{
    MtWindow w;
    w.m_group = true;
    w.m_button = kGIdTbMarkdown;
    w.m_fontButton = kGIdTbFont;
    w.m_lastToolbarButton = kGIdMention;
    w.m_splitter = kGIdInputSplitter;
    w.m_minWidth = kGroupChatWindowMinWidth;
    w.m_minHeight = kGroupChatWindowMinHeight;
    return w;
}

//按窗口种类建出客户区控件树（不预填模拟消息）
std::unique_ptr<DuiControl> buildBody(const MtWindow& w)
{
    return w.m_group ? BuildGroupChatWindowBody(false) : BuildChatWindowBody(false);
}

DuiControl* findCtrl(DuiControl* root, UINT id)
{
    DuiControl* c = root->FindCtrlById(id);
    REQUIRE(c != NULL);
    return c;
}

/**
 *  按整窗宽、高排版客户区，并像窗口 OnRelayout 那样把输入区块固定为默认高度 kChatInputBlockDefaultPx。
 */
void layoutWindow(DuiControl* root, const MtWindow& w, int windowWidth, int windowHeight)
{
    const RECT area = { 0, 0, windowWidth, windowHeight - kMtTitleBarPx };
    root->ForceLayout(area);
    DuiSplitter* sp = dynamic_cast<DuiSplitter*>(root->FindCtrlById(w.m_splitter));
    REQUIRE(sp != NULL);
    const int track = (sp->GetRect().bottom - sp->GetRect().top) - sp->GetBarThickness();
    sp->SetSplitPx(track - kChatInputBlockDefaultPx);
}

//工具栏里紧跟在 id 后面的那个子控件；找不到时返回 NULL
DuiControl* nextSibling(DuiControl* root, UINT id)
{
    DuiControl* ctrl = findCtrl(root, id);
    DuiControl* toolbar = ctrl->GetParent();
    REQUIRE(toolbar != NULL);
    const std::vector<std::unique_ptr<DuiControl> >& kids = toolbar->Children();
    for (size_t i = 0; i + 1 < kids.size(); ++i)
    {
        if (kids[i].get() == ctrl)
            return kids[i + 1].get();
    }
    return NULL;
}

//MT1：DLL 不可用
void checkUnavailable(const MtWindow& w)
{
    MtApiOverride api(false);
    std::unique_ptr<DuiControl> root = buildBody(w);
    REQUIRE(root != NULL);
    layoutWindow(root.get(), w, w.m_minWidth, w.m_minHeight);
    CHECK_FALSE(ApplyMarkdownEditorButton(root.get(), w.m_button));
    CHECK_FALSE(findCtrl(root.get(), w.m_button)->IsVisible());

    //隐藏的按钮不在工具栏里留空位：「字体」右边紧接着按钮后面的那个控件
    DuiControl* after = nextSibling(root.get(), w.m_button);
    REQUIRE(after != NULL);
    CHECK(after->GetRect().left - findCtrl(root.get(), w.m_fontButton)->GetRect().right <= kMtToolbarGap);
}

//MT2：DLL 可用
void checkAvailable(const MtWindow& w)
{
    MtApiOverride api(true);
    std::unique_ptr<DuiControl> root = buildBody(w);
    REQUIRE(root != NULL);
    layoutWindow(root.get(), w, w.m_minWidth, w.m_minHeight);
    CHECK(ApplyMarkdownEditorButton(root.get(), w.m_button));
    DuiControl* button = findCtrl(root.get(), w.m_button);
    CHECK(button->IsVisible());
    CHECK(button->GetRect().right - button->GetRect().left > 0);
    //排在「字体」右边，中间只隔一个间距
    CHECK(button->GetRect().left - findCtrl(root.get(), w.m_fontButton)->GetRect().right == kMtToolbarGap);
    //不存在的编号返回 false
    CHECK_FALSE(ApplyMarkdownEditorButton(root.get(), 0));
    CHECK_FALSE(ApplyMarkdownEditorButton(NULL, w.m_button));
}

//MT3：最小宽度下工具栏不越界
void checkToolbarFits(const MtWindow& w)
{
    MtApiOverride api(true);
    std::unique_ptr<DuiControl> root = buildBody(w);
    REQUIRE(root != NULL);
    layoutWindow(root.get(), w, w.m_minWidth, w.m_minHeight);
    ApplyMarkdownEditorButton(root.get(), w.m_button);

    DuiControl* last = findCtrl(root.get(), w.m_lastToolbarButton);
    DuiControl* toolbar = last->GetParent();
    REQUIRE(toolbar != NULL);
    INFO("toolbar right = " << toolbar->GetRect().right << ", last button right = " << last->GetRect().right);
    CHECK(last->GetRect().right <= toolbar->GetRect().right);
    CHECK(last->GetRect().right - last->GetRect().left > 0);
}

} // namespace

TEST_CASE("MT1 DLL 不可用：Markdown 编辑器按钮隐藏，工具栏不留空位", "[mdtoolbar]")
{
    SECTION("单聊窗")
    {
        checkUnavailable(singleWindow());
    }
    SECTION("群聊窗")
    {
        checkUnavailable(groupWindow());
    }
}

TEST_CASE("MT2 DLL 可用：Markdown 编辑器按钮显示在「字体」右边", "[mdtoolbar]")
{
    SECTION("单聊窗")
    {
        checkAvailable(singleWindow());
    }
    SECTION("群聊窗")
    {
        checkAvailable(groupWindow());
    }
}

TEST_CASE("MT3 最小窗口宽度下输入区工具栏不越过右边缘", "[mdtoolbar]")
{
    SECTION("单聊窗")
    {
        checkToolbarFits(singleWindow());
    }
    SECTION("群聊窗")
    {
        checkToolbarFits(groupWindow());
    }
}
