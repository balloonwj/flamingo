/**
 *  RelayNotifyApply.h
 *
 *  收到接龙变更推送（cmd_relay_changed_notify=1604）后，把新名单与状态合并进本地消息库里
 *  那张卡片的内容。主面板（CBalloonMainPanel::OnV2RelayChanged）调用一次即可，步骤是：
 *  按主键取出本地那一行 → 取出卡片内容（content 列优先，退回原始JSON）→ 用推送替换名单与
 *  状态 → 写回 content 列。原始JSON不动，下次从本地库还原时以 content 列为准
 *  （docs/群接龙方案.md 2c，2026-10-02 用户确认）。
 *
 *  从主面板里单独拿出来，是为了让这串步骤能被 FlamingoUnit 对着真的 SQLite 库测试
 *  （Tests/RelayNotifyApplyTests.cpp）；主面板本身只负责调用它、再刷新界面。
 *
 *  典型用法：
 *      std::string mergedJson;
 *      bool hit = false;
 *      if (balloonmain::ApplyRelayNotifyToStore(m_msgStore, notify, mergedJson, hit) && hit)
 *          ... 用 mergedJson 刷新已打开的群聊窗里的那张卡片 ...
 *
 *  本头文件只依赖标准库与前向声明，可以被任何文件包含。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>

namespace net { struct RelayChangedNotify; }
namespace balloonhistory { class CV2MsgStore; }

namespace balloonmain {

/**
 *  把一次接龙变更推送合并进本地消息库里那张卡片的内容。
 *  @param store        本地消息库；须已打开。调用期间须有效，本函数不保存它
 *  @param notify       已解析的 cmd_relay_changed_notify 推送（ctype / cid / msgid 定位卡片）
 *  @param outRelayJson [出参] 合并后的完整卡片内容（紧凑 JSON，即 net::MessageNotify::m_relayContent
 *                      的格式）；只在命中并合并成功时写入
 *  @param outHit       [出参] 本地是否有这张卡片。没有时为 false：本机没同步过这段历史属正常，
 *                      下次同步会带回服务端合并后的最新内容，不需要补拉
 *  @return 读写本地库都成功返回 true（含本地没有这张卡片的情况）；读库失败、本地那一行取不出
 *          卡片内容、或写库失败时返回 false，此时 content 列保持原样
 */
bool ApplyRelayNotifyToStore(balloonhistory::CV2MsgStore& store, const net::RelayChangedNotify& notify,
                             std::string& outRelayJson, bool& outHit);

} // namespace balloonmain
