/**
 *  群组存储管理器实现，GroupManager.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "GroupManager.h"
#include <memory>
#include <set>
#include <sstream>
#include <vector>
#include <time.h>          //time()，禁言到期判定取当前时刻用
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h" //CDbTransaction：解散、移出成员、转让群主的多条写语句一起成败
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "UserManager.h"   //仅为复用群ID下界宏 GROUPID_BOUBDARY
#include "GroupAutoName.h" //自动群名的生成规则
#include "MessageSearchText.h" //MakeLikeContainsPattern：群公告搜索的 LIKE 匹配串
#include "../dbapi/SqlDialect.h" //dbapi::EscapeString（搜索关键字拼进 SQL 前转义）
#include "MessageManager.h"        //GroupSearchScope：getUserHistoryScopes 的出参

/**
 *  执行一条只返回一个整数的查询（如 SELECT MAX(...)），取回这个整数。
 *    pConn：已建立的数据库连接，不转移所有权；
 *    sql：  查询语句。
 *  返回：查询结果；查询失败、没有结果行或结果为 NULL（空表上的 MAX）时返回 0。
 */
static int64_t queryInt64(IDatabase* pConn, const char* sql)
{
    int64_t value = 0;
    QueryResult* pResult = pConn->query(sql);
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            value = static_cast<int64_t>(pRow[0].getUInt64());
        }
        pResult->endQuery();
        delete pResult;
    }
    return value;
}

/**
 *  在一个事务里依次执行多条写语句，任何一条失败都整体回滚。
 *
 *  解散群、移出成员、转让群主都要同时改好几张表，中途失败若不回滚，就会留下"群主指向新群主、
 *  新群主的角色却还是普通成员"或"成员行已删、收藏行还在"这类不一致的数据。走 CDbTransaction
 *  守卫而不是自己发事务控制语句：两种后端开启事务的语句不同，由驱动各自处理；每条错误路径直接
 *  返回即可，守卫析构时自动回滚。
 *    pConn：     已建立的数据库连接，不转移所有权；
 *    statements：要执行的写语句，按下标顺序执行；
 *    caller：    调用方函数名，只用于日志；
 *    groupid：   本次操作的群ID，只用于日志。
 *  返回：全部语句执行成功且提交成功时返回 true。
 */
static bool executeInTransaction(IDatabase* pConn, const std::vector<std::string>& statements,
                                 const char* caller, int64_t groupid)
{
    CDbTransaction transaction(pConn);
    if (!transaction.IsActive())
    {
        LOGE("%s begin transaction error, groupid: %lld", caller, (long long)groupid);
        return false;
    }
    for (size_t i = 0; i < statements.size(); ++i)
    {
        if (!pConn->execute(statements[i].c_str()))
        {
            LOGE("%s statement %u failed, rolled back, groupid: %lld, sql: %s",
                 caller, static_cast<unsigned>(i), (long long)groupid, statements[i].c_str());
            return false;
        }
    }
    if (!transaction.Commit())
    {
        LOGE("%s commit error, groupid: %lld", caller, (long long)groupid);
        return false;
    }
    return true;
}

/**
 *  把一组 userid 拼成 "a,b,c"，用于 SQL 的 IN (...)。
 *    userids：userid 列表，调用方保证非空。
 *  返回：逗号分隔的十进制串。
 */
static std::string joinUserIds(const std::vector<int64_t>& userids)
{
    std::ostringstream os;
    for (size_t i = 0; i < userids.size(); ++i)
    {
        if (i > 0)
            os << ",";
        os << userids[i];
    }
    return os.str();
}

//一条多行 INSERT 最多带多少个成员行。500 行约 30KB，远低于两种后端的语句长度上限；企业群上限也在这个量级，
//常见规模一条语句写完
static const size_t kMemberInsertBatch = 500;

/**
 *  把一批成员行按 kMemberInsertBatch 一条语句多行写入 t_group_member（INSERT IGNORE：已是成员的 userid 静默跳过，
 *  同一批里重复的 userid 只写第一个）。调用方负责把它放进事务：逐条各自提交时每次都要刷盘，WSL 访问 /mnt/e 上的
 *  SQLite 库时一次提交约 8 毫秒，166 人的群要 1.3 秒（用例 GB1，2026-10-08 实测）。
 *    pConn：      数据库连接（借用，不持有）
 *    groupid：    群ID
 *    userids：    要写入的 userid；等于 skipUserId 的项跳过
 *    role：       写入的角色，取值见 group_member_role
 *    joinTime：   入群时间，Unix毫秒
 *    historyFrom：能看到的最早一条群消息 msgid（f_history_from），0 表示不限
 *    skipUserId： 要跳过的 userid（建群时为群主，群主一行另写）；0 表示不跳过
 *  返回：全部语句执行成功返回 true；任一条失败返回 false（已执行的部分由调用方的事务回滚）
 */
static bool insertMemberRows(IDatabase* pConn, int64_t groupid, const std::vector<int64_t>& userids, int role,
                             int64_t joinTime, int64_t historyFrom, int64_t skipUserId)
{
    size_t index = 0;
    while (index < userids.size())
    {
        std::ostringstream osSql;
        osSql << "INSERT IGNORE INTO t_group_member(f_groupid, f_userid, f_role, f_join_time, f_history_from) VALUES ";
        size_t rows = 0;
        for (; index < userids.size() && rows < kMemberInsertBatch; ++index)
        {
            if (skipUserId != 0 && userids[index] == skipUserId)
                continue;
            osSql << (rows > 0 ? ", " : "") << "(" << groupid << ", " << userids[index] << ", " << role << ", "
                  << joinTime << ", " << historyFrom << ")";
            ++rows;
        }
        if (rows == 0)
            continue;
        if (!pConn->execute(osSql.str().c_str()))
        {
            LOGE("insertMemberRows error, groupid: %lld, rows: %u", (long long)groupid, static_cast<unsigned>(rows));
            return false;
        }
    }
    return true;
}

GroupManager::GroupManager()
{
}

GroupManager::~GroupManager()
{
}

bool GroupManager::init()
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::init db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //群ID分配器 seed 到"用过的最大群ID"；一个群都没有时退回到 GROUPID_BOUBDARY。
    //
    //只看 t_group 不够：解散群会删掉 t_group 行，但群消息保留在 t_chatmsg 里供审计。若被解散的
    //恰好是群号最大的群，重启后新群就会复用这个群号，新群成员也就能翻到旧群的聊天记录。故同时
    //取 t_chatmsg.f_to 的最大值：群号都大于 GROUPID_BOUBDARY、用户号都小于它，单聊消息的 f_to
    //不会抬高结果；不加 f_ctype 条件，MAX(f_to) 可以直接走 f_to 打头的索引，不扫表。
    int64_t base = GROUPID_BOUBDARY;
    const int64_t maxGroupId = queryInt64(pConn.get(), "SELECT MAX(f_groupid) FROM t_group");
    if (maxGroupId > base)
        base = maxGroupId;
    const int64_t maxMessageTarget = queryInt64(pConn.get(), "SELECT MAX(f_to) FROM t_chatmsg");
    if (maxMessageTarget > base)
        base = maxMessageTarget;
    m_baseGroupId = base;
    LOGI("GroupManager::init ok, group id allocator seeded at %lld (max groupid %lld, max message target %lld)",
         (long long)base, (long long)maxGroupId, (long long)maxMessageTarget);
    return true;
}

