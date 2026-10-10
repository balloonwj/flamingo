/**
 *  v2 会话列表报文编解码（客户端侧）：解析 cmd_sync_conversations(1501) 应答，
 *  构造 cmd_set_conversation(1513) 会话设置（置顶 / 消息提醒模式 / 删除）请求，
 *  以及消息提醒模式的批量设置 cmd_set_notify_modes(1524) 与其应答、
 *  cmd_notify_modes_changed_notify(1525) 推送（2026-10-03 起，见
 *  docs/消息提醒模式与消息盒子方案.md）。
 *
 *  本模块与服务端 chatserversrc/MessageProtocol 互为镜像——服务端构造同步应答 /
 *  解析会话设置请求，客户端解析同步应答 / 构造会话设置请求。纯逻辑，只依赖
 *  jsoncpp，不触及网络与 UI，便于单元测试，见
 *  flamingoclient/Source/Tests/ConversationsProtocolTests.cpp。
 *
 *  会话名称 / 头像不在本应答里——客户端按 m_cid 去通讯录缓存查；最后一条消息
 *  的预览文字 m_digest 由服务端拼好（中文），解析时能在本地生成的类型换成当前界面语言（多语言第 5 期）。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//一条会话（cmd_sync_conversations 应答里 conversations 数组的一项）。
struct ConversationItem
{
    int         m_ctype;        //会话类型：见 enum conversation_type，1单聊 2群聊
    int64_t     m_cid;          //会话ID：单聊=对端userid，群聊=群id
    int         m_unread;       //未读消息数
    int         m_pinned;       //是否置顶：0否 1是
    int         m_notifyMode;   //消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode；
                                //缺失或不认识的值按 0（允许提醒）。主面板以 ConversationsResult::m_notifyModes
                                //建立的模式表为准，本字段只用于核对与日志
    int64_t     m_lastmsgid;    //最后一条消息的服务器msgid
    int64_t     m_lastFrom;     //最后一条消息的发送方userid
    int         m_lastMsgType;  //最后一条消息内容类型：见 enum message_type
    std::string m_digest;       //最后一条消息的预览文字（UTF-8，如"[图片]"/正文）。服务端拼好的是中文，解析时
                                //能在本地生成的类型已换成当前界面语言（多语言第 5 期，见 .cpp 的 LocalizeDigest）
    int64_t     m_timestamp;    //最后一条消息时间（Unix 毫秒）
    bool        m_lastRecalled; //最后一条消息是否已被撤回（lastmsg.recalled）。为真时 m_digest 是服务端的
                                //中性提示「撤回了一条消息」，主面板按撤回者改写成「你 / 某某撤回了一条消息」
    int64_t     m_lastRecalledBy; //最后一条消息的撤回者 userid（lastmsg.recalledby）；0 表示未撤回或服务端
                                  //没有记录撤回者，此时按 m_lastFrom 处理
    bool        m_unreadMark;   //本人是否把该会话手动标为未读（unreadmark，docs/会话标为未读方案.md）；老服务端不带时为 false
    int64_t     m_threadPostId; //最后一条消息是帖子卡片或帖子动态卡片时，那条帖子的 ID（lastmsg.threadpostid，
                                //docs/帖子群方案.md 6.2）；点这个会话时帖子群窗口定位到它。其余为 0

    ConversationItem()
        : m_ctype(0)
        , m_cid(0)
        , m_unread(0)
        , m_pinned(0)
        , m_notifyMode(0)
        , m_lastmsgid(0)
        , m_lastFrom(0)
        , m_lastMsgType(0)
        , m_timestamp(0)
        , m_lastRecalled(false)
        , m_lastRecalledBy(0)
        , m_unreadMark(false)
        , m_threadPostId(0)
    {
    }
};

//cmd_conversation_changed_notify(1514) 推送解析结果 —— 会话属性的多端同步。
//
//某一端改了会话的置顶 / 消息提醒模式 / 删除之后，服务端把该会话<u>读回来的完整状态</u>推给这个
//用户的每一个在线端（含发起改动的那一端自己，见服务端 ChatSession::sendToUser）。所以
//本推送带的是"改完之后是什么样"，而不是"这次改了什么"，接收端直接照它覆盖本地状态即可，
//重复收到同一状态也不会出问题。
struct ConversationChangedNotify
{
    int     m_ctype;     //会话类型：见 enum conversation_type，1单聊 2群聊
    int64_t m_cid;       //会话ID：单聊=对端userid，群聊=群id
    int     m_pinned;    //改完之后是否置顶：0否 1是
    int     m_notifyMode; //改完之后的消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode
    int     m_deleted;   //改完之后是否已从会话列表删除：0否 1是
    bool    m_unreadMark; //改完之后是否被本人手动标为未读（docs/会话标为未读方案.md）；老服务端不带时为 false

    ConversationChangedNotify()
        : m_ctype(0)
        , m_cid(0)
        , m_pinned(0)
        , m_notifyMode(0)
        , m_deleted(0)
        , m_unreadMark(false)
    {
    }
};

//本人在一个群里「关注的成员」名单（docs/消息提醒模式与消息盒子方案.md §12）。用在三处：
//cmd_sync_conversations 应答顶层的 groupfollows 数组（只含名单不为空的群）、cmd_set_group_follow 请求、
//cmd_group_follow_changed_notify 推送（包体同请求，整份名单，接收端直接覆盖本地缓存）。
struct GroupFollowEntry
{
    int64_t               m_groupid;  //群ID
    std::vector<uint32_t> m_targets;  //关注的成员 userid；空表示清空

    GroupFollowEntry()
        : m_groupid(0)
    {
    }
};

//一个群最多关注多少位成员。必须与服务端 GroupProtocol.h 的 kGroupFollowMax 一致，
//选人窗口按它限制勾选数，超出的请求服务端会拒绝
const size_t kGroupFollowMax = 20;

//一个会话的消息提醒模式。用在三处：cmd_sync_conversations 应答顶层的 notifymodes 数组
//（本人全部非默认模式的会话，包括还没有消息的群与联系人）、cmd_set_notify_modes 请求的
//items、cmd_notify_modes_changed_notify 推送的 items。
struct NotifyModeEntry
{
    int     m_ctype;  //会话类型：1单聊 2群聊
    int64_t m_cid;    //会话ID：单聊=对端userid，群聊=群id
    int     m_mode;   //提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode

    NotifyModeEntry()
        : m_ctype(0)
        , m_cid(0)
        , m_mode(0)
    {
    }
};

//cmd_set_notify_modes 应答里的一条结果，与请求条目一一对应、顺序相同。
struct NotifyModeResult
{
    int     m_ctype;  //会话类型，服务端原样回带
    int64_t m_cid;    //会话ID，服务端原样回带
    int     m_code;   //该条的结果码：0 成功；210 特别关注已满；其余见 Protocol.h 的 enum error_code

    NotifyModeResult()
        : m_ctype(0)
        , m_cid(0)
        , m_code(0)
    {
    }
};

//cmd_set_notify_modes 应答解析结果。
struct SetNotifyModesResult
{
    int                           m_code;     //整单结果码：0 表示请求已受理（各条是否成功看 m_results）；-1 尚未解析出有效结果码
    std::string                   m_msg;      //结果描述
    std::vector<NotifyModeResult> m_results;  //逐条结果；整单失败时为空

    SetNotifyModesResult()
        : m_code(-1)
    {
    }
};

//cmd_notify_modes_changed_notify 推送解析结果：本人一批会话的提醒模式变了（只含改成功的条目）。
//发起批量设置的那一端自己也会收到，接收端直接照它覆盖本地模式表即可。
struct NotifyModesChangedNotify
{
    std::vector<NotifyModeEntry> m_items;  //变化后的模式
};

//cmd_set_notify_modes 单次请求可带的条目数上限。必须与服务端 MessageProtocol.h 的
//NOTIFY_MODE_BATCH_MAX 一致，超出的部分由 splitNotifyModeBatches 拆到下一批。
const size_t kNotifyModeBatchMax = 200;

//cmd_sync_conversations 应答解析结果。
struct ConversationsResult
{
    int                           m_code;      //结果码，0 成功
    std::string                   m_msg;       //结果描述
    int64_t                       m_synctime;  //服务器同步时间（Unix 毫秒）
    std::vector<ConversationItem> m_items;     //会话列表
    std::vector<NotifyModeEntry>  m_notifyModes; //本人全部非默认提醒模式的会话（应答顶层 notifymodes），主面板以它建立模式表
    std::vector<GroupFollowEntry> m_groupFollows; //本人在各群关注的成员（应答顶层 groupfollows，只含名单不为空的群）；老服务端不带时为空

    ConversationsResult()
        : m_code(-1)        //-1：尚未解析出有效结果码
        , m_synctime(0)
    {
    }
};

//会话列表报文编解码工具类；全部为静态方法，不可实例化。
class ConversationsProtocolClient
{
public:
    /**
     *  解析 cmd_sync_conversations 应答 JSON 包体。
     *  JSON 合法且含整型 code 即返回 true；code==0 时 m_synctime / m_items 有效。
     *  JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseSyncConversationsResponse(const std::string& jsonbody,
                                               ConversationsResult& out);

    /**
     *  构造"会话设置"请求 JSON 包体（cmd_set_conversation=1513），用于置顶与消息提醒模式；
     *  与服务端 MessageProtocol::parseSetConversationRequest 对齐——pinned（bool）与
     *  notifymode（整数）为可选字段，只有"本次要改"的字段才写入 JSON，未写入的字段
     *  服务端保持原值不变。2026-10-03 之前的布尔字段 mute 已删除，任何情况下都不再写。
     *  cid 以字符串编码（避免大整数精度丢失）。
     *  @param ctype         会话类型（见 enum conversation_type，1单聊 2群聊）
     *  @param cid           会话ID（单聊=对端userid，群聊=群id；>0）
     *  @param hasPinned     true 表示本次要改置顶，按 pinned 写入；false 则不写 pinned 字段
     *  @param pinned        置顶目标值（hasPinned 为 true 时有效）
     *  @param hasNotifyMode true 表示本次要改提醒模式，按 notifyMode 写入；false 则不写
     *  @param notifyMode    提醒模式目标值，取值见 Protocol.h 的 enum conversation_notify_mode
     *                       （hasNotifyMode 为 true 时有效；是否合法由服务端校验）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSetConversationRequest(int ctype, int64_t cid,
                                                   bool hasPinned, bool pinned,
                                                   bool hasNotifyMode, int notifyMode);

    /**
     *  构造"删除会话"请求 JSON 包体（复用 cmd_set_conversation=1513 的 deleted 字段）。
     *  与服务端 MessageProtocol::parseSetConversationRequest 对齐——只写 deleted 一个
     *  可选字段，故服务端只更新 t_conversation.f_deleted，置顶 / 免打扰保持原值。
     *
     *  语义是"把会话从最近会话列表移除"，<u>不删除聊天消息本体</u>：t_chatmsg 不动，
     *  历史消息记录窗仍能查到；服务端记下删除时刻的最后消息 msgid 作锚点，之后该
     *  会话再来新消息时会重新出现在列表里。
     *
     *  @param ctype 会话类型（见 enum conversation_type，1单聊 2群聊）
     *  @param cid   会话ID（单聊=对端userid，群聊=群id；>0）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildDeleteConversationRequest(int ctype, int64_t cid);

    /**
     *  构造「标为未读 / 清除标记」请求 JSON 包体（复用 cmd_set_conversation=1513 的 unreadmark 字段，
     *  docs/会话标为未读方案.md）。只写 unreadmark 一个可选字段，置顶、提醒模式与删除标记都不受影响。
     *  @param ctype 会话类型（见 enum conversation_type，1单聊 2群聊）
     *  @param cid   会话ID（单聊=对端userid，群聊=群id；>0）
     *  @param mark  true 标为未读，false 清除标记
     *  @return 紧凑格式的请求 JSON 文本，unreadmark 写成 JSON bool（服务端按 isBool 取值）
     */
    static std::string buildSetUnreadMarkRequest(int ctype, int64_t cid, bool mark);

    /**
     *  构造 cmd_set_group_follow(1330) 请求 { groupid:"群ID", targets:["userid", ...] }，id 一律写成字符串。
     *  @param groupid 群ID（>0）
     *  @param targets 完整名单（整份覆盖）；空表示清空。条数不得超过 kGroupFollowMax
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSetGroupFollowRequest(int64_t groupid, const std::vector<uint32_t>& targets);

    /**
     *  解析 cmd_group_follow_changed_notify(1331) 推送，包体与 1330 请求相同。
     *  targets 的元素接受字符串或整数，非正的元素跳过。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果，仅返回 true 时有效
     *  @return JSON 合法、groupid 为正且 targets 是数组时返回 true；否则 false（调用方应丢弃该推送）
     */
    static bool parseGroupFollowChangedNotify(const std::string& jsonbody, GroupFollowEntry& out);

    /**
     *  解析 cmd_conversation_changed_notify(1514) 推送包体，用于会话属性的多端同步。
     *
     *  服务端把 pinned / deleted 写成 <u>JSON bool</u>、notifymode 写成整数（见服务端
     *  MessageProtocol::buildConversationChangedNotify），本函数对两个开关也接受写成整数 0/1
     *  的形式，免得日后哪一端改了写法就静默失效；notifymode 缺失或越界按 0（允许提醒）。
     *  cid 以字符串编码（防大整数精度丢失），同样兼容整数写法。
     *
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果，仅返回 true 时有效
     *  @return JSON 合法、且 cid 为正数时返回 true；否则 false（调用方应丢弃该推送）
     */
    static bool parseConversationChangedNotify(const std::string& jsonbody,
                                               ConversationChangedNotify& out);

    /**
     *  构造批量设置提醒模式请求 JSON 包体（cmd_set_notify_modes=1524）：
     *  { items:[ {ctype, cid, mode} ] }，cid 以字符串编码。条数不得超过 kNotifyModeBatchMax，
     *  超出时先用 splitNotifyModeBatches 拆批。
     *  @param items 要设置的会话与目标模式
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildSetNotifyModesRequest(const std::vector<NotifyModeEntry>& items);

    /**
     *  把一组条目按 batchMax 拆成几批，顺序不变。纯函数。
     *  @param items    全部条目
     *  @param batchMax 每批最多条数；取 0 时视同 kNotifyModeBatchMax
     *  @param out      [出参] 拆好的各批；调用前会被清空；items 为空时为空
     */
    static void splitNotifyModeBatches(const std::vector<NotifyModeEntry>& items, size_t batchMax,
                                       std::vector<std::vector<NotifyModeEntry> >& out);

    /**
     *  解析 cmd_set_notify_modes 应答 { code, msg, results:[ {ctype, cid, code} ] }。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含整型 code 返回 true，否则 false；results 里缺字段的条目照常保留（取默认值）
     */
    static bool parseSetNotifyModesResponse(const std::string& jsonbody, SetNotifyModesResult& out);

    /**
     *  解析 cmd_notify_modes_changed_notify 推送 { items:[ {ctype, cid, mode} ] }。
     *  坏条目（cid 非正、ctype 不是 1 或 2、mode 越界）跳过，不影响其余条目。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且 items 为数组返回 true，否则 false（调用方应丢弃该推送）
     */
    static bool parseNotifyModesChangedNotify(const std::string& jsonbody, NotifyModesChangedNotify& out);

    /**
     *  统计一组会话的未读消息总数 —— 把各会话的 m_unread 逐项相加。供主面板把
     *  "消息"Tab 的角标刷成真实未读总数（而非占位数）。纯函数，便于单元测试。
     *  @param items 会话列表（一般为已过滤后的单聊会话；本函数不关心会话类型，
     *               只对传入项求和，由调用方决定统计范围）
     *  @return 未读消息总数；空列表返回 0
     */
    static int computeTotalUnread(const std::vector<ConversationItem>& items);

    /**
     *  判断一条刚收到的消息推送是否应该让会话未读数 +1。
     *
     *  存在的理由：服务端把群消息推给<u>包括发送者在内</u>的全体群成员，单聊在多端
     *  登录时也会把自己发的消息同步回本端。这类"自己发出、又回推回来"的消息不应
     *  计入未读，否则自己发一条群消息就会给自己点亮一个红点。
     *
     *  服务端生成的系统消息（发布公告、入群欢迎语、退出群聊、转让群主，msgtype 为
     *  message_type_system）同样不计：它们是群里的变动通知，不是有人对你说话。服务端算会话
     *  未读数时也不计它们（MessageManager 的群未读查询），两边口径一致。
     *
     *  @param fromUserId 消息发送方 userid（取自推送的 from 字段；系统消息为 0）
     *  @param selfId     本端登录用户的 userid；为 0 表示尚未登录 / 取不到，
     *                    此时一律不计未读，避免在未登录兜底态下误加
     *  @param msgtype    消息内容类型，取值见 Protocol.h 的 msg_type 一组 message_type_xxx
     *  @return 应计未读返回 true；自己发出的、系统消息、或 selfId 无效时返回 false
     */
    static bool shouldCountAsUnread(unsigned int fromUserId, unsigned int selfId, int msgtype);

    /**
     *  同上，另认一种例外（2026-10-07 用户确认）：「有人入群」系统提示的新成员名单里有本人时计未读，
     *  加群申请审批通过、被人拉进群时会话行因此有红点。服务端算会话未读数时同样计入
     *  （MessageManager 的 countJoinTipsForUser），两边口径一致。实时收到消息时用这一个。
     *  @param fromUserId 同上
     *  @param selfId     同上
     *  @param msgtype    同上
     *  @param tipKind    系统提示类型（MessageNotify::m_tipKind）；不是系统消息时传空串
     *  @param tipArgs    系统提示参数的 JSON 文本（MessageNotify::m_tipArgs）；不是系统消息时传空串
     *  @return 应计未读返回 true
     */
    static bool shouldCountAsUnread(unsigned int fromUserId, unsigned int selfId, int msgtype,
                                    const std::string& tipKind, const std::string& tipArgs);

private:
    ConversationsProtocolClient() = delete;
    ~ConversationsProtocolClient() = delete;
    ConversationsProtocolClient(const ConversationsProtocolClient&) = delete;
    ConversationsProtocolClient& operator=(const ConversationsProtocolClient&) = delete;
};

} // namespace net
