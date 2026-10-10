/**
 *  群里发送本地文件时「上传群文件后再在聊天里发一条文件消息」的登记，GroupFileChatPoster.h
 *
 *  背景：2026-10-05 用户反馈在群聊里录完屏点「发送」看不到效果——录像被上传进了群文件库，聊天里什么也没有。用户定的做法：
 *  仍上传群文件，并在群聊里发一条文件消息（msgtype 3，内容为群文件的 fileid、文件名、大小、md5，群成员按群文件鉴权下载）。
 *  普通的「上传群文件」按钮不受影响，只有经宿主「发送到会话」发来的文件才登记。
 *
 *  本类只做登记与配对（纯逻辑，单元测试见 Source/Tests/GroupFileChatPosterTests.cpp）：
 *      poster.Mark(uploadId, path);                 // 发起上传时
 *      GroupFileChatPost post;
 *      if (poster.Take(uploadResult, post)) ...     // 上传结果到达时：登记过且成功才返回 true；不论成败都取消登记
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include "../../net/FileTransfer.h"   // net::GroupFileUploadResult
#include "ChatMockData.h"             // ChatMessage：本人文件气泡

namespace balloonchatwnd {

//上传成功后要在群里发出的文件消息
struct GroupFileChatPost
{
    std::wstring m_localPath;   //本地文件路径（本人气泡据此打开文件）
    std::string  m_fileid;      //群文件的文件 id
    std::string  m_filename;    //文件名（UTF-8，含扩展名）
    int64_t      m_size;        //字节数
    std::string  m_md5;         //文件内容的 MD5

    GroupFileChatPost();
};

//「上传完要发到聊天里」的群文件上传登记
class GroupFileChatPoster
{
public:
    /**
     *  登记一笔上传：成功后要在群里发文件消息。
     *  @param uploadId  FileTransfer::EnqueueGroupFileUpload 返回的上传 id；不大于 0 时忽略
     *  @param localPath 本地文件路径
     */
    void Mark(int uploadId, const std::wstring& localPath);

    //某笔上传是否登记过
    bool IsMarked(int uploadId) const;

    /**
     *  上传结果到达：取消这一笔的登记；登记过且上传成功时给出要发的文件消息。
     *  @param result 上传结果
     *  @param out    [出参] 要发的文件消息；返回 false 时不变
     *  @return 要发文件消息返回 true
     */
    bool Take(const net::GroupFileUploadResult& result, GroupFileChatPost& out);

private:
    std::map<int, std::wstring> m_marked;   //登记过的上传：上传 id → 本地文件路径
};

/**
 *  群里本人刚发出的文件消息气泡（上传群文件成功后由 SendUploadedFileMessage 插入）：文件已在本机与群文件里，状态为已完成；
 *  另记下群文件的 fileid 与 md5，被服务端拒绝后点「重新发送」时据此直接重发文件消息，不必再上传（2026-10-05）。
 *  @param post        上传成功的群文件
 *  @param clientMsgId 这条消息的客户端消息 ID（UTF-8），发送回执据此回填
 *  @return 气泡数据
 */
ChatMessage MakeGroupFileEcho(const GroupFileChatPost& post, const std::string& clientMsgId);

//群聊文件气泡上「重新发送 / 重新下载」按钮的去向
enum GroupFileRetryKind
{
    group_file_retry_none     = 0,   //不处理：没有 fileid，或是别人的消息却还没有服务器 msgid（正常不会出现）
    group_file_retry_resend   = 1,   //重发文件消息：本人发出、被服务端拒绝（没有服务器 msgid），文件已在群文件里
    group_file_retry_download = 2,   //重新下载：已有服务器 msgid 的文件消息（转发进群的、别人发的，或本人发出且已成功的）
};

/**
 *  决定群聊文件气泡上的重试按钮做什么（2026-10-05 起本人发出、被拒绝的文件消息可以重发）。
 *  @param fromMe 是否本人发出
 *  @param msgid  服务器消息 ID；没有时不大于 0
 *  @param fileId 文件 ID；为空时什么都做不了
 */
GroupFileRetryKind ChooseGroupFileRetry(bool fromMe, long long msgid, const std::string& fileId);

} // namespace balloonchatwnd
