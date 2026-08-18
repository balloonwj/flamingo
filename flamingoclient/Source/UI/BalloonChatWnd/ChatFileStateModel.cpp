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
        return 2;   // 重新发送(下载) / 取消
    case kFileCancelled:
        return 1;   // 重新发送(下载)
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
    else if (state == kFileCancelled || state == kFileRejected)
    {
        //两者都是"没传成"，给一条回头路即可
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
    else   // kFileFailed
    {
        out[0] = kChatFileRetry;
        out[1] = kChatFileCancel;
    }
    return count;
}

} // namespace balloonchatwnd
