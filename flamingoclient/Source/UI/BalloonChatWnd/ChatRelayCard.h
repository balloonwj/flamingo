/**
 *  ChatRelayCard.h
 *
 *  群接龙卡片（msgtype=11）的显示模型：把卡片内容（net::RelayCardContent）换算成聊天气泡与
 *  详情窗要显示的文字——标题、说明、前几条接龙、状态行、底部按钮、复制为文本的内容——并负责
 *  合并接龙变更推送、按本机时间判断是否已截止、判断当前用户能否提前结束。纯逻辑，不碰绘制，
 *  可被 FlamingoUnit 直接测试；卡片的绘制在 ChatBubble 里（docs/群接龙方案.md 第 3 期）。
 *
 *  JSON 的解析与序列化在 net/CardProtocolClient 里（parseRelayCardContent /
 *  serializeRelayCardContent），本文件只处理解析之后的结构，与合并转发卡片 ChatMergedCard 的分工一致。
 *
 *  名字通过 IRelayNameResolver 由调用方传入：聊天窗按群名片、通讯录解析，单测里换成固定的
 *  假实现（编码规范要求尽量不用 lambda，故用接口类）。
 *
 *  典型用法（聊天窗收到一张接龙卡片、或收到变更推送后重画时）：
 *      net::RelayCardContent card;
 *      net::CardProtocolClient::parseRelayCardContent(contentJson, card);
 *      balloonchatwnd::RelayCardPreview preview;
 *      balloonchatwnd::BuildRelayCardPreview(card, selfId, selfMuted, nowMs, resolver, preview);
 *
 *  本头文件使用 CString，包含它之前须已引入 ATL 的 atlstr.h（主工程经预编译头，单测经
 *  Tests\stdafx.h，实现文件见 ChatRelayCard.cpp 开头）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "../../net/CardProtocolClient.h"   // net::RelayCardContent / RelayChangedNotify
#include "ChatMockData.h"                  // ChatMessage / kMsgRelay

namespace balloonchatwnd {

//卡片上最多显示几条接龙。再多卡片就太高，用户点开详情窗看全部。
//取值暂定，3a 步骤按截图确认（docs/群接龙方案.md §3.2）
const size_t kRelayCardPreviewLines = 5;

//卡片底部的按钮，由 DecideRelayButton 判定
enum RelayCardButton
{
    kRelayButtonNone   = 0,   //不显示按钮：接龙已结束或已截止、本人被禁言、或名额已满且本人未参与
    kRelayButtonJoin   = 1,   //「参与接龙」：接龙进行中、本人还没参与、名额未满
    kRelayButtonModify = 2,   //「修改我的」：接龙进行中、本人已参与（修改不占新名额，满员也显示）
};

/**
 *  名字解析接口：把 userid 换成界面上显示的名字。
 *  聊天窗的实现按群名片、通讯录依次查找；单测里换成固定映射的假实现。
 */
class IRelayNameResolver
{
public:
    virtual ~IRelayNameResolver()
    {
    }

    /**
     *  取某个用户在界面上显示的名字。
     *  @param userid 用户 userid
     *  @return 显示名；查不到时返回空串，由调用方写成「用户<id>」
     */
    virtual CString ResolveName(int64_t userid) const = 0;
};

/**
 *  卡片要显示的内容，由 BuildRelayCardPreview 生成。
 */
struct RelayCardPreview
{
    CString              m_title;       //标题；卡片内容里标题为空（旧数据）时为「接龙」
    CString              m_note;        //说明全文，可多行；卡片上最多显示两行，由绘制方截断
    std::vector<CString> m_lines;       //前 kRelayCardPreviewLines 条接龙，每条形如「1. 张三　本人+1」
    CString              m_statusText;  //状态行，如「共 12 人参与 · 截止 10-03 18:00」
    int                  m_count;       //参与人数（名单条数）
    int                  m_state;       //按本机时间换算后的接龙状态，取值见 enum relay_state
    int                  m_button;      //底部按钮，取值见 RelayCardButton

    RelayCardPreview()
        : m_count(0)
        , m_state(0)
        , m_button(kRelayButtonNone)
    {
    }
};

/**
 *  用接龙变更推送更新卡片：名单、状态、结束人、结束时间整体替换为推送里的值，
 *  标题、说明、截止时间保持不变（推送不带这三项，它们发出后也不会再变）。
 *  @param card   [入参/出参] 卡片内容
 *  @param notify 已解析的 cmd_relay_changed_notify 推送
 */
void ApplyRelayNotify(net::RelayCardContent& card, const net::RelayChangedNotify& notify);

/**
 *  按本机时间换算接龙的实际状态。服务端到截止时间时不发推送，客户端显示时要自己判断；
 *  以服务端的判定为准：服务端已标为已结束或已截止的保持不变，手动结束优先于截止。
 *  @param card  卡片内容
 *  @param nowMs 本机当前时间，Unix 毫秒
 *  @return relay_state_open / relay_state_closed / relay_state_expired 之一
 */
