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

namespace balloonhistory {

namespace {

//菜单各项的显示文案。集中放在这里而不是散在各处，改文案时只动一处；
//「Pin / 取消 Pin」两条互为反面，由 m_pinned 挑一条。
const wchar_t* const kLabelCopy           = L"复制";
const wchar_t* const kLabelCopyImage      = L"复制图片";
const wchar_t* const kLabelSaveImageAs    = L"图片另存为";
const wchar_t* const kLabelOpenFile       = L"打开文件";
const wchar_t* const kLabelOpenFolder     = L"打开所在文件夹";
const wchar_t* const kLabelOpenFolderRoot = L"打开文件夹";
const wchar_t* const kLabelSaveFileAs     = L"另存为";
const wchar_t* const kLabelDownload       = L"下载";
const wchar_t* const kLabelCancelDownload = L"取消下载";
const wchar_t* const kLabelLocate         = L"定位到聊天窗";
const wchar_t* const kLabelViewContext    = L"查看上下文";
const wchar_t* const kLabelForward        = L"转发";
const wchar_t* const kLabelMultiSelect    = L"多选";
const wchar_t* const kLabelPin            = L"Pin";
const wchar_t* const kLabelUnpin          = L"取消 Pin";
const wchar_t* const kLabelDelete         = L"删除";

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
//按当前的分组（通用组里的「定位到聊天窗」恒有）末行不会是分隔线，这里是道防线：
//日后若在末尾再加一组、而那组整组消失，靠它兜住"分隔线结尾"这条不变式，不必再去
//记得改收尾逻辑。
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
        AppendItem(out, kHistoryRowMenuCancelDownload, kLabelCancelDownload);
        return;
    }

    //文本 / 链接：v2 协议没有单独的链接类型，一条带 URL 的正文仍是
    //::message_type_text（见 net/Protocol.h 的 enum message_type），故这一支同时
    //覆盖两者。整条正文复制走窗口侧的 flamingo::CopyTextToClipboard，
    //内联表情按 docs/消息记录内容交互方案.md §10-7 写成“[表情]”。
    if (ctx.m_msgType == ::message_type_text)
    {
        AppendItem(out, kHistoryRowMenuCopy, kLabelCopy);
        return;
    }

    if (IsHistoryRowImageType(ctx.m_msgType))
    {
        if (ctx.m_hasLocalFile)
        {
            //三项都要拿到本地原图才做得了：复制进剪贴板、另存到别处、在资源管理器里定位。
            AppendItem(out, kHistoryRowMenuCopyImage, kLabelCopyImage);
            AppendItem(out, kHistoryRowMenuSaveImageAs, kLabelSaveImageAs);
            AppendItem(out, kHistoryRowMenuOpenFolder, kLabelOpenFolder);
        }
        else
        {
            //本地没有原图：消息记录窗只存了 36px 缩略图，要做别的先得把原图拉回来。
            AppendItem(out, kHistoryRowMenuDownload, kLabelDownload);
        }
        return;
    }

    if (IsHistoryRowFileType(ctx.m_msgType))
    {
        if (ctx.m_hasLocalFile)
        {
            //「打开文件」放第一位：本地文件在的时候，用户十有八九是想直接打开它。
            AppendItem(out, kHistoryRowMenuOpenFile, kLabelOpenFile);
            AppendItem(out, kHistoryRowMenuOpenFolder, kLabelOpenFolder);
            AppendItem(out, kHistoryRowMenuSaveFileAs, kLabelSaveFileAs);
        }
        else
        {
            AppendItem(out, kHistoryRowMenuDownload, kLabelDownload);
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
            AppendItem(out, kHistoryRowMenuOpenFolderRoot, kLabelOpenFolderRoot);
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
    //服务端生成的三类记录条：文件夹传输记录、通话记录、系统提示，都不是用户发出的内容
    if (msgType == ::message_type_folder || msgType == ::message_type_rtc
        || msgType == ::message_type_system)
    {
        return false;
    }
    return true;
}

std::vector<HistoryRowMenuItem> BuildHistoryRowMenuModel(const HistoryRowMenuContext& ctx)
{
    std::vector<HistoryRowMenuItem> model;

    AppendContentGroup(model, ctx);

    //内容组与通用组之间隔一条线：上面那组是"拿这条消息的内容做点什么"，下面那组是
    //"拿这条消息本身做点什么"，两件事。内容组为空时 AppendSeparator 自己会跳过。
    AppendSeparator(model);

    //「定位到聊天窗」是本窗口的主操作 —— 用户在消息记录里翻到某条，最常做的就是
    //跳回聊天窗看上下文，所以任何类型的行都给，且排在通用组头一位。
    AppendItem(model, kHistoryRowMenuLocate, kLabelLocate);

    //「查看上下文」：与「定位到聊天窗」同为"看这条前后说了什么"，区别是留在本窗口里看，
    //故紧跟其后。只在有筛选条件时给（没有筛选时这一行本来就在上下文里）
    if (ctx.m_canViewContext)
    {
        AppendItem(model, kHistoryRowMenuViewContext, kLabelViewContext);
    }

    //「转发」紧挨在「Pin」之前，与聊天窗气泡菜单的排法一致：两者都是"把这条消息拿到别处去用"
    if (CanForwardHistoryRow(ctx.m_msgType, ctx.m_msgId))
    {
        AppendItem(model, kHistoryRowMenuForward, kLabelForward);
    }

    //「多选」紧跟「转发」：进入多选后最常做的就是合并转发。msgid 未分配时不给 —— 这条既不能
    //转发也不能删除，选进来也做不了什么。点它进入多选模式并预先选中这一条
    if (ctx.m_msgId > kHistoryRowMsgIdUnassigned)
    {
        AppendItem(model, kHistoryRowMenuMultiSelect, kLabelMultiSelect);
    }

    //Pin 项：msgid 未分配时整项不出现（不是置灰）。文案随当前是否已钉住取反；
    //「取消 Pin」的权限不在客户端预判，无权时由服务端拒绝并提示（理由见头文件）。
    if (ctx.m_msgId > kHistoryRowMsgIdUnassigned)
    {
        AppendItem(model, kHistoryRowMenuPin, ctx.m_pinned ? kLabelUnpin : kLabelPin);
    }

    //「删除」：不可撤销，放在最末、前面单独隔一条线，避免手滑误点。msgid 未分配时整项
    //不出现 —— 服务端还没有这条消息，删除请求必被判非法（与 Pin 同一处理）
    if (ctx.m_msgId > kHistoryRowMsgIdUnassigned)
    {
        AppendSeparator(model);
        AppendItem(model, kHistoryRowMenuDelete, kLabelDelete);
    }

    TrimTrailingSeparator(model);
    return model;
}

} // namespace balloonhistory
