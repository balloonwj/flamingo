/**
 *  入群系统提示与新成员已读位点的单元测试（Suite GJ），test_groupjoin.cpp
 *
 *  背景（docs/邀请入群方案.md §2.1、§2.5）：邀请入群、自由加群、审批通过之后，服务端原先只推成员变动通知，
 *  群里不出现任何提示；新成员没有 t_conversation 行，已读位点按 0 处理，入群前别人发的消息会全部算成未读。
 *  本套用例覆盖第 1 期新增的两项行为：
 *
 *    GJ1  一人邀请一人：正文、tipkind，以及 tipargs 的 inviter / inviterid / members
 *    GJ2  邀请多人：名字各加书名号，用「、」连接
 *    GJ3  邀请超过 kMemberJoinTipMaxNames 人：正文只列前几位并写「等 N 人」；members 带全部 userid、只有前几位带昵称
 *    GJ4  没有邀请人（自由加群、本人申请经审批）：正文不出现「邀请」，tipargs 不写 inviter 与 inviterid
 *    GJ5  邀请人昵称为空：正文写「有人」，tipargs 只写 inviterid
 *    GJ6  部分新成员昵称为空：跳过空名字，人数照算
 *    GJ7  新成员昵称全为空：正文写「N 位新成员」，有无邀请人两种写法
 *    GJ8  复现：新成员没有会话设置行时，入群前别人发的消息全部算成未读（描述既有行为，改动前就应当通过）
 *    GJ9  入群时初始化已读位点后，入群前的消息不计未读，入群后别人新发的消息照常计
 *    GJ10 初始化已读位点只前进不后退：本人已有更大的位点时保持原值；群里没有消息时不写会话行
 *    GJ11 审批通过时入群提示的邀请人：好友邀请与扫码 / 链接来源取推荐人，群搜索没有邀请人（2026-10-07 补，
 *         此前链接申请经审批入群时提示里没有链接生成者，与链接直接入群不一致）
 *
 *  GJ8 ~ GJ10 会打开一个临时数据库跑 SQL（默认 SQLite，设了 FLAMINGO_TEST_MYSQL=1 则跑 MySQL，见 test_dbbackend.h）。
 *
 *  用法：Tests.exe --test-case="GJ*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/GroupProtocol.h"
#include "../chatserversrc/MessageManager.h"
#include "../chatserversrc/Protocol.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/IDatabase.h"
#include "../base/Singleton.h"
#include "../jsoncpp1.9.0/json.h"

namespace {

//本套用例专用的临时数据库文件与 MySQL 测试库名，与别的套分开，避免互相干扰
const char* const GJ_DB_PATH  = "flamingo_sqlite_groupjoin_test.db";
const char* const GJ_MYSQL_DB = "flamingo_test_gj";

//灌数据用的基准时间戳（Unix 毫秒），固定取值保证结果可复现
const int64_t GJ_BASE_TIME = 1700000000000LL;

//被测的新成员、群里的老成员与群 id，取值互不相同，便于看日志时分清
const int32_t GJ_NEW_MEMBER_ID   = 300001;
const int64_t GJ_OLD_MEMBER_ID   = 300002;
const int64_t GJ_GROUP_ID        = 900101;

//邀请人的 userid 与昵称
const int64_t GJ_INVITER_ID = 5127;

//GJ3 里一次邀请的人数：比 kMemberJoinTipMaxNames 多出若干位，用来检查「等 N 人」
const size_t GJ_MANY_MEMBERS = 12;

//GJ3 里新成员 userid 的起点
const int64_t GJ_MANY_FIRST_ID = 6000;

//消息正文，内容与未读口径无关
const char* const GJ_CONTENT = "{\"text\":\"gj\"}";

//把 content 的 JSON 文本解析成对象；失败时让用例直接失败
Json::Value parseContent(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    REQUIRE(ok);
    REQUIRE(root.isObject());
    return root;
}

//构造一位新成员
JoinTipMember member(int64_t userid, const char* nickname)
{
    return JoinTipMember(userid, nickname);
}

//取一个数据库连接，取不到时让用例直接失败
IDatabase* openConn()
{
    IDatabase* pConn = DbFactory::createConnection();
    REQUIRE(pConn != NULL);
    return pConn;
}

//插一条群消息：msgid 由用例自己排号，保证顺序可控；from 为发送者
void insertGroupMsg(int64_t msgid, int64_t from)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "INSERT INTO t_chatmsg "
          "(f_msgid,f_clientmsgid,f_ctype,f_from,f_to,f_msgtype,f_content,f_recalled,f_create_time) VALUES ("
       << msgid << ",'gj" << msgid << "'," << conversation_group << "," << from << "," << GJ_GROUP_ID
       << ",1,'" << GJ_CONTENT << "',0," << (GJ_BASE_TIME + msgid) << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//直接写新成员在本群的已读位点，模拟他已有一行会话设置
void setReadPos(int64_t readmsgid)
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "REPLACE INTO t_conversation "
          "(f_userid,f_ctype,f_cid,f_pinned,f_notify_mode,f_readmsgid,f_deleted,f_deleted_msgid,f_update_time) "
          "VALUES (" << GJ_NEW_MEMBER_ID << "," << conversation_group << "," << GJ_GROUP_ID << ",0,0,"
       << readmsgid << ",0,0," << GJ_BASE_TIME << ")";
    REQUIRE(pConn->execute(os.str().c_str()));
}

//数新成员在本群有几行会话设置
int countConversationRows()
{
    std::unique_ptr<IDatabase> pConn(openConn());
    std::ostringstream os;
    os << "SELECT COUNT(*) FROM t_conversation WHERE f_userid = " << GJ_NEW_MEMBER_ID
       << " AND f_ctype = " << conversation_group << " AND f_cid = " << GJ_GROUP_ID;
    QueryResult* pResult = pConn->query(os.str().c_str());
    REQUIRE(pResult != NULL);
    int n = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        n = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return n;
}

//取新成员视角下本群的会话摘要（只有一个群，直接返回那一条）
ConversationBrief groupBrief()
{
    std::vector<int64_t> groupids;
    groupids.push_back(GJ_GROUP_ID);
    std::vector<ConversationBrief> out;
    REQUIRE(Singleton<MessageManager>::Instance().getGroupConversationBriefs(GJ_NEW_MEMBER_ID, groupids, out));
    REQUIRE(out.size() == 1);
    return out[0];
}

//读新成员在本群的已读位点
int64_t readPos()
{
    int64_t pos = -1;
    REQUIRE(Singleton<MessageManager>::Instance().getReadMsgid(GJ_NEW_MEMBER_ID, conversation_group, GJ_GROUP_ID, pos));
    return pos;
}

//初始化新成员的已读位点（被测函数）
void initReadPos()
{
    std::vector<int64_t> joiners;
    joiners.push_back(GJ_NEW_MEMBER_ID);
    REQUIRE(Singleton<MessageManager>::Instance().initNewMembersReadPos(GJ_GROUP_ID, joiners, GJ_BASE_TIME));
}

} // namespace

TEST_CASE("GJ1 一人邀请一人：正文、tipkind 与 tipargs")
{
    std::vector<JoinTipMember> members;
    members.push_back(member(5308, u8"李四"));

    CHECK(GroupProtocol::buildMemberJoinTip(GJ_INVITER_ID, u8"张三", members)
          == std::string(u8"「张三」邀请「李四」加入了群聊"));

    const Json::Value c = parseContent(GroupProtocol::buildMemberJoinTipContent(GJ_INVITER_ID, u8"张三", members));
    CHECK(c["text"].asString() == std::string(u8"「张三」邀请「李四」加入了群聊"));
    CHECK(c["tipkind"].asString() == std::string(kSystemTipMemberJoin));
    const Json::Value& args = c["tipargs"];
    CHECK(args["inviter"].asString() == std::string(u8"张三"));
    //id 一律写成字符串，与群组段其它报文一致
    REQUIRE(args["inviterid"].isString());
    CHECK(args["inviterid"].asString() == std::string("5127"));
    REQUIRE(args["members"].isArray());
    REQUIRE(args["members"].size() == 1);
    CHECK(args["members"][0]["userid"].asString() == std::string("5308"));
    CHECK(args["members"][0]["nickname"].asString() == std::string(u8"李四"));
}

TEST_CASE("GJ2 邀请多人：名字各加书名号，用「、」连接")
{
    std::vector<JoinTipMember> members;
    members.push_back(member(5308, u8"李四"));
    members.push_back(member(5309, u8"王五"));
    members.push_back(member(5310, u8"赵六"));

    CHECK(GroupProtocol::buildMemberJoinTip(GJ_INVITER_ID, u8"张三", members)
          == std::string(u8"「张三」邀请「李四」、「王五」、「赵六」加入了群聊"));

    const Json::Value c = parseContent(GroupProtocol::buildMemberJoinTipContent(GJ_INVITER_ID, u8"张三", members));
    REQUIRE(c["tipargs"]["members"].size() == 3);
    CHECK(c["tipargs"]["members"][2]["userid"].asString() == std::string("5310"));
    CHECK(c["tipargs"]["members"][2]["nickname"].asString() == std::string(u8"赵六"));
}

TEST_CASE("GJ3 邀请超过上限人数：正文只列前几位并写「等 N 人」，members 带全部 userid")
{
    REQUIRE(GJ_MANY_MEMBERS > kMemberJoinTipMaxNames);

    //昵称取 m0、m1……，便于拼出期望的正文
    std::vector<JoinTipMember> members;
    std::string expectNames;
    for (size_t i = 0; i < GJ_MANY_MEMBERS; ++i)
    {
        const std::string nick = "m" + std::to_string(i);
        members.push_back(JoinTipMember(GJ_MANY_FIRST_ID + static_cast<int64_t>(i), nick));
        if (i < kMemberJoinTipMaxNames)
        {
            if (!expectNames.empty())
                expectNames += u8"、";
            expectNames += u8"「" + nick + u8"」";
        }
    }
    const std::string expectText = u8"「张三」邀请" + expectNames + u8"等 " + std::to_string(GJ_MANY_MEMBERS)
                                   + u8" 人加入了群聊";
    CHECK(GroupProtocol::buildMemberJoinTip(GJ_INVITER_ID, u8"张三", members) == expectText);

    const Json::Value c = parseContent(GroupProtocol::buildMemberJoinTipContent(GJ_INVITER_ID, u8"张三", members));
    const Json::Value& arr = c["tipargs"]["members"];
    REQUIRE(arr.size() == GJ_MANY_MEMBERS);
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        CHECK(arr[i]["userid"].asString() == std::to_string(GJ_MANY_FIRST_ID + static_cast<int64_t>(i)));
        //只有前 kMemberJoinTipMaxNames 位带昵称
        CHECK(arr[i].isMember("nickname") == (i < kMemberJoinTipMaxNames));
    }
}

TEST_CASE("GJ4 没有邀请人：正文不出现「邀请」，tipargs 不写 inviter 与 inviterid")
{
    std::vector<JoinTipMember> members;
    members.push_back(member(5308, u8"李四"));

    CHECK(GroupProtocol::buildMemberJoinTip(0, "", members) == std::string(u8"「李四」加入了群聊"));

    const Json::Value c = parseContent(GroupProtocol::buildMemberJoinTipContent(0, "", members));
    CHECK(c["text"].asString() == std::string(u8"「李四」加入了群聊"));
    CHECK_FALSE(c["tipargs"].isMember("inviter"));
    CHECK_FALSE(c["tipargs"].isMember("inviterid"));
    CHECK(c["tipargs"]["members"].size() == 1);
}

TEST_CASE("GJ5 邀请人昵称为空：正文写「有人」，tipargs 只写 inviterid")
{
    std::vector<JoinTipMember> members;
    members.push_back(member(5308, u8"李四"));

    CHECK(GroupProtocol::buildMemberJoinTip(GJ_INVITER_ID, "", members)
          == std::string(u8"「有人」邀请「李四」加入了群聊"));

    const Json::Value c = parseContent(GroupProtocol::buildMemberJoinTipContent(GJ_INVITER_ID, "", members));
    CHECK(c["tipargs"]["inviterid"].asString() == std::string("5127"));
    CHECK_FALSE(c["tipargs"].isMember("inviter"));
}

TEST_CASE("GJ6 部分新成员昵称为空：跳过空名字，人数照算")
{
    std::vector<JoinTipMember> members;
    members.push_back(member(5308, u8"李四"));
    members.push_back(member(5309, ""));

    CHECK(GroupProtocol::buildMemberJoinTip(GJ_INVITER_ID, u8"张三", members)
          == std::string(u8"「张三」邀请「李四」等 2 人加入了群聊"));

    //昵称为空的那一项只带 userid
    const Json::Value c = parseContent(GroupProtocol::buildMemberJoinTipContent(GJ_INVITER_ID, u8"张三", members));
    REQUIRE(c["tipargs"]["members"].size() == 2);
    CHECK(c["tipargs"]["members"][1]["userid"].asString() == std::string("5309"));
    CHECK_FALSE(c["tipargs"]["members"][1].isMember("nickname"));
}

TEST_CASE("GJ7 新成员昵称全为空：正文写「N 位新成员」")
{
    std::vector<JoinTipMember> members;
    members.push_back(member(5308, ""));
    members.push_back(member(5309, ""));
    members.push_back(member(5310, ""));

    CHECK(GroupProtocol::buildMemberJoinTip(GJ_INVITER_ID, u8"张三", members)
          == std::string(u8"「张三」邀请 3 位新成员加入了群聊"));
    CHECK(GroupProtocol::buildMemberJoinTip(0, "", members) == std::string(u8"3 位新成员加入了群聊"));

    //没有新成员时没有可说的，返回空串
    CHECK(GroupProtocol::buildMemberJoinTip(GJ_INVITER_ID, u8"张三", std::vector<JoinTipMember>()).empty());
}

TEST_CASE("GJ8 复现：新成员没有会话设置行时，入群前别人发的消息全部算成未读")
{
    testdb::resetDb(GJ_DB_PATH, GJ_MYSQL_DB);

    //新成员入群前，老成员在群里发了 5 条
    for (int64_t i = 1; i <= 5; ++i)
        insertGroupMsg(i, GJ_OLD_MEMBER_ID);

    //新成员从没读过、也没有 t_conversation 行：已读位点按 0 处理，5 条全部算未读
    CHECK(countConversationRows() == 0);
    CHECK(groupBrief().unread == 5);

    testdb::cleanupDb(GJ_DB_PATH);
}

TEST_CASE("GJ9 入群时初始化已读位点后，入群前的消息不计未读，入群后的照常计")
{
    testdb::resetDb(GJ_DB_PATH, GJ_MYSQL_DB);

    for (int64_t i = 1; i <= 5; ++i)
        insertGroupMsg(i, GJ_OLD_MEMBER_ID);

    //入群那一刻：位点推到本群最新一条
    initReadPos();
    CHECK(readPos() == 5);
    CHECK(groupBrief().unread == 0);

    //入群后老成员又发了 2 条：照常算未读
    insertGroupMsg(6, GJ_OLD_MEMBER_ID);
    insertGroupMsg(7, GJ_OLD_MEMBER_ID);
    CHECK(groupBrief().unread == 2);

    testdb::cleanupDb(GJ_DB_PATH);
}

TEST_CASE("GJ10 初始化已读位点只前进不后退；群里没有消息时不写会话行")
{
    //群里还没有任何消息：不写会话行
    testdb::resetDb(GJ_DB_PATH, GJ_MYSQL_DB);
    initReadPos();
    CHECK(countConversationRows() == 0);
    testdb::cleanupDb(GJ_DB_PATH);

    //本人已有一个比本群最新消息更大的位点（例如此前从别的途径上报过）：保持原值
    testdb::resetDb(GJ_DB_PATH, GJ_MYSQL_DB);
    for (int64_t i = 1; i <= 5; ++i)
        insertGroupMsg(i, GJ_OLD_MEMBER_ID);
    const int64_t kLargerReadPos = 100;
    setReadPos(kLargerReadPos);
    initReadPos();
    CHECK(readPos() == kLargerReadPos);

    //本人已有一个较小的位点：推到本群最新一条
    setReadPos(2);
    initReadPos();
    CHECK(readPos() == 5);

    testdb::cleanupDb(GJ_DB_PATH);
}

TEST_CASE("GJ11 审批通过时入群提示的邀请人：好友邀请与扫码 / 链接取推荐人，群搜索没有邀请人")
{
    //邀请人与推荐人的 userid（任取一个正数）
    const int64_t kRecommender = 5308;

    //普通成员邀请转来的申请：推荐人即邀请人
    CHECK(GroupProtocol::joinTipInviterForRequest(group_join_source_friend, kRecommender) == kRecommender);
    //扫码 / 加群链接转来的申请：推荐人是链接生成者，与链接直接入群时提示里写生成者一致（docs/邀请入群方案.md §12.1）
    CHECK(GroupProtocol::joinTipInviterForRequest(group_join_source_qrcode, kRecommender) == kRecommender);
    //本人搜索群后主动申请：没有邀请人
    CHECK(GroupProtocol::joinTipInviterForRequest(group_join_source_search, kRecommender) == 0);
    //推荐人缺失（记为 0）时没有邀请人
    CHECK(GroupProtocol::joinTipInviterForRequest(group_join_source_qrcode, 0) == 0);
    CHECK(GroupProtocol::joinTipInviterForRequest(group_join_source_friend, 0) == 0);
}
