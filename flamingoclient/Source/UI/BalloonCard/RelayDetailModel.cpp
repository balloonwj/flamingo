/**
 *  RelayDetailModel.cpp —— 接龙详情窗的显示模型，见 RelayDetailModel.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-02
 */

// ---- ATL 头文件要求的前置依赖，与 ChatRelayCard.cpp 相同 ----
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

#include "RelayDetailModel.h"
#include "../../net/Protocol.h"   // relay_state、group_member_role、error_code 的具体取值
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace ballooncard {

namespace {

//卡片标题的缺省值：卡片内容里没有标题（只有旧数据会出现）时使用，与卡片气泡一致
const wchar_t* const kDefaultRelayTitle = TR_NOOP(L"接龙");

//一条接龙里名字与内容之间的分隔：全角空格，与卡片气泡一致
const wchar_t* const kNameTextSeparator = L"\x3000";

//状态行里几段文字之间的分隔，与卡片气泡一致
const wchar_t* const kStatusSeparator = L" · ";

//全角空格 U+3000 的 UTF-8 编码长度与各字节：服务端与客户端规范化时都把它当空白去掉
const size_t kIdeographicSpaceLen = 3;
const unsigned char kIdeographicSpace[kIdeographicSpaceLen] = { 0xE3, 0x80, 0x80 };

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

//取某人的显示名：名字解析接口查不到时写成「用户<id>」，与卡片气泡一致
CString DisplayName(const balloonchatwnd::IRelayNameResolver& names, int64_t userid)
{
    CString name = names.ResolveName(userid);
    if (name.IsEmpty())
    {
        name.Format(TR(L"用户%lld"), (long long)userid);
    }
    return name;
}

//判断一段 UTF-8 文字是否只由空白组成（半角空格、制表符、回车、换行、全角空格），空串也算
bool IsBlankUtf8(const std::string& text)
{
    size_t i = 0;
    while (i < text.size())
    {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            ++i;
            continue;
        }
        if (i + kIdeographicSpaceLen <= text.size()
            && (unsigned char)text[i] == kIdeographicSpace[0]
            && (unsigned char)text[i + 1] == kIdeographicSpace[1]
            && (unsigned char)text[i + 2] == kIdeographicSpace[2])
        {
            i += kIdeographicSpaceLen;
            continue;
        }
        return false;
    }
    return true;
}

//拼状态行（写法见 BuildRelayDetailView 的说明）
CString BuildStatusText(const net::RelayCardContent& card, int state, bool fullForSelf, int64_t nowMs,
                        const balloonchatwnd::IRelayNameResolver& names)
{
    CString text;
    text.Format(TR(L"共 %u 人参与"), (unsigned)card.m_items.size());

    if (state == relay_state_closed)
    {
        text += kStatusSeparator;
        const CString when = balloonchatwnd::FormatRelayTime(card.m_closeTime, nowMs);
        if (card.m_closedBy > 0)
        {
            //{0} 为结束接龙的人，{1} 为结束时间
            const CString who = DisplayName(names, card.m_closedBy);
            if (!when.IsEmpty())
            {
                text += TRF(L"{0} 于 {1} 结束了接龙", who, when).c_str();
            }
            else
            {
                text += TRF(L"{0} 结束了接龙", who).c_str();
            }
        }
        else
        {
            text += TR(L"已结束");
        }
        return text;
    }

    if (state == relay_state_expired)
    {
        text += kStatusSeparator;
        const CString when = balloonchatwnd::FormatRelayTime(card.m_deadline, nowMs);
        if (!when.IsEmpty())
        {
            text += TRF(L"已于 {0} 截止", when).c_str();
        }
        else
        {
            text += TR(L"已截止");
        }
        return text;
    }

    if (card.m_deadline > 0)
    {
        text += kStatusSeparator;
        text += TRF(L"截止 {0}", balloonchatwnd::FormatRelayTime(card.m_deadline, nowMs)).c_str();
    }
    if (fullForSelf)
    {
        text += kStatusSeparator;
        text += TR(L"已满");
    }
    return text;
}

} // anonymous namespace