int EffectiveRelayState(const net::RelayCardContent& card, int64_t nowMs);

/**
 *  把截止时间、结束时间这类时刻格式化成卡片上的写法：与当前同一年时为「10-03 18:00」，
 *  跨年时为「2027-10-03 18:00」（光看月日分不清是哪一年）。按本机时区换算。
 *  @param timeMs 要显示的时刻，Unix 毫秒；<= 0 或无法换算时返回空串
 *  @param nowMs  本机当前时间，Unix 毫秒，用于判断是否同一年
 *  @return 显示文字（UTF-16）
 */
CString FormatRelayTime(int64_t timeMs, int64_t nowMs);

/**
 *  判断某人是否已在名单里。
 *  @param card   卡片内容
 *  @param userid 用户 userid
 */
bool IsRelayJoinedBy(const net::RelayCardContent& card, int64_t userid);

/**
 *  判定卡片底部显示哪个按钮（规则见 RelayCardButton 各取值的注释）。
 *  名额已满且本人未参与时不显示按钮，避免填完内容才被服务端以 509 拒绝（2026-10-02 用户确认）。
 *  @param card      卡片内容
 *  @param selfId    当前登录用户 userid
 *  @param selfMuted 当前用户在卡片所在群里是否被禁言（个人禁言或全员禁言下的普通成员）
 *  @param nowMs     本机当前时间，Unix 毫秒
 *  @return 取值见 RelayCardButton
 */
int DecideRelayButton(const net::RelayCardContent& card, int64_t selfId, bool selfMuted, int64_t nowMs);

/**
 *  生成卡片要显示的全部文字。
 *  状态行：进行中为「共 N 人参与」，有截止时间时追加「 · 截止 10-03 18:00」；名额已满且本人
 *  未参与时追加「 · 已满」；已结束、已截止分别追加「 · 已结束」「 · 已截止」。
 *  @param card      卡片内容
 *  @param selfId    当前登录用户 userid
 *  @param selfMuted 当前用户是否被禁言
 *  @param nowMs     本机当前时间，Unix 毫秒
 *  @param names     名字解析接口；调用期间须有效，本函数不保存它
 *  @param out       [出参] 卡片要显示的内容
 */
void BuildRelayCardPreview(const net::RelayCardContent& card, int64_t selfId, bool selfMuted,
                           int64_t nowMs, const IRelayNameResolver& names, RelayCardPreview& out);

/**
 *  判断当前用户能否提前结束这张接龙：接龙必须还在进行中；发起人总可以；群卡片另外允许
 *  群主与管理员；单聊卡片只有发起人可以。规则与服务端 CardProtocol::canCloseRelay 相同，
 *  界面据此决定是否显示「结束接龙」按钮，最终以服务端的判定为准。
 *  @param card        卡片内容
 *  @param isGroupCard 卡片是否在群聊里
 *  @param creatorId   卡片发起人（卡片消息的发送方）userid
 *  @param selfId      当前登录用户 userid
 *  @param selfRole    当前用户在群里的角色，取值见 group_member_role；单聊卡片时忽略
 *  @param nowMs       本机当前时间，Unix 毫秒
 */
bool CanCloseRelay(const net::RelayCardContent& card, bool isGroupCard, int64_t creatorId,
                   int64_t selfId, int selfRole, int64_t nowMs);

/**
 *  生成「复制为文本」的内容（docs/群接龙方案.md §3.3），行与行之间用 \r\n：
 *      接龙：周五团建报名
 *      格式：姓名+是否带家属          ← 说明原文，为空时省略这一行
 *      1. 张三　本人+1
 *      2. 李四　不带
 *      （共 2 人，截至 10-02 15:30）  ← 已结束写「（共 2 人，已结束）」，已截止写「（共 2 人，已截止）」
 *  @param card  卡片内容
 *  @param nowMs 本机当前时间，Unix 毫秒；进行中时写在末行
 *  @param names 名字解析接口；调用期间须有效
 *  @return 要放进剪贴板的文字（UTF-16）
 */
CString BuildRelayCopyText(const net::RelayCardContent& card, int64_t nowMs, const IRelayNameResolver& names);

/**
 *  生成「复制为文本」除末行以外的部分：标题行、说明、完整名单，行与行之间用回车换行分隔，末尾不带换行。
 *  气泡在填写卡片时生成一次存起来，复制时再接上 BuildRelayCopyFooter 现拼的末行。
 *  @param card  卡片内容
 *  @param names 名字解析接口；调用期间须有效
 *  @return 正文（UTF-16）
 */
CString BuildRelayCopyBody(const net::RelayCardContent& card, const IRelayNameResolver& names);

/**
 *  生成「复制为文本」的末行：已结束写「（共 N 人，已结束）」，已截止写「（共 N 人，已截止）」，
 *  进行中写「（共 N 人，截至 10-02 15:30）」。
 *  @param count 参与人数
 *  @param state 接龙状态（已按本机时间换算过），取值见 enum relay_state
 *  @param nowMs 本机当前时间，Unix 毫秒；进行中时写在末行
 *  @return 末行文字（UTF-16），不带换行
 */
