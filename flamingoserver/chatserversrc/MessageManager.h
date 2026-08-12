/**
 *  消息存储管理器，MessageManager.h
 *
 *  负责 v2 单聊消息（t_chatmsg 表）的落库与查询。每次操作单独建立一条
 *  数据库连接（与 UserManager 中的好友申请表操作同一风格），不在内存缓存
 *  消息——离线消息靠 t_chatmsg 持久化、等用户重连后经 cmd_sync_messages 拉取。
 *
 *  典型用法：main.cpp 启动时 Singleton<MessageManager>::Instance().init(...)；
 *  会话层在 cmd_send_message 处理中调用 saveMessage / getMsgIdByClientId。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include "MessageProtocol.h"

//数据库连接接口。私有辅助方法按指针借用它，不需要完整类型，前向声明即可 ——
//免得让每个包含本头文件的编译单元都跟着引入 dbapi 那一串。
class IDatabase;

//消息存储管理器；全局单例，经 Singleton<MessageManager> 访问
class MessageManager final
{
public:
    MessageManager();
    ~MessageManager();

    MessageManager(const MessageManager& rhs) = delete;
    MessageManager& operator=(const MessageManager& rhs) = delete;

    /**
     *  初始化（消息不预加载到内存）。
     *  @return 恒返回 true
     */
    bool init();

    /**
     *  保存一条消息到 t_chatmsg。
     *  @param clientmsgid   客户端消息ID（UUID）
     *  @param ctype         会话类型：1 单聊 2 群聊
     *  @param fromUserid    发送方userid
     *  @param toUserid      接收方：单聊为对端userid，群聊为群id
     *  @param msgtype       内容类型：1 文本 …17 系统
     *  @param storedContent 已打包的内容JSON（见 MessageProtocol::buildStoredContent），
     *                       内部会做 SQL 转义后落库
     *  @param createTime    消息时间，Unix毫秒
     *  @return 成功返回新消息ID（f_msgid 自增主键）；失败返回 0
     */
    int64_t saveMessage(const std::string& clientmsgid, int32_t ctype,
                        int32_t fromUserid, int32_t toUserid, int32_t msgtype,
                        const std::string& storedContent, int64_t createTime);

    /**
     *  按"发送方 + clientmsgid"查既有消息ID，用于发送去重（客户端重发同一条）。
     *  @param fromUserid  发送方userid
     *  @param clientmsgid 客户端消息ID
     *  @return 命中返回既有 f_msgid；无记录或 clientmsgid 为空返回 0
     */
    int64_t getMsgIdByClientId(int32_t fromUserid, const std::string& clientmsgid);

    /**
     *  会话未读数的上限。数到这个值就停，不再"有多少数多少"。
     *
     *  为什么要封顶：未读数原先是一条 `SELECT COUNT(*)`，未读多少条就要逐行数多少条，
     *  用户离线越久、群越活跃这个数越大。实测 50 个群、每群积 5000 条时，光是数未读就
     *  要两秒多，而且这段时间同步占着网络 I/O 线程（见 docs/会话列表拉取性能优化方案.md
     *  第 2 节的实测曲线）。聊天记录只增不减，所以那是一条会持续恶化的曲线。
     *
     *  为什么封顶不影响使用：客户端的未读角标本来就封顶显示 "99+"
     *  （见客户端 BalloonMain/BadgeRender.cpp 的 kBadgeMaxShown），数到 100 以上的部分
     *  没有任何地方用得上。
     *
     *  取 101 而不是 100：客户端要判断"是不是超过 99"才决定显示 99+，多留一格，
     *  日后把显示阈值调成"100 以上"也不必再动服务端。
     *
     *  <u>语义提醒</u>：本值意味着未读数的含义是"精确值，或达到上限时为 ≥kMaxUnreadCount"。
     *  日后若要做"全部会话未读求和"一类的功能，必须知道这个上限的存在。
     */
    static const int kMaxUnreadCount = 101;

    /**
     *  拉取某用户的全部单聊会话摘要（cmd_sync_conversations 用）。
     *  会话集合由 t_chatmsg 派生（与该用户有过往来的所有对端），每个会话补全
     *  最后一条消息、未读数，并从 t_conversation LEFT JOIN 出置顶/免打扰状态。
     *  @param userid 目标用户ID
     *  @param out    [出参] 会话摘要列表；已被该用户删除的会话不含在内
     *  @return 数据库访问成功返回 true
     */
    bool getConversationBriefs(int32_t userid, std::vector<ConversationBrief>& out);

    /**
     *  按锚点拉取某会话的一段消息（cmd_sync_messages 用），单聊 / 群聊通用。
     *  按 req.ctype 切换会话定位：ctype=1 取本人与 req.cid 的双向单聊消息；
     *  ctype=2 取 f_to=req.cid 的群消息。其余翻页逻辑两者一致。
     *  @param userid  发起拉取的用户ID
     *  @param req     已解析的请求；req.cid 为对端userid（单聊）或群id（群聊），
     *                 req.limit 须由调用方预先夹到合理区间（本方法直接取用）
     *  @param out     [出参] 消息列表，统一以 msgid 升序（时间正序）返回
     *  @param hasMore [出参] 锚点方向上是否还有更多消息
     *  @param minCreateTime 消息创建时间的下界，Unix毫秒；早于它的消息不返回。
     *                 0 表示不限。用于"新成员不可查看入群前历史消息"这项群设置 ——
     *                 群关闭 f_new_see_history 时，调用方传入该成员的入群时间。
     *                 本方法不认识群设置，只认这个下界，故不引入对 GroupManager 的依赖。
     *  @return 数据库访问成功返回 true
     */
    bool getConversationMessages(int32_t userid, const SyncMessagesRequest& req,
                                 std::vector<MessageRecord>& out, bool& hasMore,
                                 int64_t minCreateTime = 0);

    /**
     *  按群id列表算出这些群的会话摘要（cmd_sync_conversations 的群会话部分用）。
     *  仅纳入"已有至少一条消息"的群（与单聊会话由消息派生的口径一致）；
     *  每个群补全 lastmsg、未读数，并从 t_conversation 取置顶 / 免打扰状态。
     *  @param userid   会话归属用户
     *  @param groupids 该用户加入的群id列表（由会话层经 GroupManager 取得后传入）
     *  @param out      [出参] 群会话摘要列表
     *  @return 数据库访问成功返回 true
     */
    bool getGroupConversationBriefs(int32_t userid, const std::vector<int64_t>& groupids,
                                    std::vector<ConversationBrief>& out);

    /**
     *  上报已读位点（cmd_mark_read 用）：把 t_conversation 的 f_readmsgid
     *  抬高到 readmsgid（行不存在则插入；已读位点只增不减，取 GREATEST）。
     *  @param userid     会话归属用户
     *  @param ctype      会话类型
     *  @param cid        会话ID（单聊为对端userid）
     *  @param readmsgid  已读到的最大消息msgid
     *  @param updateTime 本次更新时间，Unix毫秒
     *  @return 数据库访问成功返回 true
     */
    bool markRead(int32_t userid, int32_t ctype, int64_t cid,
                  int64_t readmsgid, int64_t updateTime);

    /**
     *  按 msgid 取单条消息（撤回前的校验用）。
     *  @param msgid 目标消息ID
     *  @param out   [出参] 消息记录
     *  @return 消息存在返回 true
     */
    bool getMessageById(int64_t msgid, MessageRecord& out);

    /**
     *  记录 / 更新某条文件消息的收取状态（接收方经 cmd_file_transfer_ack 上报后调用）。
     *  同一条消息会被上报多次（开始接收 → 已接收；拒绝后反悔再接收），一律以最后一次
     *  为准，故内部为 UPSERT、不做状态机比较。
     *  @param msgid      文件消息的服务器msgid（>0）
     *  @param state      收取状态，取值见 enum file_ack_state
     *  @param updateTime 状态更新时间，Unix毫秒
     *  @return 写库成功返回 true
     */
    bool saveFileAckState(int64_t msgid, int state, int64_t updateTime);

    /**
     *  查某条文件消息的收取状态。供拉历史时回填给发送方 —— 它离线期间对方的接收 /
     *  拒绝就是靠这里补上的。
     *  @param msgid 文件消息的服务器msgid
     *  @return 收取状态（见 enum file_ack_state）；无记录 / msgid 非正 / 查询失败返回 0
     */
    int getFileAckState(int64_t msgid);

    /**
     *  将一条消息标记为已撤回（t_chatmsg.f_recalled 置 1）。
     *  @param msgid 被撤回的消息ID
     *  @return 数据库访问成功返回 true
     */
    bool recallMessage(int64_t msgid);

    /**
     *  设置会话属性（cmd_set_conversation 用）：UPSERT t_conversation，
     *  仅更新 req 中 has_* 为 true 的列（置顶/免打扰/删除标记）。
     *  @param userid     会话归属用户
     *  @param req        已解析的请求
     *  @param updateTime 本次更新时间，Unix毫秒
     *  @return 数据库访问成功返回 true
     */
    bool setConversation(int32_t userid, const SetConversationRequest& req, int64_t updateTime);

    /**
     *  读取某会话的当前属性（构造 conversation_changed_notify 用）。
     *  @param userid  会话归属用户
     *  @param ctype   会话类型
     *  @param cid     会话ID
     *  @param pinned  [出参] 是否置顶（无行时为 false）
     *  @param mute    [出参] 是否免打扰（无行时为 false）
     *  @param deleted [出参] 是否已删除（无行时为 false）
     *  @return 数据库访问成功返回 true
     */
    bool getConversationState(int32_t userid, int32_t ctype, int64_t cid,
                              bool& pinned, bool& mute, bool& deleted);

    /**
     *  会话内按关键字搜索消息（cmd_search_messages 用）。
     *  对 f_content 做 LIKE 匹配，排除该用户已删除的消息，按 beforemsgid 翻页。
     *  @param userid  发起搜索的用户ID
     *  @param req     已解析的请求；req.limit 须由调用方预先夹到合理区间
     *  @param out     [出参] 命中消息列表，以 msgid 升序返回
     *  @param hasMore [出参] 是否还有更早的命中
     *  @return 数据库访问成功返回 true
     */
    bool searchMessages(int32_t userid, const SearchMessagesRequest& req,
                        std::vector<MessageRecord>& out, bool& hasMore);

    /**
     *  本端删除一条消息（cmd_delete_message 用）：写入 t_message_deleted。
     *  消息本体不动、对端不受影响；已删除则幂等。
     *  @param userid 执行删除的用户ID
     *  @param msgid  被删除的消息ID
     *  @return 数据库访问成功返回 true
     */
    bool deleteMessageForUser(int32_t userid, int64_t msgid);

    /**
     *  按 msgid 列表批量取消息（cmd_forward_message 取转发源用）。
     *  @param msgids 目标消息ID列表
     *  @param out    [出参] 命中的消息，按 msgid 升序返回（不存在的 id 直接略过）
     *  @return 数据库访问成功返回 true
     */
    bool getMessagesByIds(const std::vector<int64_t>& msgids, std::vector<MessageRecord>& out);

