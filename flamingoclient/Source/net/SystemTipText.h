/**
 *  系统提示（message_type_system）按当前界面语言生成文字（多语言第 5 期，方案见 docs/多语言方案.md 第 9 节）。
 *
 *  服务端在系统提示的 content 里除了拼好的中文 text，另带 tipkind（提示类型，取值见 Protocol.h 的 kSystemTip*）
 *  与 tipargs（参数对象，如 { "nickname": "张三" }）。本模块按类型与参数套用 TR8 / TRC8 / TRF8 模板，
 *  生成当前界面语言的文字；简体中文界面下生成的文字与服务端的 text 相同。认不出的类型（更新的服务端新加的
 *  提示）、以及没有这两个字段的历史消息返回 false，调用方仍显示 text。
 *
 *  典型用法（解析消息时）：
 *      std::string localized;
 *      if (net::FormatSystemTip(content["tipkind"].asString(), content["tipargs"], localized))
 *          notify.m_text = localized;
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>

namespace Json {
class Value;
}

namespace net {

/**
 *  按提示类型与参数生成当前界面语言的系统提示文字。
 *  @param tipKind 提示类型（content.tipkind），取值见 Protocol.h 的 kSystemTip*
 *  @param tipArgs 参数对象（content.tipargs）；不是对象、或其中某个参数缺失 / 类型不对时按缺省写法生成
 *                 （如没有昵称时写成「有成员」），与服务端 text 的规则一致
 *  @param out     [出参] 生成的文字（UTF-8）；返回 false 时不修改
 *  @return 认得 tipKind 返回 true；不认得（含空串）返回 false。入群提示（kSystemTipMemberJoin）的 members
 *          缺失或为空时也返回 false，因为没有可以生成的名单，调用方照常显示服务端的 text
 */
bool FormatSystemTip(const std::string& tipKind, const Json::Value& tipArgs, std::string& out);

/**
 *  从系统提示的 content 对象里取 tipkind 与 tipargs 并生成文字。
 *  @param content 系统提示的 content 对象（借用）
 *  @param out     [出参] 生成的文字（UTF-8）；返回 false 时不修改
 *  @return content 带可识别的 tipkind 且 tipargs 是对象时返回 true；否则返回 false（调用方用 text）
 */
bool FormatSystemTipFromContent(const Json::Value& content, std::string& out);

/**
 *  FormatSystemTip 的 JSON 文本版：参数对象以紧凑 JSON 文本给出（如 MessageNotify::m_tipArgs）。
 *  @param tipKind     提示类型
 *  @param tipArgsJson tipargs 的 JSON 文本；解析失败时按空对象处理
 *  @param out         [出参] 生成的文字（UTF-8）；返回 false 时不修改
 *  @return 同 FormatSystemTip
 */
bool FormatSystemTipJson(const std::string& tipKind, const std::string& tipArgsJson, std::string& out);

/**
 *  判断一条系统提示是不是「有人入群」提示、且新成员名单里有某人。新成员本人的入群提示计未读（2026-10-07
 *  用户确认，加群申请审批通过、被人拉进群时会话行因此有红点），与服务端 GroupProtocol::isMemberJoinTipFor
 *  同一口径；由 ConversationsProtocolClient::shouldCountAsUnread 调用。
 *  @param tipKind     提示类型（MessageNotify::m_tipKind）
 *  @param tipArgsJson tipargs 的 JSON 文本（MessageNotify::m_tipArgs）
 *  @param userid      要判断的人；不大于 0 时返回 false
 *  @return tipKind 为 kSystemTipMemberJoin 且 members 里有这个 userid 时返回 true；邀请人不算，
 *          参数不是合法 JSON 或缺字段时返回 false
 */
bool IsMemberJoinTipForUser(const std::string& tipKind, const std::string& tipArgsJson, int64_t userid);

/**
 *  把时长格式化成时钟写法：不足 1 小时为 mm:ss，满 1 小时为 h:mm:ss（小时不补零），与服务端
 *  ScreenShareRules::formatEndTip 的写法相同。各语言通用，不经翻译。
 *  @param seconds 时长，单位秒；负数按 0 处理
 *  @return 形如 "01:05"、"1:02:05" 的文字
 */
std::string FormatDurationClock(int64_t seconds);

/**
 *  设置当前登录者的 userid。入群提示（kSystemTipMemberJoin）涉及本人时写成「你」，要靠它判断。
 *  登录线程解析登录应答成功时设置，早于收取线程解析任何业务帧；消息在网络线程解析、在 UI 线程
 *  重新生成，故以原子变量保存，可在任意线程调用。
 *  @param userid 当前登录者的 userid；0 表示未登录，此时入群提示不出现「你」
 */
void SetSystemTipSelfUserId(int64_t userid);

/**
 *  取 SetSystemTipSelfUserId 设置的当前登录者 userid。
 *  @return 当前登录者的 userid；从未设置时为 0
 */
int64_t GetSystemTipSelfUserId();

} // namespace net
