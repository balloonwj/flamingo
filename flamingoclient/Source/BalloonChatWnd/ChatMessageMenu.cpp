/**
 *  ChatMessageMenu.cpp
 *
 *  ChatMessageMenu.h 的实现：气泡右键菜单的行模型与「图片另存为」默认文件名。
 *  纯逻辑、不碰 Win32 菜单与文件系统，实现说明见头文件各函数的声明处。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#include "ChatMessageMenu.h"

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
void AppendSeparator(std::vector<ChatMessageMenuEntry>& out)
{
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

std::vector<ChatMessageMenuEntry> BuildChatMessageMenuModel(int msgKind,
                                                            int imageState,
                                                            bool hasLocalPath,
                                                            bool canRecall,
                                                            bool canPin,
                                                            bool pinned)
{
    std::vector<ChatMessageMenuEntry> model;

    AppendItem(model, kChatMsgMenuCopy, L"复制");

    //文件夹行：本地气泡，没有服务端 msgid，「引用回复」「转发」「Pin」「撤回」四项对它
    //都无从谈起（见头文件 kChatMenuKindFolder 的注释），故只留下这三项就收尾。
    if (msgKind == kChatMenuKindFolder)
    {
        AppendItem(model, kChatMsgMenuDelete, L"删除");
        AppendSeparator(model);
        AppendItem(model, kChatMsgMenuMultiSelect, L"多选");
        return model;
    }

    //图片专属的两项紧跟在「复制」之后：都属于"拿这张图做点什么"，放在一起最顺手。
    if (ShouldShowImageActions(msgKind, imageState, hasLocalPath))
    {
        AppendItem(model, kChatMsgMenuSaveImageAs, L"图片另存为");
        AppendItem(model, kChatMsgMenuResendImage, L"再次发送");
    }

    AppendItem(model, kChatMsgMenuReply,   L"引用回复");
    AppendItem(model, kChatMsgMenuForward, L"转发");

    //Pin 紧跟在「转发」之后：三者都属于"把这条消息挪到别处去用"，放一起最顺手。
    //文案随当前状态取反；服务器还没给这条消息分配 msgid 时整项不出现（发过去必被
    //判 message_not_exist），与「撤回」超时即整项消失同一种处理。
    if (canPin)
    {
        AppendItem(model, kChatMsgMenuPin, pinned ? L"取消 Pin" : L"Pin");
    }
    AppendSeparator(model);

    //撤回超时（或不是自己发的）时整项不出现，而不是置灰 —— 免得用户点了才被
    //服务端以 error_code_recall_timeout 拒绝。
    if (canRecall)
    {
        AppendItem(model, kChatMsgMenuRecall, L"撤回");
    }
    AppendItem(model, kChatMsgMenuDelete, L"删除");
    AppendSeparator(model);
    AppendItem(model, kChatMsgMenuMultiSelect, L"多选");

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