void BuildRelayDetailView(const net::RelayCardContent& card, int64_t creatorId, int64_t selfId, int selfRole,
                          bool selfMuted, int64_t nowMs, const balloonchatwnd::IRelayNameResolver& names,
                          RelayDetailView& out)
{
    out = RelayDetailView();
    out.m_title  = card.m_title.empty() ? CString(TR(kDefaultRelayTitle)) : Utf8ToCStr(card.m_title);
    out.m_note   = Utf8ToCStr(card.m_note);
    out.m_state  = balloonchatwnd::EffectiveRelayState(card, nowMs);
    out.m_joined = balloonchatwnd::IsRelayJoinedBy(card, selfId);

    for (size_t i = 0; i < card.m_items.size(); ++i)
    {
        const net::RelayItem& item = card.m_items[i];
        RelayDetailEntry entry;
        entry.m_text.Format(L"%u. ", (unsigned)(i + 1));
        entry.m_text += DisplayName(names, item.m_userid);
        entry.m_text += kNameTextSeparator;
        entry.m_text += Utf8ToCStr(item.m_text);
        entry.m_isSelf = (item.m_userid == selfId);
        if (entry.m_isSelf)
        {
            out.m_selfText = item.m_text;
        }
        out.m_entries.push_back(entry);
    }

    //名额已满且本人未参与：不能再参与，修改不受人数限制
    const bool fullForSelf = !out.m_joined && card.m_items.size() >= net::kRelayMaxParticipants;
    out.m_statusText = BuildStatusText(card, out.m_state, fullForSelf, nowMs, names);

    const bool open = (out.m_state == relay_state_open);
    out.m_showInput    = open;
    out.m_inputEnabled = open && !selfMuted && !fullForSelf;
    out.m_submitLabel  = out.m_joined ? TR(L"修改") : TR(L"提交");
    out.m_showWithdraw = open && out.m_joined && !selfMuted;
    out.m_showClose    = balloonchatwnd::CanCloseRelay(card, true, creatorId, selfId, selfRole, nowMs);
    if (open && selfMuted)
    {
        out.m_inputHint = TR(L"你已被禁言，不能参与接龙。");
    }
    else if (open && fullForSelf)
    {
        out.m_inputHint = TR(L"接龙人数已满。");
    }
}

int CheckRelaySubmit(const std::string& rawUtf8, bool joined, const std::string& selfText, std::string& normalized)
{
    std::string text;
    const int code = net::CardProtocolClient::normalizeRelayText(rawUtf8, text);
    if (code == error_code_msg_too_large)
    {
        return kRelaySubmitTooLong;
    }
    if (code != 0)
    {
        //normalizeRelayText 对「为空」与「含换行」回同一个错误码，这里再分开，提示文字不同
        return IsBlankUtf8(rawUtf8) ? kRelaySubmitEmpty : kRelaySubmitMultiline;
    }
    if (joined && text == selfText)
    {
        return kRelaySubmitUnchanged;
    }
    normalized = text;
    return kRelaySubmitOk;
}

CString RelaySubmitCheckText(int check)
{
    switch (check)
    {
    //去掉首尾空白后什么都没有
    case kRelaySubmitEmpty:
        return TR(L"请填写接龙内容。");

    //中间含换行
    case kRelaySubmitMultiline:
        return TR(L"接龙内容不能换行。");

    //超长：上限与服务端相同
    case kRelaySubmitTooLong:
        {
            CString text;
            text.Format(TR(L"内容太长，每条最多 %u 字。"), (unsigned)net::kRelayTextMaxChars);
            return text;
        }

    //与本人现有那一条相同
    case kRelaySubmitUnchanged:
        return TR(L"内容没有变化。");

    //可以提交，或认不出的取值：不显示提示
    case kRelaySubmitOk:
    default:
        return CString();
    }
}

CString BuildRelayCloseConfirmText(const net::RelayCardContent& card)
{
    CString text;
    if (card.m_title.empty())
    {
        text = TR(L"确定结束这个接龙吗？");
    }
    else
    {
        text = TR(L"确定结束接龙「");
        text += Utf8ToCStr(card.m_title);
        text += TR(L"」吗？");
    }
    text += TR(L"结束后不能再参与、修改或撤回。");
    return text;
}

} // namespace ballooncard
