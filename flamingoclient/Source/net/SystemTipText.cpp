/**
 *  系统提示按当前界面语言生成文字的实现，见 SystemTipText.h。
 *
 *  模板的简体中文原文与服务端拼 text 的写法逐字一致（GroupProtocol / CardProtocol / ScreenShareRules），
 *  所以简体中文界面下生成的文字与服务端的 text 相同。接龙提示由「操作者」「接龙」两个名词短语代入整句模板，
 *  这两个短语各有昵称 / 标题缺失时的写法，用 TRC8 带上下文，避免与界面上同名的按钮文字共用一条译文。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "SystemTipText.h"

#include <stdio.h>
#include <stdlib.h>
#include <atomic>

#include "Protocol.h"   // kSystemTip*：系统提示类型
#include "../Common/I18n/I18n.h"   // TR8 / TRC8 / TRF8：界面文字的多语言查找
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

namespace {

//时长换算：每分钟的秒数、每小时的秒数
const int64_t kSecondsPerMinute = 60;
const int64_t kSecondsPerHour = 3600;

//格式化时长所用缓冲区的大小：最长的情况是 19 位小时数加 ":mm:ss"，64 字节足够
const size_t kClockBufSize = 64;

//解析十进制 id 字符串所用的进制
const int kDecimalBase = 10;

//当前登录者的 userid（SetSystemTipSelfUserId 设置），入群提示据此把本人写成「你」；0 表示未登录。
//登录线程写、网络线程与 UI 线程读，故用原子变量
std::atomic<int64_t> g_selfUserId(0);

//取参数对象里的字符串参数；参数对象不是对象、缺这个参数或它不是字符串时返回空串
std::string GetStringArg(const Json::Value& args, const char* key)
{
    if (!args.isObject())
        return std::string();
    const Json::Value& v = args[key];
    return v.isString() ? v.asString() : std::string();
}

//取参数对象里的非负整数参数；缺失、类型不对或为负数时返回 0
int64_t GetCountArg(const Json::Value& args, const char* key)
{
    if (!args.isObject())
        return 0;
    const Json::Value& v = args[key];
    if (!v.isIntegral())
        return 0;
    const int64_t n = v.asInt64();
    return (n > 0) ? n : 0;
}

//把 JSON 文本解析成对象；失败或不是对象时返回 false
bool ParseObject(const std::string& json, Json::Value& root)
{
    if (json.empty())
        return false;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//成员退群：「张三」退出了群聊；没有昵称时为「有成员退出了群聊」
std::string FormatMemberLeave(const Json::Value& args)
{
    const std::string nickname = GetStringArg(args, "nickname");
    if (nickname.empty())
        return TR8(u8"有成员退出了群聊");
    return TRF8(u8"「{0}」退出了群聊", nickname);
}

//转让群主：两端昵称齐全、只有新群主、只有原群主、两端都缺，四种写法与服务端一致
std::string FormatOwnerTransfer(const Json::Value& args)
{
    const std::string from = GetStringArg(args, "from");
    const std::string to = GetStringArg(args, "to");
    if (!from.empty() && !to.empty())
        return TRF8(u8"「{0}」已将群主转让给「{1}」", from, to);
    if (!to.empty())
        return TRF8(u8"群主已转让给「{0}」", to);
    if (!from.empty())
        return TRF8(u8"「{0}」已转让群主", from);
    return TR8(u8"群主已变更");
}

//发布群公告：「张三」发布了群公告：标题；发布人取不到时为「有人」
std::string FormatAnnouncement(const Json::Value& args)
{
    const std::string publisher = GetStringArg(args, "publisher");
    const std::string title = GetStringArg(args, "title");
    if (publisher.empty())
        return TRF8(u8"「有人」发布了群公告：{0}", title);
    return TRF8(u8"「{0}」发布了群公告：{1}", publisher, title);
}

//入群欢迎语：新成员昵称之间用当前语言的分隔符连接，后接一个空格与群主写的欢迎词（欢迎词原样显示）
std::string FormatGroupWelcome(const Json::Value& args)
{
    const std::string welcome = GetStringArg(args, "welcome");
    std::string joined;
    if (args.isObject() && args["names"].isArray())
    {
        const Json::Value& names = args["names"];
        const std::string separator = TRC8(u8"入群欢迎语里新成员昵称之间的分隔符", u8"、");
        for (Json::ArrayIndex i = 0; i < names.size(); ++i)
        {
            if (!names[i].isString() || names[i].asString().empty())
                continue;
            if (!joined.empty())
                joined += separator;
            joined += names[i].asString();
        }
    }
    if (joined.empty())
        return welcome;
    return joined + " " + welcome;
}

//接龙提示里的操作者短语：有昵称时加书名号，没有时为「有成员」
std::string RelayActor(const Json::Value& args)
{
    const std::string nickname = GetStringArg(args, "nickname");
    if (nickname.empty())
        return TRC8(u8"接龙系统提示里取不到名字的操作者", u8"有成员");
    return TRF8(u8"「{0}」", nickname);
}

//接龙提示里的接龙短语：有标题时为 接龙「标题」，没有标题（只有旧数据）时只写「接龙」
std::string RelayCard(const Json::Value& args)
{
    const std::string title = GetStringArg(args, "title");
    if (title.empty())
        return TRC8(u8"接龙系统提示里没有标题的接龙", u8"接龙");
    return TRF8(u8"接龙「{0}」", title);
}

//屏幕共享结束：屏幕共享已结束，时长 mm:ss
std::string FormatScreenShareEnd(const Json::Value& args)
{
    return TRF8(u8"屏幕共享已结束，时长 {0}", FormatDurationClock(GetCountArg(args, "duration")));
}

//取一个 id 值。服务端把 id 写成十进制字符串，也兼容整数；缺失、类型不对或解析不出时返回 0
int64_t GetIdValue(const Json::Value& v)
{
    if (v.isString())
        return _strtoi64(v.asString().c_str(), NULL, kDecimalBase);
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

/**
 *  有人入群（docs/邀请入群方案.md §3.1）。名单取 members 里带 nickname 的项 —— 服务端只给前 10 位中昵称非空的
 *  带上，所以与服务端拼 text 时列出的名单相同。本人在新成员之中时把「你」放在最前、不再列本人的名字；本人是
 *  邀请人时写「你邀请」。列出的名字少于实际人数时写「等 N 人」，一个名字都列不出时只写人数。
 *  members 缺失、不是数组或为空时返回 false，调用方显示服务端的 text。
 */
