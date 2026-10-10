/**
 *  ChatReceivedVoice.h
 *
 *  单聊窗与群聊窗共用的语音消息（msgtype=4）接收处理（语音消息第 3d-3 步，docs/语音消息方案.md 第 7.2 节）：
 *  把收到或翻到的语音消息做成语音气泡、判断本机缓存里有没有这段语音、按本地库的已听记录决定画不画红点。
 *  头像与发送人、追加到列表、需要时发起下载都由窗口自己做——两个窗口在这几处的写法不同（群聊要显示发送人名）。
 *
 *  已听状态只记在本机（方案 V7），本地库归主面板所有，窗口经 FMG_MSG_QUERY_VOICE_PLAYED 同步查询、
 *  经 FMG_MSG_VOICE_PLAYED 通知标记，两条消息的负载结构都定义在本文件。
 *
 *  典型用法：
 *      balloonchatwnd::VoicePlayedQuery query;
 *      query.m_ctype = conversation_single;
 *      query.m_cid = peerUserId;
 *      query.m_msgids.push_back(notify.m_msgid);
 *      balloonchatwnd::QueryVoicePlayed(m_hostPanel, query);
 *      const bool played = balloonchatwnd::ResolveVoicePlayed(fromMe, query.m_storeReady,
 *                                                             query.m_played.count(notify.m_msgid) > 0);
 *      bool needDownload = false;
 *      balloonchatwnd::ChatMessage msg =
 *          balloonchatwnd::BuildReceivedVoiceMessage(fromMe, notify, played, voiceDir, &needDownload);
 *      // 窗口自己填头像 / 发送人，再 AppendMessage；needDownload 为 true 时发起下载
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（CString）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <set>
#include <string>
#include <vector>
#include <windows.h>
#include "ChatMockData.h"     // ChatMessage

namespace net {
struct MessageNotify;
}

namespace balloonchatwnd {

class ChatMessageList;

/**
 *  FMG_MSG_QUERY_VOICE_PLAYED 的负载：聊天窗在自己的调用栈上构造，主面板同步填写出参，不 delete。
 */
struct VoicePlayedQuery
{
    int                  m_ctype;       //会话类型（1 单聊 2 群聊），入参
    int64_t              m_cid;         //会话ID（单聊=对端userid，群聊=群id），入参
    std::vector<int64_t> m_msgids;      //要查的语音消息的服务器 msgid，入参
    std::set<int64_t>    m_played;      //[出参] 其中本地库记为已听的 msgid；库里没有的、未听的都不在其中
    bool                 m_storeReady;  //[出参] 本地库是否可用；没有主面板处理这条消息时保持 false

    VoicePlayedQuery()
        : m_ctype(0)
        , m_cid(0)
        , m_storeReady(false)
    {
    }
};

/**
 *  FMG_MSG_VOICE_PLAYED 的负载：聊天窗在堆上分配后投给主面板，主面板写库后 delete。
 */
struct VoicePlayedMark
{
    int     m_ctype;   //会话类型（1 单聊 2 群聊）
    int64_t m_cid;     //会话ID（单聊=对端userid，群聊=群id）
    int64_t m_msgid;   //播放过的那条语音的服务器 msgid

    VoicePlayedMark()
        : m_ctype(0)
        , m_cid(0)
        , m_msgid(0)
    {
    }
};

/**
 *  判断一条语音在界面上算不算已听（决定画不画未听红点）。
 *  @param fromMe       是否本人发的；本人发的一律已听
 *  @param storeReady   本地库是否可用；不可用时一律按已听，免得每次打开窗口都出现一片无法清除的红点
 *  @param storedPlayed 本地库是否记为已听；库里还没有这条（翻历史时先显示后入库）按未听
 *  @return true 表示已听、不画红点
 */
bool ResolveVoicePlayed(bool fromMe, bool storeReady, bool storedPlayed);

/**
 *  把一条语音消息做成语音气泡的数据（不含头像与发送人信息，由调用方按会话类型填写）。
 *
 *  本机缓存里有大小相符的 <voiceDir><md5>.ogg 时为就绪态、带上路径；否则为下载中，需要调用方发起下载；
 *  消息里缺 fileid 或 md5、或拿不到缓存目录时无法下载，直接标为下载失败。
 *
 *  @param fromMe       该条是否本人发出，决定气泡画在右侧还是左侧
 *  @param notify       语音消息（取 m_msgid / m_timestamp / m_clientmsgid 与 m_voice* 字段）
 *  @param played       本机是否已听过（ResolveVoicePlayed 的结果）；本人发的忽略此参数
 *  @param voiceDir     当前账号的 ChatVoice 目录（以反斜杠结尾）；空串表示未登录
 *  @param needDownload [出参，可为 NULL] 是否需要调用方发起下载
 *  @return 构造好的气泡数据，msgid 与 clientMsgId 已填好（列表去重、播放、标记已听都认它们）
 */
ChatMessage BuildReceivedVoiceMessage(bool fromMe, const net::MessageNotify& notify, bool played,
                                      const std::wstring& voiceDir, bool* needDownload);

/**
 *  向主面板同步查询一批语音的已听状态（SendMessage FMG_MSG_QUERY_VOICE_PLAYED）。
 *  @param hostPanel 主面板窗口；无效时（调试入口、主面板已销毁）不发送，query.m_storeReady 置为 false
 *  @param query     [入参 / 出参] 会话与 msgid 由调用方填好；返回时 m_played、m_storeReady 已填写
 */
void QueryVoicePlayed(HWND hostPanel, VoicePlayedQuery& query);

/**
 *  按 clientMsgId 在消息列表里找一条语音消息（重发时取录音文件路径、时长与波形，语音消息第 3c 步）。
 *  @param list        消息列表
 *  @param clientMsgId 发送时的客户端消息ID；空串一律找不到
 *  @param out         [出参] 找到时填该条消息的副本
 *  @return 找到一条该 clientMsgId 的语音消息时返回 true
 */
bool FindVoiceByClientMsgId(const ChatMessageList& list, const std::string& clientMsgId, ChatMessage& out);

/**
 *  把一条已存下的语音（本地库里的记录、合并记录里的子消息）做成语音消息，供消息记录窗与合并记录查看窗的语音条显示
 *  （语音消息第 4 期）。与 BuildReceivedVoiceMessage 的区别：不触发下载——本机没有文件时标为「需要下载」，
 *  等用户点了再下（借用下载失败态，DecideVoiceClick 据此返回重新下载）。
 *  @param msgid           服务器消息ID
 *  @param fromMe          是否本人发的（本人发的一律已听）
 *  @param fileId          语音文件ID
 *  @param md5             文件内容 MD5
 *  @param durationSeconds 时长（整秒）
 *  @param sizeBytes       文件字节数（判断本机文件是否完整）
 *  @param waveBase64      波形值的 Base64；为空时画均匀波形
 *  @param played          本机是否已听过
 *  @param voiceDir        当前账号的 ChatVoice 目录（以反斜杠结尾）；空串时一律按本机没有处理
 *  @return 语音消息：本机有大小相符的 <voiceDir><md5>.ogg 时为就绪并带上路径，否则为需要下载
 */
ChatMessage BuildStoredVoiceMessage(long long msgid, bool fromMe, const std::string& fileId, const std::string& md5,
                                    int durationSeconds, long long sizeBytes, const std::string& waveBase64,
                                    bool played, const std::wstring& voiceDir);

} // namespace balloonchatwnd
