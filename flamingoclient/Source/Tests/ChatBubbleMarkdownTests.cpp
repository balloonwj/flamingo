/**
 *  消息气泡的 Markdown 分支与聊天侧辅助函数的单元测试，用例 MB1 ~ MB7（docs/Markdown消息方案.md 第 8 节）：
 *
 *    - MB1 DLL 不可用时，Markdown 消息与同样正文的普通文字消息行高相同，全选后取到的就是源文本
 *    - MB2 DLL 可用时，Markdown 消息显示为卡片，卡片上没有可选文字
 *    - MB3 卡片高度随摘要增加，摘要超过三行后不再增高
 *    - MB4 不带 Markdown 标志的消息不受 DLL 可用与否的影响
 *    - MB5 SetMessage 换成普通文字消息后回到普通路径（可以选中）
 *    - MB6 摘要：DLL 可用时为「[Markdown] 标题」；不可用时与 BodyToDigest 相同
 *    - MB7 交给 DLL 的源文本：表情换成「[表情]」，转义斜杠还原，网址里的 // 保留
 *
 *  DLL 的可用与否用 markdown::SetMarkdownApiOverrideForTest 模拟（函数表直接编进测试程序），每个用例结束时恢复。
 *  ChatBubble::Measure 用屏幕 DC 测量；全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。框架 Catch2 v3，
 *  标签 [mdbubble]。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatMarkdown.h"
#include "../UI/BalloonChat/BalloonChatMsgFormat.h"
#include "../Feature/Markdown/MarkdownDocument.h"
#include "../../FlamingoMarkdown/MdApiTable.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

//消息区宽度与高度（逻辑像素）
const int kListWidth = 520;
const int kListHeight = 600;

//测试用的 Markdown 正文
const wchar_t* const kMarkdownBody = L"## Title\n\n- **done** item\n- second";

/**
 *  在作用域内替换 Markdown 函数表，析构时恢复为宿主的函数表。
 */
class ApiOverride
{
public:
    explicit ApiOverride(bool available)
    {
        markdown::SetMarkdownApiOverrideForTest(true, available ? flamingomd::GetMdApiTable() : NULL);
    }

    ~ApiOverride()
    {
        markdown::SetMarkdownApiOverrideForTest(false, NULL);
    }
};

//造一条文字消息
ChatMessage MakeMessage(const wchar_t* text, bool markdown)
{
    ChatMessage m = MakeTextMessage(false, CString(text));
    m.isMarkdown = markdown;
    return m;
}

//列表里第 idx 个气泡
ChatBubble* BubbleAt(ChatMessageList& list, size_t idx)
{
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    REQUIRE(idx < kids.size());
    ChatBubble* b = dynamic_cast<ChatBubble*>(kids[idx].get());
    REQUIRE(b != NULL);
    return b;
}

//把一条消息放进列表并排版，返回它的气泡
ChatBubble* LayoutOne(ChatMessageList& list, const ChatMessage& msg)
{
    list.AppendMessage(msg);
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
    return BubbleAt(list, list.Children().size() - 1);
}

//全选后取到的文字
CString SelectAllAndGet(ChatBubble* bubble)
{
    bubble->SelectAllText();
    return bubble->GetSelectedText();
}

} // namespace

TEST_CASE("MB1 DLL 不可用时 Markdown 消息与普通文字消息完全相同", "[mdbubble]")
{
    ApiOverride off(false);
    ChatMessageList listA;
    ChatMessageList listB;
    ChatBubble* md = LayoutOne(listA, MakeMessage(kMarkdownBody, true));
    ChatBubble* plain = LayoutOne(listB, MakeMessage(kMarkdownBody, false));
    CHECK(md->GetRowHeight() == plain->GetRowHeight());
    CHECK(SelectAllAndGet(md) == SelectAllAndGet(plain));
    CHECK(SelectAllAndGet(md).Find(L"##") >= 0);
}