//当前时间，Unix毫秒。禁言到期判定与各处落库时间戳共用同一口径。
static int64_t nowMilliseconds()
{
    return static_cast<int64_t>(time(NULL)) * 1000;
}

/**
 *  把一列文本值拼成 "列名 = '转义后的值'" 的 UPDATE SET 片段。
 *  updateGroupInfo 里有六个文本列要这么拼，逐个手写转义太啰嗦也容易漏。
 *    pConn：  已建立的数据库连接，转义要用它（转义规则与连接字符集相关）；
 *    column： 列名，编译期常量，不参与转义；
 *    value：  待写入的值，可含任意字符。
 *  返回：可直接拼进 UPDATE 语句的 SET 片段。
 */
static std::string makeTextSet(IDatabase* pConn, const char* column,
                               const std::string& value)
{
    //转义后最坏情况每个字符变两个，再留一个结束符
    std::vector<char> esc(value.size() * 2 + 1, '\0');
    pConn->escapeString(&esc[0], value.c_str(), static_cast<uint32_t>(value.size()));
    return std::string(column) + " = '" + &esc[0] + "'";
}

//群资料查询的 SELECT 列清单。getGroup 与 getUserGroups 两处查询的列必须逐字一致
//（都由 fillGroupRecord 按下标取值），故抽成一个常量，避免改一处漏一处。
//表别名固定为 g；membercount 由调用处各自拼相关子查询接在其后，作为最后一列。
static const char* const kGroupSelectColumns =
    "g.f_groupid, g.f_name, g.f_facetype, g.f_customface, g.f_announcement, "
    "g.f_ownerid, g.f_join_mode, g.f_mute_all, g.f_create_time, "
    "g.f_intro, g.f_at_all_scope, g.f_new_see_history, g.f_member_invite, "
    "g.f_welcome_on, g.f_welcome_text, "
    "g.f_announce_id, g.f_announce_title, g.f_announce_time, g.f_name_custom, g.f_must_read_id, g.f_group_type";

//群成员数子查询，接在 kGroupSelectColumns 之后作为最后一列
static const char* const kGroupMemberCountColumn =
    ", (SELECT COUNT(*) FROM t_group_member c WHERE c.f_groupid = g.f_groupid) ";

//按 kGroupSelectColumns + kGroupMemberCountColumn 的列序填一条群记录
static void fillGroupRecord(Field* pRow, GroupRecord& g)
{
    g.groupid       = static_cast<int64_t>(pRow[0].getUInt64());
    g.name          = pRow[1].getCppString();
    g.facetype      = pRow[2].getInt32();
    g.customface    = pRow[3].getCppString();
    g.announcement  = pRow[4].getCppString();
    g.ownerid       = static_cast<int64_t>(pRow[5].getUInt64());
    g.joinmode      = pRow[6].getInt32();
    g.muteall       = pRow[7].getInt32();
    g.createtime    = static_cast<int64_t>(pRow[8].getUInt64());
    g.intro         = pRow[9].getCppString();
    g.atallscope    = pRow[10].getInt32();
    g.newseehistory = pRow[11].getInt32();
    g.memberinvite  = pRow[12].getInt32();
    g.welcomeon     = pRow[13].getInt32();
    g.welcometext   = pRow[14].getCppString();
    g.announceid    = static_cast<int64_t>(pRow[15].getUInt64());
    g.announcetitle = pRow[16].getCppString();
    g.announcetime  = static_cast<int64_t>(pRow[17].getUInt64());
    g.namecustom    = pRow[18].getInt32();
    g.mustreadid    = static_cast<int64_t>(pRow[19].getUInt64());
    g.grouptype     = pRow[20].getInt32();
    g.membercount   = pRow[21].getInt32();
}

int64_t GroupManager::createGroup(const std::string& name, int32_t ownerid,
                                  const std::vector<int64_t>& memberids, int64_t createTime, int groupType)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::createGroup db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    int64_t groupid = ++m_baseGroupId;

    //群名称含引号 / 中文，须经 escapeString 转义
    std::vector<char> escName(name.size() * 2 + 1, '\0');
    pConn->escapeString(&escName[0], name.c_str(), static_cast<uint32_t>(name.size()));

    //群行与全部成员行在一个事务里写入、一次提交（2026-10-08 建群卡顿：逐条各自提交时每条都要刷盘），
    //任何一步失败整体回滚，不会留下只有一半成员的群
    {
        CDbTransaction transaction(pConn.get());
        if (!transaction.IsActive())
        {
            LOGE("GroupManager::createGroup begin transaction error, owner: %d", ownerid);
            return 0;
        }

        //1) 写 t_group（facetype/customface/announcement/join_mode/mute_all 取列默认值）。
        //   群名为空时先写空串并标记为自动群名，等成员行写完再由 refreshAutoGroupName 生成。
        //   群类型建群时写定，之后不再改（docs/帖子群方案.md）
        const int nameCustomFlag = name.empty() ? 0 : 1;
        std::ostringstream osGroup;
        osGroup << "INSERT INTO t_group(f_groupid, f_name, f_name_custom, f_ownerid, f_create_time, f_group_type) VALUES("
                << groupid << ", '" << &escName[0] << "', " << nameCustomFlag << ", "
                << ownerid << ", " << createTime << ", " << groupType << ")";
        if (!pConn->execute(osGroup.str().c_str()))
        {
            LOGE("GroupManager::createGroup insert t_group error, name: %s", name.c_str());
            return 0;
        }

        //2) 写群主成员行
        const std::vector<int64_t> owner(1, static_cast<int64_t>(ownerid));
        if (!insertMemberRows(pConn.get(), groupid, owner, group_role_owner, createTime, 0, 0))
            return 0;

        //3) 写初始成员行（多行合并写入）；INSERT IGNORE 防御重复 userid，并跳过与群主相同的项
        if (!insertMemberRows(pConn.get(), groupid, memberids, group_role_member, createTime, 0, ownerid))
            return 0;

        if (!transaction.Commit())
        {
            LOGE("GroupManager::createGroup commit error, groupid: %lld", (long long)groupid);
            return 0;
        }
    }

    //4) 没给群名的，按刚写入的成员生成自动群名。须在提交之后：它另开一条连接读成员行
    if (name.empty())
    {
        std::string autoName;
        refreshAutoGroupName(groupid, autoName);
    }

    return groupid;
}