bool FormatMemberJoin(const Json::Value& args, std::string& out)
{
    if (!args.isObject() || !args["members"].isArray() || args["members"].empty())
        return false;

    const Json::Value& members = args["members"];
    const int64_t self = GetSystemTipSelfUserId();
    const int64_t total = static_cast<int64_t>(members.size());
    const std::string separator = TRC8(u8"入群提示里新成员名字之间的分隔符", u8"、");

    //新成员这一段：本人单独记下，其他人按顺序取带昵称的
    bool selfIn = false;
    std::string others;
    int64_t listed = 0;
    for (Json::ArrayIndex i = 0; i < members.size(); ++i)
    {
        const Json::Value& m = members[i];
        if (!m.isObject())
            continue;
        if (self != 0 && GetIdValue(m["userid"]) == self)
        {
            selfIn = true;
            continue;
        }
        const std::string nickname = GetStringArg(m, "nickname");
        if (nickname.empty())
            continue;
        if (!others.empty())
            others += separator;
        others += TRF8(u8"「{0}」", nickname);
        ++listed;
    }
    std::string who = others;
    if (selfIn)
    {
        ++listed;
        if (others.empty())
            who = TRC8(u8"入群提示里指代本人的新成员", u8"你");
        else
            who = TRF8(u8"你和{0}", others);
    }

    //邀请人：没有 inviterid 表示自由加群或本人申请经审批；本人是邀请人时写「你邀请」；取不到昵称时写「有人」
    const int64_t inviterId = args.isMember("inviterid") ? GetIdValue(args["inviterid"]) : 0;
    const bool hasInviter = (inviterId != 0);
    const bool inviterIsSelf = hasInviter && self != 0 && inviterId == self;
    const std::string inviterNickname = GetStringArg(args, "inviter");
    const std::string inviter = inviterNickname.empty()
                                ? TRC8(u8"入群提示里取不到名字的邀请人", u8"「有人」")
                                : TRF8(u8"「{0}」", inviterNickname);

    //一个名字都列不出：只写人数，整句各用一条模板，数字两侧的空格写在模板里
    if (listed == 0)
    {
        if (!hasInviter)
            out = TRF8(u8"{0} 位新成员加入了群聊", total);
        else if (inviterIsSelf)
            out = TRF8(u8"你邀请 {0} 位新成员加入了群聊", total);
        else
            out = TRF8(u8"{0}邀请 {1} 位新成员加入了群聊", inviter, total);
        return true;
    }

    if (listed < total)
        who = TRF8(u8"{0}等 {1} 人", who, total);
    if (!hasInviter)
    {
        //本人自己入群（自由加群、申请经审批）单独用一条整句模板：「你」在句首，有的语言要换成句首的写法
        if (selfIn && total == 1)
            out = TR8(u8"你加入了群聊");
        else
            out = TRF8(u8"{0}加入了群聊", who);
    }
    else if (inviterIsSelf)
        out = TRF8(u8"你邀请{0}加入了群聊", who);
    else
        out = TRF8(u8"{0}邀请{1}加入了群聊", inviter, who);
    return true;
}

