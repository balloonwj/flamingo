/**
 *  代码片段批注的单测（Common/Code/CodeNotes、网络层的 content.notes、ChatCodeMessage 的转换，
 *  docs/代码片段方案.md 第 10.5 节），CodeNotesTests.cpp
 *
 *  [codenotes]
 *    CN1 整理：越界、颠倒、说明为空白的去掉；说明超过 200 字截断；按起始行排序；与前一条相交的去掉；最多 50 条
 *    CN2 重新定位：上方插入行时整体下移；删掉范围里的一行时收缩；只改缩进不变；改了被批注那一行仍保留；
 *        整段删除时删掉并计数；范围里插入行时扩大
 *    CN3 开头空白行的行数与整体平移；按行找批注
 *    CN4 协议：代码片段的请求写出 content.notes，纯文字不写；解析推送读出批注，格式不对的条目跳过；
 *        体积计入批注；本地库 content 列写入批注并能读回
 *    CN5 界面：ApplyCodeFields 按代码行数整理批注；协议与界面两种批注互转；体积检查计入批注
 *    CN6 大段代码：只改了一行时只比较改动过的中间部分，批注照常跟着走；每行都改了时按原行号保留
 *    CN7 发送时的批注：随去掉的首尾空白行平移与截断，整条落在被去掉的行里的删除
 *    CN8 作者与「新」（第 11.4 节）：作者不是发送者时写出 userid，是发送者时不写；new 写出与读出；userid 可以是字符串或整数，
 *        不是合法 id 的条目跳过；缺省作者补成消息发送者；聊天模型与本地库都带上作者与「新」
 *    CN9 不同作者的批注可以相交，同一作者相交时去掉后一条；条数上限按总数；重新定位与平移保留作者与「新」
 *    CN10 一轮追加的组装：别人的批注原样保留并清掉「新」；本人的批注可以修改、删除；新增与修改的标「新」；
 *         只删除时没有可以发送的内容
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Common/Code/CodeNotes.h"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonChatWnd/ChatCodeMessage.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace codesnippet;

namespace {

//对端 userid 与客户端消息 ID
const int64_t kPeerUid = 20002;
//本人 userid（与 MakeNotifyJson 里的 to 相同），以及另外两位批注作者
const long long kSelfUid = 10001;
const int64_t kOtherUid = 30003;
const int64_t kThirdUid = 40004;
const char* const kClientMsgId = "notes-test-0001";

//四行代码（UTF-8）
const char* const kCodeUtf8 = "int a = 1;\nint b = 2;\nint c = 3;\nint d = 4;";

//CN1：比上限多一条
const int kTooManyNotes = kCodeNotesMax + 1;

//把 JSON 文本解析成 Json::Value
bool ParseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//紧凑写出
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//拼一条服务端格式的单聊推送
std::string MakeNotifyJson(const Json::Value& content)
{
    Json::Value root;
    root["msgid"] = "900003";
    root["clientmsgid"] = kClientMsgId;
    root["ctype"] = conversation_single;
    root["cid"] = std::to_string(kPeerUid);
    root["from"] = std::to_string(kPeerUid);
    root["to"] = "10001";
    root["msgtype"] = message_type_text;
    root["content"] = content;
    root["timestamp"] = static_cast<Json::Int64>(1790000000000LL);
    return WriteCompact(root);
}

//造一条协议里的批注
net::CodeNoteData MakeData(int from, int to, const char* text)
{
    net::CodeNoteData d;
    d.m_from = from;
    d.m_to = to;
    d.m_text = text;
    return d;
}

} // anonymous namespace

TEST_CASE("CN1 sanitizing notes", "[codenotes]")
{
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(5, 6, L"后一条"));
    notes.push_back(CodeNote(0, 1, L"起始行越界"));
    notes.push_back(CodeNote(3, 2, L"起止颠倒"));
    notes.push_back(CodeNote(9, 11, L"越过末行"));
    notes.push_back(CodeNote(7, 7, L"   "));
    notes.push_back(CodeNote(1, 2, L"前一条"));
    notes.push_back(CodeNote(2, 3, L"与前一条相交"));
    notes.push_back(CodeNote(8, 8, std::wstring(300, L'字')));
    const std::vector<CodeNote> out = SanitizeCodeNotes(notes, 10);
    REQUIRE(out.size() == 3);
    CHECK(out[0].m_from == 1);
    CHECK(out[0].m_text == L"前一条");
    CHECK(out[1].m_from == 5);
    CHECK(out[2].m_from == 8);
    CHECK((int)out[2].m_text.size() == kCodeNoteTextMaxChars);

    //最多 50 条
    std::vector<CodeNote> many;
    for (int i = 1; i <= kTooManyNotes; ++i)
        many.push_back(CodeNote(i, i, L"x"));
    CHECK((int)SanitizeCodeNotes(many, 0).size() == kCodeNotesMax);
}

TEST_CASE("CN2 notes follow their lines after edits", "[codenotes]")
{
    const std::wstring before = L"a();\nb();\nc();\nd();\ne();";
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(2, 3, L"bc"));
    int removed = -1;

    //上方插入一行：整体下移
    std::vector<CodeNote> r = RemapCodeNotes(notes, before, L"x();\na();\nb();\nc();\nd();\ne();", removed);
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_from == 3);
    CHECK(r[0].m_to == 4);
    CHECK(removed == 0);

    //删掉范围里的一行：收缩
    r = RemapCodeNotes(notes, before, L"a();\nb();\nd();\ne();", removed);
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_from == 2);
    CHECK(r[0].m_to == 2);

    //只改缩进、换行符：不变
    r = RemapCodeNotes(notes, before, L"a();\r\n    b();\r\n    c();\r\nd();\r\ne();", removed);
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_from == 2);
    CHECK(r[0].m_to == 3);

    //改了被批注的那几行的内容：仍保留在原位置
    r = RemapCodeNotes(notes, before, L"a();\nB();\nC();\nd();\ne();", removed);
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_from == 2);
    CHECK(r[0].m_to == 3);
    CHECK(removed == 0);

    //整段删掉：删除并计数
    r = RemapCodeNotes(notes, before, L"a();\nd();\ne();", removed);
    CHECK(r.empty());
    CHECK(removed == 1);

    //范围里插入一行：扩大
    r = RemapCodeNotes(notes, before, L"a();\nb();\nnew();\nc();\nd();\ne();", removed);
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_from == 2);
    CHECK(r[0].m_to == 4);
}

TEST_CASE("CN3 leading blank lines, shifting and lookup", "[codenotes]")
{
    CHECK(LeadingBlankLines(L"\n  \r\n\tint a;") == 2);
    CHECK(LeadingBlankLines(L"int a;\n\n") == 0);
    CHECK(LeadingBlankLines(L"   ") == 0);

    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(1, 2, L"开头"));
    notes.push_back(CodeNote(4, 5, L"中间"));
    const std::vector<CodeNote> up = ShiftCodeNotes(notes, -2);
    REQUIRE(up.size() == 1);
    CHECK(up[0].m_from == 2);
    CHECK(up[0].m_to == 3);
    const std::vector<CodeNote> partly = ShiftCodeNotes(notes, -1);
    REQUIRE(partly.size() == 2);
    CHECK(partly[0].m_from == 1);
    CHECK(partly[0].m_to == 1);

    CHECK(FindCodeNoteAtLine(notes, 2) == 0);
    CHECK(FindCodeNoteAtLine(notes, 3) == -1);
    CHECK(FindCodeNoteAtLine(notes, 5) == 1);
}

TEST_CASE("CN4 notes on the wire and in the local database", "[codenotes]")
{
    std::vector<net::CodeNoteData> data;
    data.push_back(MakeData(1, 2, "\xE6\x89\xB9\xE6\xB3\xA8"));   //「批注」
    data.push_back(MakeData(4, 4, "last"));

    //代码片段的请求写出 notes；纯文字即使传了也不写
    Json::Value req;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, kCodeUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_code, "cpp", data), req));
    const Json::Value& notes = req["content"]["notes"];
    REQUIRE(notes.isArray());
    REQUIRE(notes.size() == 2);
    CHECK(notes[0]["from"].asInt() == 1);
    CHECK(notes[0]["to"].asInt() == 2);
    CHECK(notes[0]["text"].asString() == "\xE6\x89\xB9\xE6\xB3\xA8");
    Json::Value plain;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, "hi", net::TextFont(), net::ReplyQuote(), net::text_format_plain, "", data), plain));
    CHECK_FALSE(plain["content"].isMember("notes"));

    //体积计入批注
    const size_t without = net::MessageProtocolClient::textContentBytes(
        kCodeUtf8, net::TextFont(), net::ReplyQuote(), std::vector<balloonchatwnd::MentionSpan>(), false,
        net::text_format_code, "cpp");
    const size_t with = net::MessageProtocolClient::textContentBytes(
        kCodeUtf8, net::TextFont(), net::ReplyQuote(), std::vector<balloonchatwnd::MentionSpan>(), false,
        net::text_format_code, "cpp", data);
    CHECK(with > without);

    //解析推送：格式不对的条目跳过
    Json::Value content;
    content["text"] = kCodeUtf8;
    content["format"] = "code";
    content["lang"] = "cpp";
    Json::Value arr(Json::arrayValue);
    Json::Value good;
    good["from"] = 2;
    good["to"] = 3;
    good["text"] = "ok";
    arr.append(good);
    arr.append(Json::Value("not an object"));
    Json::Value badFrom;
    badFrom["from"] = "2";
    badFrom["to"] = 3;
    badFrom["text"] = "from is a string";
    arr.append(badFrom);
    Json::Value reversed;
    reversed["from"] = 3;
    reversed["to"] = 2;
    reversed["text"] = "reversed";
    arr.append(reversed);
    content["notes"] = arr;
    net::MessageNotify n;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(content), n));
    REQUIRE(n.m_codeNotes.size() == 1);
    CHECK(n.m_codeNotes[0].m_from == 2);
    CHECK(n.m_codeNotes[0].m_text == "ok");

    //本地库 content 列写入批注，读回时按代码行数整理
    n.m_codeNotes = data;
    Json::Value stored;
    REQUIRE(ParseJsonText(balloonhistory::BuildContentJson(n), stored));
    REQUIRE(stored["notes"].isArray());
    CHECK(stored["notes"].size() == 2);
    int lang = -1;
    std::vector<CodeNote> back;
    REQUIRE(balloonchatwnd::CodeLangFromContentJson(message_type_text, balloonhistory::BuildContentJson(n), lang, &back));
    REQUIRE(back.size() == 2);
    CHECK(back[0].m_text == L"批注");
    CHECK(back[1].m_from == 4);
}

TEST_CASE("CN5 notes in the chat model", "[codenotes]")
{
    std::vector<net::CodeNoteData> data;
    data.push_back(MakeData(1, 1, "first"));
    data.push_back(MakeData(4, 9, "beyond the last line"));

    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeTextMessage(false, CString(L"int a = 1;\nint b = 2;\nint c = 3;\nint d = 4;"));
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_code, "cpp", data);
    REQUIRE(msg.isCode);
    REQUIRE(msg.codeNotes.size() == 1);
    CHECK(msg.codeNotes[0].m_text == L"first");

    //不是代码片段：不带批注
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_plain, "", data);
    CHECK(msg.codeNotes.empty());

    //两种批注互转
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(2, 3, L"中文说明"));
    const std::vector<net::CodeNoteData> wire = balloonchatwnd::CodeNotesToData(notes);
    REQUIRE(wire.size() == 1);
    const std::vector<CodeNote> round = balloonchatwnd::CodeNotesFromData(wire, 4);
    REQUIRE(round.size() == 1);
    CHECK(round[0].m_from == 2);
    CHECK(round[0].m_to == 3);
    CHECK(round[0].m_text == L"中文说明");
    CHECK(balloonchatwnd::CodeLineCount(L"\n\n  a\nb\n\n") == 2);

    //体积检查计入批注：同一段代码，带了很长的批注后超限
    const std::string code(1024, 'x');
    std::vector<net::CodeNoteData> heavy;
    const std::string longText(static_cast<size_t>(MESSAGE_CONTENT_MAX_BYTES), 'n');
    heavy.push_back(MakeData(1, 1, longText.c_str()));
    CHECK_FALSE(balloonchatwnd::IsCodeTooLarge(code, "cpp", net::ReplyQuote()));
    CHECK(balloonchatwnd::IsCodeTooLarge(code, "cpp", net::ReplyQuote(), heavy));
}

TEST_CASE("CN6 large texts compare only the changed middle part", "[codenotes]")
{
    //3000 行互不相同的代码：新旧行数之积远超比较的上限，但只改动了一行，开头与末尾相同的行直接对应
    const int kLines = 3000;
    std::wstring before;
    std::wstring inserted;
    std::wstring rewritten;
    for (int i = 1; i <= kLines; ++i)
    {
        const std::wstring line = L"call" + std::to_wstring(static_cast<long long>(i)) + L"();";
        before += line + L"\n";
        if (i == 10)
            inserted += L"added();\n";
        inserted += line + L"\n";
        rewritten += L"other" + std::to_wstring(static_cast<long long>(i)) + L"();\n";
    }
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(1500, 1502, L"middle"));
    int removed = -1;

    //第 10 行之前插入一行：批注下移一行
    std::vector<CodeNote> r = RemapCodeNotes(notes, before, inserted, removed);
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_from == 1501);
    CHECK(r[0].m_to == 1503);
    CHECK(removed == 0);

    //每一行都改了：中间部分仍超过上限，不做比较，按原行号保留
    r = RemapCodeNotes(notes, before, rewritten, removed);
    REQUIRE(r.size() == 1);
    CHECK(r[0].m_from == 1500);
    CHECK(r[0].m_to == 1502);
    CHECK(removed == 0);
}

TEST_CASE("CN7 notes follow the code that is actually sent", "[codenotes]")
{
    //编辑框里开头两行、末尾一行只含空白；发送的代码是中间三行 a / b / c
    const std::wstring raw = L"\r  \ra();\rb();\rc();\r\t";
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(3, 3, L"on a"));     //a 那一行
    notes.push_back(CodeNote(5, 6, L"c+tail"));   //c 与末尾的空白行：截到 c
    notes.push_back(CodeNote(6, 6, L"tail"));     //整条在末尾的空白行里：删除
    const std::vector<CodeNote> sent = CodeNotesForSending(notes, raw);
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].m_from == 1);
    CHECK(sent[0].m_to == 1);
    CHECK(sent[1].m_from == 3);
    CHECK(sent[1].m_to == 3);
    CHECK(sent[1].m_text == L"c+tail");

    //开头空白行上的批注随之截掉；整段只含空白时没有批注
    std::vector<CodeNote> head;
    head.push_back(CodeNote(1, 3, L"head"));
    const std::vector<CodeNote> headSent = CodeNotesForSending(head, raw);
    REQUIRE(headSent.size() == 1);
    CHECK(headSent[0].m_from == 1);
    CHECK(headSent[0].m_to == 1);
    CHECK(CodeNotesForSending(head, L" \r\t\r").empty());
}

TEST_CASE("CN8 note authors and the new mark on the wire", "[codenotes]")
{
    //作者是发送者（0 或等于发送者）时不写 userid；不是发送者时写成字符串；「新」写成 1，不是新的不写
    std::vector<net::CodeNoteData> data;
    data.push_back(MakeData(1, 1, "sender's"));
    net::CodeNoteData other = MakeData(2, 3, "other's");
    other.m_userId = kOtherUid;
    other.m_isNew = true;
    data.push_back(other);
    Json::Value req;
    REQUIRE(ParseJsonText(net::MessageProtocolClient::buildSendTextRequest(
        kPeerUid, kClientMsgId, kCodeUtf8, net::TextFont(), net::ReplyQuote(), net::text_format_code, "cpp", data), req));
    const Json::Value& notes = req["content"]["notes"];
    REQUIRE(notes.size() == 2);
    CHECK_FALSE(notes[0].isMember("userid"));
    CHECK_FALSE(notes[0].isMember("new"));
    CHECK(notes[1]["userid"].asString() == std::to_string(kOtherUid));
    CHECK(notes[1]["new"].asInt() == 1);

    //解析：userid 可以是字符串或整数，缺省为 0；不是合法 id 的条目跳过；new 认 1 与 true
    Json::Value content;
    content["text"] = kCodeUtf8;
    content["format"] = "code";
    Json::Value arr(Json::arrayValue);
    Json::Value plain;
    plain["from"] = 1;
    plain["to"] = 1;
    plain["text"] = "no author";
    arr.append(plain);
    Json::Value byString;
    byString["from"] = 1;
    byString["to"] = 2;
    byString["text"] = "string id";
    byString["userid"] = std::to_string(kOtherUid);
    byString["new"] = 1;
    arr.append(byString);
    Json::Value byNumber;
    byNumber["from"] = 3;
    byNumber["to"] = 3;
    byNumber["text"] = "number id";
    byNumber["userid"] = static_cast<Json::Int64>(kThirdUid);
    byNumber["new"] = true;
    arr.append(byNumber);
    Json::Value badId;
    badId["from"] = 4;
    badId["to"] = 4;
    badId["text"] = "bad id";
    badId["userid"] = "abc";
    arr.append(badId);
    content["notes"] = arr;
    net::MessageNotify n;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(MakeNotifyJson(content), n));
    REQUIRE(n.m_codeNotes.size() == 3);
    CHECK(n.m_codeNotes[0].m_userId == 0);
    CHECK_FALSE(n.m_codeNotes[0].m_isNew);
    CHECK(n.m_codeNotes[1].m_userId == kOtherUid);
    CHECK(n.m_codeNotes[1].m_isNew);
    CHECK(n.m_codeNotes[2].m_userId == kThirdUid);
    CHECK(n.m_codeNotes[2].m_isNew);

    //换成界面用的批注：缺省作者补成消息发送者；不同作者的批注可以相交，都保留
    const std::vector<CodeNote> ui = balloonchatwnd::CodeNotesFromData(n.m_codeNotes, 4, kPeerUid);
    REQUIRE(ui.size() == 3);
    CHECK(ui[0].m_authorId == kPeerUid);
    CHECK(ui[0].m_text == L"no author");
    CHECK(ui[1].m_authorId == kOtherUid);
    CHECK(ui[1].m_isNew);
    CHECK(ui[2].m_authorId == kThirdUid);

    //换回协议：作者等于发送者的不写 userid
    const std::vector<net::CodeNoteData> back = balloonchatwnd::CodeNotesToData(ui, kPeerUid);
    REQUIRE(back.size() == 3);
    CHECK(back[0].m_userId == 0);
    CHECK(back[1].m_userId == kOtherUid);
    CHECK(back[1].m_isNew);
    //发送者换成别人时，原来缺省的作者要写出来
    const std::vector<net::CodeNoteData> resent = balloonchatwnd::CodeNotesToData(ui, kOtherUid);
    CHECK(resent[0].m_userId == kPeerUid);
    CHECK(resent[1].m_userId == 0);

    //聊天模型与本地库：作者与「新」都带上
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeTextMessage(false, CString(L"int a = 1;\nint b = 2;\nint c = 3;\nint d = 4;"));
    balloonchatwnd::ApplyCodeFields(msg, net::text_format_code, "cpp", n.m_codeNotes, kPeerUid);
    REQUIRE(msg.codeNotes.size() == 3);
    CHECK(msg.codeNotes[0].m_authorId == kPeerUid);
    CHECK(msg.codeNotes[1].m_isNew);
    int lang = -1;
    std::vector<CodeNote> stored;
    REQUIRE(balloonchatwnd::CodeLangFromContentJson(message_type_text, balloonhistory::BuildContentJson(n), lang,
                                                    &stored, kPeerUid));
    REQUIRE(stored.size() == 3);
    CHECK(stored[0].m_authorId == kPeerUid);
    CHECK(stored[2].m_authorId == kThirdUid);
    CHECK(stored[2].m_isNew);
}

TEST_CASE("CN9 notes by different authors may overlap", "[codenotes]")
{
    std::vector<CodeNote> notes;
    notes.push_back(CodeNote(1, 3, L"A first", kPeerUid));
    notes.push_back(CodeNote(2, 2, L"B on the same lines", kOtherUid));
    notes.push_back(CodeNote(3, 4, L"A again, overlapping A", kPeerUid));
    notes.push_back(CodeNote(2, 5, L"B again, overlapping B", kOtherUid));
    notes.push_back(CodeNote(4, 4, L"B after its first note", kOtherUid));
    const std::vector<CodeNote> out = SanitizeCodeNotes(notes, 10);
    REQUIRE(out.size() == 3);
    CHECK(out[0].m_text == L"A first");
    CHECK(out[1].m_text == L"B on the same lines");
    CHECK(out[2].m_text == L"B after its first note");

    //条数上限按总数算，不按作者
    std::vector<CodeNote> many;
    for (int i = 0; i < kTooManyNotes; ++i)
        many.push_back(CodeNote(1, 1, L"x", kPeerUid + i));
    CHECK((int)SanitizeCodeNotes(many, 0).size() == kCodeNotesMax);

    //重新定位与平移保留作者与「新」
    std::vector<CodeNote> one;
    one.push_back(CodeNote(2, 2, L"b", kOtherUid, true));
    int removed = -1;
    const std::vector<CodeNote> moved = RemapCodeNotes(one, L"a\nb\nc", L"x\na\nb\nc", removed);
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].m_from == 3);
    CHECK(moved[0].m_authorId == kOtherUid);
    CHECK(moved[0].m_isNew);
    const std::vector<CodeNote> shifted = ShiftCodeNotes(one, 1);
    REQUIRE(shifted.size() == 1);
    CHECK(shifted[0].m_authorId == kOtherUid);
}

TEST_CASE("CN10 merging one round of added notes", "[codenotes]")
{
    //原消息上的批注：A 的、本人的两条、B 的（B 在上一轮新加的，带着「新」）
    std::vector<CodeNote> original;
    original.push_back(CodeNote(1, 1, L"a1", kPeerUid));
    original.push_back(CodeNote(2, 2, L"s1", kSelfUid));
    original.push_back(CodeNote(3, 3, L"b1", kOtherUid, true));
    original.push_back(CodeNote(4, 4, L"s2", kSelfUid));

    //只有本人的批注能修改、删除；作者为 0 的是本机正在写的，也算本人的
    CHECK_FALSE(CanEditCodeNote(original[0], kSelfUid));
    CHECK(CanEditCodeNote(original[1], kSelfUid));
    CHECK(CanEditCodeNote(CodeNote(5, 5, L"draft"), kSelfUid));

    //什么都没改：别人的批注原样保留，上一轮的「新」清掉，没有可以发送的内容
    std::vector<CodeNote> merged = MergeAnnotationRound(original, original, kSelfUid, 10);
    REQUIRE(merged.size() == 4);
    CHECK(CountNewCodeNotes(merged) == 0);

    //编辑后：试图改 A 的说明、删掉 B 的（都不生效），本人的 s1 不变、s2 改了说明，又在 B 那一行新加一条（作者为 0）
    std::vector<CodeNote> edited;
    edited.push_back(CodeNote(1, 1, L"a1 changed by me", kPeerUid));
    edited.push_back(CodeNote(2, 2, L"s1", kSelfUid));
    edited.push_back(CodeNote(3, 3, L"mine on B's line"));
    edited.push_back(CodeNote(4, 4, L"s2 changed", kSelfUid));
    merged = MergeAnnotationRound(original, edited, kSelfUid, 10);
    REQUIRE(merged.size() == 5);
    CHECK(merged[0].m_text == L"a1");
    CHECK_FALSE(merged[0].m_isNew);
    CHECK(merged[1].m_text == L"s1");
    CHECK_FALSE(merged[1].m_isNew);
    //第 3 行：B 的与本人新加的都在
    int onLine3 = 0;
    for (size_t i = 0; i < merged.size(); ++i)
    {
        if (merged[i].m_from != 3)
            continue;
        ++onLine3;
        if (merged[i].m_authorId == kOtherUid)
        {
            CHECK(merged[i].m_text == L"b1");
            CHECK_FALSE(merged[i].m_isNew);
        }
        else
        {
            CHECK(merged[i].m_authorId == kSelfUid);
            CHECK(merged[i].m_isNew);
        }
    }
    CHECK(onLine3 == 2);
    CHECK(merged[4].m_text == L"s2 changed");
    CHECK(merged[4].m_isNew);
    CHECK(CountNewCodeNotes(merged) == 2);

    //只改了本人批注的行范围也算修改
    std::vector<CodeNote> moved = original;
    moved[1].m_to = 3;
    CHECK(CountNewCodeNotes(MergeAnnotationRound(original, moved, kSelfUid, 10)) == 1);

    //只删掉了本人的一条：删除生效，但没有新增或修改，不能发送
    std::vector<CodeNote> deleted;
    deleted.push_back(original[0]);
    deleted.push_back(original[1]);
    deleted.push_back(original[2]);
    merged = MergeAnnotationRound(original, deleted, kSelfUid, 10);
    CHECK(merged.size() == 3);
    CHECK(CountNewCodeNotes(merged) == 0);
}
