/**
 *  ChatCloudRecordCard.h
 *
 *  云端录像卡片（msgtype=24，开启者把一条可观看的云端录像发到会话时，服务端以他的名义写入）在单聊窗与群聊窗里显示为一张
 *  卡片。卡片复用屏幕共享卡片的版式（kind 同为 kMsgScreenShare，screenShareIsCloudRecord 为 true）：标题前画场记板
 *  （只有声音的录音画话筒）；标题行写录像标题，副标题「张三分享的云端录像」，状态行「时长 01:23 · 15.6 MB」，
 *  底部「播放」按钮。点「播放」由宿主经 CScreenShareHub::OpenCloudRecord 交给 FlamingoMedia.dll 下载并打开。
 *  设计见 docs/屏幕共享与录制方案.md 12.6.5，content 字段见 docs/协议设计-v2.md 17.10。
 *
 *  卡片内容在发出之后不再改写。录像到期被清理、或被开启者删除后，点「播放」时服务端回「录像不存在」，由 DLL 提示，
 *  卡片本身不变。
 *
 *  本文件把 content 原文解析成卡片要显示的内容并套用到一条气泡数据上。纯逻辑，不碰绘制，可被 FlamingoUnit 直接测试
 *  （Source/Tests/CloudRecordCardTests.cpp，标签 [cloudreccard]）。
 *
 *  典型用法（聊天窗收到或翻到一张云端录像卡片时）：
 *      balloonchatwnd::ChatMessage msg = MakeTextMessage(fromMe, TR(L"[云端录像]"));
 *      balloonchatwnd::ApplyCloudRecordCard(msg, notify.m_cloudRecordContent, senderName);
 *      list->AppendMessage(msg);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include "ChatMockData.h"   // ChatMessage / kMsgScreenShare

namespace balloonchatwnd {

/**
 *  一张云端录像卡片 content 解析出来的内容（文字已从 UTF-8 转成 UTF-16）。
 */
struct CloudRecordCardInfo
{
    long long m_recordId;     // 录像 id；解析失败时为 0
    long long m_owner;        // 开启者（即卡片的发送者）的 userid
    CString   m_title;        // 录像标题；可为空
    int64_t   m_durationMs;   // 时长（毫秒）
    int64_t   m_bytes;        // 最终文件的字节数
    bool      m_audioOnly;    // 是否只有声音（独立录制的 m4a 录音）

    CloudRecordCardInfo()
        : m_recordId(0), m_owner(0), m_durationMs(0), m_bytes(0), m_audioOnly(false)
    {
    }
};

/**
 *  解析一张云端录像卡片的 content。
 *  @param contentJson content 原文（UTF-8 JSON）；recordid、owner 可以是十进制字符串或整数
 *  @param out         [出参] 解析结果；返回 false 时内容不确定
 *  @return content 是 JSON 对象且带正的 recordid 时返回 true
 */
bool ParseCloudRecordCard(const std::string& contentJson, CloudRecordCardInfo& out);

/**
 *  卡片副标题（已按界面语言翻译）：「我分享的云端录像」「张三分享的云端录像」，名字取不到时为「云端录像」。
 *  @param senderName 发送者显示名；空串表示取不到
 *  @param fromMe     卡片是不是本人发的
 */
CString CloudRecordCardSubtitle(const CString& senderName, bool fromMe);

/**
 *  状态行文字：「时长 01:23 · 15.6 MB」；只有声音的录音为「录音 · 时长 01:23 · 1.2 MB」。时长为 0 时省去时长一段，
 *  字节数为 0 时省去大小一段。时长写法见 net::FormatDurationClock，大小写法见 FormatFileSize。
 *  @param info 卡片内容
 */
CString CloudRecordCardStatusText(const CloudRecordCardInfo& info);

/**
 *  把一张云端录像卡片套用到气泡数据上：kind 改为 kMsgScreenShare 并置 screenShareIsCloudRecord，填写标题、副标题、状态与
 *  「播放」按钮，录像 id 存进 infoCardRefId，content 原文存进 screenShareContent，text 填摘要「[云端录像] 标题」。
 *  content 解析不了时标题写「云端录像」、状态写「录像内容不可用」，没有按钮。
 *  @param msg         [入出参] 气泡数据，fromMe 须已填好
 *  @param contentJson content 原文
 *  @param senderName  发送者显示名（宿主按群名片、通讯录取得）；本人发的卡片传空串即可
 */
void ApplyCloudRecordCard(ChatMessage& msg, const std::string& contentJson, const CString& senderName);

/**
 *  卡片当前是否可以点「播放」（是云端录像卡片且内容可用）。
 */
bool IsCloudRecordCardOpenable(const ChatMessage& msg);

} // namespace balloonchatwnd