int64_t GroupManager::createGroup(const std::string& name, int32_t ownerid,
                                  const std::vector<int64_t>& adminids,
                                  const std::vector<int64_t>& memberids, int64_t createTime)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::createGroup(admin) db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    int64_t groupid = ++m_baseGroupId;

    //群名称含引号 / 中文，须经 escapeString 转义
    std::vector<char> escName(name.size() * 2 + 1, '\0');
    pConn->escapeString(&escName[0], name.c_str(), static_cast<uint32_t>(name.size()));

    //群行与全部成员行在一个事务里写入、一次提交（理由同上一个版本），任何一步失败整体回滚
    {
        CDbTransaction transaction(pConn.get());
        if (!transaction.IsActive())
        {
            LOGE("GroupManager::createGroup(admin) begin transaction error, owner: %d", ownerid);
            return 0;
        }

        //1) 写 t_group（facetype/customface/announcement/join_mode/mute_all 取列默认值）。
        //   群名为空时先写空串并标记为自动群名，等成员行写完再由 refreshAutoGroupName 生成
        const int nameCustomFlag = name.empty() ? 0 : 1;
        std::ostringstream osGroup;
        osGroup << "INSERT INTO t_group(f_groupid, f_name, f_name_custom, f_ownerid, f_create_time) VALUES("
                << groupid << ", '" << &escName[0] << "', " << nameCustomFlag << ", "
                << ownerid << ", " << createTime << ")";
        if (!pConn->execute(osGroup.str().c_str()))
        {
            LOGE("GroupManager::createGroup(admin) insert t_group error, name: %s", name.c_str());
            return 0;
        }

        //2) 写群主成员行（角色为群主）
        const std::vector<int64_t> owner(1, static_cast<int64_t>(ownerid));
        if (!insertMemberRows(pConn.get(), groupid, owner, group_role_owner, createTime, 0, 0))
            return 0;

        //3) 写管理员成员行（角色为管理员）；INSERT IGNORE 防御重复 userid，并跳过与群主相同的项
        if (!insertMemberRows(pConn.get(), groupid, adminids, group_role_admin, createTime, 0, ownerid))
            return 0;

        //4) 写普通成员行（角色为普通成员）；INSERT IGNORE 防御重复 userid，并跳过与群主相同的项
        if (!insertMemberRows(pConn.get(), groupid, memberids, group_role_member, createTime, 0, ownerid))
            return 0;

        if (!transaction.Commit())
        {
            LOGE("GroupManager::createGroup(admin) commit error, groupid: %lld", (long long)groupid);
            return 0;
        }
    }

    //5) 没给群名的，按刚写入的成员生成自动群名。须在提交之后：它另开一条连接读成员行
    if (name.empty())
    {
        std::string autoName;
        refreshAutoGroupName(groupid, autoName);
    }

    return groupid;
}

bool GroupManager::dismissGroup(int64_t groupid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::dismissGroup db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //依次删除：各成员对该群的收藏行与会话设置行、群公告、加群申请、群文件条目、成员行，
    //最后删群本身。几点说明：
    //  · 群消息（t_chatmsg）保留不删，供企业日后审计追溯；群号也因此不会被新群复用，见 init。
    //  · 群文件只删条目，文件实体按 MD5 去重存放、可能被别的会话引用，不在这里删。
    //  · 被钉住的消息（t_message_pin）由调用方另经 MessagePinManager::removePinsForGroup 清理。
    std::vector<std::string> statements;
    {
        std::ostringstream os;
        os << "DELETE FROM t_favorite WHERE f_ctype = " << conversation_group << " AND f_cid = " << groupid;
        statements.push_back(os.str());
    }
    {
        std::ostringstream os;
        os << "DELETE FROM t_conversation WHERE f_ctype = " << conversation_group << " AND f_cid = " << groupid;
        statements.push_back(os.str());
    }
    //以下各表都按 f_groupid 定位（t_group_follow 是各成员在本群的关注名单）
    const char* const kGroupTables[] = {
        "t_group_announcement",
        "t_group_join_request",
        "t_group_file",
        "t_group_follow",
        "t_group_qa",               //本群问答（docs/群问答方案.md）
        "t_enterprise_qa_group",    //企业问答应用到本群的登记
        "t_group_link",             //加群链接（docs/邀请入群方案.md §12）
        "t_thread_reaction",        //帖子群的互动（docs/帖子群方案.md 第 5 节；以下四张表普通群里没有行）
        "t_thread_comment",         //帖子群的留言与回复
        "t_thread_addendum",        //帖子群的楼主补充
        "t_thread_post",            //帖子群的帖子
        "t_group_member",
        "t_group"
    };
    for (size_t i = 0; i < sizeof(kGroupTables) / sizeof(kGroupTables[0]); ++i)
    {
        std::ostringstream os;
        os << "DELETE FROM " << kGroupTables[i] << " WHERE f_groupid = " << groupid;
        statements.push_back(os.str());
    }

    if (!executeInTransaction(pConn.get(), statements, "GroupManager::dismissGroup", groupid))
        return false;

    LOGI("GroupManager::dismissGroup ok, groupid: %lld", (long long)groupid);
    return true;
}

