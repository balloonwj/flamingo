/**
 *  ChatRelayCard.cpp —— 群接龙卡片的显示模型，见 ChatRelayCard.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-02
 */

// ---- ATL 头文件要求的前置依赖，与同目录 ChatMergedCard.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <time.h>

#include "ChatRelayCard.h"
#include "ChatCardModel.h"   // SetCardChip / ChatCardChipTone：卡片的状态标签
#include "../../net/Protocol.h"   // relay_state、group_member_role 的具体取值
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//卡片标题的缺省值：卡片内容里没有标题（只有旧数据会出现）时使用
const wchar_t* const kDefaultRelayTitle = TR_NOOP(L"接龙");

//一条接龙里名字与内容之间的分隔：全角空格，与方案 §3.2 的「1. 张三　本人+1」一致
const wchar_t* const kNameTextSeparator = L"\x3000";

//状态行里几段文字之间的分隔
const wchar_t* const kStatusSeparator = L" · ";

//复制为文本时的换行：Windows 剪贴板里的文字按 \r\n 换行，粘贴到各种编辑框都能正确分行
const wchar_t* const kCopyLineBreak = L"\r\n";

//Unix 毫秒与秒的换算
const int64_t kMsPerSecond = 1000;

//struct tm 的年份从 1900 起算、月份从 0 起算
const int kTmYearBase = 1900;
const int kTmMonthBase = 1;

//UTF-8 转 CString（UTF-16）。空串安全
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (len <= 0)
    {
        return CString();
    }
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), out.GetBuffer(len), len);
    out.ReleaseBuffer(len);
    return out;
}

//把 Unix 毫秒换成本机时区的 struct tm；时刻 <= 0 或换算失败时返回 false
bool ToLocalTm(int64_t timeMs, struct tm& out)
{
    if (timeMs <= 0)
    {
        return false;
    }
    const time_t secs = (time_t)(timeMs / kMsPerSecond);
    return localtime_s(&out, &secs) == 0;
}

//取某人的显示名：名字解析接口查不到时写成「用户<id>」，与合并卡片的兜底一致
CString DisplayName(const IRelayNameResolver& names, int64_t userid)
{
    CString name = names.ResolveName(userid);
    if (name.IsEmpty())
    {
        name.Format(TR(L"用户%lld"), (long long)userid);
    }
    return name;
}

//拼一条接龙的显示文字：「序号. 名字　内容」，序号从 1 起
CString EntryLine(size_t index, const CString& name, const std::string& textUtf8)
{
    CString line;
    line.Format(L"%u. ", (unsigned)(index + 1));
    line += name;
    line += kNameTextSeparator;
    line += Utf8ToCStr(textUtf8);
    return line;
}

//卡片要显示的标题：卡片内容里标题为空时用缺省值
CString DisplayTitle(const net::RelayCardContent& card)
{
    return card.m_title.empty() ? CString(TR(kDefaultRelayTitle)) : Utf8ToCStr(card.m_title);
}

} // anonymous namespace

void ApplyRelayNotify(net::RelayCardContent& card, const net::RelayChangedNotify& notify)
{
    //推送带的是完整名单与当前状态，整体替换；标题、说明、截止时间推送不带，保持不变
    card.m_items     = notify.m_items;
    card.m_state     = notify.m_state;
    card.m_closedBy  = notify.m_closedBy;
    card.m_closeTime = notify.m_closeTime;
}

int EffectiveRelayState(const net::RelayCardContent& card, int64_t nowMs)
{
    //服务端已标为已结束或已截止的保持不变：手动结束优先于截止
    if (card.m_state != relay_state_open)
    {
        return card.m_state;
    }
    //服务端到截止时间时不发推送，客户端按本机时间自行判断；等于截止时间即算截止
    if (card.m_deadline > 0 && nowMs >= card.m_deadline)
    {
        return relay_state_expired;
    }
    return relay_state_open;
}

CString FormatRelayTime(int64_t timeMs, int64_t nowMs)
{
    struct tm timeTm;
    if (!ToLocalTm(timeMs, timeTm))
    {
        return CString();
    }

    //当前时刻取不到时按跨年的写法输出：宁可多写一截年份，也不要让人看错是哪一年
    struct tm nowTm;
    const bool sameYear = ToLocalTm(nowMs, nowTm) && nowTm.tm_year == timeTm.tm_year;

    CString text;
    if (sameYear)
    {
        text.Format(L"%02d-%02d %02d:%02d", timeTm.tm_mon + kTmMonthBase, timeTm.tm_mday,
                    timeTm.tm_hour, timeTm.tm_min);
    }
    else
    {
        text.Format(L"%04d-%02d-%02d %02d:%02d", timeTm.tm_year + kTmYearBase, timeTm.tm_mon + kTmMonthBase,
                    timeTm.tm_mday, timeTm.tm_hour, timeTm.tm_min);
    }
    return text;
}

