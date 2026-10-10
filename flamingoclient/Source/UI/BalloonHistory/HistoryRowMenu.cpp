/**
 *  HistoryRowMenu.cpp
 *
 *  HistoryRowMenu.h 的实现：「消息记录」窗口列表行右键菜单的行模型。
 *  纯逻辑 —— 不建 Win32 / DuiMenu 菜单、不查库、不碰磁盘，行为约定与各项的出现
 *  条件写在头文件的声明处，这里只补"为什么这么排"。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#include "HistoryRowMenu.h"

#include "../../net/Protocol.h"   // ::message_type_*（全局命名空间）
#include "../../Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

namespace balloonhistory {

namespace {

//菜单各项的显示文案。集中放在这里而不是散在各处，改文案时只动一处；
//「Pin / 取消 Pin」两条互为反面，由 m_pinned 挑一条。
const wchar_t* const kLabelCopy           = TR_NOOP(L"复制");
const wchar_t* const kLabelCopyImage      = TR_NOOP(L"复制图片");
const wchar_t* const kLabelSaveImageAs    = TR_NOOP(L"图片另存为");
const wchar_t* const kLabelOpenFile       = TR_NOOP(L"打开文件");
const wchar_t* const kLabelOpenFolder     = TR_NOOP(L"打开所在文件夹");
const wchar_t* const kLabelOpenFolderRoot = TR_NOOP(L"打开文件夹");
const wchar_t* const kLabelSaveFileAs     = TR_NOOP(L"另存为");
const wchar_t* const kLabelDownload       = TR_NOOP(L"下载");
const wchar_t* const kLabelCancelDownload = TR_NOOP(L"取消下载");
const wchar_t* const kLabelLocate         = TR_NOOP(L"定位到聊天窗");
const wchar_t* const kLabelViewContext    = TR_NOOP(L"查看上下文");
const wchar_t* const kLabelForward        = TR_NOOP(L"转发");
const wchar_t* const kLabelMultiSelect    = TR_NOOP(L"多选");
const wchar_t* const kLabelPin            = TR_NOOP(L"Pin");
const wchar_t* const kLabelUnpin          = TR_NOOP(L"取消 Pin");
const wchar_t* const kLabelDelete         = TR_NOOP(L"删除");
const wchar_t* const kLabelClearConversation = TR_NOOP(L"清空聊天记录");

//往行模型里追加一个普通菜单项。
void AppendItem(std::vector<HistoryRowMenuItem>& out, HistoryRowMenuId id,
                const wchar_t* label)
{
    HistoryRowMenuItem item;
    item.m_id          = id;
    item.m_label       = label;
    item.m_isSeparator = false;
    out.push_back(item);
}

//往行模型里追加一条分隔线。
//
//这里带着两道闸：模型还空着（分隔线会落在首行）、或上一行已经是分隔线（会连成
//两条），都直接返回不追加。之所以把判断放在追加处而不是留给调用方，是因为内容组
//本身会因消息类型而整组消失（语音等类型内容组为空），调用点根本无从预判要不要
//这条分隔线；集中在这里判，各调用点只管"想分组就调一下"。
//落在末行的那条由 TrimTrailingSeparator 收尾裁掉。
void AppendSeparator(std::vector<HistoryRowMenuItem>& out)
{
    if (out.empty())
    {
        return;
    }
    if (out.back().m_isSeparator)
    {
        return;
    }

    HistoryRowMenuItem item;
    item.m_isSeparator = true;
    out.push_back(item);
}

//裁掉可能落在末行的分隔线。
//
//「定位到聊天窗」之后的三组都可能整组为空：msgid 未分配时转发、多选、Pin、删除都不出现，
//调试入口没有「清空聊天记录」。这时最后追加的那条分隔线会落在末行，由这里裁掉。
void TrimTrailingSeparator(std::vector<HistoryRowMenuItem>& out)
{
    while (!out.empty() && out.back().m_isSeparator)
    {
        out.pop_back();
    }
}

//往行模型里追加"内容组"——随消息类型与本地文件 / 下载状态变化的那几项。
//该组可能为空（语音等尚未单独支持的类型）。
void AppendContentGroup(std::vector<HistoryRowMenuItem>& out, const HistoryRowMenuContext& ctx)
{
    //下载中优先于一切：这时本地还没有完整文件，「下载」「打开」「另存为」都无从
    //谈起，唯一有意义的操作是把它停下来。与用户确认的口径是"任意类型 + 正在下载"
    //都只给这一项 —— 实际上只有图片 / 文件行会进下载态，但模型按统一口径处理，
    //调用方即便传了个自相矛盾的组合也不会得到一份点了必然出错的菜单。
    if (ctx.m_downloading)
    {
        AppendItem(out, kHistoryRowMenuCancelDownload, TR(kLabelCancelDownload));
        return;
    }

    //文本 / 链接：v2 协议没有单独的链接类型，一条带 URL 的正文仍是
    //::message_type_text（见 net/Protocol.h 的 enum message_type），故这一支同时
    //覆盖两者。整条正文复制走窗口侧的 flamingo::CopyTextToClipboard，
    //内联表情按 docs/消息记录内容交互方案.md §10-7 写成“[表情]”。
    if (ctx.m_msgType == ::message_type_text)
    {
        AppendItem(out, kHistoryRowMenuCopy, TR(kLabelCopy));
        return;
    }

    if (IsHistoryRowImageType(ctx.m_msgType))
    {
        if (ctx.m_hasLocalFile)
        {
            //三项都要拿到本地原图才做得了：复制进剪贴板、另存到别处、在资源管理器里定位。
            AppendItem(out, kHistoryRowMenuCopyImage, TR(kLabelCopyImage));
            AppendItem(out, kHistoryRowMenuSaveImageAs, TR(kLabelSaveImageAs));
            AppendItem(out, kHistoryRowMenuOpenFolder, TR(kLabelOpenFolder));
        }
        else
        {
            //本地没有原图：消息记录窗只存了 36px 缩略图，要做别的先得把原图拉回来。
            AppendItem(out, kHistoryRowMenuDownload, TR(kLabelDownload));
        }
        return;
    }

    if (IsHistoryRowFileType(ctx.m_msgType))
    {
        if (ctx.m_hasLocalFile)
        {
            //「打开文件」放第一位：本地文件在的时候，用户十有八九是想直接打开它。
            AppendItem(out, kHistoryRowMenuOpenFile, TR(kLabelOpenFile));
            AppendItem(out, kHistoryRowMenuOpenFolder, TR(kLabelOpenFolder));
            AppendItem(out, kHistoryRowMenuSaveFileAs, TR(kLabelSaveFileAs));
        }
        else
        {
            AppendItem(out, kHistoryRowMenuDownload, TR(kLabelDownload));
        }
        return;
    }

    if (IsHistoryRowFolderType(ctx.m_msgType))
    {
        //文件夹传输记录：服务端只记了动作、没记内容，因此<u>没有</u>下载与另存为这两条路。
        //本机登记过这次传输的根目录、且目录还在（由调用方查好后填进 m_hasLocalFile）时
        //给一个「打开文件夹」；换了台机器、或目录已被删改时内容组为空，只显示记录。
        if (ctx.m_hasLocalFile)
        {
            AppendItem(out, kHistoryRowMenuOpenFolderRoot, TR(kLabelOpenFolderRoot));
        }
        return;
    }

    //语音 / 大表情 / 位置 / 名片 / 投票 / 接龙 / 待办 / 抖动 / 合并转发 / 通话记录 /
    //系统提示等尚未在消息记录窗单独支持的类型：内容组为空，菜单只剩通用组。
    //宁可少给项，也不给一个点下去没反应的项。
}

} // anonymous namespace

bool IsHistoryRowImageType(int msgType)
{
    //截图是图片的子类型，与 HistoryView.cpp 的 ParseContentBrief 同口径。
    return msgType == ::message_type_image || msgType == ::message_type_screenshot;
}

bool IsHistoryRowFileType(int msgType)
{
    return msgType == ::message_type_file;
}

bool IsHistoryRowFolderType(int msgType)
{
    return msgType == ::message_type_folder;
}

bool CanForwardHistoryRow(int msgType, int64_t msgId)
{
    if (msgId <= kHistoryRowMsgIdUnassigned)
    {
        return false;
    }
    //服务端生成的三类记录条：文件夹传输记录、通话记录、系统提示，都不是用户发出的内容。
    //群接龙卡片不能单条转发（docs/群接龙方案.md §3.7），本窗口的「转发」是按逐条转发发出的。
    //文件共享卡片只给共享的成员看，转发会把它扩散给不是成员的人，服务端一律拒绝（docs/文件共享方案.md 6.1）。
    //屏幕共享卡片的房间只对原会话的成员开放，转发出去没有意义，服务端同样拒绝。
    //入群邀请卡片只对被邀请人有效，转发给别人点不了，服务端同样拒绝（docs/邀请入群方案.md §4.3）。
    //群公告卡片、群备忘卡片由服务端以操作人的名义生成，服务端同样拒绝转发（2026-10-05 起）。
    //云端录像卡片的观看权限按「发到过本人所在的会话」判定，转发出去的卡片别人看不了，服务端同样拒绝（2026-10-06 起）
    if (msgType == ::message_type_folder || msgType == ::message_type_rtc
        || msgType == ::message_type_system || msgType == ::message_type_relay
        || msgType == ::message_type_fileshare || msgType == ::message_type_screenshare
        || msgType == ::message_type_group_invite || msgType == ::message_type_announcement
        || msgType == ::message_type_memo || msgType == ::message_type_cloudrecord)
    {
        return false;
    }
    return true;
}

std::vector<HistoryRowMenuItem> BuildHistoryRowMenuModel(const HistoryRowMenuContext& ctx)
{
    std::vector<HistoryRowMenuItem> model;

    //分组（2026-10-03 与用户确认，与主面板、聊天窗几个右键菜单同一套规则）：内容 / 查看 /
    //转发与标记 / 删除这一条 / 清空整个会话，组与组之间一条分隔线，影响范围越大越靠后。
    //某一组整组为空时 AppendSeparator 自己会跳过，不会连出两条。

    //第一组：拿这条消息的内容做点什么，随消息类型变化，可能为空。
    AppendContentGroup(model, ctx);
    AppendSeparator(model);

    //第二组：看这条消息前后说了什么。「定位到聊天窗」是本窗口的主操作 —— 用户在消息记录里
    //翻到某条，最常做的就是跳回聊天窗看上下文，所以任何类型的行都给，且排在这一组头一位。
    AppendItem(model, kHistoryRowMenuLocate, TR(kLabelLocate));

    //「查看上下文」：与「定位到聊天窗」同为"看这条前后说了什么"，区别是留在本窗口里看，
    //故紧跟其后。只在有筛选条件时给（没有筛选时这一行本来就在上下文里）
    if (ctx.m_canViewContext)
    {
        AppendItem(model, kHistoryRowMenuViewContext, TR(kLabelViewContext));
    }

    AppendSeparator(model);

    //第三组：把这条消息拿到别处去用、或给它做标记，与聊天窗气泡菜单的排法一致。
    if (CanForwardHistoryRow(ctx.m_msgType, ctx.m_msgId))
    {
        AppendItem(model, kHistoryRowMenuForward, TR(kLabelForward));
    }

    //「多选」紧跟「转发」：进入多选后最常做的就是合并转发。msgid 未分配时不给 —— 这条既不能
    //转发也不能删除，选进来也做不了什么。点它进入多选模式并预先选中这一条
    if (ctx.m_msgId > kHistoryRowMsgIdUnassigned)
    {
        AppendItem(model, kHistoryRowMenuMultiSelect, TR(kLabelMultiSelect));
    }

    //Pin 项：msgid 未分配时整项不出现（不是置灰）。文案随当前是否已钉住取反；
    //「取消 Pin」的权限不在客户端预判，无权时由服务端拒绝并提示（理由见头文件）。
    //文件共享卡片不能 Pin：Pin 会把只给成员看的卡片展示给会话里的所有人，服务端一律拒绝。
    //屏幕共享卡片同样不能 Pin：共享结束后卡片就没有用处，服务端一律拒绝。入群邀请卡片、群公告卡片、群备忘卡片、
    //云端录像卡片同理，服务端一律拒绝
    if (ctx.m_msgId > kHistoryRowMsgIdUnassigned && ctx.m_msgType != ::message_type_fileshare
        && ctx.m_msgType != ::message_type_screenshare && ctx.m_msgType != ::message_type_group_invite
        && ctx.m_msgType != ::message_type_announcement && ctx.m_msgType != ::message_type_memo
        && ctx.m_msgType != ::message_type_cloudrecord)
    {
        AppendItem(model, kHistoryRowMenuPin, ctx.m_pinned ? TR(kLabelUnpin) : TR(kLabelPin));
    }

    AppendSeparator(model);

    //第四组：删除这一条。不可撤销，单独一组，避免手滑误点。msgid 未分配时整项不出现 ——
    //服务端还没有这条消息，删除请求必被判非法（与 Pin 同一处理）
    if (ctx.m_msgId > kHistoryRowMsgIdUnassigned)
    {
        AppendItem(model, kHistoryRowMenuDelete, TR(kLabelDelete));
    }
    AppendSeparator(model);

    //第五组：清空整个会话，与这一行是哪条消息无关，影响范围最大，排在最末
    if (ctx.m_canClearConversation)
    {
        AppendItem(model, kHistoryRowMenuClearConversation, TR(kLabelClearConversation));
    }

    TrimTrailingSeparator(model);
    return model;
}

} // namespace balloonhistory