bool GroupManager::getGroup(int64_t groupid, GroupRecord& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getGroup db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //membercount 用相关子查询一并带出
    std::ostringstream osSql;
    osSql << "SELECT " << kGroupSelectColumns << kGroupMemberCountColumn
          << "FROM t_group g WHERE g.f_groupid = " << groupid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        fillGroupRecord(pRow, out);
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

//群成员查询的 SELECT 列清单。getGroupMembers、getMember、getUserMemberships 三处都由 fillMemberRecord
//按下标取值，列序必须逐字一致，故抽成一个常量；kMemberSelectColumnCount 是它的列数
static const char* const kMemberSelectColumns =
    "f_userid, f_role, f_card, f_muted, f_join_time, f_mute_until, f_mute_reason, f_history_from, f_must_read";
static const int kMemberSelectColumnCount = 9;

//按 kMemberSelectColumns 的列序填一条群成员记录。now 为当前时间（Unix 毫秒），用于禁言到期判定
static void fillMemberRecord(Field* pRow, int64_t now, GroupMemberRecord& m)
{
    m.userid      = static_cast<int64_t>(pRow[0].getUInt64());
    m.role        = pRow[1].getInt32();
    m.card        = pRow[2].getCppString();
    m.muted       = pRow[3].getInt32();
    m.jointime    = static_cast<int64_t>(pRow[4].getUInt64());
    m.muteuntil   = static_cast<int64_t>(pRow[5].getUInt64());
    m.mutereason  = pRow[6].getCppString();
    m.historyfrom = static_cast<int64_t>(pRow[7].getUInt64());
    m.mustread    = pRow[8].getInt32();

    //定时禁言到期后不写库、只在读到时按未禁言呈现（惰性判定，见
    //docs/群管理接入方案.md §3.2）。真正把库里的 f_muted 抹掉发生在
    //发言校验那条路径上 —— 那里本来就要写库，顺手清理不额外增加开销。
    if (GroupProtocol::isMuteExpired(m.muted, m.muteuntil, now))
    {
        m.muted      = 0;
        m.muteuntil  = 0;
        m.mutereason.clear();
    }
}

bool GroupManager::getGroupMembers(int64_t groupid, std::vector<GroupMemberRecord>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getGroupMembers db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT " << kMemberSelectColumns
          << " FROM t_group_member WHERE f_groupid = " << groupid << " ORDER BY f_join_time ASC";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    const int64_t now = nowMilliseconds();
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        GroupMemberRecord m;
        fillMemberRecord(pRow, now, m);
        out.push_back(m);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupManager::getMember(int64_t groupid, int32_t userid, GroupMemberRecord& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getMember db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT " << kMemberSelectColumns << " FROM t_group_member WHERE f_groupid = " << groupid
          << " AND f_userid = " << userid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        fillMemberRecord(pRow, nowMilliseconds(), out);
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool GroupManager::getUserMemberships(int32_t userid, std::map<int64_t, GroupMemberRecord>& out)
{
    out.clear();
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getUserMemberships db init failed, db: %s",
             Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //列序与 kMemberSelectColumns 相同，最后多一列群 id（下标 kMemberSelectColumnCount）
    std::ostringstream osSql;
    osSql << "SELECT " << kMemberSelectColumns << ", f_groupid FROM t_group_member WHERE f_userid = " << userid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    const int64_t now = nowMilliseconds();
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        GroupMemberRecord m;
        fillMemberRecord(pRow, now, m);
        out[static_cast<int64_t>(pRow[kMemberSelectColumnCount].getUInt64())] = m;

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupManager::getUserHistoryScopes(int32_t userid, std::vector<GroupSearchScope>& out)
{
    out.clear();
    std::vector<GroupRecord> groups;
    if (!getUserGroups(userid, groups))
    {
        LOGE("GroupManager::getUserHistoryScopes failed to load groups, userid: %d", userid);
        return false;
    }
    std::map<int64_t, GroupMemberRecord> rows;
    getUserMemberships(userid, rows);
    for (size_t i = 0; i < groups.size(); ++i)
    {
        GroupSearchScope scope;
        scope.groupid = groups[i].groupid;
        std::map<int64_t, GroupMemberRecord>::const_iterator row = rows.find(groups[i].groupid);
        if (row != rows.end())
        {
            const GroupHistoryBound bound = GroupProtocol::computeHistoryBound(
                row->second.role, groups[i].newseehistory, row->second.jointime, row->second.historyfrom);
            scope.minCreateTime = bound.m_minCreateTime;
            scope.minMsgId = bound.m_minMsgId;
        }
        out.push_back(scope);
    }
    return true;
}

bool GroupManager::getUserGroups(int32_t userid, std::vector<GroupRecord>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getUserGroups db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //以 t_group_member 为驱动表，WHERE f_userid 命中 idx_user 索引直接定位该用户的
    //全部成员行，再 JOIN t_group 取群资料；membercount 用相关子查询逐群计数。
    std::ostringstream osSql;
    osSql << "SELECT " << kGroupSelectColumns << kGroupMemberCountColumn
          << "FROM t_group_member m JOIN t_group g ON m.f_groupid = g.f_groupid "
             "WHERE m.f_userid = " << userid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        GroupRecord g;
        fillGroupRecord(pRow, g);
        out.push_back(g);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupManager::isGroupMember(int64_t groupid, int32_t userid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT 1 FROM t_group_member WHERE f_groupid = " << groupid
          << " AND f_userid = " << userid << " LIMIT 1";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = (pResult->fetch() != NULL);
    pResult->endQuery();
    delete pResult;
    return found;
}

int GroupManager::getMemberRole(int64_t groupid, int32_t userid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
        return -1;

    std::ostringstream osSql;
    osSql << "SELECT f_role FROM t_group_member WHERE f_groupid = " << groupid
          << " AND f_userid = " << userid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return -1;

    //非成员返回 -1
    int role = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        role = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return role;
}

bool GroupManager::getMemberRoleAndMuted(int64_t groupid, int32_t userid, int& role, int& muted,
                                         int64_t* muteUntil, int* mustRead)
{
    role  = group_role_member;
    muted = 0;
    if (muteUntil != NULL)
        *muteUntil = 0;
    if (mustRead != NULL)
        *mustRead = 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT f_role, f_muted, f_mute_until, f_must_read FROM t_group_member WHERE f_groupid = " << groupid
          << " AND f_userid = " << userid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        role = pRow[0].getInt32();
        muted = pRow[1].getInt32();
        const int64_t until = static_cast<int64_t>(pRow[2].getUInt64());
        if (muteUntil != NULL)
            *muteUntil = until;

        //定时禁言到期后按未禁言返回；是否顺手把库里的 f_muted 抹掉由调用方
        //据出参 muteUntil 决定（见 clearExpiredMute 的注释）。
        if (GroupProtocol::isMuteExpired(muted, until, nowMilliseconds()))
            muted = 0;
        if (mustRead != NULL)
            *mustRead = pRow[3].getInt32();

        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool GroupManager::clearExpiredMute(int64_t groupid, int32_t userid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::clearExpiredMute db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //条件里再带一次 f_mute_until 的判断：从读到过期、到执行这条 UPDATE 之间，
    //管理员可能刚好又下了一次新的禁言，那种情况下不能把新禁言一并抹掉。
    std::ostringstream osSql;
    osSql << "UPDATE t_group_member SET f_muted = 0, f_mute_until = 0, f_mute_reason = '' "
             "WHERE f_groupid = " << groupid << " AND f_userid = " << userid
          << " AND f_muted <> 0 AND f_mute_until > 0 AND f_mute_until <= " << nowMilliseconds();
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::clearExpiredMute error, groupid: %lld, userid: %d",
             (long long)groupid, userid);
        return false;
    }

    LOGI("mute expired and cleared, groupid: %lld, userid: %d", (long long)groupid, userid);
    return true;
}

bool GroupManager::updateGroupInfo(const UpdateGroupInfoRequest& req)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::updateGroupInfo db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //逐项拼 SET 片段，只含 has_* 为 true 的列；文本列经 makeTextSet 转义
    std::vector<std::string> sets;
    //群名非空：改为用户设定的群名，此后不再随成员变化；
    //群名为空串：恢复为自动群名，群名本身在 UPDATE 之后由 refreshAutoGroupName 重算
    const bool restoreAutoName = req.has_name && req.name.empty();
    if (req.has_name && !restoreAutoName)
    {
        sets.push_back(makeTextSet(pConn.get(), "f_name", req.name));
        sets.push_back("f_name_custom = 1");
    }
    if (restoreAutoName)
        sets.push_back("f_name_custom = 0");
    if (req.has_facetype)
        sets.push_back("f_facetype = " + std::to_string(req.facetype));
    if (req.has_customface)
        sets.push_back(makeTextSet(pConn.get(), "f_customface", req.customface));
    //f_announcement 刻意<u>不</u>在这里写。群公告功能上线后它已经变成派生列 ——
    //是 t_group_announcement 里最新一条已发布公告的正文快照，由
    //refreshLatestAnnouncement 统一维护。若这里仍允许直接改，同一份数据就有了
    //两条写入路径，先后顺序不同结果就不同。老客户端带 announcement 上来时静默
    //忽略该项（其余项照改），改公告请走 cmd_publish_group_announcement。
    if (req.has_announcement)
    {
        LOGI("updateGroupInfo ignores announcement field, groupid: %lld "
             "(use cmd_publish_group_announcement instead)", (long long)req.groupid);
    }
    if (req.has_joinmode)
        sets.push_back("f_join_mode = " + std::to_string(req.joinmode));
    if (req.has_muteall)
        sets.push_back("f_mute_all = " + std::to_string(req.muteall));

    //---- 以下六项为群管理功能新增的群设置 ----
    if (req.has_intro)
        sets.push_back(makeTextSet(pConn.get(), "f_intro", req.intro));
    if (req.has_atallscope)
        sets.push_back("f_at_all_scope = " + std::to_string(req.atallscope));
    if (req.has_newseehistory)
        sets.push_back("f_new_see_history = " + std::to_string(req.newseehistory));
    if (req.has_memberinvite)
        sets.push_back("f_member_invite = " + std::to_string(req.memberinvite));
    if (req.has_welcomeon)
        sets.push_back("f_welcome_on = " + std::to_string(req.welcomeon));
    if (req.has_welcometext)
        sets.push_back(makeTextSet(pConn.get(), "f_welcome_text", req.welcometext));

    //没有任何字段需要更新，视为成功（no-op）
    if (sets.empty())
        return true;

    std::ostringstream osSql;
    osSql << "UPDATE t_group SET ";
    for (size_t i = 0; i < sets.size(); ++i)
    {
        if (i > 0)
            osSql << ", ";
        osSql << sets[i];
    }
    osSql << " WHERE f_groupid = " << req.groupid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::updateGroupInfo error, groupid: %lld", (long long)req.groupid);
        return false;
    }

    if (restoreAutoName)
    {
        std::string autoName;
        refreshAutoGroupName(req.groupid, autoName);
        LOGI("group name restored to auto, groupid: %lld", (long long)req.groupid);
    }
    return true;
}

bool GroupManager::addMembers(int64_t groupid, const std::vector<int64_t>& userids, int64_t joinTime,
                              int64_t historyFrom)
{
    if (userids.empty())
        return true;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::addMembers db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //INSERT IGNORE：已是成员的 userid 静默跳过，保证幂等（已在群里的人的历史边界也因此不变）。
    //全部行在一个事务里多行合并写入、一次提交（2026-10-08 建群卡顿，用例 GB3），失败时整体回滚
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("GroupManager::addMembers begin transaction error, groupid: %lld", (long long)groupid);
        return false;
    }
    if (!insertMemberRows(pConn.get(), groupid, userids, group_role_member, joinTime, historyFrom, 0))
    {
        LOGE("GroupManager::addMembers insert error, groupid: %lld, users: %u",
             (long long)groupid, static_cast<unsigned>(userids.size()));
        return false;
    }
    if (!transaction.Commit())
    {
        LOGE("GroupManager::addMembers commit error, groupid: %lld", (long long)groupid);
        return false;
    }
    return true;
}

bool GroupManager::removeMembers(int64_t groupid, const std::vector<int64_t>& userids)
{
    if (userids.empty())
        return true;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::removeMembers db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //当事人离开后，他对这个群的收藏行与会话设置行（置顶、免打扰、已读位点）都已无意义：收藏
    //分区显示不出来却仍占收藏上限，日后重新入群时旧的置顶与免打扰状态也会意外恢复。各条语句
    //一起成败，成员行放在最后删
    const std::string idList = joinUserIds(userids);
    std::vector<std::string> statements;
    {
        std::ostringstream os;
        os << "DELETE FROM t_favorite WHERE f_ctype = " << conversation_group << " AND f_cid = " << groupid
           << " AND f_userid IN (" << idList << ")";
        statements.push_back(os.str());
    }
    {
        std::ostringstream os;
        os << "DELETE FROM t_conversation WHERE f_ctype = " << conversation_group << " AND f_cid = " << groupid
           << " AND f_userid IN (" << idList << ")";
        statements.push_back(os.str());
    }
    //关注名单（docs/消息提醒模式与消息盒子方案.md §12.1）：当事人自己在本群的名单作废；别人名单里的当事人
    //也一并删掉，否则这些名额一直被不在群里的人占着，当事人日后重新入群时关注还会意外恢复
    {
        std::ostringstream os;
        os << "DELETE FROM t_group_follow WHERE f_groupid = " << groupid
           << " AND (f_userid IN (" << idList << ") OR f_target_userid IN (" << idList << "))";
        statements.push_back(os.str());
    }
    {
        std::ostringstream os;
        os << "DELETE FROM t_group_member WHERE f_groupid = " << groupid << " AND f_userid IN (" << idList << ")";
        statements.push_back(os.str());
    }

    if (!executeInTransaction(pConn.get(), statements, "GroupManager::removeMembers", groupid))
        return false;

    LOGI("GroupManager::removeMembers ok, groupid: %lld, users: %s", (long long)groupid, idList.c_str());
    return true;
}

int64_t GroupManager::addJoinRequest(int64_t groupid, int32_t userid,
                                     const std::string& verifymsg, int64_t createTime,
                                     int source, int64_t recommender, int historyLimit, int64_t inviteId)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::addJoinRequest db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    //验证留言为用户输入，须经 escapeString 转义
    std::vector<char> escMsg(verifymsg.size() * 2 + 1, '\0');
    pConn->escapeString(&escMsg[0], verifymsg.c_str(), static_cast<uint32_t>(verifymsg.size()));

    std::ostringstream osSql;
    osSql << "INSERT INTO t_group_join_request(f_groupid, f_userid, f_verify_msg, f_status, "
             "f_create_time, f_source, f_recommender, f_history_limit, f_invite_id) VALUES("
          << groupid << ", " << userid << ", '" << &escMsg[0] << "', 0, " << createTime
          << ", " << source << ", " << recommender << ", " << historyLimit << ", " << inviteId << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::addJoinRequest insert error, groupid: %lld, userid: %d",
             (long long)groupid, userid);
        return 0;
    }

    //取回自增主键 f_id 作为申请ID（须与 INSERT 同一连接）
    int64_t requestId = 0;
    QueryResult* pResult = pConn->query("SELECT LAST_INSERT_ID()");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            requestId = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }
    return requestId;
}

