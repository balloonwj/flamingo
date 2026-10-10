/**
 *  ScreenShareInfoApply.h
 *
 *  发起人在共享中修改了标题与描述后，服务端推送 cmd_screenshare_info_notify（1734，第 15 期），带上会话里那张共享卡片的
 *  消息 id 与改写后的卡片 content。这条信令属于音视频类信令，主连接把它原样转交 FlamingoMedia.dll（观看窗、名单等
 *  在 DLL 里刷新）；主程序这一侧另外要做两件事：把本地消息库里那张卡片的 content 列换成新内容，再刷新已打开的聊天窗里
 *  那张卡片，并按新标题刷新屏幕共享状态缓存（群聊横幅）。本文件提供前两件事用到的函数：解析推送、改写本地库。
 *
 *  与接龙卡片的做法一致（见 RelayNotifyApply.h）：只改 content 列，原始JSON保持服务端发来的原样，从本地库还原时
 *  以 content 列为准（见 V2MsgQuery.h 的 ScreenShareContentFromRecord）。
 *
 *  从主面板里单独拿出来，是为了让这串步骤能被 FlamingoUnit 对着真的 SQLite 库测试（Tests/ScreenShareCardTests.cpp
 *  的 SSTIC11、SSTIC13、SSTIC14）；主面板本身只负责调用它、再刷新界面。
 *
 *  典型用法（主面板收到转交的 1734 时）：
 *      balloonmain::ScreenShareInfoUpdate update;
 *      bool hit = false;
 *      if (balloonmain::ParseScreenShareInfoUpdate(body, update) && update.m_msgid > 0)
 *          balloonmain::ApplyScreenShareInfoToStore(m_msgStore, update, hit);
 *
 *  本头文件只依赖标准库与前向声明，可以被任何文件包含。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>

namespace balloonhistory { class CV2MsgStore; }

namespace balloonmain {

//共享标题与描述变更推送（cmd_screenshare_info_notify=1734）里主程序要用的部分
struct ScreenShareInfoUpdate
{
    std::string m_roomId;    //房间号
    int         m_ctype;     //会话类型（1 单聊、2 群聊）
    int64_t     m_cid;       //从本人角度看的会话 id（单聊为对端 userid，群聊为群 id）
    std::string m_title;     //新的共享标题（UTF-8），群聊横幅据此刷新；推送里没有时为空
    int64_t     m_msgid;     //会话里那张共享卡片的消息 id；服务端没能改写卡片时为 0
    std::string m_content;   //改写后的卡片 content 原文（JSON 文本）；m_msgid 为 0 时为空
    int64_t     m_hostId;    //新主持人 userid（2026-10-08）：移交主持人后的那条推送才带；不带（修改标题与描述、
                             //老服务端）时为 0，群聊横幅的主持人沿用状态缓存里的
    std::string m_hostName;  //新主持人昵称（UTF-8）；m_hostId 为 0 时为空，服务端查不到资料时也可能为空

    ScreenShareInfoUpdate();
};

/**
 *  解析 1734 推送的包体。cid、msgid 以字符串编码，也接受整数写法；不带 msgid（服务端没能改写卡片）时 m_msgid 为 0。
 *  @param json 包体 JSON 文本
 *  @param out  [出参] 解析结果；返回 false 时内容不确定
 *  @return 包体是 JSON 对象、带非空房间号与有效的会话类型和会话 id 时返回 true
 */
bool ParseScreenShareInfoUpdate(const std::string& json, ScreenShareInfoUpdate& out);

/**
 *  把改写后的卡片内容写进本地消息库里那张卡片的 content 列（原始JSON不动）。
 *  @param store  本地消息库；须已打开。调用期间须有效，本函数不保存它
 *  @param update 已解析的 1734 推送
 *  @param outHit [出参] 是否命中了本地的行；本地没有这张卡片（本机没同步过这段历史）或推送不带卡片时为 false
 *  @return 本地没有这张卡片、推送不带卡片也返回 true（属正常情况）；新内容不是 JSON 对象或写库失败时返回 false
 */
bool ApplyScreenShareInfoToStore(balloonhistory::CV2MsgStore& store, const ScreenShareInfoUpdate& update, bool& outHit);

} // namespace balloonmain
