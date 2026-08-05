/**
 *  群组存储管理器实现，GroupManager.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "GroupManager.h"
#include <memory>
#include <sstream>
#include <vector>
#include <time.h>          //time()，禁言到期判定取当前时刻用
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "UserManager.h"   //仅为复用群ID下界宏 GROUPID_BOUBDARY

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

    //群ID分配器 seed 到当前最大群ID；t_group 为空时退回到 GROUPID_BOUBDARY
    int64_t base = GROUPID_BOUBDARY;
    QueryResult* pResult = pConn->query("SELECT MAX(f_groupid) FROM t_group");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
        {
            int64_t maxId = static_cast<int64_t>(pRow[0].getUInt64());
            if (maxId > base)
                base = maxId;
        }
        pResult->endQuery();
        delete pResult;
    }
    m_baseGroupId = base;
    LOGI("GroupManager::init ok, group id allocator seeded at %lld", (long long)base);
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
    "g.f_welcome_on, g.f_welcome_text, g.f_sensitive_on, "
    "g.f_announce_id, g.f_announce_title, g.f_announce_time";

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
    g.sensitiveon   = pRow[15].getInt32();
    g.announceid    = static_cast<int64_t>(pRow[16].getUInt64());
    g.announcetitle = pRow[17].getCppString();
    g.announcetime  = static_cast<int64_t>(pRow[18].getUInt64());
    g.membercount   = pRow[19].getInt32();
}

int64_t GroupManager::createGroup(const std::string& name, int32_t ownerid,
                                  const std::vector<int64_t>& memberids, int64_t createTime)
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

    //1) 写 t_group（facetype/customface/announcement/join_mode/mute_all 取列默认值）
    std::ostringstream osGroup;
    osGroup << "INSERT INTO t_group(f_groupid, f_name, f_ownerid, f_create_time) VALUES("
            << groupid << ", '" << &escName[0] << "', " << ownerid << ", " << createTime << ")";
    if (!pConn->execute(osGroup.str().c_str()))
    {
        LOGE("GroupManager::createGroup insert t_group error, name: %s", name.c_str());
        return 0;
    }

    //2) 写群主成员行
    std::ostringstream osOwner;
    osOwner << "INSERT INTO t_group_member(f_groupid, f_userid, f_role, f_join_time) VALUES("
            << groupid << ", " << ownerid << ", " << group_role_owner << ", " << createTime << ")";
    pConn->execute(osOwner.str().c_str());

    //3) 写初始成员行；用 INSERT IGNORE 防御重复 userid，并跳过与群主相同的项
    for (size_t i = 0; i < memberids.size(); ++i)
    {
        if (memberids[i] == ownerid)
            continue;

        std::ostringstream osMember;
        osMember << "INSERT IGNORE INTO t_group_member(f_groupid, f_userid, f_role, f_join_time) VALUES("
                 << groupid << ", " << memberids[i] << ", " << group_role_member << ", "
                 << createTime << ")";
        pConn->execute(osMember.str().c_str());
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

    //1) 写 t_group（facetype/customface/announcement/join_mode/mute_all 取列默认值）
    std::ostringstream osGroup;
    osGroup << "INSERT INTO t_group(f_groupid, f_name, f_ownerid, f_create_time) VALUES("
            << groupid << ", '" << &escName[0] << "', " << ownerid << ", " << createTime << ")";
    if (!pConn->execute(osGroup.str().c_str()))
    {
        LOGE("GroupManager::createGroup(admin) insert t_group error, name: %s", name.c_str());
        return 0;
    }

    //2) 写群主成员行（角色为群主）
    std::ostringstream osOwner;
    osOwner << "INSERT INTO t_group_member(f_groupid, f_userid, f_role, f_join_time) VALUES("
            << groupid << ", " << ownerid << ", " << group_role_owner << ", " << createTime << ")";
    pConn->execute(osOwner.str().c_str());

    //3) 写管理员成员行（角色为管理员）；INSERT IGNORE 防御重复 userid，并跳过与群主相同的项
    for (size_t i = 0; i < adminids.size(); ++i)
    {
        if (adminids[i] == ownerid)
            continue;

        std::ostringstream osAdmin;
        osAdmin << "INSERT IGNORE INTO t_group_member(f_groupid, f_userid, f_role, f_join_time) VALUES("
                << groupid << ", " << adminids[i] << ", " << group_role_admin << ", "
                << createTime << ")";
        pConn->execute(osAdmin.str().c_str());
    }

    //4) 写普通成员行（角色为普通成员）；INSERT IGNORE 防御重复 userid，并跳过与群主相同的项
    for (size_t i = 0; i < memberids.size(); ++i)
    {
        if (memberids[i] == ownerid)
            continue;

        std::ostringstream osMember;
        osMember << "INSERT IGNORE INTO t_group_member(f_groupid, f_userid, f_role, f_join_time) VALUES("
                 << groupid << ", " << memberids[i] << ", " << group_role_member << ", "
                 << createTime << ")";
        pConn->execute(osMember.str().c_str());
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

    //先删全部成员行，再删群行
    std::ostringstream osMembers;
    osMembers << "DELETE FROM t_group_member WHERE f_groupid = " << groupid;
    if (!pConn->execute(osMembers.str().c_str()))
    {
        LOGE("GroupManager::dismissGroup delete members error, groupid: %lld", (long long)groupid);
        return false;
    }

    std::ostringstream osGroup;
    osGroup << "DELETE FROM t_group WHERE f_groupid = " << groupid;
    if (!pConn->execute(osGroup.str().c_str()))
    {
        LOGE("GroupManager::dismissGroup delete group error, groupid: %lld", (long long)groupid);
        return false;
    }
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
    osSql << "SELECT f_userid, f_role, f_card, f_muted, f_join_time, f_mute_until, f_mute_reason "
             "FROM t_group_member WHERE f_groupid = " << groupid << " ORDER BY f_join_time ASC";
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
        m.userid     = static_cast<int64_t>(pRow[0].getUInt64());
        m.role       = pRow[1].getInt32();
        m.card       = pRow[2].getCppString();
        m.muted      = pRow[3].getInt32();
        m.jointime   = static_cast<int64_t>(pRow[4].getUInt64());
        m.muteuntil  = static_cast<int64_t>(pRow[5].getUInt64());
        m.mutereason = pRow[6].getCppString();

        //定时禁言到期后不写库、只在读到时按未禁言呈现（惰性判定，见
        //docs/群管理接入方案.md §3.2）。真正把库里的 f_muted 抹掉发生在
        //发言校验那条路径上 —— 那里本来就要写库，顺手清理不额外增加开销。
        if (GroupProtocol::isMuteExpired(m.muted, m.muteuntil, now))
        {
            m.muted      = 0;
            m.muteuntil  = 0;
            m.mutereason.clear();
        }
        out.push_back(m);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
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
                                         int64_t* muteUntil)
{
    role  = group_role_member;
    muted = 0;
    if (muteUntil != NULL)
        *muteUntil = 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
        return false;

    std::ostringstream osSql;
    osSql << "SELECT f_role, f_muted, f_mute_until FROM t_group_member WHERE f_groupid = " << groupid
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
    if (req.has_name)
        sets.push_back(makeTextSet(pConn.get(), "f_name", req.name));
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

    //---- 以下七项为群管理功能新增的群设置 ----
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
    if (req.has_sensitiveon)
        sets.push_back("f_sensitive_on = " + std::to_string(req.sensitiveon));

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
    return true;
}

bool GroupManager::addMembers(int64_t groupid, const std::vector<int64_t>& userids, int64_t joinTime)
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

    //逐个 INSERT IGNORE：已是成员的 userid 静默跳过，保证幂等
    for (size_t i = 0; i < userids.size(); ++i)
    {
        std::ostringstream osSql;
        osSql << "INSERT IGNORE INTO t_group_member(f_groupid, f_userid, f_role, f_join_time) VALUES("
              << groupid << ", " << userids[i] << ", " << group_role_member << ", " << joinTime << ")";
        if (!pConn->execute(osSql.str().c_str()))
        {
            LOGE("GroupManager::addMembers insert error, groupid: %lld, userid: %lld",
                 (long long)groupid, (long long)userids[i]);
            return false;
        }
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

    std::ostringstream osSql;
    osSql << "DELETE FROM t_group_member WHERE f_groupid = " << groupid << " AND f_userid IN (";
    for (size_t i = 0; i < userids.size(); ++i)
    {
        if (i > 0)
            osSql << ",";
        osSql << userids[i];
    }
    osSql << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::removeMembers error, groupid: %lld", (long long)groupid);
        return false;
    }
    return true;
}

int64_t GroupManager::addJoinRequest(int64_t groupid, int32_t userid,
                                     const std::string& verifymsg, int64_t createTime,
                                     int source, int64_t recommender)
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
             "f_create_time, f_source, f_recommender) VALUES("
          << groupid << ", " << userid << ", '" << &escMsg[0] << "', 0, " << createTime
          << ", " << source << ", " << recommender << ")";
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
    "f_handle_time, f_handler, f_source, f_recommender";

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
                                           int64_t handleTime, int32_t handler)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::updateJoinRequestStatus db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "UPDATE t_group_join_request SET f_status = " << status
          << ", f_handle_time = " << handleTime << ", f_handler = " << handler
          << " WHERE f_id = " << requestid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::updateJoinRequestStatus error, requestid: %lld", (long long)requestid);
        return false;
    }
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

    //1) t_group 的群主指针指向新群主
    std::ostringstream osGroup;
    osGroup << "UPDATE t_group SET f_ownerid = " << newOwner << " WHERE f_groupid = " << groupid;
    if (!pConn->execute(osGroup.str().c_str()))
    {
        LOGE("GroupManager::transferOwner update t_group error, groupid: %lld", (long long)groupid);
        return false;
    }

    //2) 新群主成员行角色置为群主
    std::ostringstream osNew;
    osNew << "UPDATE t_group_member SET f_role = " << group_role_owner
          << " WHERE f_groupid = " << groupid << " AND f_userid = " << newOwner;
    pConn->execute(osNew.str().c_str());

    //3) 原群主成员行角色降为普通成员
    std::ostringstream osOld;
    osOld << "UPDATE t_group_member SET f_role = " << group_role_member
          << " WHERE f_groupid = " << groupid << " AND f_userid = " << oldOwner;
    pConn->execute(osOld.str().c_str());

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

// ====================================================================
// 群敏感词库
// ====================================================================

bool GroupManager::getSensitiveWords(int64_t groupid, std::vector<GroupSensitiveWordRecord>& out)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::getSensitiveWords db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    std::ostringstream osSql;
    osSql << "SELECT f_id, f_word, f_create_time FROM t_group_sensitive_word "
             "WHERE f_groupid = " << groupid << " ORDER BY f_create_time ASC";
    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
        return false;

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        GroupSensitiveWordRecord rec;
        rec.id         = static_cast<int64_t>(pRow[0].getUInt64());
        rec.word       = pRow[1].getCppString();
        rec.createtime = static_cast<int64_t>(pRow[2].getUInt64());
        out.push_back(rec);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

int64_t GroupManager::addSensitiveWord(int64_t groupid, const std::string& word, int64_t createTime)
{
    if (word.empty())
        return 0;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::addSensitiveWord db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return 0;
    }

    //同群下已有同一个词就不再插一遍。这里用"先查后插"而不是唯一索引，是因为
    //(f_groupid, f_word) 上的唯一索引要给 varchar(64) 建索引、代价不小，而本表
    //的写入频率极低（管理员偶尔加个词），多一次查询完全可以接受。
    std::vector<char> escWord(word.size() * 2 + 1, '\0');
    pConn->escapeString(&escWord[0], word.c_str(), static_cast<uint32_t>(word.size()));

    std::ostringstream osExist;
    osExist << "SELECT f_id FROM t_group_sensitive_word WHERE f_groupid = " << groupid
            << " AND f_word = '" << &escWord[0] << "'";
    QueryResult* pExist = pConn->query(osExist.str().c_str());
    if (pExist != NULL)
    {
        bool duplicated = (pExist->fetch() != NULL);
        pExist->endQuery();
        delete pExist;
        if (duplicated)
        {
            LOGI("sensitive word already exists, groupid: %lld, word: %s",
                 (long long)groupid, word.c_str());
            return 0;
        }
    }

    std::ostringstream osSql;
    osSql << "INSERT INTO t_group_sensitive_word(f_groupid, f_word, f_create_time) VALUES("
          << groupid << ", '" << &escWord[0] << "', " << createTime << ")";
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::addSensitiveWord insert error, groupid: %lld", (long long)groupid);
        return 0;
    }

    int64_t wordId = 0;
    QueryResult* pResult = pConn->query("SELECT LAST_INSERT_ID()");
    if (pResult != NULL)
    {
        Field* pRow = pResult->fetch();
        if (pRow != NULL)
            wordId = static_cast<int64_t>(pRow[0].getUInt64());
        pResult->endQuery();
        delete pResult;
    }

    invalidateSensitiveCache(groupid);
    LOGI("sensitive word added, groupid: %lld, id: %lld, word: %s",
         (long long)groupid, (long long)wordId, word.c_str());
    return wordId;
}

bool GroupManager::deleteSensitiveWord(int64_t groupid, int64_t id)
{
    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("GroupManager::deleteSensitiveWord db init failed, db: %s", Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //条件里带上 f_groupid，防止拿着别的群的词条ID越权删除
    std::ostringstream osSql;
    osSql << "DELETE FROM t_group_sensitive_word WHERE f_id = " << id
          << " AND f_groupid = " << groupid;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("GroupManager::deleteSensitiveWord error, groupid: %lld, id: %lld",
             (long long)groupid, (long long)id);
        return false;
    }

    invalidateSensitiveCache(groupid);
    LOGI("sensitive word deleted, groupid: %lld, id: %lld", (long long)groupid, (long long)id);
    return true;
}

//把一段文本整体转小写。敏感词匹配不区分大小写，词与待检文本都先过这一道。
//只折叠 ASCII 字母 —— 中文没有大小写，而对 UTF-8 多字节序列逐字节调 tolower
//是安全的（多字节序列的每个字节都 >= 0x80，不落在 'A'-'Z' 区间内）。
static std::string toLowerAscii(const std::string& s)
{
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
    return out;
}

void GroupManager::ensureSensitiveCacheLoaded(int64_t groupid)
{
    //调用方已持有 m_sensitiveMutex
    if (m_sensitiveCache.find(groupid) != m_sensitiveCache.end())
        return;

    std::vector<GroupSensitiveWordRecord> records;
    getSensitiveWords(groupid, records);

    //即便该群一个词都没有，也要留一个空 vector 占位，否则每条消息都会为
    //"这个群没有敏感词"再查一次库。
    std::vector<std::string>& cached = m_sensitiveCache[groupid];
    cached.clear();
    cached.reserve(records.size());
    for (size_t i = 0; i < records.size(); ++i)
    {
        if (!records[i].word.empty())
            cached.push_back(toLowerAscii(records[i].word));
    }

    LOGI("sensitive word cache loaded, groupid: %lld, count: %d",
         (long long)groupid, static_cast<int>(cached.size()));
}

void GroupManager::invalidateSensitiveCache(int64_t groupid)
{
    std::lock_guard<std::mutex> guard(m_sensitiveMutex);
    m_sensitiveCache.erase(groupid);
}

bool GroupManager::hitSensitiveWord(int64_t groupid, const std::string& text, std::string* hitWord)
{
    if (text.empty())
        return false;

    const std::string lowered = toLowerAscii(text);

    std::lock_guard<std::mutex> guard(m_sensitiveMutex);
    ensureSensitiveCacheLoaded(groupid);

    const std::vector<std::string>& words = m_sensitiveCache[groupid];
    for (size_t i = 0; i < words.size(); ++i)
    {
        if (lowered.find(words[i]) != std::string::npos)
        {
            if (hitWord != NULL)
                *hitWord = words[i];
            return true;
        }
    }
    return false;
}