//开启 / 关闭仅通知模式（原称全员禁言，docs/群管理接入方案.md §14）：操作人是本人时写「你」，
//其他人写「张三」，昵称取不到时用不带名字的写法
std::string FormatMuteAll(const Json::Value& args, bool on)
{
    const int64_t self = GetSystemTipSelfUserId();
    const int64_t operatorId = (args.isObject() && args.isMember("operatorid")) ? GetIdValue(args["operatorid"]) : 0;
    const std::string op = GetStringArg(args, "operator");
    if (self != 0 && operatorId == self)
        return on ? TR8(u8"你开启了仅通知模式，仅群主和管理员可以发言") : TR8(u8"你关闭了仅通知模式");
    if (!op.empty())
        return on ? TRF8(u8"「{0}」开启了仅通知模式，仅群主和管理员可以发言", op) : TRF8(u8"「{0}」关闭了仅通知模式", op);
    return on ? TR8(u8"已开启仅通知模式，仅群主和管理员可以发言") : TR8(u8"已关闭仅通知模式");
}

//群机器人的添加、删除与在本群关闭 / 打开（docs/机器人方案.md §6.4）。这些操作只有群主能做，
//操作人昵称取不到时写「群主」；external 为 "1" 表示该机器人会把 @ 它的消息发到外部服务，须提醒群成员
std::string FormatBotTip(const Json::Value& args, const std::string& tipKind)
{
    std::string op = GetStringArg(args, "operator");
    if (op.empty())
        op = TR8(u8"群主");
    const std::string bot = GetStringArg(args, "bot");
    if (tipKind == kSystemTipBotAdded)
    {
        if (GetStringArg(args, "external") == "1")
            return TRF8(u8"{0} 添加了机器人「{1}」，@ 它的消息会发送到外部服务", op, bot);
        return TRF8(u8"{0} 添加了机器人「{1}」", op, bot);
    }
    if (tipKind == kSystemTipBotRemoved)
        return TRF8(u8"{0} 删除了机器人「{1}」", op, bot);
    if (tipKind == kSystemTipBotOff)
        return TRF8(u8"{0} 在本群关闭了机器人「{1}」", op, bot);
    return TRF8(u8"{0} 在本群打开了机器人「{1}」", op, bot);
}

} // namespace

