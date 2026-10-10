/**
 *  代码片段在其它显示面上的单测（docs/代码片段方案.md §3.4、§5.6），CodeDigestTests.cpp
 *
 *  [codedigest]
 *    CD1 摘要：「[代码] 」加第一行非空内容，首尾空白去掉；过长时截断；代码全是空白时只有「[代码]」；UTF-8 版本结果相同
 *    CD2 DigestForNotify（会话列表、字幕、合并卡片预览共用）：代码片段写成「[代码] 第一行」，普通文字不受影响
 *    CD3 引用块摘要：代码片段写成「[代码] 第一行」
 *    CD4 合并卡片的预览行：代码片段子消息写成「发送者：[代码] 第一行」
 *    CD5 从本地库的 content JSON 读代码片段字段：format 为 code 的文字消息才算，lang 缺省或不认识时按纯文本
 *  [codedigest][codehistory]
 *    CD6 聊天记录查看窗的行数据：代码片段子消息标为代码卡片并带上语言，普通文字不标
 *    CD7 消息记录窗的列表：代码片段行里建出代码卡片，卡片落在行内；超过折叠行数的两段代码卡片一样高（不在列表里展开）
 *    CD8 追加批注的摘要（第 11.3 节）：有「新」批注时写成「[批注] 」加按行号排在最前的新批注的说明（换行与 Tab 换成空格、
 *        去掉首尾空白、过长截断）；没有时仍为「[代码] 第一行」；UTF-8 版本、DigestForNotify、引用块摘要同一口径
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

#include "../Common/Code/CodeLexer.h"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../UI/BalloonChatWnd/ChatCodeMessage.h"
#include "../UI/BalloonChatWnd/ChatCodeCardControl.h"
#include "../UI/BalloonChatWnd/ChatMarkdown.h"
#include "../UI/BalloonChatWnd/ChatMergedCard.h"
#include "../UI/BalloonChatWnd/ChatQuoteModel.h"
#include "../UI/BalloonHistory/HistoryWindowLayout.h"
#include "../UI/BalloonHistory/MergedRecordRows.h"

using namespace balloonchatwnd;

namespace {

//摘要的前缀（测试进程不加载语言包，TR 返回原文）
const std::wstring kCodePrefix = L"[代码]";
const std::wstring kNotePrefix = L"[批注]";

//测试列表的宽度（像素）
const int kTestListWidth = 640;

//CD7：两段代码的行数，都超过卡片的折叠行数
const int kLongCodeLines = 40;
const int kLongerCodeLines = 80;

//一份含代码片段、普通文字两条子消息的合并记录 content
const char* const kMergedWithCode =
    "{\"title\":\"代码评审\",\"messages\":["
    "{\"msgid\":\"21\",\"from\":\"100001\",\"msgtype\":1,\"timestamp\":1700000000000,"
    "\"content\":{\"text\":\"\\n  int main()\\n{\\n    return 0;\\n}\",\"format\":\"code\",\"lang\":\"cpp\"},"
    "\"fromname\":\"张三\"},"
    "{\"msgid\":\"22\",\"from\":\"100002\",\"msgtype\":1,\"timestamp\":1700000060000,"
    "\"content\":{\"text\":\"看一下\"},\"fromname\":\"李四\"}"
    "]}";

//由行数造一段代码：每行一条赋值语句
std::wstring MakeCode(int lines)
{
    std::wstring code;
    for (int i = 0; i < lines; ++i)
    {
        if (i > 0)
            code += L"\n";
        code += L"x = x + 1;";
    }
    return code;
}

//造一条代码片段行（消息记录窗的行数据）
balloonhistory::HistoryRowView MakeCodeRow(const std::wstring& code, long long msgid)
{
    balloonhistory::HistoryRowView row;
    row.m_senderName = _T("张三");
    row.m_timeText = _T("10:00");
    row.m_previewText = CString(code.c_str());
    row.m_fullText = CString(code.c_str());
    row.m_msgid = msgid;
    row.m_isCode = true;
    row.m_codeLang = codesnippet::code_lang_cpp;
    return row;
}

//按编号找代码卡片
ChatCodeCardControl* FindCodeCard(balloonwjui::DuiControl* root, int rowIndex)
{
    return dynamic_cast<ChatCodeCardControl*>(root->FindCtrlById((UINT)(balloonhistory::kIdHistoryCodeCardBase + rowIndex)));
}

} // anonymous namespace

TEST_CASE("CD1 code digest is the first non-empty line", "[codedigest]")
{
    CHECK(CodeDigestFromCode(L"\n\n   int main()  \n{\n}") == kCodePrefix + L" int main()");
    CHECK(CodeDigestFromCode(L"  \n\t\n") == kCodePrefix);
    CHECK(CodeDigestFromCode(L"") == kCodePrefix);

    //过长时截断并以省略号结尾
    const std::wstring longLine(200, L'a');
    const std::wstring digest = CodeDigestFromCode(longLine);
    CHECK(digest.size() < longLine.size());
    CHECK(digest.compare(0, kCodePrefix.size(), kCodePrefix) == 0);
    CHECK(digest[digest.size() - 1] == L'\x2026');

    //UTF-8 版本与宽字符版本结果相同
    const std::string utf8 = CodeDigestFromUtf8("def f():\n    pass");
    CHECK(utf8 == std::string("[\xE4\xBB\xA3\xE7\xA0\x81] def f():"));
}

TEST_CASE("CD2 DigestForNotify writes code snippets as [code] first line", "[codedigest]")
{
    net::MessageNotify code;
    code.m_msgtype = ::message_type_text;
    code.m_text = "\n#include <stdio.h>\nint main() {}";
    code.m_textFormat = net::text_format_code;
    code.m_codeLang = "cpp";
    CHECK(DigestForNotify(code) == std::string("[\xE4\xBB\xA3\xE7\xA0\x81] #include <stdio.h>"));

    //普通文字照旧取正文
    net::MessageNotify plain;
    plain.m_msgtype = ::message_type_text;
    plain.m_text = "#include <stdio.h>";
    CHECK(DigestForNotify(plain) == net::MessageProtocolClient::digestForNotify(plain));
}

TEST_CASE("CD3 quote digest of a code snippet", "[codedigest]")
{
    ChatMessage msg = MakeTextMessage(false, CString(L"SELECT *\nFROM t_user;"));
    msg.isCode = true;
    msg.codeLang = codesnippet::code_lang_sql;
    const CString digest = BuildQuoteDigest(msg);
    CHECK(std::wstring((LPCWSTR)digest) == kCodePrefix + L" SELECT *");
}

TEST_CASE("CD4 merged card preview line of a code snippet", "[codedigest]")
{
    MergedCardPreview preview;
    REQUIRE(BuildMergedCardPreview(kMergedWithCode, preview));
    REQUIRE(preview.m_lines.size() == 2);
    CHECK(std::wstring((LPCWSTR)preview.m_lines[0]) == L"张三：" + kCodePrefix + L" int main()");
    CHECK(std::wstring((LPCWSTR)preview.m_lines[1]) == L"李四：看一下");
}

TEST_CASE("CD5 code fields from the local content JSON", "[codedigest]")
{
    int lang = -1;
    REQUIRE(CodeLangFromContentJson(::message_type_text, "{\"text\":\"x\",\"format\":\"code\",\"lang\":\"python\"}", lang));
    CHECK(lang == codesnippet::code_lang_python);

    //lang 缺省或不认识：按纯文本
    REQUIRE(CodeLangFromContentJson(::message_type_text, "{\"text\":\"x\",\"format\":\"code\"}", lang));
    CHECK(lang == codesnippet::code_lang_plain);
    REQUIRE(CodeLangFromContentJson(::message_type_text, "{\"text\":\"x\",\"format\":\"code\",\"lang\":\"cobol\"}", lang));
    CHECK(lang == codesnippet::code_lang_plain);

    //不是代码片段
    CHECK_FALSE(CodeLangFromContentJson(::message_type_text, "{\"text\":\"x\"}", lang));
    CHECK_FALSE(CodeLangFromContentJson(::message_type_text, "{\"text\":\"x\",\"format\":\"markdown\"}", lang));
    CHECK_FALSE(CodeLangFromContentJson(::message_type_image, "{\"text\":\"x\",\"format\":\"code\"}", lang));
    CHECK_FALSE(CodeLangFromContentJson(::message_type_text, "not json", lang));
    CHECK_FALSE(CodeLangFromContentJson(::message_type_text, "", lang));
}

TEST_CASE("CD6 merged record rows mark code snippets", "[codedigest][codehistory]")
{
    net::MergedRecord record;
    REQUIRE(net::MessageProtocolClient::parseMergedContent(kMergedWithCode, record));
    std::vector<balloonhistory::HistoryRowView> rows;
    balloonhistory::BuildMergedRecordRows(record, std::map<int64_t, CString>(), 0, rows);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_isCode);
    CHECK(rows[0].m_codeLang == codesnippet::code_lang_cpp);
    CHECK(std::wstring((LPCWSTR)rows[0].m_fullText).find(L"return 0;") != std::wstring::npos);
    CHECK_FALSE(rows[1].m_isCode);
}

TEST_CASE("CD7 history list builds a code card inside the row", "[codedigest][codehistory]")
{
    using namespace balloonhistory;
    std::vector<HistoryRowView> rows;
    rows.push_back(MakeCodeRow(L"int a = 1;\nint b = 2;", 9101));
    rows.push_back(MakeCodeRow(MakeCode(kLongCodeLines), 9102));
    rows.push_back(MakeCodeRow(MakeCode(kLongerCodeLines), 9103));

    std::unique_ptr<balloonwjui::DuiControl> content = BuildListContent(rows, _T(""), _T(""), kTestListWidth);
    REQUIRE(content != nullptr);
    content->ForceLayout(RECT{ 0, 0, kTestListWidth, 4000 });

    for (int i = 0; i < (int)rows.size(); ++i)
    {
        INFO("row " << i);
        ChatCodeCardControl* card = FindCodeCard(content.get(), i);
        REQUIRE(card != NULL);
        balloonwjui::DuiControl* row = content->FindCtrlById((UINT)(kIdHistoryRowBase + i));
        REQUIRE(row != NULL);
        const RECT rcCard = card->GetRect();
        const RECT rcRow = row->GetRect();
        CHECK(rcCard.right > rcCard.left);
        CHECK(rcCard.bottom > rcCard.top);
        //行高按卡片高度算出：卡片完整落在行内
        CHECK(rcCard.top >= rcRow.top);
        CHECK(rcCard.bottom <= rcRow.bottom);
        CHECK(rcCard.right <= rcRow.right);
    }

    //两段都超过折叠行数：列表里只显示前面几行，卡片一样高；短代码的卡片更矮
    const RECT rcShort = FindCodeCard(content.get(), 0)->GetRect();
    const RECT rcLong = FindCodeCard(content.get(), 1)->GetRect();
    const RECT rcLonger = FindCodeCard(content.get(), 2)->GetRect();
    CHECK(rcLong.bottom - rcLong.top == rcLonger.bottom - rcLonger.top);
    CHECK(rcShort.bottom - rcShort.top < rcLong.bottom - rcLong.top);
}

TEST_CASE("CD8 digest of a code snippet with new notes", "[codedigest]")
{
    std::vector<codesnippet::CodeNote> notes;
    notes.push_back(codesnippet::CodeNote(5, 5, L"later new note", 100, true));
    notes.push_back(codesnippet::CodeNote(1, 1, L"old note", 100));
    notes.push_back(codesnippet::CodeNote(2, 3, L"  第一条\n新批注\t说明  ", 200, true));
    CHECK(CodeDigestFromCode(L"int main()", notes) == kNotePrefix + L" 第一条 新批注 说明");

    //没有「新」批注：仍按代码写
    const std::vector<codesnippet::CodeNote> oldOnly(1, codesnippet::CodeNote(1, 1, L"old note", 100));
    CHECK(CodeDigestFromCode(L"int main()", oldOnly) == kCodePrefix + L" int main()");

    //说明过长时截断并以省略号结尾
    const std::wstring longText(200, L'b');
    const std::vector<codesnippet::CodeNote> longNote(1, codesnippet::CodeNote(1, 1, longText, 100, true));
    const std::wstring digest = CodeDigestFromCode(L"x", longNote);
    CHECK(digest.compare(0, kNotePrefix.size(), kNotePrefix) == 0);
    CHECK(digest.size() < longText.size());
    CHECK(digest[digest.size() - 1] == L'\x2026');

    //UTF-8 版本与会话列表的 DigestForNotify：协议里的批注
    std::vector<net::CodeNoteData> data;
    net::CodeNoteData fresh;
    fresh.m_from = 2;
    fresh.m_to = 2;
    fresh.m_text = "check null";
    fresh.m_userId = 200;
    fresh.m_isNew = true;
    data.push_back(fresh);
    const std::string expected = "[\xE6\x89\xB9\xE6\xB3\xA8] check null";
    CHECK(CodeDigestFromUtf8("int main()", data) == expected);
    net::MessageNotify n;
    n.m_msgtype = ::message_type_text;
    n.m_text = "int main()";
    n.m_textFormat = net::text_format_code;
    n.m_codeNotes = data;
    CHECK(DigestForNotify(n) == expected);

    //引用块摘要
    ChatMessage msg = MakeTextMessage(false, CString(L"int main()"));
    msg.isCode = true;
    msg.codeNotes = notes;
    CHECK(std::wstring((LPCWSTR)BuildQuoteDigest(msg)) == kNotePrefix + L" 第一条 新批注 说明");
}