//加群申请查询的 SELECT 列清单。getJoinRequestById 与 getPendingJoinRequests 两处
//查询的列必须逐字一致（都由 fillJoinRequestRecord 按下标取值），故抽成一个常量。
static const char* const kJoinRequestSelectColumns =
    "f_id, f_groupid, f_userid, f_verify_msg, f_status, f_create_time, "
    "f_handle_time, f_handler, f_source, f_recommender, f_history_limit, f_invite_id";

//按 kJoinRequestSelectColumns 的列序填一条加群申请记录
static void fillJoinRequestRecord(Field* pRow, GroupJoinRequestRecord& rec)
{
    rec.id          = static_cast<int64_t>(pRow[0].getUInt64());
    rec.groupid     = static_cast<int64_t>(pRow[1].getUInt64());
    rec.userid      = static_cast<int64_t>(pRow[2].getUInt64());
    rec.verifymsg   = pRow[3].getCppString();
    rec.status      = pRow[4].getInt32();
    rec.createtime  = static_cast<int64_t>(pRow[5].getUInt64());
    rec.handletime  = static_cast<int64_t>(pRow[6].getUInt64());
    rec.handler     = static_cast<int64_t>(pRow[7].getUInt64());
    rec.source      = pRow[8].getInt32();
    rec.recommender = static_cast<int64_t>(pRow[9].getUInt64());
    rec.historylimit = pRow[10].getInt32();
    rec.inviteid    = static_cast<int64_t>(pRow[11].getUInt64());
}

bool GroupManager::getJoinRequestById(int64_t requestid, GroupJoinRequestRecord& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getJoinRequestById db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT " << kJoinRequestSelectColumns
          << " FROM t_group_join_request WHERE f_id = " << requestid;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    bool found = false;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
    {
        fillJoinRequestRecord(pRow, out);
        found = true;
    }
    pResult->endQuery();
    delete pResult;
    return found;
}