bool FormatSystemTip(const std::string& tipKind, const Json::Value& tipArgs, std::string& out)
{
    if (tipKind == kSystemTipMemberLeave)
    {
        out = FormatMemberLeave(tipArgs);
        return true;
    }
    if (tipKind == kSystemTipOwnerTransfer)
    {
        out = FormatOwnerTransfer(tipArgs);
        return true;
    }
    if (tipKind == kSystemTipMuteAllOn || tipKind == kSystemTipMuteAllOff)
    {
        out = FormatMuteAll(tipArgs, tipKind == kSystemTipMuteAllOn);
        return true;
    }
    if (tipKind == kSystemTipAnnouncement)
    {
        out = FormatAnnouncement(tipArgs);
        return true;
    }
    if (tipKind == kSystemTipGroupWelcome)
    {
        out = FormatGroupWelcome(tipArgs);
        return true;
    }
    if (tipKind == kSystemTipRelayJoin)
    {
        out = TRF8(u8"{0}参与了{1}", RelayActor(tipArgs), RelayCard(tipArgs));
        return true;
    }
    if (tipKind == kSystemTipRelayModify)
    {
        out = TRF8(u8"{0}修改了在{1}中的内容", RelayActor(tipArgs), RelayCard(tipArgs));
        return true;
    }
    if (tipKind == kSystemTipRelayWithdraw)
    {
        out = TRF8(u8"{0}撤回了在{1}中的内容", RelayActor(tipArgs), RelayCard(tipArgs));
        return true;
    }
    if (tipKind == kSystemTipRelayClose)
    {
        out = TRF8(u8"{0}结束了{1}，共 {2} 人参与", RelayActor(tipArgs), RelayCard(tipArgs),
                   GetCountArg(tipArgs, "count"));
        return true;
    }
    if (tipKind == kSystemTipRelayUpdate)
    {
        out = TRF8(u8"{0}更新了{1}", RelayActor(tipArgs), RelayCard(tipArgs));
        return true;
    }
    if (tipKind == kSystemTipScreenShareEnd)
    {
        out = FormatScreenShareEnd(tipArgs);
        return true;
    }
    if (tipKind == kSystemTipMemberJoin)
        return FormatMemberJoin(tipArgs, out);
    if (tipKind == kSystemTipBotAdded || tipKind == kSystemTipBotRemoved || tipKind == kSystemTipBotOff
        || tipKind == kSystemTipBotOn)
    {
        out = FormatBotTip(tipArgs, tipKind);
        return true;
    }
    return false;
}

void SetSystemTipSelfUserId(int64_t userid)
{
    g_selfUserId.store(userid);
}

int64_t GetSystemTipSelfUserId()
{
    return g_selfUserId.load();
}

bool FormatSystemTipFromContent(const Json::Value& content, std::string& out)
{
    if (!content.isObject())
        return false;
    const Json::Value& kind = content["tipkind"];
    const Json::Value& args = content["tipargs"];
    if (!kind.isString() || !args.isObject())
        return false;
    return FormatSystemTip(kind.asString(), args, out);
}

bool FormatSystemTipJson(const std::string& tipKind, const std::string& tipArgsJson, std::string& out)
{
    Json::Value args(Json::objectValue);
    Json::Value parsed;
    if (ParseObject(tipArgsJson, parsed))
        args = parsed;
    return FormatSystemTip(tipKind, args, out);
}

bool IsMemberJoinTipForUser(const std::string& tipKind, const std::string& tipArgsJson, int64_t userid)
{
    if (userid <= 0 || tipKind != kSystemTipMemberJoin)
        return false;
    Json::Value args;
    if (!ParseObject(tipArgsJson, args))
        return false;
    //只看新成员名单；邀请人记在 inviterid，不算
    const Json::Value& members = args["members"];
    if (!members.isArray())
        return false;
    for (Json::ArrayIndex i = 0; i < members.size(); ++i)
    {
        if (members[i].isObject() && GetIdValue(members[i]["userid"]) == userid)
            return true;
    }
    return false;
}

std::string FormatDurationClock(int64_t seconds)
{
    if (seconds < 0)
        seconds = 0;
    const long long hours = static_cast<long long>(seconds / kSecondsPerHour);
    const long long minutes = static_cast<long long>((seconds % kSecondsPerHour) / kSecondsPerMinute);
    const long long secs = static_cast<long long>(seconds % kSecondsPerMinute);

    char buf[kClockBufSize] = { 0 };
    if (hours > 0)
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lld:%02lld:%02lld", hours, minutes, secs);
    else
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02lld:%02lld", minutes, secs);
    return std::string(buf);
}

} // namespace net
