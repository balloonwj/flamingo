/**
 *  单聊文件夹气泡「状态 → 可用操作」映射实现，ChatFolderStateModel.cpp
 *  设计与约定见 ChatFolderStateModel.h，需求来源见 docs/单聊文件夹传输方案.md。
 *  balloonwj@qq.com   2026-08-13
 */
#include "ChatFolderStateModel.h"

//错误码取值的唯一出处。只在实现文件里引入，头文件保持零依赖 —— 这样气泡绘制
//那些编译单元包含本模块的头文件时，不会被动拖进整个协议头。
//（此前还有一条理由：协议头与 v1 的 net/Msg.h 枚举名冲突、不得同处一个编译单元。
// v1 协议头已于 2026-08-14 删除，那条限制不复存在。）
#include "../net/Protocol.h"

namespace balloonchatwnd {

bool FolderStateHasProgress(int state)
{
    //失败与已过保留期保留进度条：它停在中断处，用户据此能看出传到哪儿断的，
    //也能大致判断重试续传还剩多少要传。
    //用户取消<u>不</u>画进度条 —— 取消是用户自己的决定，这一笔已经作废，"传到哪儿了"
    //不再有参考价值；而且取消往往发生在刚开始、进度条停在 0% 上，看着像出了故障。
    //（2026-08-14 按实测反馈调整）
    return state == kFolderTransferring
        || state == kFolderFailed
        || state == kFolderExpired;
}

int FolderStateButtonCount(int state, bool fromMe)
{
    switch (state)
    {
    //邀请已发出、等待应答：接收端要当场决定收不收，给「接收 / 另存为 / 拒绝」三个 ——
    //「接收」直接落到本机默认的文件夹接收目录、不打断用户，「另存为」才弹选目录框，
    //口径与文件气泡一致；发送端在对方应答前可以撤回邀请，只给一个「取消」
    case kFolderInviting:
        return fromMe ? 1 : 3;

    //接收端建目录中、以及正式传字节中：两端都只有中止本次传输这一个操作
    case kFolderPreparing:
    case kFolderTransferring:
        return 1;

    //传输失败且仍在保留期内：只有发送端能发起重试续传（接收端全程只是看到气泡
    //从失败变回传输中，见方案文档 8.3），故接收端一个按钮都没有
    case kFolderFailed:
        return fromMe ? 1 : 0;

    //失败已过保留期、以及传输途中被取消：续传都已不可行，只剩「重新发送」这一条路，
    //而源目录在发送端手里，接收端同样没有可点的东西
    case kFolderExpired:
    case kFolderUserCanceled:
        return fromMe ? 1 : 0;

    //全部完成：两端各给一个「打开文件夹」
    case kFolderCompleted:
        return 1;

    //会话在开始传输之前就作废的四种情形：一个字节都没传，气泡上不再提供任何按钮，
    //用户若还想发，从工具栏重新选目录发起即可
    case kFolderRejected:
    case kFolderInviteCanceled:
    case kFolderPeerOffline:
    case kFolderTimeout:
        return 0;

    //未知状态：将来若有人加了新状态却漏改本映射，宁可不给操作，也不要给错操作
    default:
        return 0;
    }
}

int FolderButtonActions(int state, bool fromMe, int out[kMaxChatFolderActions])
{
    const int count = FolderStateButtonCount(state, fromMe);
    if (count <= 0)
        return 0;

    switch (state)
    {
    //邀请态：接收端从左到右依次是「接收 / 另存为 / 拒绝」——主动作「接收」靠左，
    //需要挑位置的「另存为」次之，否定动作「拒绝」放最右，与文件气泡的排布口径一致；
    //发送端的唯一操作是撤回邀请，动作用的同样是 kChatFolderCancel，宿主按 fromMe
    //决定发哪条信令
    case kFolderInviting:
        if (fromMe)
        {
            out[0] = kChatFolderCancel;
        }
        else
        {
            out[0] = kChatFolderAccept;
            out[1] = kChatFolderSaveAs;
            out[2] = kChatFolderReject;
        }
        break;

    //建目录中与传输中：唯一的操作是中止本次传输
    case kFolderPreparing:
    case kFolderTransferring:
        out[0] = kChatFolderCancel;
        break;

    //失败且仍在保留期内：给发送端一条续传的路。按钮本身是否置灰另由
    //FolderRetryEnabled 判定——保留期是否已过不在本函数的输入里
    case kFolderFailed:
        out[0] = kChatFolderRetry;
        break;

    //已过保留期：续传位点已随会话一起销毁，只能从零重发。
    //用户取消：取消可能来自接收端，直接续传会绕过对方刚刚作出的决定，故这里同样
    //不提供续传入口，只给「重新发送」——它会重新征求一次同意
    case kFolderExpired:
    case kFolderUserCanceled:
        out[0] = kChatFolderResend;
        break;

    //完成：两端都是「打开文件夹」，动作 id 相同，接收端开落地目录、发送端开源目录，
    //开哪个由宿主按 fromMe 决定，气泡这一层不必区分
    case kFolderCompleted:
        out[0] = kChatFolderOpenFolder;
        break;

    //其余状态的 count 已经是 0，正常走不到这里；真走到就按不给操作处理，
    //绝不让 out[] 留着未初始化的值被当成动作 id 用
    default:
        return 0;
    }

    return count;
}

bool FolderRetryEnabled(int state, bool fromMe, bool withinRetryWindow)
{
    //重试由发送端发起：接收端的气泡上根本没有这个按钮，也就无所谓可不可用
    if (!fromMe)
        return false;

    //只有失败态谈得上重试续传。kFolderExpired 落在这里返回 false，与它的语义
    //（保留期已过）一致；用户取消同样不给续传，理由见 FolderButtonActions
    if (state != kFolderFailed)
        return false;

    //保留期是服务端的会话保留时长（folderretrywindow，默认 10 分钟）：期内断点续传，
    //期满服务端已销毁会话，再点也只会被回 error_code_folder_session_expired
    return withinRetryWindow;
}

const wchar_t* FolderErrorText(int errcode)
{
    switch (errcode)
    {
    //接收端在气泡上点了「拒绝」，会话立即结束，一条中转连接都不会建立
    case error_code_folder_rejected:
        return L"对方拒绝接收这个文件夹";

    //发起邀请时对方无在线连接，或响铃 / 传输期间对方退出登录
    case error_code_folder_peer_offline:
        return L"对方不在线，文件夹传输已结束";

    //邀请在服务端配置的时限内无人应答，服务端自动结束了会话
    case error_code_folder_invite_timeout:
        return L"对方一直没有响应，邀请已超时";

    //文件数超过服务端配置的上限。上限值由服务端在应答里回带，具体数字由界面另行拼接，
    //本函数只给出通用措辞，不硬编码任何阈值
    case error_code_folder_too_many_files:
        return L"文件夹内的文件数超出服务器允许的上限";

    //总字节数超过服务端配置的上限，同样不在本函数里写死阈值
    case error_code_folder_too_large:
        return L"文件夹总大小超出服务器允许的上限";

    //接收端在传字节之前就建目录失败：目标位置无写权限、只读、路径过长、含非法字符等
    case error_code_folder_mkdir_fail:
        return L"对方创建目录失败，请确认保存位置可写";

    //接收端本机磁盘剩余空间装不下整个文件夹
    case error_code_folder_disk_full:
        return L"对方磁盘剩余空间不足";

    //接收端写盘失败：磁盘 I/O 出错、文件被别的程序占用、目标文件中途被删等
    case error_code_folder_write_fail:
        return L"对方保存文件失败，传输已中断";

    //发送端读源文件失败：文件在扫描之后被删除、被移走或被独占打开
    case error_code_folder_read_fail:
        return L"读取本地文件失败，文件可能已被移动或删除";

    //接收端校验收到的内容不符：字节数与清单不等，或 MD5 与发送端给出的不一致
    case error_code_folder_verify_fail:
        return L"文件内容校验不通过，传输的数据与原文件不一致";

    //任一条中转通道断链即整个会话失败，本功能不做自动重连，由用户决定是否重试
    case error_code_folder_relay_broken:
        return L"传输通道已断开，网络可能不稳定";

    //任一端在传输过程中点了「取消」，双方气泡都显示已取消
    case error_code_folder_canceled:
        return L"传输已取消";

    //会话已超出保留期（默认 10 分钟），续传位点作废，只能重新发起一次完整传输
    case error_code_folder_session_expired:
        return L"本次传输已超过可续传的时限，请重新发送";

    //整台服务器同时进行的文件夹传输会话数已达上限，新会话被拒绝
    case error_code_folder_server_busy:
        return L"服务器正忙，请稍后再试";

    //发起方或接收方参与中的文件夹传输笔数已达上限（服务端 foldermaxperuser，默认 5 笔，
    //发送与接收各算一笔；失败待重试的会话在保留期内仍占名额）。文案里不写死具体笔数 ——
    //上限由服务端配置决定，界面另有一处按当前配置拼提示，这里只给通用说法
    case error_code_folder_busy:
        return L"同时进行的文件夹传输已达上限，请等其中一笔结束后再试";

    //未收录的取值：包括 0、非文件夹域的错误码，以及今后服务端新增而本表尚未跟上的码。
    //给一句通用措辞，保证界面上不会出现空白的一行（那种情况下用户只会看到「传输失败」
    //四个字下面空着，无从判断发生了什么）
    default:
        return L"文件夹传输失败";
    }
}

int FolderStateFromRecord(int result, int errCode)
{
    switch (result)
    {
    //接收端点了「拒绝」，会话立即结束
    case folder_state_rejected:
        return kFolderRejected;

    //发送端在对方应答前撤回了邀请
    case folder_state_canceled:
        return kFolderInviteCanceled;

    //发起邀请时对方无在线连接，或响铃期间对方退出登录
    case folder_state_peer_offline:
        return kFolderPeerOffline;

    //邀请在限定时间内无人应答，服务端自动结束了会话
    case folder_state_timeout:
        return kFolderTimeout;

    //失败态要按错误码再分三档：协议把「用户取消」与「会话已过保留期」都归进失败，
    //而气泡上这两种各有各的措辞与按钮，混成一档会让用户看到"传输失败"却找不到原因
    case folder_state_failed:
        if (errCode == error_code_folder_canceled)
            return kFolderUserCanceled;
        if (errCode == error_code_folder_session_expired)
            return kFolderExpired;
        return kFolderFailed;

    //全部文件传完并通过校验
    case folder_state_completed:
        return kFolderCompleted;

    //非终态（响铃中 / 传输中）与越界值：记录条按协议只会带终态，走到这里说明 content
    //是脏的或协议实现不一致。返回 0（不是合法状态）让气泡退化成无进度条无按钮，
    //而不是硬套一个看着像那么回事、实则不对的状态
    default:
        return 0;
    }
}

bool FolderInviteRejectShouldRemoveBubble(int errcode)
{
    //只有"对方不在线"这一种：服务端在收到邀请时就直接拒绝并返回，会话没建、消息记录
    //也没写，本端那条占位气泡撤掉之后翻历史不会再出现。其余拒绝理由都属于"这个文件夹
    //发不出去"，气泡留在原地转失败态才说得清原因。
    return errcode == error_code_folder_peer_offline;
}

} // namespace balloonchatwnd
