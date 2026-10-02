/**
 *  GroupSenderRefresh.h
 *
 *  群聊气泡的发送者显示信息（名字、头像字形、底色、真图头像路径）：按成员表解析，以及
 *  成员表到达后就地刷新已经渲染出来的气泡。
 *
 *  背景：群聊窗原先要等成员列表回来之后才去请求首屏历史，两次网络往返是串行的（实测
 *  本机也要多等约 100 毫秒）。现在两者并行请求，历史可能先于成员到达，这时对方消息先
 *  按备用信息（"用户<id>"）显示，成员到了再用 ApplyGroupSendersToList 把这些气泡的
 *  发送者信息换成真的。
 *
 *  解析规则原先在群聊窗里写了三份（历史文字消息、图片消息、实时消息），现统一到
 *  ResolveGroupSender。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（需要 CString）。
 *
 *  典型用法：
 *      //渲染一条对方消息
 *      GroupSenderRender found;
 *      bool hit = 按 userid 查成员表(found);
 *      GroupSenderRender r = ResolveGroupSender(userId, hit ? &found : NULL, 备用底色);
 *      //成员表到达后
 *      ApplyGroupSendersToList(list, &查成员表的回调, 回调上下文);
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include <stdint.h>
#include <string>

namespace balloonchatwnd {

class ChatMessageList;

/**
 *  一名发送者在群聊气泡里的显示信息。
 */
struct GroupSenderRender
{
    CString  m_name;        // 显示名（群名片优先，再账号昵称）；空表示成员表里没有可用名字
    CString  m_glyph;       // 头像字形；空表示按名字首字回退
    COLORREF m_color;       // 头像底色
    CString  m_imagePath;   // 真图头像本地路径；空表示画字形头像

    GroupSenderRender()
        : m_color(0)
    {
    }
};

/**
 *  得到一名发送者在气泡里的最终显示信息。
 *
 *  回退规则：成员表里查不到（found 为 NULL）或名字为空时用"用户<id>"；字形为空时取
 *  名字的第一个字；查不到时底色用 fallbackColor、没有真图头像。
 *
 *    userId：       发送者 userid。
 *    found：        成员表里查到的信息（借用，不持有）；查不到传 NULL。
 *    fallbackColor：查不到时使用的头像底色（调用方一般按 userid 从调色板取）。
 *  @return 可以直接填进消息的显示信息，名字与字形保证非空。
 */
GroupSenderRender ResolveGroupSender(int64_t userId, const GroupSenderRender* found,
                                     COLORREF fallbackColor);

/**
 *  按 userid 查成员显示信息的回调。
 *    ctx：   调用方透传的上下文（一般是群聊窗 this）。
 *    userId：要查的成员 userid。
 *    out：   [出参] 查到时填入该成员的显示信息。
 *  @return 查到返回 true；不在成员表里返回 false。
 */
typedef bool (*GroupSenderLookup)(void* ctx, int64_t userId, GroupSenderRender& out);

/**
 *  ContactBriefQuery —— 群聊窗向主面板同步查询通讯录里某人的昵称与头像（FMG_MSG_QUERY_CONTACT_BRIEF
 *  的 lParam）。已经退群或被移出的人不在成员表里，他以前发的消息靠它显示真名（BUG-49）。
 *  调用方在栈上构造、填好 m_userId 后 SendMessage；主面板就地填写其余字段，不转移所有权。
 */
struct ContactBriefQuery
{
    int64_t     m_userId;      // [入] 要查的 userid
    bool        m_found;       // [出] 通讯录缓存里有没有此人
    std::string m_nickname;    // [出] 账号昵称（UTF-8）
    int         m_facetype;    // [出] 头像类型（0 自动 / 1 字形 / 2 真图，见 AvatarSpec.h）
    std::string m_customface;  // [出] 头像标识（UTF-8）：字形为 "glyph|colorIdx"，真图为 fileid

    ContactBriefQuery()
        : m_userId(0)
        , m_found(false)
        , m_facetype(0)
    {
    }
};

/**
 *  依次在成员表、通讯录里查一名发送者的显示信息。
 *
 *  成员表命中时以成员信息为准（群名片优先），不再查通讯录；成员表里没有时（已经退群或被移出）
 *  再查通讯录，取账号昵称与头像 —— 这些人以前发的消息原先一律显示"用户<id>"（BUG-49）。两处都
 *  查不到返回 false，调用方再经 ResolveGroupSender 回退。
 *    userId：       要查的发送者 userid。
 *    memberLookup： 查本群成员表的回调；为 NULL 时跳过成员表。
 *    memberCtx：    透传给 memberLookup 的上下文。
 *    contactLookup：查通讯录的回调；为 NULL 时跳过通讯录（如调试入口没有宿主主面板）。
 *    contactCtx：   透传给 contactLookup 的上下文。
 *    out：          [出参] 查到时填入显示信息；查不到时不保证内容。
 *  @return 任一处查到返回 true。纯函数（副作用只取决于两个回调）。
 */
bool LookupGroupSenderChain(int64_t userId,
                            GroupSenderLookup memberLookup, void* memberCtx,
                            GroupSenderLookup contactLookup, void* contactCtx,
                            GroupSenderRender& out);

/**
 *  用成员表刷新列表里对方消息气泡的发送者显示信息。
 *
 *  只动别人发的、带发送者 userid 的气泡；自己发的、时间分隔行、撤回提示行都不动。成员表
 *  里查不到的发送者（已退群）保持原样，不会被清空。只改显示信息，不增删气泡、不改顺序，
 *  也不改滚动位置。有气泡被更新时重排并重绘一次列表。
 *
 *    list：  要刷新的消息列表。
 *    lookup：查成员显示信息的回调；为 NULL 时什么都不做。
 *    ctx：   透传给 lookup 的上下文。
 *  @return 被更新的气泡数。
 */
int ApplyGroupSendersToList(ChatMessageList& list, GroupSenderLookup lookup, void* ctx);

} // namespace balloonchatwnd
