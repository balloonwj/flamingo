/**
 *  会话未读数的正确性用例（Suite CVU），test_conversationunread.cpp
 *
 *  这一套是为「会话列表拉取性能优化」第一步（未读数封顶）做的<u>参照物</u>：先把
 *  「现在什么算未读」这件事逐条钉死，改动之后再跑一遍，行为不许变样。
 *  写在动代码之前，且 CVU1~CVU5 在<u>改动前就应当全绿</u> —— 它们描述的是既有行为，
 *  不是新需求。CVU6 描述的才是这次要引入的新行为（超过上限时返回上限值），
 *  改动前预期失败、改完转绿。
 *  CVU7（2026-09-30 补）钉住已读位点的读取口径 getReadMsgid，供单聊同步消息应答
 *  带出「对方已读到哪条」。
 *  CVU8（2026-10-02 补）：服务端生成的群系统消息（发布公告、入群欢迎语、退出群聊、转让群主，
 *  msgtype=17、发送者为 0）不计入未读 —— 它们是群里的变动通知，不是有人对你说话。
 *  CVU9（2026-10-07 补，用户确认）：上一条的唯一例外 —— 「有人入群」提示对名单里的新成员本人计未读，
 *  加群申请审批通过、被人拉进群时，新成员的会话行因此有红点；邀请人与其他老成员仍不计。
 *
 *  未读的口径（读 MessageManager.cpp 得出，两条路各一份）：
 *    · 群聊：f_ctype=2 且 f_to=群id 且 <u>f_from != 本人</u> 且 f_msgid > 已读位点，
 *            且不是系统消息（f_msgtype != 17），再排除本人已删除的消息（t_message_deleted）；
 *    · 单聊：f_ctype=1 且 <u>f_from=对端 且 f_to=本人</u>（即只数对方发来的）
 *            且 f_msgid > 已读位点，同样排除已删除的。
 *
 *  这些用例会真的打开一个临时 .db 文件跑 SQL —— MessageManager 经 DbFactory 取连接，
 *  而 DbFactory 读的是全局 CDbConfig，故每个用例先把 CDbConfig 指到本套专用的库文件上。
 *  数据量都很小（几十条），进默认测试集没有负担；真正耗时的规模测试在 CVB 套。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include <stdio.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupProtocol.h"     //CVU9：入群提示的 content
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/MessageProtocol.h"   //CVU9：消息入库时的 content 包装
#include "../chatserversrc/Protocol.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../sqliteapi/SqliteManager.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件，与别的套用的文件分开，避免互相干扰
static const char* const CVU_DB_PATH = "flamingo_sqlite_convunread_test.db";

//灌数据用的基准时间戳（Unix 毫秒）。固定取值而不取当前时间，保证结果可复现。
static const int64_t CVU_BASE_TIME = 1700000000000LL;

//被测用户与对端 / 群的 id。取值互不相同，便于看日志时一眼分清。
static const int32_t CVU_USER_ID  = 100001;
static const int64_t CVU_PEER_ID  = 200001;
static const int64_t CVU_GROUP_ID = 900001;

//另一个群成员的 userid，用来产生"别人发的群消息"。
static const int64_t CVU_OTHER_ID = 200002;

//消息正文，内容本身与未读口径无关，取一条短文本即可。
static const char* const CVU_CONTENT = "{\"text\":\"用于未读口径核对的消息\"}";


//MySQL 模式下用的测试库名。会被整个 DROP 重建，故用测试专用名字。
static const char* const CVU_MYSQL_DB = "flamingo_test_cvu";

//重建一个干净的测试库，并把 CDbConfig 指过去。按环境变量选后端（见 test_dbbackend.h）：
//默认 SQLite；设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL。
static void resetCvuDb()
{
    testdb::resetDb(CVU_DB_PATH, CVU_MYSQL_DB);
}

//收尾清理。SQLite 删临时库文件；MySQL 留着库便于事后排查。
static void removeCvuDbFiles()
{
    testdb::cleanupDb(CVU_DB_PATH);
}

//取一个数据库连接。每个辅助函数各取各的 —— 与 MessageManager 的做法一致。
static IDatabase* openConn()
{
    IDatabase* pConn = DbFactory::createConnection();
    REQUIRE(pConn != NULL);
    return pConn;
}

/**
 *  插一条消息。
 *    msgid：   消息 id（本套用例自己排号，保证顺序可控）。
 *    ctype：   1 单聊 2 群聊。
 *    from/to： 发送方 / 接收方（群聊时 to 取群 id）。
 */
