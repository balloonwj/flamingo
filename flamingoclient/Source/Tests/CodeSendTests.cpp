/**
 *  代码片段发送端的单测（docs/代码片段方案.md §3.1、§5.5），CodeSendTests.cpp
 *
 *  [codesend]
 *    CS1 ``` 写法带语言名：取出中间的代码，语言按名字
 *    CS2 ``` 写法不带语言名：语言按自动识别
 *    CS3 前后有空白行与空格、换行为 CRLF 或 CR 时照样识别，代码的换行统一为 LF
 *    CS4 不算 ``` 写法：没有闭合、只有开头一行、中间还有 ``` 行、闭合之后还有文字、中间没有内容、开头之前有文字
 *    CS5 语言名不认识或含非 ASCII 字符时按纯文本
 *    CS6 Markdown 模式下不按 ``` 写法转换（TryCodeFenceSend）
 *    CS7 粘贴识别只在输入框原本为空、不在 Markdown 模式、内容像代码时提示；RichEdit 的 CR 换行同样识别
 *    CS8 「重新编辑」：发送失败的代码片段带上原文与语言，普通文字不带
 *    CS10 追加批注的消息发送失败后「重新编辑」（docs/代码片段方案.md 第 11.3 节）：带上引用的原消息 msgid 与发送者；
 *         原消息不在列表里时以没有标「新」的批注代替原消息的批注，与带回的批注合并后新批注仍是新的；不带引用的没有原消息
 *    CS9 粘贴识别提示条：单聊、群聊建好后隐藏；显示并重排后在输入框上方，说明文字与两个链接都落在提示条内
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../Common/Code/CodeLexer.h"
#include "../Common/Code/CodeNotes.h"
#include "../UI/BalloonChatWnd/ChatCodeMessage.h"
#include "../UI/BalloonChatWnd/ChatReedit.h"
#include "../UI/BalloonChatWnd/ChatWindowLayout.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"

using namespace balloonchatwnd;

namespace {

//多行、带缩进与分号的 C++ 代码：LooksLikeCode 与 DetectCodeLanguage 都能认出
const wchar_t* const kCppSample =
    L"#include <stdio.h>\n"
    L"\n"
    L"int main(int argc, char* argv[])\n"
    L"{\n"
    L"    printf(\"hello\\n\");\n"
    L"    return 0;\n"
    L"}";

//一段中文正文：不该被当成代码
const wchar_t* const kProseSample =
    L"明天上午十点在三楼会议室开会，讨论下个版本的发布计划。\n"
    L"请大家提前看一下需求文档，有问题会上一起讨论。";

//把 LF 换成 RichEdit 里的 CR
std::wstring ToCr(const std::wstring& s)
{
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == L'\n')
            out[i] = L'\r';
    }
    return out;
}

//CS9：聊天窗按默认整窗尺寸扣掉 32 像素标题栏后的内容区宽高（与 BalloonChatWindow.cpp、BalloonGroupChatWindow.cpp 的常量一致）
const int kSingleContentW = 760;
const int kSingleContentH = 688;
const int kGroupContentW = 1000;
const int kGroupContentH = 688;

//CS9：提示条建好后隐藏；显示并对所在容器重排后，提示条在输入框上方，几个子控件都落在提示条内
void CheckPasteBar(balloonwjui::DuiControl* root, UINT barId, UINT textId, UINT sendId, UINT dismissId,
                   UINT inputId, int width, int height)
{
    REQUIRE(root != NULL);
    balloonwjui::DuiControl* bar = root->FindCtrlById(barId);
    REQUIRE(bar != NULL);
    CHECK_FALSE(bar->IsVisible());
    root->ForceLayout(RECT{ 0, 0, width, height });

    bar->SetVisible(true);
    balloonwjui::DuiControl* holder = bar->GetParent();
    REQUIRE(holder != NULL);
    holder->ForceLayout(holder->GetRect());

    const RECT rcBar = bar->GetRect();
    balloonwjui::DuiControl* input = root->FindCtrlById(inputId);
    REQUIRE(input != NULL);
    const RECT rcInput = input->GetRect();
    CHECK(rcBar.bottom > rcBar.top);
    CHECK(rcBar.bottom <= rcInput.top);
    CHECK(rcInput.bottom > rcInput.top);
    const UINT parts[] = { textId, sendId, dismissId };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
    {
        INFO("control id = " << parts[i]);
        balloonwjui::DuiControl* part = root->FindCtrlById(parts[i]);
        REQUIRE(part != NULL);
        const RECT rc = part->GetRect();
        CHECK(rc.right > rc.left);
        CHECK(rc.left >= rcBar.left);
        CHECK(rc.right <= rcBar.right);
        CHECK(rc.top >= rcBar.top);
        CHECK(rc.bottom <= rcBar.bottom);
    }
}

} // anonymous namespace

TEST_CASE("CS1 fence with a language name", "[codesend]")
{
    std::wstring code;
    int lang = -1;
    REQUIRE(ParseCodeFence(L"```python\ndef f():\n    return 1\n```", code, lang));
    CHECK(code == L"def f():\n    return 1");
    CHECK(lang == codesnippet::code_lang_python);

    //语言名的别名与大小写
    REQUIRE(ParseCodeFence(L"```JS\nconst a = 1;\n```", code, lang));
    CHECK(code == L"const a = 1;");
    CHECK(lang == codesnippet::code_lang_javascript);
}

TEST_CASE("CS2 fence without a language name detects the language", "[codesend]")
{
    const std::wstring body(kCppSample);
    std::wstring code;
    int lang = -1;
    REQUIRE(ParseCodeFence(L"```\n" + body + L"\n```", code, lang));
    CHECK(code == body);
    CHECK(lang == codesnippet::DetectCodeLanguage(body));
    CHECK(lang == codesnippet::code_lang_cpp);
}

TEST_CASE("CS3 blank lines around the fence and CRLF / CR newlines", "[codesend]")
{
    std::wstring code;
    int lang = -1;
    REQUIRE(ParseCodeFence(L"\r\n  ```sql  \r\nSELECT 1;\r\nSELECT 2;\r\n```  \r\n\r\n", code, lang));
    CHECK(code == L"SELECT 1;\nSELECT 2;");
    CHECK(lang == codesnippet::code_lang_sql);

    REQUIRE(ParseCodeFence(L"```go\rpackage main\r```", code, lang));
    CHECK(code == L"package main");
    CHECK(lang == codesnippet::code_lang_go);

    //代码自己的缩进与中间的空行保留
    REQUIRE(ParseCodeFence(L"```\n    a();\n\n    b();\n```", code, lang));
    CHECK(code == L"    a();\n\n    b();");
}

TEST_CASE("CS4 texts that are not a whole fenced block", "[codesend]")
{
    std::wstring code;
    int lang = -1;
    //没有闭合
    CHECK_FALSE(ParseCodeFence(L"```\nint a = 1;\n", code, lang));
    //只有开头一行
    CHECK_FALSE(ParseCodeFence(L"```python", code, lang));
    //中间还有 ``` 行：几段代码块混着文字
    CHECK_FALSE(ParseCodeFence(L"```\na();\n```\n说明\n```\nb();\n```", code, lang));
    //闭合之后还有文字
    CHECK_FALSE(ParseCodeFence(L"```\na();\n```\n后面还有一句", code, lang));
    //中间没有内容
    CHECK_FALSE(ParseCodeFence(L"```\n\n   \n```", code, lang));
    //开头之前有文字
    CHECK_FALSE(ParseCodeFence(L"看这段：\n```\na();\n```", code, lang));
    //普通文字
    CHECK_FALSE(ParseCodeFence(L"今天的代码评审改到下午", code, lang));
}

TEST_CASE("CS5 unknown or non-ASCII language names fall back to plain text", "[codesend]")
{
    std::wstring code;
    int lang = -1;
    REQUIRE(ParseCodeFence(L"```foolang\nx = 1\n```", code, lang));
    CHECK(code == L"x = 1");
    CHECK(lang == codesnippet::code_lang_plain);

    REQUIRE(ParseCodeFence(L"```中文\nx = 1\n```", code, lang));
    CHECK(lang == codesnippet::code_lang_plain);
}

TEST_CASE("CS6 Markdown mode keeps fenced text as Markdown", "[codesend]")
{
    const std::wstring text = L"```python\nprint(1)\n```";
    std::wstring code;
    int lang = -1;
    CHECK_FALSE(TryCodeFenceSend(text, true, code, lang));
    REQUIRE(TryCodeFenceSend(text, false, code, lang));
    CHECK(code == L"print(1)");
    CHECK(lang == codesnippet::code_lang_python);
    CHECK_FALSE(TryCodeFenceSend(L"普通的一句话", false, code, lang));
}

TEST_CASE("CS7 paste recognition only offers for code pasted into an empty input", "[codesend]")
{
    const std::wstring cpp(kCppSample);
    CHECK(ShouldOfferCodePaste(true, false, cpp));
    //RichEdit 里取出的文字换行是 CR
    CHECK(ShouldOfferCodePaste(true, false, ToCr(cpp)));
    //输入框原本有内容：粘进来的只是一部分
    CHECK_FALSE(ShouldOfferCodePaste(false, false, cpp));
    //Markdown 模式
    CHECK_FALSE(ShouldOfferCodePaste(true, true, cpp));
    //中文正文、单行短句、空白
    CHECK_FALSE(ShouldOfferCodePaste(true, false, kProseSample));
    CHECK_FALSE(ShouldOfferCodePaste(true, false, L"好的"));
    CHECK_FALSE(ShouldOfferCodePaste(true, false, L"  \r\r "));
}

TEST_CASE("CS8 reedit of a failed code snippet carries the code and the language", "[codesend]")
{
    ChatMessage code = MakeTextMessage(true, CString(kCppSample));
    code.isCode = true;
    code.codeLang = codesnippet::code_lang_cpp;
    code.sendFailed = true;
    REQUIRE(CanReeditMessage(code));
    const ChatReeditData data = BuildReeditData(code);
    CHECK(data.m_isCode);
    CHECK(data.m_codeLang == codesnippet::code_lang_cpp);
    CHECK(data.m_code == std::wstring(kCppSample));

    ChatMessage text = MakeTextMessage(true, CString(L"普通文字"));
    text.sendFailed = true;
    const ChatReeditData plain = BuildReeditData(text);
    CHECK_FALSE(plain.m_isCode);
    CHECK(plain.m_code.empty());
    CHECK_FALSE(plain.m_segments.empty());
}

TEST_CASE("CS9 paste recognition bar sits above the input", "[codesend]")
{
    std::unique_ptr<balloonwjui::DuiControl> single = BuildChatWindowBody(false);
    CheckPasteBar(single.get(), kIdCodePasteBar, kIdCodePasteText, kIdCodePasteSend, kIdCodePasteDismiss,
                  kIdInputEdit, kSingleContentW, kSingleContentH);

    std::unique_ptr<balloonwjui::DuiControl> group = BuildGroupChatWindowBody(false);
    CheckPasteBar(group.get(), kGIdCodePasteBar, kGIdCodePasteText, kGIdCodePasteSend, kGIdCodePasteDismiss,
                  kGIdInputEdit, kGroupContentW, kGroupContentH);
}

TEST_CASE("CS10 reedit of a failed annotation goes back to the annotation mode", "[codesend]")
{
    const long long kBaseMsgid = 900200;
    const long long kBaseSender = 20002;
    const long long kSelf = 10001;
    ChatMessage code = MakeTextMessage(true, CString(kCppSample));
    code.isCode = true;
    code.codeLang = codesnippet::code_lang_cpp;
    code.sendFailed = true;
    code.hasQuote = true;
    code.quoteMsgid = kBaseMsgid;
    code.quoteFrom = kBaseSender;
    code.codeNotes.push_back(codesnippet::CodeNote(1, 1, L"from the code's sender", kBaseSender));
    code.codeNotes.push_back(codesnippet::CodeNote(2, 2, L"my new note", kSelf, true));
    const ChatReeditData data = BuildReeditData(code);
    CHECK(data.m_isCode);
    CHECK(data.m_codeQuoteMsgid == kBaseMsgid);
    CHECK(data.m_codeQuoteFrom == kBaseSender);
    REQUIRE(data.m_codeNotes.size() == 2);
    CHECK(codesnippet::CountNewCodeNotes(data.m_codeNotes) == 1);

    //原消息不在列表里：没有标「新」的批注代替原消息的批注；与带回的批注合并后，新批注仍是新的，别人的保留
    const std::vector<codesnippet::CodeNote> baseline = BaselineNotesForReedit(data.m_codeNotes);
    REQUIRE(baseline.size() == 1);
    CHECK(baseline[0].m_text == L"from the code's sender");
    const std::vector<codesnippet::CodeNote> merged = codesnippet::MergeAnnotationRound(baseline, data.m_codeNotes, kSelf, 0);
    CHECK(merged.size() == 2);
    CHECK(codesnippet::CountNewCodeNotes(merged) == 1);

    //不带引用的代码片段：没有原消息
    ChatMessage plain = MakeTextMessage(true, CString(kCppSample));
    plain.isCode = true;
    plain.sendFailed = true;
    const ChatReeditData plainData = BuildReeditData(plain);
    CHECK(plainData.m_codeQuoteMsgid == 0);
    CHECK(plainData.m_codeQuoteFrom == 0);
}
