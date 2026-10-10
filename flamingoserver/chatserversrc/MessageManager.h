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
#include <set>        //getPinnedConversationIds 的出参
#include <mutex>      //m_revMutex：改写序号的分配与写库串行执行
#include "MessageProtocol.h"
#include "MessageDeleteRange.h"   //msgdelete::DeleteRange：按时间范围清理的区间
#include "GlobalSearchProtocol.h" //GlobalSearchConversationHit：全局搜索的会话命中

//数据库连接接口。私有辅助方法按指针借用它，不需要完整类型，前向声明即可 ——
//免得让每个包含本头文件的编译单元都跟着引入 dbapi 那一串。
class IDatabase;

//全局搜索里本人所在的一个群，以及本人在这个群里能看到的历史消息的时间下界
struct GroupSearchScope
{
    int64_t groupid;         //群 id
    int64_t minCreateTime;   //时间下界（Unix 毫秒），早于它的消息不算命中；0 表示不限
    int64_t minMsgId;        //msgid 下界，小于它的消息不算命中；0 表示不限（邀请时选择同步最近 N 条得出）
                             //（两项的取值规则见 GroupProtocol::computeHistoryBound）

    GroupSearchScope()
        : groupid(0), minCreateTime(0), minMsgId(0)
    {
    }
};

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
     *  @param audience      可见范围（借用，不持有）。NULL 表示会话内所有人可见（f_audience=0），与改动前
     *                       完全一致；非 NULL 时 f_audience 置 1，只有发送者本人与列表里的 userid 能看到，
     *                       列表会去重后写入 t_message_audience，与消息本身在同一个事务里提交。
     *                       空列表表示只有发送者本人可见。用于文件共享卡片等只给部分成员看的消息
     *  @return 成功返回新消息ID（f_msgid 自增主键）；失败返回 0（带可见范围时整个事务回滚，不留半条消息）
     */
    int64_t saveMessage(const std::string& clientmsgid, int32_t ctype,
                        int32_t fromUserid, int32_t toUserid, int32_t msgtype,
                        const std::string& storedContent, int64_t createTime,
                        const std::vector<int32_t>* audience = NULL);

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
     *  @param minMsgId 消息 msgid 的下界，小于它的消息不返回；0 表示不限。用于邀请入群时选择的「同步最近 N 条」
     *                 （本人成员行的 f_history_from，docs/邀请入群方案.md §5.3）。与 minCreateTime 同时生效
     *  @return 数据库访问成功返回 true
     */
    bool getConversationMessages(int32_t userid, const SyncMessagesRequest& req,
                                 std::vector<MessageRecord>& out, bool& hasMore,
                                 int64_t minCreateTime = 0, int64_t minMsgId = 0);

    /**
     *  按群id列表算出这些群的会话摘要（cmd_sync_conversations 的群会话部分用）。
     *  仅纳入"已有至少一条消息"的群（与单聊会话由消息派生的口径一致）；
     *  每个群补全 lastmsg、未读数，并从 t_conversation 取置顶状态与消息提醒模式。
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
     *  新成员入群时初始化他们在该群的已读位点：取本群此刻最大的消息 msgid，经 markRead 写给每位新成员。
     *  新成员原先没有 t_conversation 行，已读位点按 0 处理，入群前别人发的消息会全部算成未读
     *  （docs/邀请入群方案.md §2.5）；在入群那一刻把位点推到最新一条即可避免。位点只增不减，
     *  本人已有更大的位点时保持原值。须在插入入群系统提示之前调用（提示本身不计未读，先后不影响结果，
     *  但这样位点恰好停在入群前的最后一条）。
     *  @param groupid    群ID
     *  @param userids    本次入群的新成员 userid
     *  @param updateTime 本次更新时间，Unix毫秒
     *  @return 数据库访问全部成功返回 true；群里还没有任何消息时不写、直接返回 true
     */
    bool initNewMembersReadPos(int64_t groupid, const std::vector<int64_t>& userids, int64_t updateTime);

    /**
     *  按邀请时选择的同步条数算新成员的历史边界（docs/邀请入群方案.md §5.3），结果写进 t_group_member.f_history_from。
     *  按 msgid 而不是按时间截断：服务端时间戳精度为秒，同一秒内的多条消息按时间分不出条数。本群的全部消息
     *  （含已撤回的、系统提示）都计入条数。须在新成员入群、插入入群提示之前调用。
     *  @param groupid      群ID
     *  @param historyLimit 同步条数：kInviteHistoryLimitAll（-1）为全部，0 为不同步，N 为最近 N 条
     *  @param out          [出参] 历史边界：全部为 0；不同步为本群当前最大 msgid + 1（本群还没有消息时为 0）；
     *                      最近 N 条为倒数第 N 条的 msgid，本群消息不足 N 条时为 0
     *  @return 数据库访问成功返回 true；失败时 out 为 0（按不限处理，宁可多给也不让入群失败）
     */
    bool computeGroupHistoryFrom(int64_t groupid, int historyLimit, int64_t& out);

    /**
     *  取某人对某会话记下的已读位点（t_conversation 的 f_readmsgid）。
     *  单聊同步消息应答用它带出「对方已读到哪条」：查的是<u>对方</u>那一行，即
     *  userid=对端、cid=请求者本人。
     *  @param userid 会话归属用户（记位点的那个人）
     *  @param ctype  会话类型
     *  @param cid    会话ID（单聊为对端userid）
     *  @param out    [出参] 已读位点；该行不存在（从没读过）时为 0
     *  @return 数据库访问成功返回 true（行不存在也算成功）；失败时 out 为 0
     */
    bool getReadMsgid(int32_t userid, int32_t ctype, int64_t cid, int64_t& out);

    /**
     *  本人视角清空某会话的聊天记录（cmd_clear_conversation 用）。
     *
     *  取该会话此刻的最大 msgid 作为清空锚点，写入本人这一行 t_conversation.f_cleared_msgid，锚点
     *  只增不减；置顶、免打扰、已读位点等其余列不动。消息本体不动，对方与其他群成员不受影响；读取时
     *  由 visibleToUserClause 排除 msgid 不大于锚点的消息，会话里没有可见消息时它也不再出现在会话列表里。
     *  @param userid     执行清空的用户ID
     *  @param ctype      会话类型：1 单聊 2 群聊
     *  @param cid        会话ID：单聊为对端userid，群聊为群id
     *  @param updateTime 写入 f_update_time 的时刻，Unix毫秒
     *  @param outAnchor  [出参] 生效的锚点；之前清空得更靠后时为原值；会话没有任何消息时为 0
     *  @return 数据库访问成功返回 true
     */
    bool clearConversation(int32_t userid, int32_t ctype, int64_t cid,
                           int64_t updateTime, int64_t& outAnchor);

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
     *  将一条消息标记为已撤回（t_chatmsg.f_recalled 置 1），并记下撤回者（f_recalled_by）。
     *  撤回者随历史消息与会话列表下发，客户端据此写「某某撤回了一条消息」：群主与管理员可以
     *  撤回别人的群消息，只看发送方会写错名字。
     *  @param msgid      被撤回的消息ID
     *  撤回同时给这条消息分配一个新的改写序号（f_rev），离线期间被撤回的消息，客户端登录后经
     *  cmd_sync_rewritten_messages 也能拿到（docs/消息改写离线同步方案.md）。
     *  @param msgid      被撤回的消息ID
     *  @param operatorId 撤回者userid，即发起撤回请求的用户
     *  @return 数据库访问成功返回 true
     */
    bool recallMessage(int64_t msgid, int64_t operatorId);

    /**
     *  改写一条已存消息的内容（第 15 期：屏幕共享中途修改标题与描述后改写那张共享卡片）。改写 f_content，
     *  并按新内容重新生成 f_search_text；发送方、时间、撤回状态等都不变。服务端改写已发出的消息
     *  一律经由本函数（公告卡片被删、通话结束、屏幕共享改标题、帖子被编辑或删除），每次改写同时给
     *  这条消息分配一个新的改写序号（f_rev），供离线的成员登录后经 cmd_sync_rewritten_messages 补上
     *  （docs/消息改写离线同步方案.md）。新增改写场景时不要绕开本函数直接写 f_content。
     *  @param msgid         消息ID
     *  @param storedContent 新的 f_content（已经过 MessageProtocol::buildStoredContent 打包的 JSON 文本）
     *  @return 数据库访问成功返回 true（msgid 不存在时同样返回 true，此时什么也不改）
     */
    bool updateMessageContent(int64_t msgid, const std::string& storedContent);

    /**
     *  设置会话属性（cmd_set_conversation 用）：UPSERT t_conversation，
     *  仅更新 req 中 has_* 为 true 的列（置顶/消息提醒模式/删除标记）。
     *  notifymode 的取值须由调用方先用 MessageProtocol::isValidNotifyMode 校验过。
     *  改提醒模式时另有两条规则（与 setNotifyModes 相同）：改成特别关注时校验
     *  NOTIFY_MODE_STARRED_MAX 上限；从屏蔽改成别的模式时，把已读位点推进到该会话当前
     *  最后一条可见消息，屏蔽期间的消息不再算未读。全部改动在一个事务里完成。
     *  @param userid     会话归属用户
     *  @param req        已解析的请求
     *  @param updateTime 本次更新时间，Unix毫秒
     *  @param errorCode  [出参] 失败原因：error_code_starred_limit（特别关注已满，本次什么都没改）、
     *                    error_code_server_internal（数据库失败）；成功时为 error_code_ok
     *  @return 成功返回 true
     */
    bool setConversation(int32_t userid, const SetConversationRequest& req, int64_t updateTime,
                         int& errorCode);

    /**
     *  同上，供只关心成败、不需要失败原因的调用方（主要是单元测试的数据准备）。
     */
    bool setConversation(int32_t userid, const SetConversationRequest& req, int64_t updateTime);

    /**
     *  批量设置多个会话的消息提醒模式（cmd_set_notify_modes 用），全部在一个事务里写入。
     *
     *  results 由调用方预先填好、与 items 一一对应：调用方已判定非法的条目（格式不对、模式
     *  不合法、不是群成员等）把 code 置为非 0，本函数跳过它们；code 为 0 的条目逐条写入，
     *  遇到特别关注已满时把该条的 code 改为 error_code_starred_limit 并跳过，其余条目照常。
     *  上限按条目顺序累计：同一批里先把某个会话改出特别关注，后面的条目就能用上空出的名额。
     *  从屏蔽改出的群同样推进已读位点（规则同 setConversation）。
     *  @param userid     会话归属用户
     *  @param items      目标会话与模式，取值已由调用方用 MessageProtocol::isValidNotifyMode 校验
     *  @param updateTime 本次更新时间，Unix毫秒
     *  @param results    [入参 / 出参] 逐条结果码，大小须与 items 相同
     *  @param changed    [出参] 写入成功的条目（推送 cmd_notify_modes_changed_notify 用）
     *  @return 数据库访问成功返回 true；返回 false 时事务已回滚，原本待写入的条目一律标为
     *          error_code_server_internal，changed 为空
     */
    bool setNotifyModes(int32_t userid, const std::vector<NotifyModeItem>& items, int64_t updateTime,
                        std::vector<NotifyModeResult>& results, std::vector<NotifyModeItem>& changed);

    /**
     *  取某用户全部非默认提醒模式的会话（cmd_sync_conversations 应答的 notifymodes 用）。
     *  直接查 t_conversation，所以没有任何消息的群与联系人、被从列表删除的会话也会取出来。
     *  @param userid 会话归属用户
     *  @param out    [出参] 按 ctype、cid 升序；调用前会被清空
     *  @return 数据库访问成功返回 true
     */
    bool getNotifyModes(int32_t userid, std::vector<NotifyModeItem>& out);

    /**
     *  数某用户处于某种提醒模式的会话个数（单聊与群聊合计）。
     *  @param userid 会话归属用户
     *  @param mode   提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode
     *  @param count  [出参] 个数；失败时为 0
     *  @return 数据库访问成功返回 true
     */
    bool countNotifyMode(int32_t userid, int mode, int& count);

    /**
     *  读取某会话的当前属性（构造 conversation_changed_notify 用）。
     *  @param userid  会话归属用户
     *  @param ctype   会话类型
     *  @param cid     会话ID
     *  @param pinned     [出参] 是否置顶（无行时为 false）
     *  @param notifymode [出参] 消息提醒模式，取值见 Protocol.h 的 enum conversation_notify_mode
     *                    （无行时为 notify_mode_allow）
     *  @param deleted    [出参] 是否已删除（无行时为 false）
     *  @param unreadmark [出参] 本人是否把该会话手动标为未读（无行时为 false，docs/会话标为未读方案.md）
     *  @return 数据库访问成功返回 true
     */
    bool getConversationState(int32_t userid, int32_t ctype, int64_t cid,
                              bool& pinned, int& notifymode, bool& deleted, bool& unreadmark);

    /**
     *  同上，不取「手动标为未读」，供只关心置顶、提醒模式与删除标记的调用方使用。
     */
    bool getConversationState(int32_t userid, int32_t ctype, int64_t cid,
                              bool& pinned, int& notifymode, bool& deleted);

    /**
     *  取某用户置顶的全部会话ID（cmd_get_user_groups 给每个群带上置顶状态时用）。
     *  直接查 t_conversation，所以没有任何消息的空群、被删除（从会话列表隐藏）的会话，
     *  只要置顶标记还在，都会被取出来。
     *  @param userid 会话归属用户
     *  @param ctype  会话类型：1 单聊（结果为对端userid）、2 群聊（结果为群ID）
     *  @param out    [出参] 置顶的会话ID集合；调用前不会被清空
     *  @return 数据库访问成功返回 true
     */
    bool getPinnedConversationIds(int32_t userid, int32_t ctype, std::set<int64_t>& out);

    /**
     *  会话内按关键字搜索消息（cmd_search_messages 用）。
     *  对 f_search_text（入库时提取的纯文本，见 MessageSearchText.h）做 LIKE 匹配；还没回填
     *  搜索文本的存量消息（该列为 NULL）退回对 f_content 做 LIKE，回填期间也不会少结果。
     *  排除已撤回的消息与该用户已删除、不在其可见范围内的消息，按 beforemsgid 翻页。
     *  @param userid        发起搜索的用户ID
     *  @param req           已解析的请求；req.limit 须由调用方预先夹到合理区间
     *  @param out           [出参] 命中消息列表，以 msgid 升序返回
     *  @param hasMore       [出参] 是否还有更早的命中
     *  @param minCreateTime 消息时间下界（Unix 毫秒），早于它的消息不返回；0 表示不限。群设为
     *                       「新成员不可查看入群前的历史消息」时，调用方传本人的入群时间（与
     *                       getConversationMessages 同一口径）
     *  @param minMsgId      消息 msgid 下界，小于它的消息不返回；0 表示不限（与 getConversationMessages 同一口径）
     *  @return 数据库访问成功返回 true
     */
    bool searchMessages(int32_t userid, const SearchMessagesRequest& req,
                        std::vector<MessageRecord>& out, bool& hasMore, int64_t minCreateTime = 0,
                        int64_t minMsgId = 0);

    /**
     *  回填一批存量消息的搜索文本（f_search_text 为 NULL 的行，按 msgid 升序）：逐行用
     *  BuildStoredSearchText 提取后写回，没有可搜文字的类型写空串，之后不会再被选中。整批在
     *  一个事务里提交。chatserver 启动后在主循环上周期调用，直到返回 0。
     *  @param batchSize 这一批最多处理几行，须大于 0
     *  @return 这一批处理的行数；没有待回填的行返回 0；数据库访问失败返回 -1
     */
    int backfillSearchText(int batchSize);

    /**
     *  按改写序号取本人可见范围内被改写过（或被撤回）的消息（cmd_sync_rewritten_messages 用）。
     *  范围与 globalSearchConversations 相同：本人参与的单聊（双向）与 groups 里列出的群（按各群的历史下界
     *  过滤），并排除本人已删除、清空过与不在可见范围内的消息；不排除已撤回的消息（撤回本身就要同步）。
     *  @param userid  发起同步的用户ID
     *  @param groups  本人所在的群及各自的历史下界（GroupManager::getUserHistoryScopes）；为空时只取单聊
     *  @param since   只取 f_rev 大于它的消息
     *  @param limit   本页最多几条，须大于 0
     *  @param out     [出参] 本页消息，按 f_rev 升序；只填消息本体与 rev，卡片状态等由调用方另行合并
     *  @param hasMore [出参] 本页之后是否还有
     *  @param currev  [出参] 库里当前的最大改写序号（不限可见范围，空表为 0）
     *  @return 数据库访问成功返回 true（没有任何消息也算成功）
     */
    bool getRewrittenMessages(int32_t userid, const std::vector<GroupSearchScope>& groups, int64_t since,
                              int limit, std::vector<RewrittenMessage>& out, bool& hasMore, int64_t& currev);

    /**
     *  全局搜索（cmd_global_search 用）：在本人能看到的全部聊天记录里按关键字搜索，按会话汇总。
     *  范围是本人参与的单聊（双向）与 groups 里列出的群；命中口径与 searchMessages 相同（查
     *  f_search_text、未回填的行退回查 f_content、排除已撤回、已删除与不在可见范围内的消息），
     *  群聊另按各群的时间下界过滤。
     *  @param userid  发起搜索的用户ID
     *  @param keyword 关键字（UTF-8，非空）
     *  @param groups  本人所在的群及各自的时间下界；为空时只搜单聊
     *  @param limit   最多返回几个会话，须大于 0
     *  @param out     [出参] 会话命中，按最近一条命中的 msgid 倒序；latesttext 为它的搜索文本中
     *                 关键字附近的摘录（MakeSearchExcerpt）
     *  @return 数据库访问成功返回 true（没有命中也算成功）
     */
    bool globalSearchConversations(int32_t userid, const std::string& keyword,
                                   const std::vector<GroupSearchScope>& groups, int limit,
                                   std::vector<GlobalSearchConversationHit>& out);

    /**
     *  本端删除一条消息（cmd_delete_message 用）：写入 t_message_deleted。
     *  消息本体不动、对端不受影响；已删除则幂等。
     *  @param userid 执行删除的用户ID
     *  @param msgid  被删除的消息ID
     *  @return 数据库访问成功返回 true
     */
    bool deleteMessageForUser(int32_t userid, int64_t msgid);

    /**
     *  按时间范围清理本人的聊天记录（cmd_delete_messages_by_time 用）：把区间并入
     *  t_message_deleted_range。
     *
     *  在一个事务里读出本人已有的区间，经 msgdelete::MergeDeleteRanges 合并后删掉本人的全部
     *  旧行、写回合并结果。写入量只与本人的区间段数有关（通常只有几行），与区间内的消息条数
     *  无关，因此不会长时间占住网络 I/O 线程。消息本体不动、对方与其他群成员不受影响；读取时
     *  由 visibleToUserClause 按区间过滤。
     *
     *  @param userid 执行清理的用户ID
     *  @param range  本次清理的区间，须已经过 msgdelete::NormalizeDeleteRange
     *  @param nowMs  当前时刻（Unix毫秒），写入 f_update_time 供排查
     *  @return 数据库访问成功返回 true；失败时事务整体回滚，本人原有的区间不受影响
     */
    bool addDeletedRange(int32_t userid, const msgdelete::DeleteRange& range, int64_t nowMs);

    /**
     *  读出某用户已清理的全部区间，按开始时间升序。
     *  @param userid 用户ID
     *  @param out    [出参] 区间列表；调用前会被清空，没有清理过时为空
     *  @return 数据库访问成功返回 true
     */
    bool getDeletedRanges(int32_t userid, std::vector<msgdelete::DeleteRange>& out);

    /**
     *  按 msgid 列表批量取消息（cmd_forward_message 取转发源用）。
     *  排除 viewerUserid 本人已删除的消息（单条删除与按时间范围清理两种）以及不在其可见范围内的消息，
     *  口径与同步、搜索所用的 visibleToUserClause 一致；删除只对本人生效，别人删了不影响本人取得到。
     *  是否撤回、本人能否看到该会话，仍由调用方判断。
     *  @param viewerUserid 取消息的用户ID（即转发人）
     *  @param msgids       目标消息ID列表
     *  @param out          [出参] 命中的消息，按 msgid 升序返回（不存在或已被本人删除的 id 直接略过）
     *  @return 数据库访问成功返回 true
     */
    bool getMessagesByIds(int32_t viewerUserid, const std::vector<int64_t>& msgids,
                          std::vector<MessageRecord>& out);

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
        bool        recalled;    //这条消息是否已被撤回（f_recalled）；会话摘要据此不再下发原文
        int64_t     recalledby;  //撤回者 userid（f_recalled_by）；0 表示未撤回或撤回者未知

        LastMsgDetail()
            : from(0), msgtype(1), timestamp(0), recalled(false), recalledby(0)
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

    /**
     *  分配下一个改写序号（f_rev）。调用方须已持有 m_revMutex，并在<u>释放锁之前</u>把这个序号连同
     *  改写一起写进库：取号与写库若不在同一把锁里，序号大的可能先落库，客户端恰好在这时按序号同步，
     *  就会把水位推过还没落库的那个小序号、从此漏掉它。
     *  第一次调用时从库里读 MAX(f_rev) 作为起点，之后在内存里递增，进程重启后接着库里的最大值继续。
     *    pConn：已打开的数据库连接（借用，不接管生命周期），用来读起点。
     *  @return 新序号（大于 0）；读起点失败返回 0，调用方应放弃本次改写
     */
    int64_t allocateRevisionLocked(IDatabase* pConn);

    std::mutex m_revMutex;    //保护 m_lastRev / m_revLoaded，并使「取号 + 写库」串行执行
    int64_t    m_lastRev;     //已分配出去的最大改写序号；m_revLoaded 为 false 时无意义
    bool       m_revLoaded;   //是否已从库里读过 MAX(f_rev) 作为起点；进程内只读一次
};
