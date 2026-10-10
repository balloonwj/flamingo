/**
 *  群里发送本地文件时「上传群文件后再在聊天里发一条文件消息」的登记，说明见 GroupFileChatPoster.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "GroupFileChatPoster.h"
#include "ChatFileStateModel.h"            // kFileNormal
#include "../../Common/Util/EncodeUtil.h"  // Utf8ToUnicode：文件名转成气泡显示用的宽字符

namespace balloonchatwnd {

GroupFileChatPost::GroupFileChatPost()
    : m_size(0)
{
}

void GroupFileChatPoster::Mark(int uploadId, const std::wstring& localPath)
{
    //上传 id 由文件传输线程从 1 起分配，不大于 0 表示没有真正入队
    if (uploadId <= 0)
        return;
    m_marked[uploadId] = localPath;
}

bool GroupFileChatPoster::IsMarked(int uploadId) const
{
    return m_marked.find(uploadId) != m_marked.end();
}

bool GroupFileChatPoster::Take(const net::GroupFileUploadResult& result, GroupFileChatPost& out)
{
    std::map<int, std::wstring>::iterator it = m_marked.find(result.m_uploadid);
    if (it == m_marked.end())
        return false;
    //不论成败都取消登记：失败或取消的上传不会再有结果
    const std::wstring path = it->second;
    m_marked.erase(it);
    if (!result.m_ok || result.m_fileid.empty())
        return false;
    out.m_localPath = path;
    out.m_fileid = result.m_fileid;
    out.m_filename = result.m_filename;
    out.m_size = result.m_size;
    out.m_md5 = result.m_md5;
    return true;
}

ChatMessage MakeGroupFileEcho(const GroupFileChatPost& post, const std::string& clientMsgId)
{
    //文件已在本机与群文件里：直接是已完成态（kFileNormal），点开打开本地文件
    ChatMessage echo = MakeFileMessage(true, CString(EncodeUtil::Utf8ToUnicode(post.m_filename).c_str()),
                                       static_cast<long long>(post.m_size), CString(post.m_localPath.c_str()),
                                       clientMsgId, kFileNormal);
    echo.fileFileId = post.m_fileid;
    echo.fileMd5 = post.m_md5;
    return echo;
}

GroupFileRetryKind ChooseGroupFileRetry(bool fromMe, long long msgid, const std::string& fileId)
{
    if (fileId.empty())
        return group_file_retry_none;
    //已有服务器 msgid：消息已入库，按 msgid 重新下载
    if (msgid > 0)
        return group_file_retry_download;
    //没有 msgid 的只能是本人被拒绝的那条；别人的消息总带着 msgid
    return fromMe ? group_file_retry_resend : group_file_retry_none;
}

} // namespace balloonchatwnd
