/**
 *  帖子群的规则（纯函数，docs/帖子群方案.md 4.2~4.4、6.4）：发帖、补充、留言前的本地校验，与界面上各操作入口的显隐判定。
 *
 *  本地校验与服务端 ThreadProtocol::validateBody 的口径一致（字数按线上正文的码点计，表情编码 /f["n"] 也算字数），
 *  先在客户端拦下明显不合法的内容，免得发出去再被服务端以 1303 拒绝；服务端仍会再校验一次。
 *  权限判定与服务端 canEditPost / canDeleteContent / canPin / canAddAddendum 一致，只决定按钮显不显示。
 *
 *  典型用法：
 *      if (threadgroup::CheckCompose(threadgroup::compose_post, title, wire, images) != threadgroup::compose_ok) { ... }
 *      bool showDelete = threadgroup::CanDeleteContent(post.m_author, myId, role);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace threadgroup {

//要校验的内容种类
enum ComposeKind
{
    compose_post     = 1,   //帖子：可有标题，正文 5000 字，最多 9 张图
    compose_addendum = 2,   //楼主补充：没有标题，正文 2000 字，最多 9 张图
    compose_comment  = 3,   //留言与回复：没有标题与图片，正文 1000 字
};

//本地校验的结果
enum ComposeCheck
{
    compose_ok              = 0,   //合法
    compose_empty           = 1,   //没有文字（只有空白也算）也没有图片
    compose_title_too_long  = 2,   //标题超过 60 个字
    compose_text_too_long   = 3,   //正文超过该种类的上限
    compose_too_many_images = 4,   //图片超过 9 张
    compose_not_allowed     = 5,   //该种类不允许的内容：补充或留言带了标题，留言带了图片
    compose_too_many_mentions = 6, //@ 的人超过 50 个
};

/**
 *  校验一份要发出的内容。
 *  @param kind        内容种类，取值见 ComposeKind
 *  @param title       标题（只有帖子可以非空）
 *  @param wireText    线上正文（表情已编码为 /f["n"]）
 *  @param imageCount  图片张数
 *  @param mentionCount @ 的人数
 *  @return 校验结果，取值见 ComposeCheck
 */
ComposeCheck CheckCompose(int kind, const std::wstring& title, const std::wstring& wireText, int imageCount,
                          int mentionCount);

/**
 *  该种类正文的字数上限。
 */
int TextLimitOf(int kind);

//只有作者能修改帖子
bool CanEditPost(int64_t authorId, int64_t myId);

/**
 *  能否删除帖子、补充、留言或回复：作者本人，或群主、管理员。
 *  @param role 本人在群里的角色，取值见 ThreadGroupRole
 */
bool CanDeleteContent(int64_t authorId, int64_t myId, int role);

//只有群主、管理员能置顶
bool CanPin(int role);

//帖子群窗口头部右侧的按钮（docs/帖子群方案.md §15.1 第 3 条），HeaderButtonsFor 按从左到右的顺序给出
enum ThreadHeaderButton
{
    //「成员」：在窗口右侧滑出群成员面板；所有成员都有
    thread_header_members = 1,
    //「群资料」：经宿主打开现有的群资料窗口；所有成员都有
    thread_header_group_info = 2,
    //「群管理」：经宿主打开现有的群管理窗口（没有「群公告」页）；只有群主与管理员有
    thread_header_group_manage = 3,
};

//能否打开群管理窗口（头部有没有「群管理」按钮）：只有群主与管理员
bool CanManageGroup(int role);

/**
 *  本人能否 @所有人（@ 成员列表里有没有「所有人」，docs/帖子群方案.md §15.1 第 1 条）：与服务端
 *  GroupProtocol::canMentionAll 同一条规则——群主与管理员恒可，普通成员只在群设置为「所有人」时可以；不是成员时不可。
 *  @param role       本人角色，取值见 ThreadGroupRole
 *  @param atAllScope 群设置「谁可以 @ 全体」，取值见 ThreadAtAllScope
 */
bool CanMentionAll(int role, int atAllScope);

/**
 *  让详情里的一条留言或回复完整可见所需的滚动位置（本人发的留言、回复成功后，docs/帖子群方案.md §15.1 第 5 条）。
 *  坐标都相对详情内容的顶部，单位：像素。
 *  @param itemTop    条目的上边
 *  @param itemBottom 条目的下边（不含）
 *  @param scrollPos  当前的滚动位置（视野上边）
 *  @param viewHeight 视野的高度
 *  @param maxScroll  最大滚动位置（内容高度减视野高度，不小于 0）
 *  @return 新的滚动位置：已完整可见时不变；在视野下方（或下边超出视野）时条目底边对齐视野底边；在视野上方（或上边超出）
 *          时顶边对齐；条目比视野还高时顶边对齐。结果夹在 [0, maxScroll] 之内
 */
int ScrollToReveal(int itemTop, int itemBottom, int scrollPos, int viewHeight, int maxScroll);

/**
 *  帖子群窗口头部右侧应显示的按钮，从左到右。
 *  @param role 本人角色，取值见 ThreadGroupRole；不是成员或不知道时按普通成员处理
 *  @return ThreadHeaderButton 的取值
 */
std::vector<int> HeaderButtonsFor(int role);

/**
 *  是否显示「追加补充」：只有楼主，且补充还没满 20 段。
 */
bool CanAddAddendum(int64_t postAuthorId, int64_t myId, int addendumCount);

/**
 *  本人此刻能否发帖、留言、补充：是群成员且没有被禁止发言。
 *  @param role        本人角色，取值见 ThreadGroupRole
 *  @param mutedForMe  宿主报告的「此刻不能发言」（被禁言、仅通知模式下不是管理员、入群必读未确认）
 */
bool CanSpeak(int role, bool mutedForMe);

/**
 *  判断互动编号是否合法：0（赞）或 1000~1999（表情回应）。
 */
bool IsValidReaction(int reaction);

/**
 *  表情编号 → 互动编号（1000 + 编号）。
 */
int ReactionOfFace(int faceId);

/**
 *  互动编号 → 表情编号；不是表情回应（是赞或不合法）时返回 -1。
 */
int FaceOfReaction(int reaction);

} // namespace threadgroup