bool IsRelayJoinedBy(const net::RelayCardContent& card, int64_t userid)
{
    for (size_t i = 0; i < card.m_items.size(); ++i)
    {
        if (card.m_items[i].m_userid == userid)
        {
            return true;
        }
    }
    return false;
}

int DecideRelayButton(const net::RelayCardContent& card, int64_t selfId, bool selfMuted, int64_t nowMs)
{
    //接龙已结束或已截止：名单冻结
    if (EffectiveRelayState(card, nowMs) != relay_state_open)
    {
        return kRelayButtonNone;
    }
    //被禁言：参与与修改都会被服务端以 306 拒绝
    if (selfMuted)
    {
        return kRelayButtonNone;
    }
    //已参与：修改不占新名额，满员也可以修改
    if (IsRelayJoinedBy(card, selfId))
    {
        return kRelayButtonModify;
    }
    //名额已满：不显示「参与接龙」，避免填完内容才被拒（2026-10-02 用户确认）
    if (card.m_items.size() >= net::kRelayMaxParticipants)
    {
        return kRelayButtonNone;
    }
    return kRelayButtonJoin;
}

void BuildRelayCardPreview(const net::RelayCardContent& card, int64_t selfId, bool selfMuted,
                           int64_t nowMs, const IRelayNameResolver& names, RelayCardPreview& out)
{
    out = RelayCardPreview();
    out.m_title  = DisplayTitle(card);
    out.m_note   = Utf8ToCStr(card.m_note);
    out.m_count  = (int)card.m_items.size();
    out.m_state  = EffectiveRelayState(card, nowMs);
    out.m_button = DecideRelayButton(card, selfId, selfMuted, nowMs);

    for (size_t i = 0; i < card.m_items.size() && out.m_lines.size() < kRelayCardPreviewLines; ++i)
    {
        out.m_lines.push_back(EntryLine(i, DisplayName(names, card.m_items[i].m_userid), card.m_items[i].m_text));
    }

    //状态行：先写人数，再按状态追加一段。名额已满且本人未参与时只写「已满」、不再写截止时间：
    //此时用户已经不能参与，截止时间对他没有意义
    out.m_statusText.Format(TR(L"共 %d 人参与"), out.m_count);
    if (out.m_state == relay_state_closed)
    {
        out.m_statusText += kStatusSeparator;
        out.m_statusText += TR(L"已结束");
    }
    else if (out.m_state == relay_state_expired)
    {
        out.m_statusText += kStatusSeparator;
        out.m_statusText += TR(L"已截止");
    }
    else if (!IsRelayJoinedBy(card, selfId) && card.m_items.size() >= net::kRelayMaxParticipants)
    {
        out.m_statusText += kStatusSeparator;
        out.m_statusText += TR(L"已满");
    }
    else if (card.m_deadline > 0)
    {
        out.m_statusText += kStatusSeparator;
        out.m_statusText += TRF(L"截止 {0}", FormatRelayTime(card.m_deadline, nowMs)).c_str();
    }
}

bool CanCloseRelay(const net::RelayCardContent& card, bool isGroupCard, int64_t creatorId,
                   int64_t selfId, int selfRole, int64_t nowMs)
{
    //已结束或已截止的接龙不能再结束
    if (EffectiveRelayState(card, nowMs) != relay_state_open)
    {
        return false;
    }
    //发起人总可以结束自己发起的接龙
    if (selfId == creatorId)
    {
        return true;
    }
    //单聊卡片没有群角色，只有发起人可以
    if (!isGroupCard)
    {
        return false;
    }
    return selfRole == group_role_owner || selfRole == group_role_admin;
}

CString BuildRelayCopyBody(const net::RelayCardContent& card, const IRelayNameResolver& names)
{
    CString text = TR(L"接龙：");
    text += DisplayTitle(card);

    //说明原文，为空时省略这一行。说明可能多行，里面的换行统一成 \r\n
    if (!card.m_note.empty())
    {
        CString note = Utf8ToCStr(card.m_note);
        note.Replace(L"\r\n", L"\n");
        note.Replace(L"\n", kCopyLineBreak);
        text += kCopyLineBreak;
        text += note;
    }

    for (size_t i = 0; i < card.m_items.size(); ++i)
    {
        text += kCopyLineBreak;
        text += EntryLine(i, DisplayName(names, card.m_items[i].m_userid), card.m_items[i].m_text);
    }
    return text;
}

