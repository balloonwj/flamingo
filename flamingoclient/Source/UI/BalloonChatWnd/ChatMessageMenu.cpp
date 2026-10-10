/**
 *  ChatMessageMenu.cpp
 *
 *  ChatMessageMenu.h 的实现：气泡右键菜单的行模型与「图片另存为」默认文件名。
 *  纯逻辑、不碰 Win32 菜单与文件系统，实现说明见头文件各函数的声明处。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#include "ChatMessageMenu.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//往行模型里追加一个普通菜单项。
void AppendItem(std::vector<ChatMessageMenuEntry>& out, ChatMessageMenuId id,
                const wchar_t* label)
{
    ChatMessageMenuEntry e;
    e.id          = id;
    e.label       = label;
    e.isSeparator = false;
    out.push_back(e);
}

//往行模型里追加一条分隔线。
//往行模型里追加一条分隔线。模型还空着（分隔线会落在首行）、或上一行已经是分隔线（会连成两条）时
//不追加：合并卡片没有「复制」，第一组整组为空，调用点无从预判要不要这条分隔线
void AppendSeparator(std::vector<ChatMessageMenuEntry>& out)
{
    if (out.empty() || out.back().isSeparator)
    {
        return;
    }
    ChatMessageMenuEntry e;
    e.isSeparator = true;
    out.push_back(e);
}

//兜底文件名的主干与扩展名（缓存图没有原始文件名时用）。
const wchar_t* const kFallbackNameStem = L"IMG";
const wchar_t* const kFallbackNameExt  = L".png";

} // anonymous namespace

bool ShouldShowImageActions(int msgKind, int imageState, bool hasLocalPath)
{
    if (msgKind != kChatMenuKindImage)
    {
        return false;
    }
    if (imageState != kChatImageStateReady)
    {
        return false;
    }
    return hasLocalPath;
}

bool CanForwardMessage(int msgKind, long long msgid, long long fileMsgid)
{
    //接龙卡片转发到别的会话没有参与的意义，单条转发与逐条转发都不收；共享卡片只给成员看，不能转发；
    //屏幕共享卡片的房间只对原会话的成员开放，转发出去也加入不了
    if (msgKind == kChatMenuKindFolder || msgKind == kChatMenuKindTime || msgKind == kChatMenuKindRelay
        || msgKind == kChatMenuKindFileShare || msgKind == kChatMenuKindScreenShare
        || msgKind == kChatMenuKindGroupInvite)
    {
        return false;
    }
    return ForwardMsgidOf(msgid, fileMsgid) > 0;
}

bool CanMergeForwardMessage(int msgKind, long long msgid, long long fileMsgid)
{
    //接龙卡片可以作为一条「[接龙] 标题」进入合并记录
    if (msgKind == kChatMenuKindRelay)
    {
        return ForwardMsgidOf(msgid, fileMsgid) > 0;
    }
    return CanForwardMessage(msgKind, msgid, fileMsgid);
}

long long ForwardMsgidOf(long long msgid, long long fileMsgid)
{
    return (msgid > 0) ? msgid : fileMsgid;
}

std::vector<ChatMessageMenuEntry> BuildChatMessageMenuModel(int msgKind,
                                                            int imageState,
                                                            bool hasLocalPath,
                                                            bool canRecall,
                                                            bool canPin,
                                                            bool pinned,
                                                            bool canForward,
                                                            bool sendFailed,
                                                            bool canReedit,
                                                            bool canOcr,
                                                            int memoMenu)
{
    std::vector<ChatMessageMenuEntry> model;

    //共享卡片、屏幕共享卡片与入群邀请卡片：转发、Pin、撤回一律不出现（见头文件几个 kind 常量的注释），不论调用方怎么传
    if (msgKind == kChatMenuKindFileShare || msgKind == kChatMenuKindScreenShare
        || msgKind == kChatMenuKindGroupInvite)
    {
        canRecall = false;
        canPin = false;
        canForward = false;
    }

    //合并卡片、语音没有可复制的正文（复制出来只会是一个「[聊天记录]」「[语音]」占位），不给「复制」
    if (msgKind != kChatMenuKindMerged && msgKind != kChatMenuKindVoice)
    {
        AppendItem(model, kChatMsgMenuCopy, TR(L"复制"));
    }

    //分组（2026-10-03 与用户确认，与主面板几个右键菜单同一套规则）：取用内容 / 再发出去 /
    //标记与选择 / 撤回与删除，组与组之间一条分隔线，删除类动作永远在最后。

    //发送失败的行：服务端拒绝了这条消息、不认识它，「引用回复」「转发」「Pin」「撤回」四项都无从谈起；
    //文字消息多一项「重新编辑」，紧跟在「复制」之后（docs/敏感词方案.md 8.4）
    if (sendFailed)
    {
        if (canReedit)
        {
            AppendItem(model, kChatMsgMenuReedit, TR(L"重新编辑"));
        }
        //语音没有正文可编辑，改给「重新发送」：与点气泡左侧的红色感叹号相同，用原 clientmsgid 重发
        if (msgKind == kChatMenuKindVoice)
        {
            AppendItem(model, kChatMsgMenuResendVoice, TR(L"重新发送"));
        }
        AppendSeparator(model);
        AppendItem(model, kChatMsgMenuMultiSelect, TR(L"多选"));
        AppendSeparator(model);
        AppendItem(model, kChatMsgMenuDelete, TR(L"删除"));
        return model;
    }

    //文件夹行：本地气泡，没有服务端 msgid，「引用回复」「转发」「Pin」「撤回」四项对它
    //都无从谈起（见头文件 kChatMenuKindFolder 的注释），故只留下这三项就收尾。
    if (msgKind == kChatMenuKindFolder)
    {
        AppendSeparator(model);
        AppendItem(model, kChatMsgMenuMultiSelect, TR(L"多选"));
        AppendSeparator(model);
        AppendItem(model, kChatMsgMenuDelete, TR(L"删除"));
        return model;
    }

    //第一组：取用这条消息的内容。「图片另存为」紧跟「复制」：都是"把内容拿走"
    const bool imageActions = ShouldShowImageActions(msgKind, imageState, hasLocalPath);
    if (imageActions)
    {
        AppendItem(model, kChatMsgMenuSaveImageAs, TR(L"图片另存为"));
        //图片文字识别模块可用时（DLL 已加载）紧跟「图片另存为」；模块不可用时整项不出现
        if (canOcr)
        {
            AppendItem(model, kChatMsgMenuOcr, TR(L"图片文字识别"));
        }
    }
    //「添加到备忘」也是取用内容的一种，排在本组末尾；只对文字消息出现（docs/备忘与提醒方案.md §8）。
    //群聊里分成「我的备忘」「群备忘」两项，后者只给能新建群备忘的人
    if (msgKind == kChatMenuKindText)
    {
        if (memoMenu == kChatMemoMenuSingle)
        {
            AppendItem(model, kChatMsgMenuAddMemo, TR(L"添加到备忘"));
        }
        else if (memoMenu == kChatMemoMenuGroupMine || memoMenu == kChatMemoMenuGroupBoth)
        {
            AppendItem(model, kChatMsgMenuAddMemo, TR(L"添加到我的备忘"));
            if (memoMenu == kChatMemoMenuGroupBoth)
            {
                AppendItem(model, kChatMsgMenuAddGroupMemo, TR(L"添加到群备忘"));
            }
        }
    }
    AppendSeparator(model);

    //第二组：把这条消息再发出去。「转发」没有服务器 msgid 时整项不出现：转发请求必被服务端判非法，
    //与「撤回」超时即整项消失同一种处理。「再次发送」把这张图作为一条新消息发到当前会话
    //入群邀请卡片不给「引用回复」：引用一张邀请卡片没有意义（见头文件 kChatMenuKindGroupInvite 的注释）
    if (msgKind != kChatMenuKindGroupInvite)
    {
        AppendItem(model, kChatMsgMenuReply, TR(L"引用回复"));
    }
    if (canForward)
    {
        AppendItem(model, kChatMsgMenuForward, TR(L"转发"));
    }
    if (imageActions)
    {
        AppendItem(model, kChatMsgMenuResendImage, TR(L"再次发送"));
    }
    AppendSeparator(model);

    //第三组：标记与选择。Pin 文案随当前状态取反；服务器还没给这条消息分配 msgid 时整项不出现
    //（发过去必被判 message_not_exist）
    if (canPin)
    {
        AppendItem(model, kChatMsgMenuPin, pinned ? TR(L"取消 Pin") : L"Pin");
    }
    AppendItem(model, kChatMsgMenuMultiSelect, TR(L"多选"));
    AppendSeparator(model);

    //第四组：撤回与删除，放在最后。撤回超时（或不是自己发的）时整项不出现，而不是置灰 ——
    //免得用户点了才被服务端以 error_code_recall_timeout 拒绝。
    if (canRecall)
    {
        AppendItem(model, kChatMsgMenuRecall, TR(L"撤回"));
    }
    AppendItem(model, kChatMsgMenuDelete, TR(L"删除"));

    return model;
}

std::wstring MakeImageSaveAsDefaultName(const std::wstring& imagePath, long long msgid)
{
    //取最后一个路径分隔符之后的那一段当文件名。两种分隔符都认：路径多数来自
    //Win32 接口（反斜杠），但拼接来源不止一处，正斜杠也可能混进来。
    std::wstring::size_type cut = imagePath.find_last_of(L"\\/");
    std::wstring name = (cut == std::wstring::npos) ? imagePath : imagePath.substr(cut + 1);

    //有扩展名才认为这个文件名对用户有意义。点号在开头的（如 ".gitignore"）不算
    //扩展名，那是没有主干的隐藏文件名，同样走兜底。
    const std::wstring::size_type dot = name.find_last_of(L'.');
    if (!name.empty() && dot != std::wstring::npos && dot > 0)
    {
        return name;
    }

    std::wstring fallback = kFallbackNameStem;
    if (msgid > 0)
    {
        fallback += L"_";
        fallback += std::to_wstring(msgid);
    }
    fallback += kFallbackNameExt;
    return fallback;
}

} // namespace balloonchatwnd