CString BuildRelayCopyFooter(int count, int state, int64_t nowMs);

/**
 *  把一条气泡数据改成接龙卡片：kind 置为 kMsgRelay，按卡片内容填写 relayXxx 字段，text 置为
 *  BuildRelayDigest 的摘要「[接龙] 标题」（引用回复、会话列表等按 text 取摘要的地方因此拿到统一写法）。
 *  content 不是 JSON 对象时仍是一张卡片：标题为「接龙」、没有接龙、不显示按钮。
 *  @param msg         [入参/出参] 已按普通文字气泡构造好的气泡数据（头像、发送人、msgid 等保持不变）
 *  @param contentJson 卡片 content 原文（UTF-8 JSON），原样存入 msg.relayContent
 *  @param selfId      当前登录用户 userid
 *  @param selfMuted   当前用户在卡片所在群里是否被禁言
 *  @param nowMs       本机当前时间，Unix 毫秒
 *  @param names       名字解析接口；调用期间须有效，本函数不保存它
 */
void ApplyRelayCard(ChatMessage& msg, const std::string& contentJson, int64_t selfId, bool selfMuted,
                    int64_t nowMs, const IRelayNameResolver& names);

/**
 *  把一条接龙变更推送合并进一张卡片气泡：在 msg.relayContent 的基础上换上推送里的名单与状态，
 *  写回 msg.relayContent，再按合并后的内容重填 relayXxx 字段。
 *  @param msg       [入参/出参] 气泡数据
 *  @param notify    已解析的 cmd_relay_changed_notify 推送
 *  @param selfId    当前登录用户 userid
 *  @param selfMuted 当前用户是否被禁言
 *  @param nowMs     本机当前时间，Unix 毫秒
 *  @param names     名字解析接口；调用期间须有效
 *  @return msg 是接龙卡片（kind 为 kMsgRelay）且 msgid 与推送相同时合并并返回 true；
 *          否则（例如卡片已被撤回、变成了提示行）不改动 msg，返回 false
 */
bool ApplyRelayNotifyToMessage(ChatMessage& msg, const net::RelayChangedNotify& notify, int64_t selfId,
                               bool selfMuted, int64_t nowMs, const IRelayNameResolver& names);

/**
 *  判断一张卡片是否在填写之后到了截止时间、需要按当前时间重填（重填后显示为已截止、按钮消失）。
 *  @param msg   气泡数据
 *  @param nowMs 本机当前时间，Unix 毫秒
 *  @return 是接龙卡片、填写时还在进行中、有截止时间且 nowMs 已到截止时间时返回 true
 */
bool RelayCardDueForExpiry(const ChatMessage& msg, int64_t nowMs);

/**
 *  按 msg.relayContent 与当前时间重填一张卡片的 relayXxx 字段（用于截止时间到了之后的刷新，
 *  以及群名片、通讯录名字加载完之后的刷新）。不是接龙卡片时不做任何事。
 *  @param msg       [入参/出参] 气泡数据
 *  @param selfId    当前登录用户 userid
 *  @param selfMuted 当前用户是否被禁言
 *  @param nowMs     本机当前时间，Unix 毫秒
 *  @param names     名字解析接口；调用期间须有效
 */
void RefreshRelayCard(ChatMessage& msg, int64_t selfId, bool selfMuted, int64_t nowMs,
                      const IRelayNameResolver& names);

/**
 *  生成一张卡片「复制」时要放进剪贴板的完整文字：msg.relayCopyBody 接上按 nowMs 现拼的末行。
 *  填写之后到了截止时间但还没来得及重填的卡片，末行按已截止写。
 *  @param msg   接龙卡片的气泡数据
 *  @param nowMs 本机当前时间，Unix 毫秒
 *  @return 要放进剪贴板的文字（UTF-16）
 */
CString RelayCopyTextForMessage(const ChatMessage& msg, int64_t nowMs);

/**
 *  生成接龙卡片的摘要「[接龙] 标题」，标题为空（只有旧数据会出现）时为「[接龙]」。
 *  与服务端 MessageProtocol 的会话摘要写法相同，会话列表、托盘、引用、消息记录窗都用它。
 *  @param contentJson 卡片 content 原文（UTF-8 JSON）；不是 JSON 对象时按标题为空处理
 *  @return 摘要文字（UTF-16）
 */
CString BuildRelayDigest(const std::string& contentJson);

/**
 *  接龙操作失败时给用户看的提示（文字 2026-10-02 与用户确认）。按服务端回的错误码给出具体
 *  原因，认不出的错误码（含服务端内部错误）按操作分别写「参与接龙失败 / 撤回失败 /
 *  结束接龙失败，请稍后重试。」。
 *  @param cmd  失败的命令：cmd_join_relay / cmd_withdraw_relay / cmd_close_relay
 *  @param code 服务端回的错误码（非 0），取值见 enum error_code
 *  @return 提示文字（UTF-16）
 */
CString RelayOpErrorText(int cmd, int code);

} // namespace balloonchatwnd