CString BuildRelayCopyFooter(int count, int state, int64_t nowMs)
{
    //进行中写复制时的时刻，已结束、已截止写状态
    CString footer;
    const unsigned n = (count > 0) ? (unsigned)count : 0u;
    if (state == relay_state_closed)
    {
        footer.Format(TR(L"（共 %u 人，已结束）"), n);
    }
    else if (state == relay_state_expired)
    {
        footer.Format(TR(L"（共 %u 人，已截止）"), n);
    }
    else
    {
        footer.Format(TR(L"（共 %u 人，截至 %s）"), n, (LPCWSTR)FormatRelayTime(nowMs, nowMs));
    }
    return footer;
}

CString BuildRelayCopyText(const net::RelayCardContent& card, int64_t nowMs, const IRelayNameResolver& names)
{
    CString text = BuildRelayCopyBody(card, names);
    text += kCopyLineBreak;
    text += BuildRelayCopyFooter((int)card.m_items.size(), EffectiveRelayState(card, nowMs), nowMs);
    return text;
}

namespace {

//按已解析的卡片内容填写气泡数据的 relayXxx 字段与摘要。contentValid 为 false 表示 content
//解析失败，此时不显示按钮：卡片内容都认不出来，让用户去参与没有意义
void FillRelayFields(ChatMessage& msg, const net::RelayCardContent& card, bool contentValid, int64_t selfId,
                     bool selfMuted, int64_t nowMs, const IRelayNameResolver& names)
{
    RelayCardPreview preview;
    BuildRelayCardPreview(card, selfId, selfMuted, nowMs, names, preview);

    msg.kind          = kMsgRelay;
    msg.relayTitle    = preview.m_title;
    msg.relayNote     = preview.m_note;
    msg.relayLines    = preview.m_lines;
    msg.relayStatus   = preview.m_statusText;
    msg.relayCount    = preview.m_count;
    msg.relayState    = preview.m_state;
    msg.relayButton   = contentValid ? preview.m_button : (int)kRelayButtonNone;
    msg.relayDeadline = (long long)card.m_deadline;
    msg.relayCopyBody = BuildRelayCopyBody(card, names);

    //卡片的状态标签、信息行与副标题（docs/聊天卡片美化方案.md 5.3）：进行中为绿，已结束、已截止、名额已满且本人未参与为灰；
    //信息行写人数，副标题写截止时间。原来的状态行 relayStatus 照旧保留（复制等处仍在用）
    const bool full = !IsRelayJoinedBy(card, selfId) && card.m_items.size() >= net::kRelayMaxParticipants;
    CString chip = TR(L"进行中");
    int tone = kCardChipLive;
    if (preview.m_state == relay_state_closed)
    {
        chip = TR(L"已结束");
        tone = kCardChipEnded;
    }
    else if (preview.m_state == relay_state_expired)
    {
        chip = TR(L"已截止");
        tone = kCardChipEnded;
    }
    else if (full)
    {
        chip = TR(L"已满");
        tone = kCardChipEnded;
    }
    CString count;
    count.Format(TR(L"共 %d 人参与"), preview.m_count);
    SetCardChip(msg, chip, tone, count);
    msg.relayDeadlineText = (card.m_deadline > 0)
                          ? CString(TRF(L"截止 {0}", FormatRelayTime(card.m_deadline, nowMs)).c_str()) : CString();

    //摘要与服务端会话摘要同一写法；标题为空的旧数据只写「[接龙]」
    msg.text = TR(L"[接龙]");
    if (!card.m_title.empty())
    {
        msg.text += L" ";
        msg.text += Utf8ToCStr(card.m_title);
    }
}

} // anonymous namespace

void ApplyRelayCard(ChatMessage& msg, const std::string& contentJson, int64_t selfId, bool selfMuted,
                    int64_t nowMs, const IRelayNameResolver& names)
{
    net::RelayCardContent card;
    const bool valid = net::CardProtocolClient::parseRelayCardContent(contentJson, card);
    msg.relayContent = contentJson;
    FillRelayFields(msg, card, valid, selfId, selfMuted, nowMs, names);
}