bool GroupManager::updateJoinRequestStatus(int64_t requestid, int status,
                                           int64_t handleTime, int32_t handler, bool& changed)
{
    changed = false;
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::updateJoinRequestStatus db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    //只改仍是待处理的申请：已被别的管理员处理过时改到 0 行，本次处理无效
    const int kStatusPending = 0;
    osSql << "UPDATE t_group_join_request SET f_status = " << status
          << ", f_handle_time = " << handleTime << ", f_handler = " << handler
          << " WHERE f_id = " << requestid << " AND f_status = " << kStatusPending;
    uint32_t affected = 0;
    int dbErrno = 0;
    if (!pConn->execute(osSql.str().c_str(), affected, dbErrno))
    {
        LOGE("GroupManager::updateJoinRequestStatus error, requestid: %lld, errno: %d", (long long)requestid, dbErrno);
        return false;
    }
    changed = (affected > 0);
    return true;
}

bool GroupManager::getPendingJoinRequests(int64_t groupid, std::vector<GroupJoinRequestRecord>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getPendingJoinRequests db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT " << kJoinRequestSelectColumns
          << " FROM t_group_join_request WHERE f_groupid = " << groupid
          << " AND f_status = 0 ORDER BY f_create_time DESC";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        GroupJoinRequestRecord rec;
        fillJoinRequestRecord(pRow, rec);
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupManager::updateMemberRole(int64_t groupid, int32_t userid, int role)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::updateMemberRole db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_group_member SET f_role = " << role
          << " WHERE f_groupid = " << groupid << " AND f_userid = " << userid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::updateMemberRole error, groupid: %lld, userid: %d",
             (long long)groupid, userid);
        return false;
    }
    return true;
}

bool GroupManager::updateMemberMuted(int64_t groupid, int32_t userid, int muted,
                                     int64_t muteUntil, const std::string& reason)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::updateMemberMuted db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //解禁时一并清掉到期时间与原因，避免下次禁言读到上一轮的残留值
    std::ostringstream osSql;
    osSql << "UPDATE t_group_member SET f_muted = " << muted;
    if (muted == 0)
    {
        osSql << ", f_mute_until = 0, f_mute_reason = ''";
    }
    else
    {
        osSql << ", f_mute_until = " << muteUntil
              << ", " << makeTextSet(pConn.get(), "f_mute_reason", reason);
    }
    osSql << " WHERE f_groupid = " << groupid << " AND f_userid = " << userid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::updateMemberMuted error, groupid: %lld, userid: %d",
             (long long)groupid, userid);
        return false;
    }

    LOGI("member mute updated, groupid: %lld, userid: %d, muted: %d, until: %lld, reason: %s",
         (long long)groupid, userid, muted, (long long)muteUntil, reason.c_str());
    return true;
}

bool GroupManager::updateMemberCard(int64_t groupid, int32_t userid, const std::string& card)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::updateMemberCard db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //群名片为用户输入，须经 escapeString 转义
    std::vector<char> escCard(card.size() * 2 + 1, '\0');
    pConn->escapeString(&escCard[0], card.c_str(), static_cast<uint32_t>(card.size()));

    std::ostringstream osSql;
    osSql << "UPDATE t_group_member SET f_card = '" << &escCard[0] << "'"
          << " WHERE f_groupid = " << groupid << " AND f_userid = " << userid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::updateMemberCard error, groupid: %lld, userid: %d",
             (long long)groupid, userid);
        return false;
    }
    return true;
}

bool GroupManager::transferOwner(int64_t groupid, int32_t oldOwner, int32_t newOwner)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::transferOwner db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //三条更新必须一起成败：任何一条失败而不回滚，就会出现"群主指向新群主、新群主的角色却还是
    //普通成员"或"两个人都是群主角色"，之后的权限判断全乱
    std::vector<std::string> statements;
    {
        //1) t_group 的群主指针指向新群主
        std::ostringstream os;
        os << "UPDATE t_group SET f_ownerid = " << newOwner << " WHERE f_groupid = " << groupid;
        statements.push_back(os.str());
    }
    {
        //2) 新群主成员行角色置为群主
        std::ostringstream os;
        os << "UPDATE t_group_member SET f_role = " << group_role_owner
           << " WHERE f_groupid = " << groupid << " AND f_userid = " << newOwner;
        statements.push_back(os.str());
    }
    {
        //3) 原群主成员行角色降为普通成员
        std::ostringstream os;
        os << "UPDATE t_group_member SET f_role = " << group_role_member
           << " WHERE f_groupid = " << groupid << " AND f_userid = " << oldOwner;
        statements.push_back(os.str());
    }

    if (!executeInTransaction(pConn.get(), statements, "GroupManager::transferOwner", groupid))
        return false;

    LOGI("GroupManager::transferOwner ok, groupid: %lld, from: %d, to: %d",
         (long long)groupid, oldOwner, newOwner);
    return true;
}

// ====================================================================
// 自动群名
// ====================================================================

bool GroupManager::refreshAutoGroupName(int64_t groupid, std::string& newName)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::refreshAutoGroupName db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //1) 读群主、当前群名与自定义标记；用户设定了群名的群不做改动
    std::ostringstream osGroup;
    osGroup << "SELECT f_ownerid, f_name, f_name_custom FROM t_group WHERE f_groupid = " << groupid;
    QueryResult* pGroup = pConn->query(osGroup.str().c_str());
    if (pGroup == NULL)
        return false;

    Field* pGroupRow = pGroup->fetch();
    if (pGroupRow == NULL)
    {
        pGroup->endQuery();
        delete pGroup;
        return false;
    }
    const int64_t     ownerid    = static_cast<int64_t>(pGroupRow[0].getUInt64());
    const std::string oldName    = pGroupRow[1].getCppString();
    const int         nameCustom = pGroupRow[2].getInt32();
    pGroup->endQuery();
    delete pGroup;
    if (nameCustom != 0)
        return false;

    //2) 读全部成员的 userid、昵称与入群时间。用 LEFT JOIN 是为了在用户行缺失时成员
    //   仍被读出（昵称为空串，生成群名时被跳过），而不是整行消失
    std::ostringstream osMembers;
    osMembers << "SELECT m.f_userid, u.f_nickname, m.f_join_time FROM t_group_member m "
                 "LEFT JOIN t_user u ON u.f_user_id = m.f_userid WHERE m.f_groupid = " << groupid;
    QueryResult* pMembers = pConn->query(osMembers.str().c_str());
    //群里至少有群主一行，读不到任何成员只可能是数据库出错，此时不改群名
    if (pMembers == NULL)
        return false;

    std::vector<chatserver::GroupNameMember> members;
    while (true)
    {
        Field* pRow = pMembers->fetch();
        if (pRow == NULL)
            break;

        chatserver::GroupNameMember m;
        m.userid   = static_cast<int64_t>(pRow[0].getUInt64());
        m.nickname = pRow[1].getCppString();
        m.jointime = static_cast<int64_t>(pRow[2].getUInt64());
        members.push_back(m);

        if (!pMembers->nextRow())
            break;
    }
    pMembers->endQuery();
    delete pMembers;

    //3) 与当前群名相同就不写库，调用方也就不必推送
    const std::string name = chatserver::BuildAutoGroupName(ownerid, members);
    if (name == oldName)
        return false;

    //4) 写库。条件里再判一次 f_name_custom，避免读与写之间群主刚好设定了群名时被覆盖
    std::ostringstream osUpdate;
    osUpdate << "UPDATE t_group SET " << makeTextSet(pConn.get(), "f_name", name)
             << " WHERE f_groupid = " << groupid << " AND f_name_custom = 0";
    if (!pConn->execute(osUpdate.str().c_str()))
    {
        LOGE("GroupManager::refreshAutoGroupName update error, groupid: %lld", (long long)groupid);
        return false;
    }

    LOGI("auto group name refreshed, groupid: %lld, members: %d, name: %s",
         (long long)groupid, (int)members.size(), name.c_str());
    newName = name;
    return true;
}

