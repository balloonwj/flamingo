/**
 *  帖子群规则的实现，见 ThreadRules.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadRules.h"

#include "ThreadText.h"
#include "ThreadTypes.h"

namespace threadgroup {

ComposeCheck CheckCompose(int kind, const std::wstring& title, const std::wstring& wireText, int imageCount,
                          int mentionCount)
{
    const bool allowTitle = (kind == compose_post);
    const bool allowImages = (kind != compose_comment);
    if (!allowTitle && !title.empty())
        return compose_not_allowed;
    if (!allowImages && imageCount > 0)
        return compose_not_allowed;
    if (CountChars(title) > kTitleMaxChars)
        return compose_title_too_long;
    if (CountChars(wireText) > TextLimitOf(kind))
        return compose_text_too_long;
    if (imageCount > kMaxImages)
        return compose_too_many_images;
    if (mentionCount > kMaxMentions)
        return compose_too_many_mentions;
    //空内容：没有文字（只有空白也算）也没有图片；帖子只有标题没有正文与图片同样算空，与服务端一致
    if (IsBlank(wireText) && imageCount <= 0)
        return compose_empty;
    return compose_ok;
}

int TextLimitOf(int kind)
{
    switch (kind)
    {
    //帖子正文
    case compose_post:
        return kPostTextMaxChars;

    //楼主补充
    case compose_addendum:
        return kAddendumTextMaxChars;

    //留言与回复，以及不认识的种类（按最严的上限）
    default:
        return kCommentTextMaxChars;
    }
}

bool CanEditPost(int64_t authorId, int64_t myId)
{
    return myId > 0 && authorId == myId;
}

bool CanDeleteContent(int64_t authorId, int64_t myId, int role)
{
    return (myId > 0 && authorId == myId) || role >= thread_role_admin;
}

bool CanPin(int role)
{
    return role >= thread_role_admin;
}

bool CanManageGroup(int role)
{
    return role >= thread_role_admin;
}

int ScrollToReveal(int itemTop, int itemBottom, int scrollPos, int viewHeight, int maxScroll)
{
    int pos = scrollPos;
    const int viewBottom = scrollPos + viewHeight;
    if (itemBottom - itemTop >= viewHeight || itemTop < scrollPos)
    {
        //比视野还高、或上边在视野之上：顶边对齐，至少让开头露出来
        pos = itemTop;
    }
    else if (itemBottom > viewBottom)
    {
        //在视野下方或下边被截掉：底边对齐视野底边
        pos = itemBottom - viewHeight;
    }
    if (pos > maxScroll)
        pos = maxScroll;
    if (pos < 0)
        pos = 0;
    return pos;
}

bool CanMentionAll(int role, int atAllScope)
{
    if (role >= thread_role_admin)
        return true;
    //普通成员：群设为「仅群主和管理员」时不可；不是成员（-1）一律不可
    return role == thread_role_member && atAllScope != thread_at_all_owner_admin;
}

std::vector<int> HeaderButtonsFor(int role)
{
    std::vector<int> buttons;
    buttons.push_back(thread_header_members);
    buttons.push_back(thread_header_group_info);
    if (CanManageGroup(role))
        buttons.push_back(thread_header_group_manage);
    return buttons;
}

bool CanAddAddendum(int64_t postAuthorId, int64_t myId, int addendumCount)
{
    return myId > 0 && postAuthorId == myId && addendumCount < kMaxAddenda;
}

bool CanSpeak(int role, bool mutedForMe)
{
    return role >= thread_role_member && !mutedForMe;
}

bool IsValidReaction(int reaction)
{
    return reaction == kReactionLike
           || (reaction >= kReactionFaceBase && reaction <= kReactionFaceBase + kReactionMaxFaceId);
}

int ReactionOfFace(int faceId)
{
    return kReactionFaceBase + faceId;
}

int FaceOfReaction(int reaction)
{
    if (reaction >= kReactionFaceBase && reaction <= kReactionFaceBase + kReactionMaxFaceId)
        return reaction - kReactionFaceBase;
    return -1;
}

} // namespace threadgroup