static void insertMsg(int64_t msgid, int ctype, int64_t from, int64_t to)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "INSERT INTO t_chatmsg "
          "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,"
          "f_recalled,f_create_time) VALUES ("
       << msgid << ",'c" << msgid << "'," << ctype << "," << from << "," << to
       << ",1,'" << CVU_CONTENT << "',0," << (CVU_BASE_TIME + msgid) << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

/**
 *  插一条服务端生成的群系统消息：发送者为 0、msgtype 为 message_type_system（17）。
 *    msgid：  消息 id。
 *    groupid：所在群 id。
 */
static void insertSystemMsg(int64_t msgid, int64_t groupid)
{
    //系统消息由服务器生成，没有真实发送人，发送者 userid 记 0（与 ChatSession 的 kSystemMessageSenderId 一致）
    const int64_t kSystemSender = 0;
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "INSERT INTO t_chatmsg "
          "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,"
          "f_recalled,f_create_time) VALUES ("
       << msgid << ",'sys" << msgid << "'," << conversation_group << "," << kSystemSender << "," << groupid
       << "," << message_type_system << ",'" << CVU_CONTENT << "',0," << (CVU_BASE_TIME + msgid) << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//把某个会话的已读位点写进 t_conversation。不写这一行时视作"一条都没读过"。
static void setReadPos(int ctype, int64_t cid, int64_t readmsgid)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "REPLACE INTO t_conversation "
          "(f_userid,f_ctype,f_cid,f_pinned,f_notify_mode,f_readmsgid,f_deleted,"
          "f_deleted_msgid,f_update_time) "
          "VALUES (" << CVU_USER_ID << "," << ctype << "," << cid << ",0,0,"
       << readmsgid << ",0,0," << CVU_BASE_TIME << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//把某条消息标记成"本人已删除"。
static void markDeleted(int64_t msgid)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "REPLACE INTO t_message_deleted (f_userid,f_msgid) VALUES ("
       << CVU_USER_ID << "," << msgid << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//取本人的群会话摘要（只有一个群，直接返回那一条）。
static ConversationBrief groupBrief()
{
    std::vector<int64_t> groupids;
    groupids.push_back(CVU_GROUP_ID);
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance()
                .getGroupConversationBriefs(CVU_USER_ID, groupids, out));
    REQUIRE(out.size() == 1);
    return out[0];
}

//取本人的单聊会话摘要（只有一个对端，直接返回那一条）。
static ConversationBrief singleBrief()
{
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance()
                .getConversationBriefs(CVU_USER_ID, out));
    REQUIRE(out.size() == 1);
    return out[0];
}