bool GroupManager::refreshAutoGroupNamesOfMember(int32_t userid,
                                                 std::vector<std::pair<int64_t, std::string> >& changes)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::refreshAutoGroupNamesOfMember db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //该成员所在的全部自动命名群
    std::ostringstream osSql;
    osSql << "SELECT g.f_groupid FROM t_group g "
             "INNER JOIN t_group_member m ON m.f_groupid = g.f_groupid "
             "WHERE m.f_userid = " << userid << " AND g.f_name_custom = 0";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    //结果集为空时 query 也返回 NULL：该成员不在任何自动命名群里，无事可做
    if (pResult == NULL)
        return true;

    std::vector<int64_t> groupids;
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        groupids.push_back(static_cast<int64_t>(pRow[0].getUInt64()));

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;

    //逐个群重算；某成员是群主时他的昵称不在群名里，重算结果不变，不会产生推送
    int renamed = 0;
    for (size_t i = 0; i < groupids.size(); ++i)
    {
        std::string name;
        if (refreshAutoGroupName(groupids[i], name))
        {
            changes.push_back(std::make_pair(groupids[i], name));
            ++renamed;
        }
    }

    LOGI("auto group names refreshed for member, userid: %d, auto groups: %d, renamed: %d",
         userid, (int)groupids.size(), renamed);
    return true;
}

// ====================================================================
// 群公告
// ====================================================================

//单页最多返回的公告条数。调用方给的 count 越界时按它兜底，防止一次把整张表
//拖回来 —— 群公告是低频写、按条浏览的东西，一页几十条足够。
static const int kAnnouncementPageMax = 100;

//公告查询的 SELECT 列清单。getAnnouncements 与 getAnnouncement 两处的列必须
//逐字一致（都由 fillAnnouncementRecord 按下标取值），故抽成一个常量。
static const char* const kAnnouncementSelectColumns =
    "f_id, f_groupid, f_title, f_content, f_severity, "
    "f_publisher, f_publish_time";

//按 kAnnouncementSelectColumns 的列序填一条公告记录。publishername 不在库里，
//由会话层从 UserManager 补。
static void fillAnnouncementRecord(Field* pRow, GroupAnnouncementRecord& a)
{
    a.id          = static_cast<int64_t>(pRow[0].getUInt64());
    a.groupid     = static_cast<int64_t>(pRow[1].getUInt64());
    a.title       = pRow[2].getCppString();
    a.content     = pRow[3].getCppString();
    a.severity    = pRow[4].getInt32();
    a.publisher   = static_cast<int64_t>(pRow[5].getUInt64());
    a.publishtime = static_cast<int64_t>(pRow[6].getUInt64());
}

bool GroupManager::getAnnouncements(int64_t groupid, int offset, int count,
                                    std::vector<GroupAnnouncementRecord>& out,
                                    int* totalOut)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getAnnouncements db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //越界的分页参数就地夹住，不把非法值拼进 SQL
    if (offset < 0)
        offset = 0;
    if (count <= 0 || count > kAnnouncementPageMax)
        count = kAnnouncementPageMax;

    //列表与计数必须用同一个 WHERE，否则 total 与实际条数对不上、界面上的
    //"第 x/y 页"就会算错
    std::ostringstream osWhere;
    osWhere << " WHERE f_groupid = " << groupid;

    if (totalOut != NULL)
    {
        *totalOut = 0;
        std::ostringstream osCount;
        osCount << "SELECT COUNT(*) FROM t_group_announcement" << osWhere.str();
        QueryResult* pCount = pConn->query(osCount.str().c_str());
        if (pCount != NULL)
        {
            Field* pRow = pCount->fetch();
            if (pRow != NULL)
                *totalOut = pRow[0].getInt32();
            pCount->endQuery();
            delete pCount;
        }
    }

    //倒序是刻意的：两处界面都是"最新的排最前"。次序键补上 f_id，避免同一毫秒
    //连发两条时次序不定（客户端翻页会看到重复或漏条）。
    std::ostringstream osSql;
    osSql << "SELECT " << kAnnouncementSelectColumns << " FROM t_group_announcement"
          << osWhere.str()
          << " ORDER BY f_publish_time DESC, f_id DESC LIMIT " << offset << ", " << count;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        GroupAnnouncementRecord rec;
        fillAnnouncementRecord(pRow, rec);
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupManager::searchAnnouncements(const std::vector<int64_t>& groupids, const std::string& keyword,
                                       int limit, std::vector<GroupAnnouncementRecord>& out)
{
    out.clear();
    if (groupids.empty())
        return true;
    if (keyword.empty() || limit <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::searchAnnouncements db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //标题与正文是纯文本列（不是 JSON），直接 LIKE 即可，中文能搜到。先按 LIKE 规则转义 ! % _，
    //再按 SQL 字符串字面量规则转义引号等，顺序不能颠倒
    const std::string likeEsc = dbapi::EscapeString(chatserver::MakeLikeContainsPattern(keyword));
    std::ostringstream osSql;
    osSql << "SELECT " << kAnnouncementSelectColumns << " FROM t_group_announcement WHERE f_groupid IN (";
    for (size_t i = 0; i < groupids.size(); ++i)
        osSql << (i > 0 ? ", " : "") << groupids[i];
    osSql << ") AND (f_title LIKE '" << likeEsc << "' ESCAPE '!' OR f_content LIKE '" << likeEsc << "' ESCAPE '!')"
          << " ORDER BY f_publish_time DESC, f_id DESC LIMIT " << limit;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        LOGE("GroupManager::searchAnnouncements query error");
        return false;
    }
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        GroupAnnouncementRecord rec;
        fillAnnouncementRecord(pRow, rec);
        out.push_back(rec);
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupManager::getAnnouncement(int64_t id, GroupAnnouncementRecord& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getAnnouncement db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT " << kAnnouncementSelectColumns
          << " FROM t_group_announcement WHERE f_id = " << id;
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    Field* pRow = pResult->fetch();
    bool found = (pRow != NULL);
    if (found)
        fillAnnouncementRecord(pRow, out);

    pResult->endQuery();
    delete pResult;
    return found;
}

int64_t GroupManager::publishAnnouncement(int64_t groupid, const std::string& title,
                                          const std::string& content, int severity,
                                          int64_t publisher, int64_t publishTime)
{
    //标题是必填项。会话层已经挡过一道，这里再挡一次 —— 数据层不该产出
    //一条点开是空白的公告。
    if (title.empty())
        return 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::publishAnnouncement db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    std::vector<char> escTitle(title.size() * 2 + 1, '\0');
    pConn->escapeString(&escTitle[0], title.c_str(), static_cast<uint32_t>(title.size()));

    std::vector<char> escContent(content.size() * 2 + 1, '\0');
    pConn->escapeString(&escContent[0], content.c_str(), static_cast<uint32_t>(content.size()));

    std::ostringstream osSql;
    osSql << "INSERT INTO t_group_announcement(f_groupid, f_title, f_content, f_severity, "
             "f_publisher, f_publish_time) VALUES("
          << groupid << ", '" << &escTitle[0] << "', '" << &escContent[0] << "', "
          << severity << ", " << publisher << ", " << publishTime << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::publishAnnouncement insert error, groupid: %lld", (long long)groupid);
        return 0;
    }

    int64_t announceId = 0;
    QueryResult* pResult = pConn->query("SELECT LAST_INSERT_ID()");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            announceId = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }

    //刚插进去的这条按定义就是最新的，快照跟着走一遍
    refreshLatestAnnouncement(groupid);

    LOGI("group announcement published, groupid: %lld, id: %lld, publisher: %lld",
         (long long)groupid, (long long)announceId, (long long)publisher);
    return announceId;
}

