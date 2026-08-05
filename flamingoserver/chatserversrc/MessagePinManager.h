/**
 *  消息 Pin 存储管理器，MessagePinManager.h
 *
 *  负责 t_message_pin 表的落库与查询。Pin 是<u>会话内共享</u>的状态——单聊里对方也
 *  看得到，群里全体成员都看得到，故本表不含"归属用户"这一维，主键只有 f_msgid。
 *  每次操作单独建立一条数据库连接（与 MessageManager / FavoriteManager 同一风格），
 *  不在内存做缓存 —— Pin 是低频写、按会话一次性读，缓存不划算还要处理失效。
 *
 *  典型用法：
 *      会话层 ChatSession::onPinMessage 收到 cmd_pin_message 后调
 *      Singleton<MessagePinManager>::Instance().pinMessage / unpinMessage；
 *      ChatSession::onGetPinnedMessages 调 getPinnedMessages 组装应答。
 *
 *  注意与 1901~1903 的"收藏消息"、以及 t_conversation.f_pinned 的"会话置顶"都是
 *  两回事：前者是把消息存进个人收藏跨会话查看，后者是把整个会话排到列表顶部。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "MessageProtocol.h"   //MessageRecord / PinRecord

//一次 cmd_get_pinned_messages 最多返回的消息条数。
//Pin 的<u>总数不限</u>，这里限制的只是单次应答的体量：每条都带完整消息体，一个被钉了
//上千条的群会把包体撑到离谱（chatserver 单帧包体上限 MAX_BODY_CHATSERVER=1MiB）。
//超出的那些并没有丢——它们仍在 t_message_pin 里，客户端本地库若有对应行也照常带着
//Pin 标记，只是不由这次拉取来补齐。应答的 total 字段带回真实总数，客户端据它判断
//是否发生了截断，进而决定本地对账的时间下界（见 docs/消息Pin接入方案.md §4.3）。
constexpr int PIN_FETCH_LIMIT = 200;

//注：Pin 记录结构 PinRecord 定义在 MessageProtocol.h —— 它既是本表的一行、也是
//cmd_get_pinned_messages 应答里 pin 子对象的来源，放在那边两处都能用。

//消息 Pin 存储管理器；全局单例，经 Singleton<MessagePinManager> 访问
class MessagePinManager final
{
public:
    MessagePinManager();
    ~MessagePinManager();

    MessagePinManager(const MessagePinManager& rhs) = delete;
    MessagePinManager& operator=(const MessagePinManager& rhs) = delete;

    /**
     *  初始化（Pin 记录不预加载到内存）。
     *
     *  @return 恒返回 true。留这个方法是为了与其余 Manager 的启动形态一致，
     *          main.cpp 里挨个 init 时不必对本类破例。
     */
    bool init();

    /**
     *  钉住一条消息。
     *
     *    msgid：      被Pin的消息msgid，须 > 0。
     *    operatorId： 执行Pin的用户userid，落进 f_operator 作为日后取消Pin的权限依据。
     *    now：        当前时刻，Unix 毫秒，写入 f_pin_time。由调用方传入而非本类自取，
     *                 便于测试构造确定的时间序。
     *
     *  @return true 操作成功；false 表示参数非法或数据库不可用。
     *
     *  <u>幂等</u>：重复钉住同一条消息不会产生第二行、也不算失败，且**不刷新**
     *  f_pin_time 与 f_operator —— 否则甲钉的消息被乙点一下"Pin"，取消权限就悄悄
     *  转到乙手里了。
     *
     *  本方法<u>不校验</u>"消息是否存在""请求者在不在这个会话里""有没有权限"，
     *  那些属于会话层的业务判定。
     */
    bool pinMessage(int64_t msgid, int64_t operatorId, int64_t now);

    /**
     *  取消一条消息的Pin（真删 t_message_pin 的那一行）。
     *
     *    msgid： 消息msgid，须 > 0。
     *
     *  @return true 操作成功；false 表示参数非法或数据库不可用。
     *
     *  <u>幂等</u>：取消一条本来就没钉住的消息同样返回成功，客户端连点两下不会收到
     *  莫名的报错。
     *
     *  权限判断<u>不在这里</u>：调用方须先用 getPinRecord 取出 f_operator，自行按
     *  "操作者本人 + 群主 / 管理员"的规则放行（会话层才认识群内角色）。
     */
    bool unpinMessage(int64_t msgid);

    /**
     *  查一条消息的Pin记录。
     *
     *    msgid： 消息msgid。
     *    out：   出参，仅返回 true 时有效。
     *
     *  @return true 该消息当前被钉着；false 表示没钉、msgid 非正或数据库不可用。
     *
     *  两个用途：判断"要不要幂等短路"，以及取出 f_operator 做取消Pin的权限判断。
     */
    bool getPinRecord(int64_t msgid, PinRecord& out);

    /**
     *  取某会话当前被Pin的消息，按消息时间<u>倒序</u>（最新的排最前）。
     *
     *    ctype：      会话类型：1 单聊、2 群聊（取值见 conversation_type）。
     *    cid：        会话ID：单聊为对端userid，群聊为群id。
     *    selfUserId： 发起拉取的用户userid。单聊据它与 cid 一起定位双向消息；
     *                 两种会话都据它排除"该用户本端已删除"的消息。
     *    limit：      本次最多返回条数，须 > 0，一般传 PIN_FETCH_LIMIT。
     *    out：        出参，命中的消息（最新在前）；调用前会被清空。
     *    outPins：    出参，与 out <u>一一对应</u>的Pin元信息（谁钉的、什么时候钉的）；
     *                 调用前会被清空。它与消息在同一条联表查询里一起取回 —— 逐条再查
     *                 一次的话，200 条就是 200 次建连接，那个代价不该由拉一次列表来付。
     *    outTotal：   出参，**不受 limit 约束**的真实总数。客户端据它与 out.size()
     *                 比较判断是否发生截断，进而决定本地对账的时间下界。
     *
     *  @return true 查询成功（一条都没有也算成功）；false 表示参数非法或数据库不可用。
     *
     *  已撤回的消息不计入、也不返回：撤回时会顺手删掉它的Pin行（见 ChatSession::
     *  onRecallMessage），这里的 f_recalled = 0 条件是双保险，防住历史脏数据。
     */
    bool getPinnedMessages(int ctype, int64_t cid, int64_t selfUserId, int limit,
                           std::vector<MessageRecord>& out, std::vector<PinRecord>& outPins,
                           int64_t& outTotal);

    /**
     *  删除某条消息的Pin行，不做任何权限判断。
     *
     *    msgid： 消息msgid。
     *
     *  @return true 执行成功（本来就没钉住、删到 0 行也算成功）；false 表示数据库不可用。
     *
     *  用在消息被撤回这类"这条消息不该再出现在任何地方"的场合。与 unpinMessage 的
     *  区别只在语义：那个是用户主动取消，这个是系统清理，日志与调用点都不一样。
     */
    bool removePinForMessage(int64_t msgid);

    /**
     *  删除某个群里全部消息的Pin行。
     *
     *    groupid： 群id。
     *
     *  @return true 执行成功（一条都没删到也算成功）；false 表示数据库不可用。
     *
     *  用在群被解散这类"整个会话不复存在"的场合。<u>退群不要调本方法</u>——人退了群，
     *  群里的Pin与他无关，但对留下的人仍然有效。
     */
    bool removePinsForGroup(int64_t groupid);
};
