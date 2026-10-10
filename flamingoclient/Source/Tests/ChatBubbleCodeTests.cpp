/**
 *  消息气泡的代码片段分支的单测（docs/代码片段方案.md §3.2、§5.3），ChatBubbleCodeTests.cpp
 *
 *  [codebubble]
 *    CU1 代码片段走卡片排版：行高与同样正文的普通文字不同；卡片里不能选取文字（整条复制给原文）
 *    CU2 超过折叠行数时只显示前 15 行：40 行与 80 行的代码片段行高相同，都比 5 行的高
 *    CU3 不认识的语言按纯文本显示：与纯文本的代码片段行高相同
 *    CU4 SetMessage 换成普通文字消息后回到普通路径（可以选取文字）
 *    CU5 带批注的代码片段与不带批注时行高相同（卡片只标出被批注的行，不显示说明）
 *    CU6 哪些消息能追加批注（右键「添加批注…」与代码查看窗的「添加批注」，docs/代码片段方案.md 第 11.3 节）：
 *        已有服务器 msgid 的代码片段能，发送中、发送失败（还没有 msgid）的与普通文字不能
 *
 *  气泡的卡片方法是私有的，这里只通过行高与文字选取这类公开行为观察；「复制」、点击打开查看窗、右键菜单在界面上实测。
 *  ChatBubble::Measure 用屏幕 DC 测量；全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../Common/Code/CodeLexer.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatCodeMessage.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"

using namespace balloonwjui;
using namespace balloonchatwnd;

namespace {

//消息区宽度与高度（像素）
const int kListWidth = 640;
const int kListHeight = 4000;

//CU2：几段代码的行数
const int kShortLines = 5;
const int kLongLines = 40;
const int kLongerLines = 80;

//一段 C++ 代码
const wchar_t* const kCppBody = L"int main()\n{\n    int a = 1;\n    return a;\n}";

//由行数造一段代码：每行一条赋值语句
std::wstring MakeCode(int lines)
{
    std::wstring code;
    for (int i = 0; i < lines; ++i)
    {
        if (i > 0)
            code += L"\n";
        code += L"value = value + 1;";
    }
    return code;
}

//造一条消息：isCode 为 true 时是代码片段
ChatMessage MakeMessage(const std::wstring& text, bool isCode, int lang)
{
    ChatMessage m = MakeTextMessage(false, CString(text.c_str()));
    m.isCode = isCode;
    m.codeLang = lang;
    return m;
}

//把一条消息单独放进一个列表并排版，返回该行高度
int RowHeightOf(const ChatMessage& msg)
{
    ChatMessageList list;
    list.AppendMessage(msg);
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
    const std::vector<std::unique_ptr<DuiControl> >& kids = list.Children();
    REQUIRE(!kids.empty());
    const RECT& row = kids.back()->GetRect();
    return row.bottom - row.top;
}

} // namespace

TEST_CASE("CU1 code snippets are laid out as a card", "[codebubble]")
{
    const int codeH = RowHeightOf(MakeMessage(kCppBody, true, codesnippet::code_lang_cpp));
    const int textH = RowHeightOf(MakeMessage(kCppBody, false, 0));
    CHECK(codeH > 0);
    CHECK(textH > 0);
    CHECK(codeH != textH);

    //卡片里不能选取文字
    ChatMessageList list;
    list.AppendMessage(MakeMessage(kCppBody, true, codesnippet::code_lang_cpp));
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
    ChatBubble* bubble = dynamic_cast<ChatBubble*>(list.Children().back().get());
    REQUIRE(bubble != NULL);
    bubble->SelectAllText();
    CHECK(bubble->GetSelectedText().IsEmpty());
}

TEST_CASE("CU2 long code snippets are folded to the first lines", "[codebubble]")
{
    const int shortH = RowHeightOf(MakeMessage(MakeCode(kShortLines), true, codesnippet::code_lang_cpp));
    const int longH = RowHeightOf(MakeMessage(MakeCode(kLongLines), true, codesnippet::code_lang_cpp));
    const int longerH = RowHeightOf(MakeMessage(MakeCode(kLongerLines), true, codesnippet::code_lang_cpp));
    CHECK(longH == longerH);
    CHECK(shortH < longH);
}

TEST_CASE("CU3 unknown languages are shown as plain text", "[codebubble]")
{
    const int unknownLang = codesnippet::code_lang_count + 1;
    const int plainH = RowHeightOf(MakeMessage(kCppBody, true, codesnippet::code_lang_plain));
    const int unknownH = RowHeightOf(MakeMessage(kCppBody, true, unknownLang));
    CHECK(unknownH == plainH);
}

TEST_CASE("CU4 switching back to a plain text message", "[codebubble]")
{
    ChatMessageList list;
    list.AppendMessage(MakeMessage(kCppBody, true, codesnippet::code_lang_cpp));
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
    ChatBubble* bubble = dynamic_cast<ChatBubble*>(list.Children().back().get());
    REQUIRE(bubble != NULL);

    bubble->SetMessage(MakeMessage(kCppBody, false, 0));
    list.ForceLayout(rc);
    bubble->SelectAllText();
    CHECK_FALSE(bubble->GetSelectedText().IsEmpty());
}

TEST_CASE("CU5 notes do not change the height of the card", "[codebubble]")
{
    ChatMessage plain = MakeMessage(MakeCode(kLongLines), true, codesnippet::code_lang_cpp);
    ChatMessage noted = plain;
    noted.codeNotes.push_back(codesnippet::CodeNote(2, 3, L"note"));
    noted.codeNotes.push_back(codesnippet::CodeNote(kLongLines, kLongLines, L"below the fold"));
    CHECK(RowHeightOf(noted) == RowHeightOf(plain));
}

TEST_CASE("CU6 which messages can be annotated", "[codebubble]")
{
    const long long kMsgid = 900123;
    ChatMessage code = MakeTextMessage(false, CString(L"int a;"));
    code.isCode = true;
    //还没有服务器 msgid（发送中、发送失败）：不能追加批注
    CHECK_FALSE(CanAnnotateCodeMessage(code));
    code.msgid = kMsgid;
    CHECK(CanAnnotateCodeMessage(code));
    //普通文字：不能
    ChatMessage text = MakeTextMessage(false, CString(L"int a;"));
    text.msgid = kMsgid;
    CHECK_FALSE(CanAnnotateCodeMessage(text));
}