bool GroupManager::deleteAnnouncement(int64_t groupid, int64_t id)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::deleteAnnouncement db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //条件里带上 f_groupid，防止拿着别的群的公告ID越权删除
    std::ostringstream osSql;
    osSql << "DELETE FROM t_group_announcement WHERE f_id = " << id
          << " AND f_groupid = " << groupid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::deleteAnnouncement error, groupid: %lld, id: %lld",
             (long long)groupid, (long long)id);
        return false;
    }

    //删掉的可能正是快照指着的那条，无条件重算一次 —— 判断"是不是最新那条"要多读
    //一次 t_group，而重算本身也就一条查询加一条更新，不如直接算。
    refreshLatestAnnouncement(groupid);

    LOGI("group announcement deleted, groupid: %lld, id: %lld", (long long)groupid, (long long)id);
    return true;
}

bool GroupManager::refreshLatestAnnouncement(int64_t groupid)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::refreshLatestAnnouncement db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //取该群当前最新的一条公告；次序键与 getAnnouncements 保持一致
    std::ostringstream osSel;
    osSel << "SELECT f_id, f_title, f_content, f_publish_time FROM t_group_announcement"
             " WHERE f_groupid = " << groupid
          << " ORDER BY f_publish_time DESC, f_id DESC LIMIT 1";

    int64_t     latestId = 0;
    std::string latestTitle;
    std::string latestContent;
    int64_t     latestTime = 0;

    QueryResult* pResult = pConn->query(osSel.str().c_str());
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            latestId      = static_cast<int64_t>(pRow[0].getUInt64());
            latestTitle   = pRow[1].getCppString();
            latestContent = pRow[2].getCppString();
            latestTime    = static_cast<int64_t>(pRow[3].getUInt64());
        }
        pResult->endQuery();
        delete pResult;
    }

    //一条公告都没有（全删了，或本来就没发过）时，四列一起清空 —— 界面上随之回到
    //"暂无群公告"，而不是留着一条已经删掉的公告在那儿显示。
    std::ostringstream osSql;
    osSql << "UPDATE t_group SET f_announce_id = " << latestId
          << ", " << makeTextSet(pConn.get(), "f_announce_title", latestTitle)
          << ", f_announce_time = " << latestTime
          << ", " << makeTextSet(pConn.get(), "f_announcement", latestContent)
          << " WHERE f_groupid = " << groupid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::refreshLatestAnnouncement error, groupid: %lld", (long long)groupid);
        return false;
    }
    return true;
}

//读出本群全部待确认入群必读的成员
static bool queryMustReadPending(IDatabase* pConn, int64_t groupid, std::vector<int64_t>& out)
{
    out.clear();
    std::ostringstream osSql;
    osSql << "SELECT f_userid FROM t_group_member WHERE f_groupid = " << groupid << " AND f_must_read = 1";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return true;   //结果集为空时 dbapi 同样返回 NULL，按没有待确认的人处理
    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;
        out.push_back(static_cast<int64_t>(pRow[0].getUInt64()));
        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool GroupManager::setMustRead(int64_t groupid, int64_t announcementId, std::vector<int64_t>& cleared)
{
    cleared.clear();
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::setMustRead db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::vector<std::string> statements;
    {
        std::ostringstream os;
        os << "UPDATE t_group SET f_must_read_id = " << announcementId << " WHERE f_groupid = " << groupid;
        statements.push_back(os.str());
    }
    //取消必读：所有待确认的人一并解除限制。换成另一条必读公告时不动 —— 已确认的人不必重新确认，
    //还没确认的人改为确认新的这一条（docs/群公告接入方案.md §14.1）
    if (announcementId == 0)
    {
        if (!queryMustReadPending(pConn.get(), groupid, cleared))
            return false;
        std::ostringstream os;
        os << "UPDATE t_group_member SET f_must_read = 0 WHERE f_groupid = " << groupid << " AND f_must_read = 1";
        statements.push_back(os.str());
    }
    if (!executeInTransaction(pConn.get(), statements, "GroupManager::setMustRead", groupid))
    {
        cleared.clear();
        return false;
    }
    LOGI("GroupManager::setMustRead ok, groupid: %lld, announcement: %lld, cleared: %u",
         (long long)groupid, (long long)announcementId, static_cast<unsigned>(cleared.size()));
    return true;
}

bool GroupManager::markMustReadPending(int64_t groupid, const std::vector<int64_t>& userids)
{
    if (userids.empty())
        return true;
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::markMustReadPending db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }
    std::ostringstream osSql;
    osSql << "UPDATE t_group_member SET f_must_read = 1 WHERE f_groupid = " << groupid
          << " AND f_userid IN (" << joinUserIds(userids) << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::markMustReadPending error, groupid: %lld", (long long)groupid);
        return false;
    }
    return true;
}

bool GroupManager::clearMustReadPending(int64_t groupid, const std::vector<int64_t>& userids,
                                        std::vector<int64_t>& cleared)
{
    cleared.clear();
    if (userids.empty())
        return true;
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::clearMustReadPending db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //先找出其中确实待确认的人，再清零；不是待确认的人不算进 cleared，调用方不必为他们推送
    std::vector<int64_t> pending;
    if (!queryMustReadPending(pConn.get(), groupid, pending))
        return false;
    std::set<int64_t> wanted(userids.begin(), userids.end());
    for (size_t i = 0; i < pending.size(); ++i)
    {
        if (wanted.count(pending[i]) != 0)
            cleared.push_back(pending[i]);
    }
    if (cleared.empty())
        return true;

    std::ostringstream osSql;
    osSql << "UPDATE t_group_member SET f_must_read = 0 WHERE f_groupid = " << groupid
          << " AND f_userid IN (" << joinUserIds(cleared) << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::clearMustReadPending error, groupid: %lld", (long long)groupid);
        cleared.clear();
        return false;
    }
    return true;
}