TEST_CASE("CVU1 群聊未读只数别人发的，自己发的不计")
{
    resetCvuDb();

    //别人发 3 条、自己发 2 条，一条都没读过
    insertMsg(1, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    insertMsg(2, conversation_group, CVU_USER_ID,  CVU_GROUP_ID);
    insertMsg(3, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    insertMsg(4, conversation_group, CVU_USER_ID,  CVU_GROUP_ID);
    insertMsg(5, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);

    //自己发的两条不算未读 —— 否则自己在群里说句话，自己的未读数还往上涨
    CHECK(groupBrief().unread == 3);

    removeCvuDbFiles();
}

TEST_CASE("CVU2 未读只数已读位点之后的")
{
    resetCvuDb();

    for (int64_t i = 1; i <= 5; ++i)
    {
        insertMsg(i, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    }

    //已读到 3：未读应当是 4、5 两条
    setReadPos(conversation_group, CVU_GROUP_ID, 3);
    CHECK(groupBrief().unread == 2);

    removeCvuDbFiles();
}

TEST_CASE("CVU3 本人已删除的消息不计入未读")
{
    resetCvuDb();

    for (int64_t i = 1; i <= 5; ++i)
    {
        insertMsg(i, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    }

    //删掉其中两条：未读从 5 降到 3。删除是"只对本人生效"，故走 t_message_deleted
    markDeleted(2);
    markDeleted(4);
    CHECK(groupBrief().unread == 3);

    removeCvuDbFiles();
}

TEST_CASE("CVU4 全部已读时未读为 0")
{
    resetCvuDb();

    for (int64_t i = 1; i <= 5; ++i)
    {
        insertMsg(i, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    }
    setReadPos(conversation_group, CVU_GROUP_ID, 5);

    const ConversationBrief brief = groupBrief();
    CHECK(brief.unread == 0);
    //已读不影响"最后一条消息"，会话本身照常在列表里
    CHECK(brief.lastmsgid == 5);

    removeCvuDbFiles();
}

TEST_CASE("CVU5 单聊未读与群聊同口径")
{
    resetCvuDb();

    //对端发 3 条、自己发 2 条
    insertMsg(1, conversation_single, CVU_PEER_ID, CVU_USER_ID);
    insertMsg(2, conversation_single, CVU_USER_ID, CVU_PEER_ID);
    insertMsg(3, conversation_single, CVU_PEER_ID, CVU_USER_ID);
    insertMsg(4, conversation_single, CVU_USER_ID, CVU_PEER_ID);
    insertMsg(5, conversation_single, CVU_PEER_ID, CVU_USER_ID);

    //自己发的不计
    CHECK(singleBrief().unread == 3);

    //已读位点之后的才算
    setReadPos(conversation_single, CVU_PEER_ID, 3);
    CHECK(singleBrief().unread == 1);

    //已删除的不计
    markDeleted(5);
    CHECK(singleBrief().unread == 0);

    removeCvuDbFiles();
}

TEST_CASE("CVU6 未读超过上限时返回上限值")
{
    //这一条描述的是本次要引入的新行为：未读数最多数到 kMaxUnreadCount 就停，
    //不再"有多少数多少"。依据是客户端角标本来就封顶显示 99+，数到 100 以上没人用得上，
    //而"有多少数多少"正是登录越用越慢的根子（见 docs/会话列表拉取性能优化方案.md）。
    //改动之前这条用例预期失败（会返回真实条数 150），改完转绿。
    resetCvuDb();

    const int64_t kTotal = 150;
    for (int64_t i = 1; i <= kTotal; ++i)
    {
        insertMsg(i, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    }

    const ConversationBrief brief = groupBrief();
    CHECK(brief.unread == MessageManager::kMaxUnreadCount);
    //封顶不影响最后一条消息 —— 会话预览仍要显示真实的最新那条
    CHECK(brief.lastmsgid == kTotal);

    //不到上限时仍是精确值，别把所有会话都变成上限值
    resetCvuDb();
    for (int64_t i = 1; i <= 7; ++i)
    {
        insertMsg(i, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    }
    CHECK(groupBrief().unread == 7);

    removeCvuDbFiles();
}

//2026-09-30：单聊同步消息应答要带上「对方读到了哪条」，数据来自对方那一行
//t_conversation 的 f_readmsgid。本条钉住读取口径：按 (userid, ctype, cid) 精确取一行，
//没有这一行就是 0，且读回的值与 markRead 的「只增不减」一致。
TEST_CASE("CVU7 getReadMsgid 取某人对某会话记下的已读位点")
{
    resetCvuDb();
    MessageManager& mgr = Singleton<MessageManager>::Instance();

    //一行都没有：对方从没读过，位点为 0，查询本身是成功的
    int64_t pos = -1;
    REQUIRE(mgr.getReadMsgid(CVU_USER_ID, conversation_single, CVU_PEER_ID, pos));
    CHECK(pos == 0);

    //写入之后读回
    const int64_t kReadPos = 42;
    setReadPos(conversation_single, CVU_PEER_ID, kReadPos);
    REQUIRE(mgr.getReadMsgid(CVU_USER_ID, conversation_single, CVU_PEER_ID, pos));
    CHECK(pos == kReadPos);

    //更小的上报不回退：markRead 取 GREATEST，读回的仍是原来的位点
    const int64_t kSmallerPos = 10;
    REQUIRE(mgr.markRead(CVU_USER_ID, conversation_single, CVU_PEER_ID, kSmallerPos, CVU_BASE_TIME));
    REQUIRE(mgr.getReadMsgid(CVU_USER_ID, conversation_single, CVU_PEER_ID, pos));
    CHECK(pos == kReadPos);

    //别的会话不串：同一个人对群会话没有记录，读到的是 0
    REQUIRE(mgr.getReadMsgid(CVU_USER_ID, conversation_group, CVU_GROUP_ID, pos));
    CHECK(pos == 0);

    //别人对同一个 cid 也没有记录：键里的 userid 必须参与匹配
    REQUIRE(mgr.getReadMsgid((int32_t)CVU_OTHER_ID, conversation_single, CVU_PEER_ID, pos));
    CHECK(pos == 0);

    removeCvuDbFiles();
}

TEST_CASE("CVU8 群系统消息不计入未读，但仍是会话的最后一条消息")
{
    resetCvuDb();

    //别人发 1 条，之后服务端插了 2 条系统提示（例如有人退群、转让群主），一条都没读过
    insertMsg(1, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    insertSystemMsg(2, CVU_GROUP_ID);
    insertSystemMsg(3, CVU_GROUP_ID);

    const ConversationBrief brief = groupBrief();
    CHECK(brief.unread == 1);
    //系统消息照样更新会话的最后一条（会话列表的摘要与时间据此显示）
    CHECK(brief.lastmsgid == 3);

    removeCvuDbFiles();
}

//CVU9 另外两位群成员：同批入群的另一位新成员、未入群的路人（userid 任取，与上面几个互不相同）
static const int32_t CVU_NEWBIE2_ID = 100002;
static const int64_t CVU_STRANGER_ID = 300001;

//把某人对本群的已读位点写进 t_conversation（setReadPos 只写 CVU_USER_ID 的那一行）
static void setGroupReadPosFor(int32_t userid, int64_t readmsgid)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "REPLACE INTO t_conversation "
          "(f_userid,f_ctype,f_cid,f_pinned,f_notify_mode,f_readmsgid,f_deleted,"
          "f_deleted_msgid,f_update_time) "
          "VALUES (" << userid << "," << conversation_group << "," << CVU_GROUP_ID << ",0,0,"
       << readmsgid << ",0,0," << CVU_BASE_TIME << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

/**
 *  插一条「有人入群」系统提示，content 与 ChatSession::completeGroupJoin 写入的完全相同
 *  （GroupProtocol::buildMemberJoinTipContent 再经 MessageProtocol::buildStoredContent 包装）。
 *    msgid：    消息 id。
 *    inviterId：邀请人 userid；0 表示没有邀请人。
 *    members：  新成员 userid。
 */
static void insertJoinTip(int64_t msgid, int64_t inviterId, const std::vector<int64_t>& members)
{
    std::vector<JoinTipMember> tipMembers;
    for (size_t i = 0; i < members.size(); ++i)
        tipMembers.push_back(JoinTipMember(members[i], "m" + std::to_string(members[i])));
    const std::string content = GroupProtocol::buildMemberJoinTipContent(inviterId, "inviter", tipMembers);
    const std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(content, noMentions, false, "");

    const int64_t kSystemSender = 0;
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "INSERT INTO t_chatmsg "
          "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,"
          "f_recalled,f_create_time) VALUES ("
       << msgid << ",'sys-join-" << msgid << "'," << conversation_group << "," << kSystemSender << ","
       << CVU_GROUP_ID << "," << message_type_system << ",'" << stored << "',0," << (CVU_BASE_TIME + msgid) << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//取某人的群会话摘要（只有一个群，直接返回那一条）
static ConversationBrief groupBriefFor(int32_t userid)
{
    std::vector<int64_t> groupids;
    groupids.push_back(CVU_GROUP_ID);
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance().getGroupConversationBriefs(userid, groupids, out));
    REQUIRE(out.size() == 1);
    return out[0];
}

TEST_CASE("CVU9 「有人入群」提示对名单里的新成员本人计未读，对邀请人与其他成员不计")
{
    resetCvuDb();

    //群里原有一条别人发的消息（msgid 1）；随后 CVU_OTHER_ID 邀请两位新成员入群（msgid 2），
    //接着是一条别的系统提示（msgid 3，例如有人退群）。三个人的已读位点都停在 1：
    //新成员入群时服务端把位点初始化到入群前的最后一条（GJ9），老成员此前读到了 1
    insertMsg(1, conversation_group, CVU_OTHER_ID, CVU_GROUP_ID);
    std::vector<int64_t> newbies;
    newbies.push_back(CVU_USER_ID);
    newbies.push_back(CVU_NEWBIE2_ID);
    insertJoinTip(2, CVU_OTHER_ID, newbies);
    insertSystemMsg(3, CVU_GROUP_ID);
    setGroupReadPosFor(CVU_USER_ID, 1);
    setGroupReadPosFor(CVU_NEWBIE2_ID, 1);
    setGroupReadPosFor(static_cast<int32_t>(CVU_OTHER_ID), 1);

    //两位新成员各计 1（只算入群提示，别的系统提示仍不计）
    CHECK(groupBriefFor(CVU_USER_ID).unread == 1);
    CHECK(groupBriefFor(CVU_NEWBIE2_ID).unread == 1);
    //邀请人不在新成员名单里，不计
    CHECK(groupBriefFor(static_cast<int32_t>(CVU_OTHER_ID)).unread == 0);
    //会话的最后一条仍按原口径取：msgid 3（另一条系统提示）
    CHECK(groupBriefFor(CVU_USER_ID).lastmsgid == 3);

    //别人入群的提示（名单里没有本人）不计
    std::vector<int64_t> stranger(1, CVU_STRANGER_ID);
    insertJoinTip(4, 0, stranger);
    CHECK(groupBriefFor(CVU_USER_ID).unread == 1);

    //读过入群提示之后不再计
    setGroupReadPosFor(CVU_USER_ID, 2);
    CHECK(groupBriefFor(CVU_USER_ID).unread == 0);

    //判断函数本身：认 tipkind 与 members 里的 userid；邀请人、别的提示、坏数据都返回 false
    std::vector<JoinTipMember> one(1, JoinTipMember(CVU_USER_ID, "a"));
    const std::vector<int64_t> noMentions;
    const std::string stored = MessageProtocol::buildStoredContent(
        GroupProtocol::buildMemberJoinTipContent(CVU_OTHER_ID, "b", one), noMentions, false, "");
    CHECK(GroupProtocol::isMemberJoinTipFor(stored, CVU_USER_ID));
    CHECK_FALSE(GroupProtocol::isMemberJoinTipFor(stored, CVU_OTHER_ID));
    CHECK_FALSE(GroupProtocol::isMemberJoinTipFor(CVU_CONTENT, CVU_USER_ID));
    CHECK_FALSE(GroupProtocol::isMemberJoinTipFor("not json", CVU_USER_ID));
    CHECK_FALSE(GroupProtocol::isMemberJoinTipFor(stored, 0));

    removeCvuDbFiles();
}