bool ApplyRelayNotifyToMessage(ChatMessage& msg, const net::RelayChangedNotify& notify, int64_t selfId,
                               bool selfMuted, int64_t nowMs, const IRelayNameResolver& names)
{
    //卡片已被撤回时气泡变成了提示行（kind 不再是接龙），不能把名单合并进去
    if (msg.kind != kMsgRelay || msg.msgid != (long long)notify.m_msgid)
    {
        return false;
    }

    //气泡里的 content 解析失败时按空卡片合并：推送带来的名单与状态仍然可以显示，
    //标题、说明、截止时间缺失，与解析失败时的卡片一致
    net::RelayCardContent card;
    net::CardProtocolClient::parseRelayCardContent(msg.relayContent, card);
    ApplyRelayNotify(card, notify);
    msg.relayContent = net::CardProtocolClient::serializeRelayCardContent(card);
    FillRelayFields(msg, card, true, selfId, selfMuted, nowMs, names);
    return true;
}

bool RelayCardDueForExpiry(const ChatMessage& msg, int64_t nowMs)
{
    return msg.kind == kMsgRelay
        && msg.relayState == relay_state_open
        && msg.relayDeadline > 0
        && nowMs >= msg.relayDeadline;
}

void RefreshRelayCard(ChatMessage& msg, int64_t selfId, bool selfMuted, int64_t nowMs,
                      const IRelayNameResolver& names)
{
    if (msg.kind != kMsgRelay)
    {
        return;
    }
    net::RelayCardContent card;
    const bool valid = net::CardProtocolClient::parseRelayCardContent(msg.relayContent, card);
    FillRelayFields(msg, card, valid, selfId, selfMuted, nowMs, names);
}

CString RelayCopyTextForMessage(const ChatMessage& msg, int64_t nowMs)
{
    //填写之后到了截止时间、还没重填的卡片，末行按已截止写
    const int state = RelayCardDueForExpiry(msg, nowMs) ? (int)relay_state_expired : msg.relayState;
    CString text = msg.relayCopyBody;
    text += kCopyLineBreak;
    text += BuildRelayCopyFooter(msg.relayCount, state, nowMs);
    return text;
}

CString BuildRelayDigest(const std::string& contentJson)
{
    net::RelayCardContent card;
    net::CardProtocolClient::parseRelayCardContent(contentJson, card);
    CString digest = TR(L"[接龙]");
    if (!card.m_title.empty())
    {
        digest += L" ";
        digest += Utf8ToCStr(card.m_title);
    }
    return digest;
}

CString RelayOpErrorText(int cmd, int code)
{
    switch (code)
    {
    //接龙已被提前结束，或已过截止时间
    case error_code_card_closed:
        return TR(L"接龙已结束或已截止。");

    //名额已满（只有参与接龙会遇到）
    case error_code_card_full:
        return TR(L"接龙人数已满。");

    //本人被禁言（参与、撤回本人那条会遇到；提前结束不受禁言限制）
    case error_code_member_muted:
        return TR(L"你已被禁言，不能参与接龙。");

    //卡片不存在或已撤回
    case error_code_message_not_exist:
        return TR(L"这条接龙已被撤回。");

    //已不是群成员
    case error_code_not_group_member:
        return TR(L"你已不在该群聊中。");

    //无权限：提前结束时是不是发起人、群主或管理员；参与与撤回时是不在卡片所在会话里
    case error_code_permission_denied:
        if (cmd == cmd_close_relay)
            return TR(L"只有发起人、群主或管理员可以结束接龙。");
        return TR(L"你已不在该群聊中。");

    //接龙文本超长。只有参与接龙带文本，另两种操作落到下面的通用提示
    case error_code_msg_too_large:
        if (cmd == cmd_join_relay)
            return TR(L"内容太长，每条最多 100 字。");
        break;

    //接龙文本命中发送者所属企业的敏感词库。同样只有参与接龙会遇到（docs/敏感词方案.md 8.6）
    case error_code_sensitive_word:
        if (cmd == cmd_join_relay)
            return TR(L"接龙内容包含敏感词，未能提交。");
        break;

    //请求非法：参与接龙时是文本为空或含换行；另两种操作不带文本，此时是请求本身有问题，
    //落到下面的通用提示
    case error_code_bad_request:
        if (cmd == cmd_join_relay)
            return TR(L"内容不能为空，也不能换行。");
        break;

    //其余错误码（含服务端内部错误）：落到下面按操作给出的通用提示
    default:
        break;
    }

    if (cmd == cmd_withdraw_relay)
        return TR(L"撤回失败，请稍后重试。");
    if (cmd == cmd_close_relay)
        return TR(L"结束接龙失败，请稍后重试。");
    return TR(L"参与接龙失败，请稍后重试。");
}

} // namespace balloonchatwnd
