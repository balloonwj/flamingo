/**
 *  ChatAnnouncementCard.h
 *
 *  群公告卡片（msgtype=22，发布或重新发布群公告时服务端以发布人的名义写入群聊）在群聊窗里显示为一张卡片。卡片复用屏幕共享
 *  卡片的版式（kind 同为 kMsgScreenShare，screenShareIsAnnouncement 为 true），与群通话卡片、一对一通话卡片的做法相同：
 *  标题前画喇叭；标题行写公告标题，副标题「林知夏发布的群公告」，描述块是正文的前两行，状态行写优先级，底部「查看」按钮。
 *  公告被删除后服务端把卡片改写为已删除形态（没有正文、带 deleted），卡片只剩标题，状态行写「该公告已被删除」，不能再点。
 *  设计见 docs/群公告接入方案.md §13.4。
 *
 *  本文件把 content 原文解析成卡片要显示的内容并套用到一条气泡数据上。纯逻辑，不碰绘制，可被 FlamingoUnit 直接测试。
 *
 *  典型用法（群聊窗收到或翻到一张群公告卡片时）：
 *      balloonchatwnd::ChatMessage msg = MakeTextMessage(fromMe, _T("[群公告]"));
 *      balloonchatwnd::ApplyAnnouncementCard(msg, notify.m_announcementContent, senderName);
 *      list->AppendMessage(msg);
 *  点卡片时，宿主据 msg.screenShareContent 取出整条公告：
 *      balloonchatwnd::AnnouncementCardInfo info;
 *      if (balloonchatwnd::ParseAnnouncementCard(msg.screenShareContent, info) && !info.m_deleted) { 打开详情窗 }
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include "ChatMockData.h"   // ChatMessage / kMsgScreenShare

namespace balloonchatwnd {

/**
 *  一张群公告卡片 content 解析出来的内容（文字已从 UTF-8 转成 UTF-16）。
 */
struct AnnouncementCardInfo
{
    long long m_announcementId;  // 公告 id；解析失败时为 0
    long long m_groupId;         // 所属群 id
    CString   m_title;           // 公告标题
    CString   m_body;            // 公告正文全文；已删除形态为空
    int       m_severity;        // 优先级，取值见 net/Protocol.h 的 group_announcement_severity
    long long m_publisherId;     // 发布人 userid
    CString   m_publisherName;   // 发布人昵称（服务端填写；取不到时为空）
    long long m_publishTimeMs;   // 发布时间，Unix 毫秒
    bool      m_republish;       // 是否由「重新发布」产生
    bool      m_deleted;         // 公告是否已被删除（服务端改写后的已删除形态）
    bool      m_mustRead;        // 是否为入群必读卡片：服务端以系统名义、只发给新成员本人（docs/群公告接入方案.md §14）

    AnnouncementCardInfo()
        : m_announcementId(0), m_groupId(0), m_severity(0), m_publisherId(0), m_publishTimeMs(0)
        , m_republish(false), m_deleted(false), m_mustRead(false)
    {
    }
};

/**
 *  解析一张群公告卡片的 content。
 *  @param contentJson content 原文（UTF-8 JSON）
 *  @param out         [出参] 解析结果；返回 false 时内容不确定
 *  @return content 是 JSON 对象且带正的 announcementid 时返回 true
 */
bool ParseAnnouncementCard(const std::string& contentJson, AnnouncementCardInfo& out);

/**
 *  优先级的文字（已按界面语言翻译）：「通知」「重要」「紧急」，与群管理页、群聊窗边栏的徽章文案一致。
 *  @param severity 取值见 group_announcement_severity；越界值按「通知」处理
 */
CString AnnouncementSeverityText(int severity);

/**
 *  卡片副标题（已按界面语言翻译）：本人发的写「我发布的群公告」「我重新发布的群公告」，别人发的写
 *  「林知夏发布的群公告」「林知夏重新发布的群公告」，名字取不到时只写「群公告」。
 *  @param publisherName 发布人显示名；空串表示取不到
 *  @param fromMe        卡片是不是本人发的（发送者即发布人）
 *  @param republish     是否由「重新发布」产生
 */
CString AnnouncementCardSubtitle(const CString& publisherName, bool fromMe, bool republish);

/**
 *  把一张群公告卡片套用到气泡数据上：kind 改为 kMsgScreenShare 并置 screenShareIsAnnouncement，填写标题、副标题、描述、
 *  状态与按钮，content 原文存进 screenShareContent，text 填摘要「[群公告] 标题」（引用、复制等按 text 取摘要的地方用到它）。
 *  content 解析不了时标题写「群公告」，不可点。
 *  @param msg           [入出参] 气泡数据，fromMe 须已填好
 *  @param contentJson   content 原文
 *  @param publisherName 发布人显示名（宿主按群名片、通讯录取得）；空串时退回 content 里服务端写的昵称
 */
void ApplyAnnouncementCard(ChatMessage& msg, const std::string& contentJson, const CString& publisherName);

/**
 *  卡片当前是否可以点开（内容可用且公告未被删除）。
 *  @param msg 气泡数据
 *  @return 是群公告卡片、按钮可点时返回 true
 */
bool IsAnnouncementCardOpenable(const ChatMessage& msg);

} // namespace balloonchatwnd
