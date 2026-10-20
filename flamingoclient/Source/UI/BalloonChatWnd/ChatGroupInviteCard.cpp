/**
 *  ChatGroupInviteCard.cpp
 *
 *  入群邀请卡片的内容解析与状态文字，说明见 ChatGroupInviteCard.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */

// ---- ATL 头文件要求的前置依赖，与同目录 ChatRelayCard.cpp 相同 ----
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

#include "ChatGroupInviteCard.h"
#include "ChatCardModel.h"   // SetCardChip / ChatCardChipTone：卡片的状态标签

#include <stdlib.h>

#include "../../net/Protocol.h"   // group_invite_state
#include "../../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../third-party/jsoncpp-1.9.0/json.h"

namespace balloonchatwnd {

namespace {

//卡片在 text 字段里的摘要前缀：引用回复、复制等按 text 取摘要的地方拿到它，与会话列表摘要一致
const wchar_t* const kCardDigest = TR_NOOP(L"[入群邀请]");

//解析十进制 id 字符串所用的进制
const int kDecimalBase = 10;

//UTF-8 → CString（宽字符）
CString Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return CString();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return CString();
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.GetBuffer(n), n);
    out.ReleaseBuffer(n);
    return out;
}

//把 JSON 文本解析成对象；失败或不是对象时返回 false
bool ParseObject(const std::string& json, Json::Value& root)
{
    if (json.empty())
        return false;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取一个 id：服务端把 id 写成十进制字符串，也兼容整数；缺失或解析不出时返回 0
long long IdOf(const Json::Value& v)
{
    if (v.isString())
        return _strtoi64(v.asString().c_str(), NULL, kDecimalBase);
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//取字符串字段；缺失或类型不对时返回空串
std::string StringOf(const Json::Value& root, const char* key)
{
    const Json::Value& v = root[key];
    return v.isString() ? v.asString() : std::string();
}

//把紧凑 JSON 写回文本
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//按当前状态重填副标题：群名；只有待加入时再带上群的成员数，帮被邀请人判断要不要加入。其余状态只写群名：
//状态变化推送（1328）只带状态，成员数会停在卡片下发那一刻，入群后仍写原来的人数会让人误解（2026-10-04 用户确认）
void FillSubtitle(ChatMessage& msg)
{
    Json::Value root;
    if (!ParseObject(msg.inviteContent, root))
    {
        msg.inviteSubtitle.Empty();
        return;
    }
    const CString groupName = Utf8ToWide(StringOf(root, "groupname"));
    const int memberCount = root["membercount"].isInt() ? root["membercount"].asInt() : 0;
    if (msg.inviteState == group_invite_pending && memberCount > 0)
        msg.inviteSubtitle = TRF(L"{0} · {1} 名成员", (LPCTSTR)groupName, memberCount).c_str();
    else
        msg.inviteSubtitle = groupName;
}

//按当前状态重填副标题、状态行与按钮
void FillStateFields(ChatMessage& msg)
{
    const bool isInvitee = !msg.fromMe;
    FillSubtitle(msg);
    msg.inviteStatus    = GroupInviteStatusText(msg.inviteState, isInvitee);
    msg.inviteHasButton = (msg.inviteId > 0) && GroupInviteHasButton(msg.inviteState, isInvitee);
    //状态标签就是这句状态文字：待加入、等待审批为橙，已加入为绿，其余为灰（docs/聊天卡片美化方案.md 5.4）
    int tone = kCardChipEnded;
    if (msg.inviteState == group_invite_pending || msg.inviteState == group_invite_approving)
        tone = kCardChipPending;
    else if (msg.inviteState == group_invite_joined)
        tone = kCardChipLive;
    SetCardChip(msg, msg.inviteStatus, tone);
}

} // namespace

long long GroupInviteIdOf(const std::string& contentJson)
{
    Json::Value root;
    if (!ParseObject(contentJson, root))
        return 0;
    return IdOf(root["inviteid"]);
}

CString GroupInviteStatusText(int state, bool isInvitee)
{
    switch (state)
    {
    //待加入：被邀请人一侧提示有效期，邀请人一侧等对方确认
    case group_invite_pending:
        return isInvitee ? CString(TR(L"邀请 7 天内有效")) : CString(TR(L"等待对方确认"));
    //等待审批：被邀请人点了加入，但邀请人是普通成员且群需审批
    case group_invite_approving:
        return isInvitee ? CString(TR(L"已申请加入，等待群主或管理员审批"))
                         : CString(TR(L"对方已同意，等待群主或管理员审批"));
    //已加入
    case group_invite_joined:
        return isInvitee ? CString(TR(L"已加入")) : CString(TR(L"对方已加入"));
    //未通过审批
    case group_invite_rejected:
        return CString(TR(L"未通过群主或管理员审批"));
    //已过期
    case group_invite_expired:
        return CString(TR(L"邀请已过期"));
    //已失效：群已解散或邀请人已退出群聊
    case group_invite_invalid:
        return CString(TR(L"邀请已失效"));
    //认不出的状态（更新的服务端新加的）：不写状态
    default:
        return CString();
    }
}

bool GroupInviteHasButton(int state, bool isInvitee)
{
    return isInvitee && state == group_invite_pending;
}

CString GroupInviteButtonText(bool joining)
{
    return joining ? CString(TR(L"正在加入…")) : CString(TR(L"加入群聊"));
}

void ApplyGroupInviteCard(ChatMessage& msg, const std::string& contentJson)
{
    msg.kind = kMsgGroupInvite;
    msg.inviteContent = contentJson;
    msg.inviteJoining = false;

    Json::Value root;
    if (!ParseObject(contentJson, root) || IdOf(root["inviteid"]) <= 0)
    {
        //内容不可用：只写类型，不给按钮
        msg.inviteId = 0;
        msg.inviteGroupId = 0;
        msg.inviteTitle = TR(L"入群邀请");
        msg.inviteSubtitle.Empty();
        msg.inviteState = group_invite_invalid;
        msg.inviteStatus.Empty();
        msg.inviteHasButton = false;
        msg.text = TR(kCardDigest);
        SetCardChip(msg, CString(), kCardChipNone);
        return;
    }

    msg.inviteId = IdOf(root["inviteid"]);
    msg.inviteGroupId = IdOf(root["groupid"]);
    const CString groupName = Utf8ToWide(StringOf(root, "groupname"));

    //标题：被邀请人一侧写谁邀请了我，邀请人一侧写我邀请了谁；名字取不到时用不带名字的写法
    if (!msg.fromMe)
    {
        const CString inviter = Utf8ToWide(StringOf(root, "inviter"));
        if (inviter.IsEmpty())
            msg.inviteTitle = TR(L"有人邀请你加入群聊");
        else
            msg.inviteTitle = TRF(L"「{0}」邀请你加入群聊", (LPCTSTR)inviter).c_str();
    }
    else
    {
        const CString invitee = Utf8ToWide(StringOf(root, "invitee"));
        if (invitee.IsEmpty())
            msg.inviteTitle = TR(L"你发出了入群邀请");
        else
            msg.inviteTitle = TRF(L"你邀请「{0}」加入群聊", (LPCTSTR)invitee).c_str();
    }

    //状态：服务端下发时合并的 state；没有这个字段（不该出现）时按待加入处理。副标题随状态而定，一并由 FillStateFields 填
    msg.inviteState = root["state"].isInt() ? root["state"].asInt() : group_invite_pending;
    FillStateFields(msg);

    //摘要：「[入群邀请] 群名」
    msg.text = TR(kCardDigest);
    if (!groupName.IsEmpty())
        msg.text += _T(" ") + groupName;
}

void ApplyGroupInviteState(ChatMessage& msg, int state)
{
    msg.inviteState = state;
    msg.inviteJoining = false;
    msg.inviteContent = SetGroupInviteContentState(msg.inviteContent, state);
    FillStateFields(msg);
}

void SetGroupInviteJoining(ChatMessage& msg, bool joining)
{
    msg.inviteJoining = joining;
}

std::string SetGroupInviteContentState(const std::string& contentJson, int state)
{
    Json::Value root;
    if (!ParseObject(contentJson, root))
        return contentJson;
    root["state"] = state;
    return WriteCompact(root);
}

int GroupInviteStateForAcceptError(int code)
{
    switch (code)
    {
    //邀请已超过有效期
    case error_code_group_invite_expired:
        return group_invite_expired;
    //邀请已失效：群已解散、邀请人已退出群聊，或邀请已在别的设备上处理过
    case error_code_group_invite_invalid:
        return group_invite_invalid;
    //其余错误（已在群中另有 1328 推送、服务端内部错误等）：卡片状态不变
    default:
        return kGroupInviteStateUnchanged;
    }
}

CString GroupInviteAcceptErrorText(int code)
{
    switch (code)
    {
    //已在群中：服务端已推 1328 把卡片记为已加入，调用方直接打开群聊窗，不提示
    case error_code_already_in_group:
        return CString();
    //邀请已超过有效期
    case error_code_group_invite_expired:
        return CString(TR(L"邀请已过期，请联系邀请人重新邀请。"));
    //邀请已失效
    case error_code_group_invite_invalid:
        return CString(TR(L"邀请已失效：群可能已解散，或邀请人已退出群聊。"));
    //群不存在（判定之后、入群之前群被解散）
    case error_code_group_not_exist:
        return CString(TR(L"该群已解散。"));
    //其余错误：服务端内部错误等
    default:
        return CString(TR(L"加入群聊失败，请稍后再试。"));
    }
}

} // namespace balloonchatwnd
