/**
 *  v2 群互动卡片相关报文（cmd_vote_poll / cmd_poll_changed_notify /
 *  cmd_join_relay / cmd_relay_changed_notify / cmd_update_todo /
 *  cmd_todo_changed_notify）的编解码。
 *
 *  卡片本体（投票 / 接龙 / 待办）是一条 msgtype=10/11/12 的消息；本模块处理
 *  对卡片的交互。本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，
 *  便于单元测试（见 tests/test_cardprotocol.cpp）。互动落库与推送在会话层。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

//一条接龙文本的长度上限，单位为字符（UTF-8 码点），不是字节。超长的文本整条拒绝、不截断：
//截断会悄悄改掉用户写的内容。100 个字符按 UTF-8 最多 400 字节，在 t_card_interaction.f_data
//的 varchar(1024) 之内。客户端接入时按同一取值限制输入框（见 docs/群接龙方案.md §4.8）。
constexpr size_t RELAY_TEXT_MAX_CHARS = 100;

//接龙卡片标题的长度上限，单位为字符（UTF-8 码点）。标题在卡片、系统提示与会话摘要里都只占
//一行，超长整条拒绝、不截断（见 docs/群接龙方案.md §4.8）
constexpr size_t RELAY_TITLE_MAX_CHARS = 50;

//接龙卡片说明的长度上限，单位为字符（UTF-8 码点）。说明允许多行，超长整条拒绝、不截断
constexpr size_t RELAY_NOTE_MAX_CHARS = 200;

//一张接龙卡片最多接受多少人参与（每人一条，即名单的最大条数）。2026-10-02 与用户确认取 200：
//每次参与、修改、撤回都要把完整名单推给会话里的所有人，一条写满 100 字的接龙经 jsoncpp 转义
//后约 630 字节，200 条约 130 KB，远低于客户端 1 MiB 的单帧上限。已在名单里的人修改不受此限。
//判断时不加锁，两人同时抢最后一个名额时偶尔会多出一两条（同日与用户确认），不影响体积目标
constexpr size_t RELAY_MAX_PARTICIPANTS = 200;

//发起接龙时截止时间至少要比服务端当前时间晚多少，单位毫秒。2026-10-03 与用户确认：发起接龙必须
//设置截止时间，且不能早于当前时间 + 5 分钟，客户端与服务端按同一规则检查；客户端的同名常量见
//flamingoclient/Source/net/CardProtocolClient.h
constexpr int64_t RELAY_DEADLINE_MIN_LEAD_MS = 5LL * 60LL * 1000LL;

//一条卡片互动记录，对应数据库 t_card_interaction 的一行
struct CardInteractionRecord
{
    int64_t     userid;      //互动用户userid
    std::string data;        //互动数据：投票为选项下标JSON数组 / 接龙为接龙文本 / 待办为勾选标记
    int64_t     createtime;  //互动时间，Unix毫秒

    CardInteractionRecord()
        : userid(0), createtime(0)
    {
    }
};

//一次参与接龙（cmd_join_relay）属于哪一种，由 CardProtocol::classifyRelayJoin 判定。
//服务端内部使用，不出现在协议里
enum relay_join_kind
{
    relay_join_new       = 1,   //名单里还没有本人：首次参与，占一个新名额，群里发「参与了」提示
    relay_join_modify    = 2,   //名单里已有本人、内容不同：修改本人那条，名次不变，群里发「修改了」提示
    relay_join_unchanged = 3,   //名单里已有本人、内容相同（如双击提交）：直接回成功，不写库、不推送、不发提示
};

//接龙相关的群系统提示种类，决定 CardProtocol::buildRelayTip 生成的文字。服务端内部使用
enum relay_tip_kind
{
    relay_tip_join     = 1,   //首次参与：「张三」参与了接龙「标题」
    relay_tip_modify   = 2,   //修改本人那条：「张三」修改了在接龙「标题」中的内容
    relay_tip_withdraw = 3,   //撤回本人那条：「张三」撤回了在接龙「标题」中的内容
    relay_tip_close    = 4,   //提前结束：「李四」结束了接龙「标题」，共 N 人参与
};

//卡片的结束记录，对应数据库 t_card_state 的一行；表里没有这一行表示卡片从未被提前结束
struct CardStateRecord
{
    bool    closed;     //是否已被提前结束
    int64_t closer;     //提前结束的操作者userid
    int64_t closetime;  //提前结束的时间，Unix毫秒

    CardStateRecord()
        : closed(false), closer(0), closetime(0)
    {
    }
};

//接龙卡片在某一时刻的状态，由 CardProtocol::computeRelayStatus 算出，
//供 cmd_relay_changed_notify 推送与下发卡片消息时合并进卡片内容
struct RelayStatus
{
    int     state;      //接龙状态，取值见 Protocol.h 的 enum relay_state；缺省 0 即进行中
    int64_t closedby;   //提前结束的操作者userid；未被提前结束时为 0
    int64_t closetime;  //结束时间，Unix毫秒：提前结束时为操作时间，已截止时为截止时间，进行中为 0

    RelayStatus()
        : state(0), closedby(0), closetime(0)
    {
    }
};

//cmd_vote_poll 请求
struct VotePollRequest
{
    int              ctype;    //会话类型：1 单聊 2 群聊
    int64_t          cid;      //会话ID
    int64_t          msgid;    //投票卡片所在消息id
    std::vector<int> options;  //所投选项的下标列表（多选时可多个）

    VotePollRequest()
        : ctype(1), cid(0), msgid(0)
    {
    }
};

//cmd_join_relay 请求
struct JoinRelayRequest
{
    int         ctype;  //会话类型
    int64_t     cid;    //会话ID
    int64_t     msgid;  //接龙卡片所在消息id
    std::string text;   //本人的接龙文本

    JoinRelayRequest()
        : ctype(1), cid(0), msgid(0)
    {
    }
};

//只指明一张卡片的请求：cmd_withdraw_relay（撤回本人那条接龙）共用，
//cmd_close_relay（提前结束接龙）接入后同样使用
struct RelayTargetRequest
{
    int     ctype;  //会话类型：1 单聊 2 群聊；缺省取 1
    int64_t cid;    //会话ID：群聊为群id，单聊为对端userid
    int64_t msgid;  //接龙卡片所在消息id

    RelayTargetRequest()
        : ctype(1), cid(0), msgid(0)
    {
    }
};

//cmd_update_todo 请求
struct UpdateTodoRequest
{
    int     ctype;  //会话类型
    int64_t cid;    //会话ID
    int64_t msgid;  //待办卡片所在消息id
    int     done;   //勾选状态：1 勾选完成 0 取消勾选

    UpdateTodoRequest()
        : ctype(1), cid(0), msgid(0), done(0)
    {
    }
};

//群互动卡片报文编解码工具类；全部为静态方法，不可实例化
class CardProtocol
{
public:
    /**
     *  解析 cmd_vote_poll 请求；cid/msgid 缺失非正、或 options 缺失/为空返回 false。
     */
    static bool parseVotePollRequest(const std::string& json, VotePollRequest& req);

    /**
     *  解析 cmd_join_relay 请求；cid/msgid 缺失非正、或 text 缺失/为空返回 false。
     */
    static bool parseJoinRelayRequest(const std::string& json, JoinRelayRequest& req);

    /**
     *  判断取到的卡片消息能否用于本次互动（投票、接龙、待办三个处理函数共用）。
     *  @param msgtype         卡片消息的内容类型
     *  @param recalled        卡片消息是否已撤回
     *  @param expectedMsgtype 本次互动要求的卡片类型：message_type_poll / message_type_relay /
     *                         message_type_todo
     *  @return 0 表示可以互动；
     *          error_code_message_not_exist 表示卡片已撤回。撤回的判断先于类型：卡片撤回后，
     *          对客户端来说它已经不存在，无论类型是否相符都按"卡片不存在"回复；
     *          error_code_bad_request 表示卡片类型与本次互动不符
     */
    static int checkCardTarget(int msgtype, bool recalled, int expectedMsgtype);

    /**
     *  规范化一条接龙文本，供 cmd_join_relay 在落库前调用。
     *  先去掉首尾空白（半角空格、制表符、回车、换行、全角空格 U+3000），再依次校验：
     *  去空白后不能为空；中间不能含回车或换行（每条接龙只有一行，卡片、详情窗与复制出的
     *  序号列表都按单行排版）；长度不超过 RELAY_TEXT_MAX_CHARS 个字符。
     *  @param raw 客户端提交的原始文本（UTF-8）
     *  @param out [出参] 去掉首尾空白后的文本；只在返回 0 时写入
     *  @return 0 表示合法；
     *          error_code_bad_request 表示去空白后为空，或中间含回车、换行；
     *          error_code_msg_too_large 表示超过 RELAY_TEXT_MAX_CHARS 个字符
     */
    static int normalizeRelayText(const std::string& raw, std::string& out);

    /**
     *  规范化发送接龙卡片（cmd_send_message，msgtype=11）时客户端提交的 content，
     *  供 onSendMessage 在落库与推送之前调用，单聊、群聊共用。
     *  输出只保留 title、note、deadline 三个字段，客户端自带的其它字段（items、state 等）
     *  一律丢弃，防止伪造接龙名单与结束状态；这些状态由服务端在下发时另行合并。
     *    - title：必填字符串，去掉首尾空白（规则同 normalizeRelayText）后不能为空、中间不能含
     *      回车或换行，最多 RELAY_TITLE_MAX_CHARS 个字符；
     *    - note：选填字符串，缺省或 null 视为空串；去掉首尾空白，中间的换行保留，
     *      最多 RELAY_NOTE_MAX_CHARS 个字符；
     *    - deadline：必填，截止时间，Unix 毫秒整数，不能早于 nowMs + RELAY_DEADLINE_MIN_LEAD_MS
     *      （2026-10-03 起；此前可以缺省、为 0 表示不限）。字符串形式的数字、小数、负数一律视为非法。
     *  @param content 客户端提交的 content JSON 文本
     *  @param nowMs   服务端当前时间，Unix 毫秒；用于判断截止时间是否够晚
     *  @param out     [出参] 规范化后的紧凑 JSON 文本；只在返回 0 时写入
     *  @return 0 表示合法；
     *          error_code_bad_request 表示 content 不是 JSON 对象、字段类型不对、标题为空或含换行；
     *          error_code_msg_too_large 表示标题或说明超过长度上限；
     *          error_code_relay_deadline_invalid 表示没有设置截止时间（缺省、null 或 0），
     *          或截止时间早于 nowMs + RELAY_DEADLINE_MIN_LEAD_MS
     */
    static int normalizeRelayContent(const std::string& content, int64_t nowMs, std::string& out);

    /**
     *  判断某类消息能否被逐条转发（cmd_forward_message 且 merge=0）。
     *  接龙卡片不能逐条转发：转发出去的是一条新消息，没有原卡片的参与记录，在别的会话里参与
     *  也不会同步回原卡片。合并转发不受本函数限制，接龙卡片照常作为子消息打包。
     *  投票、待办两种卡片等其客户端接入时再按同一规则处理，目前返回 true。
     *  @param msgtype 源消息的内容类型
     *  @return 可以逐条转发返回 true
     */
    static bool canForwardSeparately(int msgtype);

    /**
     *  解析只指明一张卡片的请求 { ctype, cid, msgid }（cmd_withdraw_relay 使用）。
     *  @param json 请求包体
     *  @param req  [出参] 解析结果；ctype 缺省取 1
     *  @return cid 或 msgid 缺失、不是正数时返回 false
     */
    static bool parseRelayTargetRequest(const std::string& json, RelayTargetRequest& req);

    /**
     *  构造接龙操作（cmd_join_relay / cmd_withdraw_relay）的应答：
     *  { code, msg, ctype, cid, msgid }。无论成败都带回请求里的 ctype / cid / msgid，客户端据
     *  msgid 把结果送到对应卡片的详情窗。请求解析失败、拿不到这三个值时改用 buildSimpleResponse。
     *  @param code  错误码，0 表示成功
     *  @param msg   结果说明（英文）
     *  @param ctype 请求里的会话类型
     *  @param cid   请求里的会话ID，按字符串编码
     *  @param msgid 请求里的卡片消息id，按字符串编码
     */
    static std::string buildRelayOpResponse(int code, const std::string& msg,
                                            int ctype, int64_t cid, int64_t msgid);

    /**
     *  算出某个收件人视角下卡片所在会话的会话ID，用于给卡片会话的成员逐个推送。
     *  群卡片对所有人都是群id；单聊卡片的会话ID是"对方"的userid，收发双方各不相同。
     *  @param ctype    卡片消息的会话类型：1 单聊 2 群聊
     *  @param from     卡片消息的发送方userid
     *  @param to       卡片消息的接收方：单聊为对端userid，群聊为群id
     *  @param viewerId 收件人userid；单聊卡片时应为 from 或 to 之一
     *  @return 该收件人视角下的会话ID
     */
    static int64_t cardConversationIdFor(int ctype, int64_t from, int64_t to, int64_t viewerId);

    /**
     *  由结束记录、截止时间与当前时间算出接龙状态。
     *  手动结束优先：有结束记录时一律为"已提前结束"，即使截止时间也已经过了；
     *  否则截止时间非 0 且当前时间不早于它时为"已截止"（等于截止时间即算截止，与发卡片时
     *  "截止时间必须晚于当前时间"的口径一致）；其余为"进行中"。
     *  @param stateRow 该卡片在 t_card_state 里的记录；没有记录时传缺省构造的对象
     *  @param deadline 卡片内容里的截止时间，Unix毫秒；0 表示不限
     *  @param nowMs    服务端当前时间，Unix毫秒
     *  @return 接龙状态；已截止时 closetime 取截止时间、closedby 为 0
     */
    static RelayStatus computeRelayStatus(const CardStateRecord& stateRow, int64_t deadline, int64_t nowMs);

    /**
     *  从接龙卡片的 content 里取出截止时间。
     *  @param content 卡片消息的 content JSON 文本（已经过 normalizeRelayContent 规范化）
     *  @return 截止时间，Unix毫秒；字段缺失、内容不是 JSON 对象、类型不是整数或为负数时返回 0
     */
    static int64_t getRelayDeadline(const std::string& content);

    /**
     *  判断某人能否提前结束一张接龙。发起人总可以；群卡片另外允许群主与管理员；
     *  单聊卡片只有发起人可以。提前结束是管理动作，不受禁言限制，本函数不涉及禁言。
     *  @param isGroupCard  卡片是否在群聊里
     *  @param creatorId    卡片发起人（卡片消息的发送方）userid
     *  @param operatorId   请求结束的人的userid
     *  @param operatorRole 请求结束的人在群里的角色，取值见 group_member_role；单聊卡片时忽略
     */
    static bool canCloseRelay(bool isGroupCard, int64_t creatorId, int64_t operatorId, int operatorRole);

    /**
     *  参与接龙时的人数上限判断。已在名单里的人是修改，不占新名额，总是放行；
     *  新人只有在当前名单不足 RELAY_MAX_PARTICIPANTS 条时才放行。
     *  @param alreadyJoined 请求者是否已在名单里
     *  @param currentCount  当前名单条数
     *  @return 0 表示放行；error_code_card_full 表示名额已满
     */
    static int checkRelayJoinCapacity(bool alreadyJoined, size_t currentCount);

    /**
     *  把接龙卡片的当前名单与状态合并进卡片 content，供 cmd_sync_messages、
     *  cmd_get_pinned_messages、cmd_search_messages 下发卡片消息时使用。
     *  保留 title / note / deadline，写入 items（以 rows 为准，覆盖 content 里可能残留的旧值）
     *  以及 state / closedby / closetime。
     *  @param content 卡片消息原始 content JSON 文本
     *  @param rows    该卡片的全部互动记录，按名次排列
     *  @param status  该卡片当前的状态（见 computeRelayStatus）
     *  @return 合并后的 content JSON 文本；content 不是 JSON 对象时原样返回
     */
    static std::string augmentRelayContent(const std::string& content,
                                           const std::vector<CardInteractionRecord>& rows,
                                           const RelayStatus& status);

    /**
     *  判断一条消息下发时要不要合并卡片的当前互动状态（名单、票数、勾选情况）。
     *  只有投票、接龙、待办三种卡片需要；已撤回的卡片不合并，名单与投票结果不随它下发
     *  （撤回的卡片与其互动数据都保留在库里供审计，2026-10-02 与用户确认不删）。
     *  @param msgtype  消息内容类型
     *  @param recalled 消息是否已撤回
     *  @return 需要合并返回 true
     */
    static bool shouldAugmentCard(int msgtype, bool recalled);

    /**
     *  判定一次参与接龙属于首次参与、修改还是内容未变的重复提交。
     *  @param rows   该卡片当前的全部互动记录
     *  @param userid 参与者userid
     *  @param text   本次提交的接龙文本（已经过 normalizeRelayText 规范化）
     *  @return relay_join_new / relay_join_modify / relay_join_unchanged 之一
     */
    static int classifyRelayJoin(const std::vector<CardInteractionRecord>& rows, int64_t userid,
                                 const std::string& text);

    /**
     *  生成接龙相关的群系统提示文字（docs/群接龙方案.md §3.4）。提示里不带接龙文本本身，
     *  避免把超长或不合适的内容再复制一遍。
     *  @param kind     提示种类，取值见 relay_tip_kind；未知取值按"更新了接龙"兜底
     *  @param nickname 操作者昵称（UTF-8）；为空时写成「有成员」
     *  @param title    接龙标题（UTF-8）；为空时省略标题，只写「接龙」
     *  @param count    当前参与人数，只在 relay_tip_close 时写进文字
     *  @return 提示文字（UTF-8）
     */
    static std::string buildRelayTip(int kind, const std::string& nickname, const std::string& title,
                                     size_t count);

    /**
     *  从接龙卡片的 content 里取出标题，供系统提示使用。
     *  @param content 卡片消息的 content JSON 文本
     *  @return 标题（UTF-8）；字段缺失、不是字符串、内容不是 JSON 对象时返回空串
     */
    static std::string getRelayTitle(const std::string& content);

    /**
     *  接龙提示种类对应的系统提示类型（content 的 tipkind，多语言第 5 期）。
     *  @param kind 提示种类，取值见 relay_tip_kind；未知取值返回 kSystemTipRelayUpdate（与 buildRelayTip 的兜底一致）
     *  @return Protocol.h 里 kSystemTipRelay* 之一，指向静态字符串
     */
    static const char* relayTipKindName(int kind);

    /**
     *  构造接龙系统提示的 content：{ text, cardmsgid, tipkind, tipargs }。text 是 buildRelayTip 的中文，
     *  与文本消息同构（老客户端照常按 text 显示）；cardmsgid 是卡片所在消息id，客户端据此让提示行可以点击
     *  并打开对应卡片的详情窗；tipkind / tipargs（多语言第 5 期）供新客户端按界面语言生成文字：
     *  tipargs { nickname, title[, count] }，昵称或标题为空时不写，count 只在 relay_tip_close 时写。
     *  @param kind      提示种类，取值见 relay_tip_kind
     *  @param nickname  操作者昵称（UTF-8），可为空
     *  @param title     接龙标题（UTF-8），可为空
     *  @param count     当前参与人数，只在 relay_tip_close 时用到
     *  @param cardMsgId 接龙卡片所在消息id，按字符串编码
     *  @return content 的紧凑 JSON 文本
     */
    static std::string buildRelayTipContent(int kind, const std::string& nickname, const std::string& title,
                                            size_t count, int64_t cardMsgId);

    /**
     *  为一条接龙系统提示生成占位的客户端消息ID。系统消息没有真实的客户端消息ID，按
     *  sendGroupSystemMessage 的约定以 "sys-" 开头；拼上提示种类、卡片、操作者与时间，
     *  使不同操作产生的提示各不相同，不会被按 clientmsgid 去重误当成同一条。
     *  @param kind      提示种类，取值见 relay_tip_kind
     *  @param cardMsgId 接龙卡片所在消息id
     *  @param userid    操作者userid
     *  @param nowMs     操作时间，Unix毫秒
     *  @return 形如 sys-relay-<kind>-<cardMsgId>-<userid>-<nowMs> 的字符串
     */
    static std::string buildRelayTipClientMsgId(int kind, int64_t cardMsgId, int64_t userid, int64_t nowMs);

    /**
     *  统计 UTF-8 文本的字符数（码点数）。逐字节扫描，凡不是续字节（10xxxxxx）的字节都计
     *  一个字符；不校验编码是否合法，非法序列按其中的非续字节个数计数。
     *  @param text UTF-8 文本，可以为空
     *  @return 字符数；空串返回 0
     */
    static size_t utf8CharCount(const std::string& text);

    /**
     *  解析 cmd_update_todo 请求；cid/msgid 缺失非正、或 done 缺失返回 false。
     */
    static bool parseUpdateTodoRequest(const std::string& json, UpdateTodoRequest& req);

    /**
     *  把投票选项下标列表序列化为紧凑JSON数组文本，作为 t_card_interaction.f_data。
     */
    static std::string buildVoteData(const std::vector<int>& options);

    /**
     *  从投票卡片的 content（定义）中取出元信息。
     *  @param content     投票卡片消息的 content JSON 文本
     *  @param optioncount [出参] 选项个数
     *  @param multi       [出参] 是否多选：0 单选 1 多选
     *  @param expiretime  [出参] 截止时间，Unix毫秒；0 表示不过期
     *  @return content 为合法投票卡片定义返回 true
     */
    static bool getPollMeta(const std::string& content, int& optioncount,
                            int& multi, int64_t& expiretime);

    /**
     *  判断某用户是否在待办卡片的 assignees（负责人）列表中。
     *  @param content 待办卡片消息的 content JSON 文本
     *  @param userid  待判定用户userid
     */
    static bool isTodoAssignee(const std::string& content, int64_t userid);

    /**
     *  构造 cmd_poll_changed_notify 推送 JSON：{ msgid, votes:{下标:票数}, totalcount }。
     *  仅含聚合票数，不暴露投票人（匿名投票安全）。
     */
    static std::string buildPollChangedNotify(int64_t msgid,
                                              const std::vector<CardInteractionRecord>& rows);

    /**
     *  构造 cmd_relay_changed_notify 推送 JSON：
     *  { ctype, cid, msgid, items:[{userid,text}], state, closedby, closetime }。
     *  带上 ctype / cid，客户端无需逐个窗口查找即可定位卡片所在的会话。
     *  @param ctype  卡片所在会话的类型：1 单聊 2 群聊
     *  @param cid    收件人视角下的会话ID（见 cardConversationIdFor），按字符串编码
     *  @param msgid  卡片消息id，按字符串编码
     *  @param rows   该卡片的全部互动记录，按名次排列；为空时 items 为空数组
     *  @param status 该卡片当前的状态（见 computeRelayStatus）；closedby 按字符串编码
     */
    static std::string buildRelayChangedNotify(int ctype, int64_t cid, int64_t msgid,
                                               const std::vector<CardInteractionRecord>& rows,
                                               const RelayStatus& status);

    /**
     *  构造 cmd_todo_changed_notify 推送 JSON：{ msgid, doneusers:[userid] }。
     */
    static std::string buildTodoChangedNotify(int64_t msgid,
                                              const std::vector<CardInteractionRecord>& rows);

    /**
     *  把卡片当前互动状态合并进卡片消息的 content。
     *  投票并入 votes/totalcount，接龙并入 items，待办并入 doneusers；供
     *  cmd_sync_messages 下发卡片消息时使用，使客户端打开会话即见最新状态。
     *  @param msgtype 卡片消息类型（10 投票 / 11 接龙 / 12 待办；其它原样返回）
     *  @param content 卡片消息原始 content JSON 文本
     *  @param rows    该卡片的全部互动记录
     *  @return 合并后的 content JSON 文本；content 非法时原样返回
     */
    static std::string augmentCardContent(int msgtype, const std::string& content,
                                          const std::vector<CardInteractionRecord>& rows);

    /**
     *  构造通用 { code, msg } 应答。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

private:
    CardProtocol() = delete;
    ~CardProtocol() = delete;
    CardProtocol(const CardProtocol&) = delete;
    CardProtocol& operator=(const CardProtocol&) = delete;
};