TEST_CASE("MB2 DLL 可用时显示为卡片，卡片上没有可选文字", "[mdbubble]")
{
    ApiOverride on(true);
    ChatMessageList list;
    ChatBubble* md = LayoutOne(list, MakeMessage(kMarkdownBody, true));
    CHECK(md->GetRowHeight() > 0);
    //卡片不可选中：全选取不到文字，在预览窗口里选中复制（docs/Markdown消息方案.md 第 11.3 节）
    CHECK(SelectAllAndGet(md).IsEmpty());
}

TEST_CASE("MB3 卡片高度随摘要增加，超过三行后不再增高", "[mdbubble]")
{
    ApiOverride on(true);
    std::wstring longer = L"# T\n\n";
    std::wstring longest = L"# T\n\n";
    for (int i = 0; i < 60; ++i)
        longer += L"many words here ";
    for (int i = 0; i < 300; ++i)
        longest += L"many words here ";
    ChatMessageList l1;
    ChatMessageList l2;
    ChatMessageList l3;
    ChatMessageList l4;
    const int titleOnly = LayoutOne(l1, MakeMessage(L"# T", true))->GetRowHeight();
    const int oneLine = LayoutOne(l2, MakeMessage(L"# T\n\nshort", true))->GetRowHeight();
    const int clampedA = LayoutOne(l3, MakeMessage(longer.c_str(), true))->GetRowHeight();
    const int clampedB = LayoutOne(l4, MakeMessage(longest.c_str(), true))->GetRowHeight();
    CHECK(titleOnly < oneLine);
    CHECK(oneLine < clampedA);
    CHECK(clampedA == clampedB);
}

TEST_CASE("MB4 不带 Markdown 标志的消息不受 DLL 影响", "[mdbubble]")
{
    int heightOff = 0;
    CString textOff;
    {
        ApiOverride off(false);
        ChatMessageList list;
        ChatBubble* b = LayoutOne(list, MakeMessage(kMarkdownBody, false));
        heightOff = b->GetRowHeight();
        textOff = SelectAllAndGet(b);
    }
    ApiOverride on(true);
    ChatMessageList list;
    ChatBubble* b = LayoutOne(list, MakeMessage(kMarkdownBody, false));
    CHECK(b->GetRowHeight() == heightOff);
    CHECK(SelectAllAndGet(b) == textOff);
}

TEST_CASE("MB5 换成普通文字消息后回到普通路径", "[mdbubble]")
{
    ApiOverride on(true);
    ChatMessageList list;
    ChatBubble* b = LayoutOne(list, MakeMessage(kMarkdownBody, true));
    CHECK(SelectAllAndGet(b).IsEmpty());
    b->SetMessage(MakeMessage(kMarkdownBody, false));
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
    CHECK(SelectAllAndGet(b).Find(L"##") >= 0);
}

TEST_CASE("MB6 摘要：DLL 可用时为「[Markdown] 标题」，不可用时同 BodyToDigest", "[mdbubble]")
{
    const CString body(kMarkdownBody);
    {
        ApiOverride on(true);
        CHECK(MarkdownDigestFromBody(body) == CString(L"[Markdown] Title"));
        //没有标题时取第一行，标记去掉
        CHECK(MarkdownDigestFromUtf8Body("**a** `b`\nsecond") == "[Markdown] a b");
        //没有文字时写「Markdown 文档」
        CHECK(MarkdownDigestFromBody(CString(L"---")) == CString(L"[Markdown] Markdown 文档"));
    }
    {
        ApiOverride off(false);
        CHECK(MarkdownDigestFromBody(body) == balloonchat::BodyToDigest(body));
    }
}

TEST_CASE("MB7 交给 DLL 的源文本：表情换成 [表情]，转义斜杠还原，网址保留", "[mdbubble]")
{
    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(_T("see http://a.com/x and /f[\"1\"] ")));
    segs.push_back(balloonchat::ChatSegment::Face(5));
    const CString body = balloonchat::BuildSendBody(segs);
    const std::string source = MarkdownSourceFromBody(body);
    //[表情] 的 UTF-8 字节
    const std::string faceText = "[\xE8\xA1\xA8\xE6\x83\x85]";
    CHECK(source == std::string("see http://a.com/x and /f[\"1\"] ") + faceText);
}
