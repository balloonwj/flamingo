/**
 *  聊天文件气泡「状态 → 可用操作」映射实现，ChatFileStateModel.cpp
 *  设计与约定见 ChatFileStateModel.h。
 *  balloonwj@qq.com   2026-08-03
 */
#include "ChatFileStateModel.h"

namespace balloonchatwnd {

bool FileStateHasProgress(int state)
{
    return state == kFileUploading || state == kFileDownloading
        || state == kFileFailed    || state == kFileCancelled;
}

bool FileCardShowsProgress(int state, bool direct)
{
    //在线直传被发送方取消：多半一格都没传，不留空进度条，状态行写「已取消发送」
    if (direct && state == kFileCancelled)
    {
        return false;
    }
    return FileStateHasProgress(state);
}

int FileStateButtonCount(int state, bool fromMe)
{
    switch (state)
    {
    case kFileUploading:
    case kFileDownloading:
        return 1;   // 取消
    case kFileNormal:
        return 3;   // 打开文件 / 打开所在文件夹 / 另存为
    case kFileFailed:
    case kFileCancelled:
        //失败与已取消都只给重新发送(下载)。失败时传输任务已经结束，没有可取消的对象：
        //原先给出的「取消」点了没有任何反应
        return 1;
    case kFilePendingAccept:
        // 对方发来的要用户决定收不收（接收 / 拒绝）；自己发出的没有"拒绝"可言，
        // 只给一个「下载」把它取回本地
        return fromMe ? 1 : 2;
    case kFileRejected:
        return 1;   // 重新下载（拒绝后反悔）
    case kFileDirectWaiting:
        return 2;   // 在线直传等对方接收（发送方）：发送离线文件 / 取消
    case kFileDirectToOffline:
        return 0;   // 接收方的直传已作废，随后到达的离线文件消息代替它
    case kFileDeleted:
    default:
        return 0;   // 已删除：无操作。未知状态一并按此保守处理
    }
}

int FileButtonActions(int state, bool fromMe, int out[kMaxChatFileActions])
{
    int count = FileStateButtonCount(state, fromMe);
    if (count <= 0)
        return 0;

    if (state == kFileUploading || state == kFileDownloading)
    {
        out[0] = kChatFileCancel;
    }
    else if (state == kFileFailed || state == kFileCancelled || state == kFileRejected)
    {
        //三者都是"没传成"，只给重新发送(下载)
        out[0] = kChatFileRetry;
    }
    else if (state == kFilePendingAccept)
    {
        //动作都是 kChatFileAccept（含义是"开始下载"），区别只在按钮文案：
        //对方发来的写「接收」，自己发出的写「下载」。宿主的处理逻辑因而完全一致。
        out[0] = kChatFileAccept;
        if (!fromMe)
        {
            out[1] = kChatFileReject;
        }
    }
    else if (state == kFileNormal)
    {
        out[0] = kChatFileOpen;        // 打开文件
        out[1] = kChatFileOpenFolder;  // 打开所在文件夹
        out[2] = kChatFileSaveAs;      // 另存为
    }
    else if (state == kFileDirectWaiting)
    {
        //主动作是改走上传服务器（对方一时不收也能先放到服务器上），取消排在后面
        out[0] = kChatFileSendOffline;
        out[1] = kChatFileCancel;
    }
    return count;
}

bool FileCardShowsP2PBadge(int state, bool direct, bool linkIsDirect)
{
    //只有经直连传过字节的在线直传才画：普通离线文件、经中转的直传都不画（方案决定 4）
    if (!direct || !linkIsDirect)
        return false;
    //正在传、已传完，以及历史记录里本机已没有文件（文件确实是直连传的，只是这台机器上找不到了）
    return state == kFileUploading || state == kFileDownloading || state == kFileNormal || state == kFileDeleted;
}

} // namespace balloonchatwnd
