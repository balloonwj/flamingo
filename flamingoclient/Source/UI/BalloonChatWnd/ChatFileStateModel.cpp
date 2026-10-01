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
        //原先给出的「取消」点了没有任何反应（bugs.md 的 BUG-37）
        return 1;
    case kFilePendingAccept:
        // 对方发来的要用户决定收不收（接收 / 拒绝）；自己发出的没有"拒绝"可言，
        // 只给一个「下载」把它取回本地
        return fromMe ? 1 : 2;
    case kFileRejected:
        return 1;   // 重新下载（拒绝后反悔）
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
    return count;
}

} // namespace balloonchatwnd
