/**
 *  撤回后重新编辑的单测（docs/撤回后重新编辑方案.md §5），RecallEditTests.cpp
 *
 *  [recalledit]
 *    RE1 判定：本人发送并本人撤回的文字类给「编辑」，其余不给
 *    RE2 快照的序列化与解析往返；损坏、版本号不认识、缺片段数组时解析失败且不改动出参
 *    RE3 到期判定：5 分钟窗口的边界、撤回时间晚于当前时间、撤回时间未知
 *    RE4 本机库 t_recall_draft：写入、按 msgid 批量查询、跨会话互不影响、覆盖写入、清理过期行
 *    RE5 气泡：带快照的撤回提示行只在「编辑」两个字上命中，多选模式与到期后不命中
 *    RE6 消息列表：撤回时取出的快照与撤回前的气泡内容一致；图片不产生快照；到期处理与下一个到期时刻
 *
 *  框架 Catch2 v3。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/RecallEdit.h"
#include "../UI/BalloonChatWnd/ChatReedit.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonHistory/V2MsgStore.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"

using namespace balloonchatwnd;

namespace {

//一条本人发出、已有 msgid 的文字消息
ChatMessage MakeMyText(const wchar_t* text)
{
    ChatMessage m;
    m.kind = kMsgText;
    m.fromMe = true;
    m.msgid = 1001;
    m.text = text;
    return m;
}

//两份快照逐字段比较
bool SameDraft(const ChatReeditData& a, const ChatReeditData& b)
{
    if (a.m_segments.size() != b.m_segments.size() || a.m_mentions.size() != b.m_mentions.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.m_segments.size(); ++i)
    {
        if (a.m_segments[i].m_kind != b.m_segments[i].m_kind)
        {
            return false;
        }
        if (a.m_segments[i].m_kind == kChatClipText && a.m_segments[i].m_text != b.m_segments[i].m_text)
        {
            return false;
        }
        if (a.m_segments[i].m_kind == kChatClipFace && a.m_segments[i].m_faceId != b.m_segments[i].m_faceId)
        {
            return false;
        }
    }
    for (size_t i = 0; i < a.m_mentions.size(); ++i)
    {
        if (a.m_mentions[i].m_uid != b.m_mentions[i].m_uid
            || a.m_mentions[i].m_displayName != b.m_mentions[i].m_displayName)
        {
            return false;
        }
    }
    if (a.m_codeNotes.size() != b.m_codeNotes.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.m_codeNotes.size(); ++i)
    {
        if (a.m_codeNotes[i].m_from != b.m_codeNotes[i].m_from || a.m_codeNotes[i].m_to != b.m_codeNotes[i].m_to
            || a.m_codeNotes[i].m_text != b.m_codeNotes[i].m_text)
        {
            return false;
        }
    }
    return a.m_isMarkdown == b.m_isMarkdown
        && a.m_markdownSource == b.m_markdownSource
        && a.m_isCode == b.m_isCode
        && a.m_codeLang == b.m_codeLang
        && a.m_code == b.m_code;
}

//5 分钟（毫秒），与 kRecallEditWindowMs 相同，单独写一份以免用例与被测代码一起改错
const long long kFiveMinutesMs = 300000LL;

//一个固定的撤回时刻（Unix 毫秒），2026-10-06 前后
const long long kRecallAt = 1791230000000LL;

} // namespace

TEST_CASE("RE1 recall edit is offered only for own text-like messages recalled by self", "[recalledit]")
{
    ChatMessage text = MakeMyText(L"hello");
    CHECK(CanOfferRecallEdit(text, true));
    //群管理员撤回别人的消息、或撤回者不是本人
    CHECK_FALSE(CanOfferRecallEdit(text, false));

    ChatMessage emoji = MakeMyText(L"/f[\"5\"]");
    emoji.kind = kMsgEmoji;
    CHECK(CanOfferRecallEdit(emoji, true));

    ChatMessage md = MakeMyText(L"**bold**");
    md.isMarkdown = true;
    CHECK(CanOfferRecallEdit(md, true));

    ChatMessage code = MakeMyText(L"int main() { return 0; }");
    code.isCode = true;
    code.codeLang = 3;
    CHECK(CanOfferRecallEdit(code, true));

    //别人发的
    ChatMessage other = MakeMyText(L"hi");
    other.fromMe = false;
    CHECK_FALSE(CanOfferRecallEdit(other, true));

    //发送失败的行、还没有 msgid 的行
    ChatMessage failed = MakeMyText(L"hi");
    failed.sendFailed = true;
    CHECK_FALSE(CanOfferRecallEdit(failed, true));
    ChatMessage noId = MakeMyText(L"hi");
    noId.msgid = 0;
    CHECK_FALSE(CanOfferRecallEdit(noId, true));

    //非文字类
    const int nonText[] = { kMsgImage, kMsgFile, kMsgVoice, kMsgMerged, kMsgRelay, kMsgFileShare,
                            kMsgScreenShare, kMsgGroupInvite, kMsgTime };
    for (size_t i = 0; i < sizeof(nonText) / sizeof(nonText[0]); ++i)
    {
        ChatMessage m = MakeMyText(L"x");
        m.kind = nonText[i];
        CHECK_FALSE(CanOfferRecallEdit(m, true));
    }
}

TEST_CASE("RE2 recall draft serializes and parses back unchanged", "[recalledit]")
{
    SECTION("text with face and mention")
    {
        ChatMessage m = MakeMyText(L"@张三 看一下/f[\"12\"]这里");
        RenderSpan at;
        at.m_uid = 20001;
        at.m_off = 0;
        at.m_len = 3;
        m.ats.push_back(at);
        const ChatReeditData draft = BuildRecallDraft(m);
        REQUIRE(draft.m_segments.size() == 3);
        CHECK(draft.m_segments[1].m_kind == kChatClipFace);
        CHECK(draft.m_segments[1].m_faceId == 12);
        REQUIRE(draft.m_mentions.size() == 1);
        CHECK(draft.m_mentions[0].m_uid == 20001);

        ChatReeditData back;
        REQUIRE(ParseRecallDraft(SerializeRecallDraft(draft), back));
        CHECK(SameDraft(draft, back));
    }

    SECTION("markdown")
    {
        ChatMessage m = MakeMyText(L"# 标题\n- 一\n- 二");
        m.isMarkdown = true;
        const ChatReeditData draft = BuildRecallDraft(m);
        CHECK(draft.m_isMarkdown);
        CHECK_FALSE(draft.m_markdownSource.empty());
        ChatReeditData back;
        REQUIRE(ParseRecallDraft(SerializeRecallDraft(draft), back));
        CHECK(SameDraft(draft, back));
    }

    SECTION("code snippet keeps language, source and notes")
    {
        ChatMessage m = MakeMyText(L"for (int i = 0; i < 3; ++i)\n\tputs(\"中文\");");
        m.isCode = true;
        m.codeLang = 4;
        m.codeNotes.push_back(codesnippet::CodeNote(2, 2, L"输出一行"));
        const ChatReeditData draft = BuildRecallDraft(m);
        CHECK(draft.m_isCode);
        CHECK(draft.m_codeLang == 4);
        REQUIRE(draft.m_codeNotes.size() == 1);
        ChatReeditData back;
        REQUIRE(ParseRecallDraft(SerializeRecallDraft(draft), back));
        CHECK(SameDraft(draft, back));
        CHECK(back.m_code == std::wstring(L"for (int i = 0; i < 3; ++i)\n\tputs(\"中文\");"));
    }

    SECTION("broken input fails and leaves the output untouched")
    {
        ChatReeditData out;
        out.m_codeLang = 77;
        CHECK_FALSE(ParseRecallDraft("", out));
        CHECK_FALSE(ParseRecallDraft("{not json", out));
        CHECK_FALSE(ParseRecallDraft("[1,2,3]", out));
        CHECK_FALSE(ParseRecallDraft("{\"v\":2,\"segs\":[]}", out));
        CHECK_FALSE(ParseRecallDraft("{\"v\":1}", out));
        CHECK(out.m_codeLang == 77);
    }

    SECTION("unknown segment kinds are skipped")
    {
        ChatReeditData out;
        REQUIRE(ParseRecallDraft("{\"v\":1,\"segs\":[{\"k\":9},{\"k\":0,\"t\":\"ok\"}]}", out));
        REQUIRE(out.m_segments.size() == 1);
        CHECK(out.m_segments[0].m_text == std::wstring(L"ok"));
    }
}

TEST_CASE("RE3 recall edit expires five minutes after the recall", "[recalledit]")
{
    const long long deadline = RecallEditDeadline(kRecallAt, kRecallAt);
    CHECK(deadline == kRecallAt + kFiveMinutesMs);
    CHECK(IsRecallEditAlive(deadline, kRecallAt));
    CHECK(IsRecallEditAlive(deadline, kRecallAt + kFiveMinutesMs - 1));
    CHECK_FALSE(IsRecallEditAlive(deadline, kRecallAt + kFiveMinutesMs));
    CHECK_FALSE(IsRecallEditAlive(deadline, kRecallAt + kFiveMinutesMs + 1));

    //撤回时间晚于当前时间（本机时钟被往回调）：仍然有效，到期不晚于「当前时间加 5 分钟」
    const long long now = kRecallAt - 60000LL;
    const long long skewed = RecallEditDeadline(kRecallAt, now);
    CHECK(IsRecallEditAlive(skewed, now));
    CHECK(skewed == now + kFiveMinutesMs);

    //撤回时间未知
    CHECK(RecallEditDeadline(0, kRecallAt) == 0);
    CHECK_FALSE(IsRecallEditAlive(0, kRecallAt));

    //本机时钟取到的是合理的 Unix 毫秒（2020 年之后）
    CHECK(RecallEditNowMs() > 1577836800000LL);
}

TEST_CASE("RE4 recall drafts are stored per conversation and purged when expired", "[recalledit]")
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    const std::wstring path = std::wstring(dir) + L"flamingo_recalledit_re4.db";
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());

    {
        balloonhistory::CV2MsgStore store;
        REQUIRE(store.Open(path.c_str()));

        //单聊 cid=7 与群聊 cid=7 里各有一条 msgid=100，互不影响
        REQUIRE(store.SaveRecallDraft(1, 7, 100, kRecallAt, "{\"v\":1,\"segs\":[]}"));
        REQUIRE(store.SaveRecallDraft(2, 7, 100, kRecallAt + 1, "group"));
        REQUIRE(store.SaveRecallDraft(1, 7, 101, kRecallAt - kFiveMinutesMs - 1, "old"));
        //msgid 非正写不进去
        CHECK_FALSE(store.SaveRecallDraft(1, 7, 0, kRecallAt, "x"));

        std::vector<int64_t> ids;
        ids.push_back(100);
        ids.push_back(101);
        ids.push_back(999);
        ids.push_back(-1);
        std::vector<balloonhistory::RecallDraftRow> rows;
        REQUIRE(store.QueryRecallDrafts(1, 7, ids, rows));
        REQUIRE(rows.size() == 2);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].m_msgid == 100)
            {
                CHECK(rows[i].m_recallTimeMs == kRecallAt);
                CHECK(rows[i].m_payload == "{\"v\":1,\"segs\":[]}");
            }
        }

        std::vector<int64_t> one(1, 100);
        REQUIRE(store.QueryRecallDrafts(2, 7, one, rows));
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].m_payload == "group");

        //同一主键覆盖写入
        REQUIRE(store.SaveRecallDraft(2, 7, 100, kRecallAt + 2, "group2"));
        REQUIRE(store.QueryRecallDrafts(2, 7, one, rows));
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].m_payload == "group2");
        CHECK(rows[0].m_recallTimeMs == kRecallAt + 2);

        //清理：只删撤回时间早于给定时刻的行
        int deleted = 0;
        REQUIRE(store.PurgeRecallDrafts(kRecallAt - kFiveMinutesMs, deleted));
        CHECK(deleted == 1);
        REQUIRE(store.QueryRecallDrafts(1, 7, ids, rows));
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].m_msgid == 100);
        store.Close();
    }

    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
}

namespace {

//消息列表测试用的宽高（像素）
const int kRecallListWidth = 560;
const int kRecallListHeight = 600;

//找带指定 msgid 的那一行
ChatBubble* FindRow(ChatMessageList& list, long long msgid)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b != NULL && b->Msgid() == msgid)
        {
            return b;
        }
    }
    return NULL;
}

//在一行的垂直中线上从左到右扫，记下「编辑」命中的最左、最右横坐标；一处都没命中时 hitLeft 为 -1
void ScanRecallEdit(const ChatBubble& b, int& hitLeft, int& hitRight)
{
    hitLeft = -1;
    hitRight = -1;
    const RECT rc = b.GetRect();
    POINT pt;
    pt.y = (rc.top + rc.bottom) / 2;
    for (pt.x = rc.left; pt.x < rc.right; ++pt.x)
    {
        if (b.RecallEditAtPoint(pt))
        {
            if (hitLeft < 0)
            {
                hitLeft = pt.x;
            }
            hitRight = pt.x;
        }
    }
}

//排好版的列表：一条本人发出的文字消息（msgid 5）、一条本人发出的图片消息（msgid 6）
void FillList(ChatMessageList& list)
{
    ChatMessage text = MakeMyText(L"今天下午三点开会");
    text.msgid = 5;
    list.AppendMessage(text);
    ChatMessage image = MakeMyText(L"");
    image.kind = kMsgImage;
    image.msgid = 6;
    image.imageW = 120;
    image.imageH = 80;
    list.AppendMessage(image);
    RECT rc = { 0, 0, kRecallListWidth, kRecallListHeight };
    list.ForceLayout(rc);
}

} // namespace

TEST_CASE("RE5 recall tip row hits only on the edit link", "[recalledit]")
{
    ChatMessageList list;
    FillList(list);
    const long long now = RecallEditNowMs();
    std::string json;
    REQUIRE(list.RecallMessageByMsgid(5, CString(L"你撤回了一条消息"), true, now, &json));
    CHECK_FALSE(json.empty());
    RECT rc = { 0, 0, kRecallListWidth, kRecallListHeight };
    list.ForceLayout(rc);

    ChatBubble* row = FindRow(list, 5);
    REQUIRE(row != NULL);
    CHECK(row->GetMessage().kind == kMsgTime);
    CHECK(row->HasRecallEdit(now));

    int hitLeft = -1;
    int hitRight = -1;
    ScanRecallEdit(*row, hitLeft, hitRight);
    REQUIRE(hitLeft >= 0);
    //「编辑」在提示文字的右边：命中区整段在行中线右侧，行中线（提示文字所在处）不命中
    const RECT r = row->GetRect();
    const int center = (r.left + r.right) / 2;
    CHECK(hitLeft > center);
    POINT mid;
    mid.x = center;
    mid.y = (r.top + r.bottom) / 2;
    CHECK_FALSE(row->RecallEditAtPoint(mid));
    //行的上下方不命中
    POINT above;
    above.x = hitLeft;
    above.y = r.top - 1;
    CHECK_FALSE(row->RecallEditAtPoint(above));

    //多选模式不命中
    list.SetMultiSelectMode(true);
    ScanRecallEdit(*row, hitLeft, hitRight);
    CHECK(hitLeft < 0);
    list.SetMultiSelectMode(false);

    //到期后不命中，也不再显示
    CHECK(list.ExpireRecallEdits(now + kFiveMinutesMs) == 0);
    CHECK_FALSE(row->HasRecallEdit(now));
    ScanRecallEdit(*row, hitLeft, hitRight);
    CHECK(hitLeft < 0);
}

TEST_CASE("RE6 recall takes the draft before the text is overwritten", "[recalledit]")
{
    const long long now = RecallEditNowMs();

    SECTION("own text recalled by self")
    {
        ChatMessageList list;
        FillList(list);
        ChatBubble* row = FindRow(list, 5);
        REQUIRE(row != NULL);
        const ChatReeditData expected = BuildRecallDraft(row->GetMessage());

        std::string json;
        REQUIRE(list.RecallMessageByMsgid(5, CString(L"你撤回了一条消息"), true, now, &json));
        ChatReeditData got;
        REQUIRE(ParseRecallDraft(json, got));
        CHECK(SameDraft(expected, got));
        CHECK(row->GetMessage().recallDraftJson == json);
        CHECK(row->GetMessage().recallEditDeadlineMs == now + kFiveMinutesMs);

        //重连补推同一条撤回：不再生成快照，「编辑」保留
        std::string again;
        CHECK(list.RecallMessageByMsgid(5, CString(L"你撤回了一条消息"), true, now + 1000, &again));
        CHECK(again.empty());
        CHECK(row->HasRecallEdit(now + 1000));
        CHECK(row->GetMessage().recallEditDeadlineMs == now + kFiveMinutesMs);

        //到期处理：未到期时返回下一个到期时刻
        CHECK(list.ExpireRecallEdits(now + 1000) == now + kFiveMinutesMs);
        CHECK(row->HasRecallEdit(now + 1000));
    }

    SECTION("image recalled by self: no draft")
    {
        ChatMessageList list;
        FillList(list);
        std::string json = "stale";
        REQUIRE(list.RecallMessageByMsgid(6, CString(L"你撤回了一条消息"), true, now, &json));
        CHECK(json.empty());
        ChatBubble* row = FindRow(list, 6);
        REQUIRE(row != NULL);
        CHECK(row->GetMessage().kind == kMsgTime);
        CHECK_FALSE(row->HasRecallEdit(now));
    }

    SECTION("recalled by someone else (group admin): no draft")
    {
        ChatMessageList list;
        FillList(list);
        std::string json;
        REQUIRE(list.RecallMessageByMsgid(5, CString(L"管理员撤回了一条消息"), false, now, &json));
        CHECK(json.empty());
        CHECK_FALSE(FindRow(list, 5)->HasRecallEdit(now));
    }

    SECTION("history tip row with an attached draft")
    {
        ChatMessageList list;
        ChatMessage tip;
        tip.kind = kMsgTime;
        tip.msgid = 9;
        tip.text = L"你撤回了一条消息";
        tip.recallDraftJson = "{\"v\":1,\"segs\":[{\"k\":0,\"t\":\"hi\"}]}";
        tip.recallEditDeadlineMs = now + 60000;
        list.AppendMessage(tip);
        RECT rc = { 0, 0, kRecallListWidth, kRecallListHeight };
        list.ForceLayout(rc);
        ChatBubble* row = FindRow(list, 9);
        REQUIRE(row != NULL);
        CHECK(row->HasRecallEdit(now));
        CHECK(list.ExpireRecallEdits(now) == now + 60000);
    }
}
