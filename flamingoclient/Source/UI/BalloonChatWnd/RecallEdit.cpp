/**
 *  RecallEdit.cpp
 *
 *  撤回后重新编辑的纯逻辑，说明见 RecallEdit.h 与 docs/撤回后重新编辑方案.md。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "RecallEdit.h"

#include "../../third-party/jsoncpp-1.9.0/json.h"
#include "../../Common/Util/EncodeUtil.h"   // EncodeUtil::UnicodeToUtf8 / Utf8ToUnicode
#include "../../Common/Code/CodeNotes.h"    // codesnippet::CodeNote / SanitizeCodeNotes：代码片段的批注
#include "../../Session/UserSessionData.h"  // FMG_MSG_QUERY_RECALL_DRAFTS

namespace balloonchatwnd {

namespace {

// 到期定时器在到期时刻之后再多等的时间（毫秒），保证触发时那一行确实已经到期
const long long kRecallEditTimerSlackMs = 50;

// 到期定时器两次触发之间的最短间隔（毫秒）
const long long kRecallEditTimerMinMs = 200;

// 1601-01-01 到 1970-01-01 之间的 100 纳秒数，FILETIME 换算 Unix 时间用
const unsigned long long kFileTimeUnixEpochDiff = 116444736000000000ULL;

// FILETIME 的单位是 100 纳秒，换算成毫秒要除以的倍数
const unsigned long long kFileTimeTicksPerMs = 10000ULL;

// 快照 JSON 的字段名。写与读共用一份，避免两边拼写不一致
const char* const kKeyVersion  = "v";       // 格式版本号
const char* const kKeySegments = "segs";    // 文字 / 表情片段数组
const char* const kKeySegKind  = "k";       // 片段类型，取值同 ChatClipKind
const char* const kKeySegText  = "t";       // 文字片段的原文
const char* const kKeySegFace  = "f";       // 表情片段的表情编号
const char* const kKeyMentions = "ats";     // @ 候选数组
const char* const kKeyAtUid    = "uid";     // 被 @ 的 userid
const char* const kKeyAtName   = "name";    // 被 @ 的显示名
const char* const kKeyMarkdown = "md";      // 是否 Markdown
const char* const kKeyMdSource = "mdsrc";   // Markdown 源文本
const char* const kKeyCode     = "code";    // 是否代码片段
const char* const kKeyCodeLang = "lang";    // 代码片段的语言
const char* const kKeyCodeSrc  = "src";     // 代码片段的原文
const char* const kKeyNotes    = "notes";   // 代码片段的批注数组（docs/代码片段方案.md 第 10.5 节）
const char* const kKeyNoteFrom = "from";    // 批注的起始行，从 1 起
const char* const kKeyNoteTo   = "to";      // 批注的结束行，从 1 起，包含这一行
const char* const kKeyNoteText = "text";    // 批注的说明文字

// 代码原文有多少行（按 \n 分隔），整理批注时用来判断行号是否越界
int CountCodeLines(const std::wstring& code)
{
    int lines = 1;
    for (size_t i = 0; i < code.size(); ++i)
    {
        if (code[i] == L'\n')
        {
            ++lines;
        }
    }
    return lines;
}

// 读一个整数字段：缺失或类型不对时返回缺省值
long long ReadInt64(const Json::Value& v, long long fallback)
{
    if (v.isInt64())
    {
        return (long long)v.asInt64();
    }
    if (v.isUInt64())
    {
        return (long long)v.asUInt64();
    }
    return fallback;
}

// 读一个字符串字段：缺失或类型不对时返回空串
std::string ReadString(const Json::Value& v)
{
    return v.isString() ? v.asString() : std::string();
}

} // namespace

bool CanOfferRecallEdit(const ChatMessage& msg, bool recalledByMe)
{
    //撤回者必须是本人：群主、管理员撤回别人的消息时，原文不属于自己，不给「编辑」
    if (!recalledByMe)
    {
        return false;
    }
    //本人发送、已经发到服务器（有 msgid）的文字类消息才给；发送失败的行本来就不能撤回
    if (!msg.fromMe || msg.sendFailed || msg.msgid <= 0)
    {
        return false;
    }
    //文字类：kMsgText 覆盖普通文字、带 @ 的文字、Markdown 与代码片段；kMsgEmoji 是大表情
    return msg.kind == kMsgText || msg.kind == kMsgEmoji;
}

ChatReeditData BuildRecallDraft(const ChatMessage& msg)
{
    if (!msg.fromMe || (msg.kind != kMsgText && msg.kind != kMsgEmoji))
    {
        return ChatReeditData();
    }
    //与发送失败后「重新编辑」放回输入框的内容完全相同，只是不要求「发送失败」
    return BuildReeditPayload(msg);
}

std::string SerializeRecallDraft(const ChatReeditData& data)
{
    Json::Value root(Json::objectValue);
    root[kKeyVersion] = kRecallDraftVersion;

    Json::Value segs(Json::arrayValue);
    for (size_t i = 0; i < data.m_segments.size(); ++i)
    {
        const ChatClipSegment& seg = data.m_segments[i];
        Json::Value one(Json::objectValue);
        if (seg.m_kind == kChatClipText)
        {
            one[kKeySegKind] = (int)kChatClipText;
            one[kKeySegText] = EncodeUtil::UnicodeToUtf8(seg.m_text);
        }
        else if (seg.m_kind == kChatClipFace)
        {
            one[kKeySegKind] = (int)kChatClipFace;
            one[kKeySegFace] = seg.m_faceId;
        }
        else
        {
            //图片片段：文字消息的正文里不会出现，不写入
            continue;
        }
        segs.append(one);
    }
    root[kKeySegments] = segs;

    Json::Value ats(Json::arrayValue);
    for (size_t i = 0; i < data.m_mentions.size(); ++i)
    {
        Json::Value one(Json::objectValue);
        one[kKeyAtUid] = (Json::Int64)data.m_mentions[i].m_uid;
        one[kKeyAtName] = data.m_mentions[i].m_displayName;
        ats.append(one);
    }
    root[kKeyMentions] = ats;

    root[kKeyMarkdown] = data.m_isMarkdown;
    root[kKeyMdSource] = data.m_markdownSource;
    root[kKeyCode] = data.m_isCode;
    root[kKeyCodeLang] = data.m_codeLang;
    root[kKeyCodeSrc] = EncodeUtil::UnicodeToUtf8(data.m_code);

    //代码片段的批注：撤回后再编辑时随代码一起回到发送代码窗口，不能丢
    Json::Value notes(Json::arrayValue);
    for (size_t i = 0; i < data.m_codeNotes.size(); ++i)
    {
        Json::Value one(Json::objectValue);
        one[kKeyNoteFrom] = data.m_codeNotes[i].m_from;
        one[kKeyNoteTo] = data.m_codeNotes[i].m_to;
        one[kKeyNoteText] = EncodeUtil::UnicodeToUtf8(data.m_codeNotes[i].m_text);
        notes.append(one);
    }
    root[kKeyNotes] = notes;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, root);
}

bool ParseRecallDraft(const std::string& json, ChatReeditData& out)
{
    if (json.empty())
    {
        return false;
    }
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool parsed = reader->parse(json.data(), json.data() + json.size(), &root, &errs);
    delete reader;
    if (!parsed || !root.isObject())
    {
        return false;
    }
    //版本号不认识时不按猜测的格式去读
    if (ReadInt64(root[kKeyVersion], 0) != kRecallDraftVersion)
    {
        return false;
    }
    const Json::Value& segs = root[kKeySegments];
    if (!segs.isArray())
    {
        return false;
    }

    ChatReeditData data;
    for (Json::ArrayIndex i = 0; i < segs.size(); ++i)
    {
        const Json::Value& one = segs[i];
        if (!one.isObject())
        {
            continue;
        }
        const long long kind = ReadInt64(one[kKeySegKind], -1);
        ChatClipSegment seg;
        if (kind == kChatClipText)
        {
            seg.m_kind = kChatClipText;
            seg.m_text = EncodeUtil::Utf8ToUnicode(ReadString(one[kKeySegText]));
        }
        else if (kind == kChatClipFace)
        {
            seg.m_kind = kChatClipFace;
            seg.m_faceId = (int)ReadInt64(one[kKeySegFace], 0);
        }
        else
        {
            //不认识的片段类型跳过，其余片段照常放回
            continue;
        }
        data.m_segments.push_back(seg);
    }

    const Json::Value& ats = root[kKeyMentions];
    if (ats.isArray())
    {
        for (Json::ArrayIndex i = 0; i < ats.size(); ++i)
        {
            const Json::Value& one = ats[i];
            if (!one.isObject())
            {
                continue;
            }
            MentionCandidate cand;
            cand.m_uid = ReadInt64(one[kKeyAtUid], 0);
            cand.m_displayName = ReadString(one[kKeyAtName]);
            data.m_mentions.push_back(cand);
        }
    }

    data.m_isMarkdown = root[kKeyMarkdown].isBool() && root[kKeyMarkdown].asBool();
    data.m_markdownSource = ReadString(root[kKeyMdSource]);
    data.m_isCode = root[kKeyCode].isBool() && root[kKeyCode].asBool();
    data.m_codeLang = (int)ReadInt64(root[kKeyCodeLang], 0);
    data.m_code = EncodeUtil::Utf8ToUnicode(ReadString(root[kKeyCodeSrc]));

    //代码片段的批注：读回后按代码行数整理（去掉越界、相交、说明为空的条目），与收到的批注同一种处理
    const Json::Value& notes = root[kKeyNotes];
    if (data.m_isCode && notes.isArray())
    {
        std::vector<codesnippet::CodeNote> raw;
        for (Json::ArrayIndex i = 0; i < notes.size(); ++i)
        {
            const Json::Value& one = notes[i];
            if (!one.isObject())
            {
                continue;
            }
            raw.push_back(codesnippet::CodeNote((int)ReadInt64(one[kKeyNoteFrom], 0), (int)ReadInt64(one[kKeyNoteTo], 0),
                                                EncodeUtil::Utf8ToUnicode(ReadString(one[kKeyNoteText]))));
        }
        data.m_codeNotes = codesnippet::SanitizeCodeNotes(raw, CountCodeLines(data.m_code));
    }

    out = data;
    return true;
}

long long RecallEditDeadline(long long recallTimeMs, long long nowMs)
{
    if (recallTimeMs <= 0)
    {
        return 0;
    }
    //本机时钟被往回调过时，撤回时间会晚于当前时间；按当前时间计，到期时刻不超过「当前时间加 5 分钟」
    const long long base = (recallTimeMs > nowMs) ? nowMs : recallTimeMs;
    return base + kRecallEditWindowMs;
}

bool IsRecallEditAlive(long long deadlineMs, long long nowMs)
{
    return deadlineMs > 0 && nowMs < deadlineMs;
}

long long RecallEditNowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ticks;
    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;
    return (long long)((ticks.QuadPart - kFileTimeUnixEpochDiff) / kFileTimeTicksPerMs);
}

void QueryRecallDrafts(HWND hostPanel, RecallDraftQuery& query)
{
    query.m_hits.clear();
    query.m_storeReady = false;
    //没有主面板（调试入口、主面板已销毁）或没有要查的：按查不到处理
    if (!::IsWindow(hostPanel) || query.m_msgids.empty())
    {
        return;
    }
    ::SendMessage(hostPanel, FMG_MSG_QUERY_RECALL_DRAFTS, 0, (LPARAM)&query);
}

bool ApplyRecallDraft(ChatMessage& tipRow, const RecallDraftQuery& query, long long nowMs)
{
    for (size_t i = 0; i < query.m_hits.size(); ++i)
    {
        const RecallDraftHit& hit = query.m_hits[i];
        if (hit.m_msgid != tipRow.msgid || hit.m_payload.empty())
        {
            continue;
        }
        const long long deadline = RecallEditDeadline(hit.m_recallTimeMs, nowMs);
        if (!IsRecallEditAlive(deadline, nowMs))
        {
            return false;
        }
        tipRow.recallDraftJson = hit.m_payload;
        tipRow.recallEditDeadlineMs = deadline;
        return true;
    }
    return false;
}

void RescheduleRecallEditTimer(HWND hwnd, UINT_PTR timerId, long long nextDeadlineMs, long long nowMs)
{
    if (!::IsWindow(hwnd))
    {
        return;
    }
    if (nextDeadlineMs <= 0)
    {
        ::KillTimer(hwnd, timerId);
        return;
    }
    long long delay = nextDeadlineMs - nowMs + kRecallEditTimerSlackMs;
    if (delay < kRecallEditTimerMinMs)
    {
        delay = kRecallEditTimerMinMs;
    }
    //同一个 id 再次 SetTimer 会替换原来的定时器
    ::SetTimer(hwnd, timerId, (UINT)delay, NULL);
}

} // namespace balloonchatwnd