private:
    /**
     *  一条消息在会话摘要里要用到的那几项。仅供下面的批量取详情使用。
     */
    struct LastMsgDetail
    {
        int64_t     from;        //发送方 userid
        int         msgtype;     //内容类型，取值见 message_type
        std::string content;     //已从 f_content 解包出来的 content 部分
        int64_t     timestamp;   //消息时间，Unix 毫秒

        LastMsgDetail()
            : from(0), msgtype(1), timestamp(0)
        {
        }
    };

    /**
     *  按 msgid <u>批量</u>取消息详情。
     *
     *  拉会话列表时，每个会话都要取自己"最后一条消息"的发送方 / 类型 / 内容 / 时间。
     *  原先是每个会话跑一条 SQL，N 个会话就是 N 次往返 —— 会话一多就线性变慢，
     *  换成 MySQL 后端时每一次往返还都是一次网络开销。改成把 msgid 收齐后一次查完。
     *
     *    pConn：  已打开的数据库连接（借用，不接管生命周期）。
     *    msgids： 要取的 msgid 集合；允许有重复，内部按 map 归并。
     *    out：    [出参] msgid → 详情；查不到的 msgid 不会出现在结果里。
     *
     *  内部按 kDetailBatchSize 分批拼 IN 列表 —— 一条 SQL 的长度有上限，
     *  会话极多时不分批会拼出一条超长语句被数据库拒掉。
     */
    void fetchLastMsgDetails(IDatabase* pConn, const std::vector<int64_t>& msgids,
                             std::map<int64_t, LastMsgDetail>& out);

    //批量取详情时每批最多放多少个 msgid。取 500：够把常见规模一两次查完，
    //又不至于把 SQL 拼得过长（500 个 20 位数字加分隔符约 10KB，远低于各后端的语句上限）。
    static const int kDetailBatchSize = 500;
};
