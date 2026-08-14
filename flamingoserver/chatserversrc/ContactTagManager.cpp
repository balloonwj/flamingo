/**
 *  联系人标签存储管理器实现，ContactTagManager.cpp
 *  设计与约定见 ContactTagManager.h。
 *  balloonwj@qq.com   2026-08-13
 */
#include "ContactTagManager.h"

#include <memory>
#include <set>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h" //CDbTransaction，整份覆盖标签时保证删与插一起成败
#include "../dbapi/SqlDialect.h"   //EscapeString / UpsertClause
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "Protocol.h"              //error_code

//t_contact_tag 的 UPSERT 冲突列。必须与该表主键的列完全一致，
//SQLite 的 ON CONFLICT 要求写明冲突目标，对不上会直接报错（MySQL 侧不使用该值）。
#define CONTACT_TAG_CONFLICT_COLUMNS   "f_userid, f_target_userid, f_tag"

//被视作标签首尾空白的字符集合。只认这四个 ASCII 空白字符：客户端的标签输入框里
//常见的是用户误敲的空格与从别处粘贴带来的制表符 / 换行，全角空格不在此列，
//它在中文语境下是有意义的可见字符。
static const char* const kTagTrimChars = " \t\r\n";

//去掉一条标签首尾的空白字符；整条都是空白时返回空串。
static std::string trimTag(const std::string& tag)
{
    const std::string::size_type first = tag.find_first_not_of(kTagTrimChars);
    if (first == std::string::npos)
        return std::string();

    const std::string::size_type last = tag.find_last_not_of(kTagTrimChars);
    return tag.substr(first, last - first + 1);
}

//把一条标签里的 ASCII 大写字母折成小写，非 ASCII 字节原样保留，用于去重时的比对。
//只折 ASCII 是刻意的：SQLite 的 COLLATE NOCASE 本身就只处理 ASCII，与它保持一致，
//去重结果才与库的主键判定一致。
static std::string toLowerAscii(const std::string& tag)
{
    std::string result(tag);
    for (std::string::size_type i = 0; i < result.size(); ++i)
    {
        if (result[i] >= 'A' && result[i] <= 'Z')
        {
            result[i] = static_cast<char>(result[i] - 'A' + 'a');
        }
    }
    return result;
}

ContactTagManager::ContactTagManager()
{
}

ContactTagManager::~ContactTagManager()
{
}

void ContactTagManager::normalizeTags(const std::vector<std::string>& in,
                                      std::vector<std::string>& out)
{
    out.clear();

    //已收下的标签（折成小写后的形态），用于不区分大小写去重。用有序集合而不是
    //逐条比对的数组：正常请求只有个位数条标签，两种写法都够快，但畸形客户端一次
    //递上来几万条时，逐条比对是平方级耗时，会把一个工作线程占住。
    std::set<std::string> seenLower;

    for (std::vector<std::string>::const_iterator it = in.begin(); it != in.end(); ++it)
    {
        const std::string trimmed = trimTag(*it);
        if (trimmed.empty())
            continue;

        //insert 的返回值第二项为 false 即表示这条标签之前已经收过（忽略大小写）
        if (!seenLower.insert(toLowerAscii(trimmed)).second)
            continue;

        //存进结果的是<u>原始大小写</u>的那一条，只有比对时才折小写
        out.push_back(trimmed);
    }
}

bool ContactTagManager::getTagsAll(int64_t userid, std::map<int32_t, std::vector<std::string> >& out)
{
    out.clear();

    if (userid <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ContactTagManager::getTagsAll db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //命中主键前缀（也命中 idx_user 索引），一个用户的标签全在连续的几行里。
    //按 (f_target_userid, f_order) 排序，同一个联系人名下的标签就按用户当初提交的
    //顺序连续返回，下面逐行 push_back 即可，不必再排一次。
    std::ostringstream osSql;
    osSql << "SELECT f_target_userid, f_tag FROM t_contact_tag WHERE f_userid = " << userid
          << " ORDER BY f_target_userid ASC, f_order ASC";

    QueryResult* pResult = pConn->query(osSql.str().c_str());
    if (pResult == NULL)
    {
        //查询失败与"一个标签都没贴"是两回事，但 dbapi 在结果集为空时同样返回 NULL，
        //此处无从区分，只能一并按"没有标签"处理并返回 true —— 标签拉不到不该
        //连累整个通讯录应答。真正的连接失败在上面那个分支里已经报了 false。
        return true;
    }

    while (true)
    {
        Field* pRow = pResult->fetch();
        if (pRow == NULL)
            break;

        const int32_t targetUserId = static_cast<int32_t>(pRow[0].getUInt64());
        const std::string tag = pRow[1].getCppString();

        //空串行理论上不存在（清空标签走的是 DELETE，空条目在归一时已被丢掉），
        //万一存量数据里混进来了也跳过，免得客户端渲染出一个空标签块
        if (!tag.empty())
            out[targetUserId].push_back(tag);

        if (!pResult->nextRow())
            break;
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool ContactTagManager::setTags(int64_t userid, int64_t targetUserId,
                                const std::vector<std::string>& tags,
                                int64_t now, int& outErrorCode)
{
    outErrorCode = error_code_ok;

    //取值域兜底。正常流程里会话层已经拦过一道，这里再判一次是防着日后
    //从别的路径（如 admin 控制台）调进来。
    if (userid <= 0 || targetUserId <= 0 || userid == targetUserId)
    {
        outErrorCode = error_code_bad_request;
        return false;
    }

    //先归一、再校验。顺序不能反：用户提交 11 条、其中两条只差大小写时，归一后
    //只剩 10 条，按归一前的条数判就会把一份本该放行的列表拒掉。
    std::vector<std::string> normalized;
    normalizeTags(tags, normalized);

    //逐条判长度。有一条超长就整份拒绝、一行都不写入 —— 留下合格的几条会让客户端
    //以为提交成功，界面上却少了几个标签，比直接报错更难排查。
    for (std::vector<std::string>::const_iterator it = normalized.begin();
         it != normalized.end(); ++it)
    {
        if (static_cast<int>(it->size()) > kMaxContactTagBytes)
        {
            //标签文本是用户自由输入的，可能含姓名、项目代号一类敏感内容，
            //日志里只记条数与字节数，不记内容。
            LOGI("ContactTagManager::setTags rejected by tag length, userid: %lld, target: %lld, bytes: %d, limit: %d",
                 (long long)userid, (long long)targetUserId,
                 static_cast<int>(it->size()), kMaxContactTagBytes);
            outErrorCode = error_code_bad_request;
            return false;
        }
    }

    if (static_cast<int>(normalized.size()) > kMaxContactTagCount)
    {
        LOGI("ContactTagManager::setTags rejected by count limit, userid: %lld, target: %lld, count: %d, limit: %d",
             (long long)userid, (long long)targetUserId,
             static_cast<int>(normalized.size()), kMaxContactTagCount);
        outErrorCode = error_code_tag_limit;
        return false;
    }

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ContactTagManager::setTags db init failed, userid: %lld, db: %s",
             (long long)userid, Singleton<CDbConfig>::Instance().describe().c_str());
        outErrorCode = error_code_server_internal;
        return false;
    }

    //整份覆盖的第一步：删掉该联系人名下现有的全部标签行。
    std::ostringstream osDelete;
    osDelete << "DELETE FROM t_contact_tag WHERE f_userid = " << userid
             << " AND f_target_userid = " << targetUserId;

    if (normalized.empty())
    {
        //清空标签：只有一条 DELETE，语句本身即是原子的，不必开事务。
        //删到 0 行也算成功 —— 用户连点两下"清空标签"不该在第二下收到报错。
        if (!pConn->execute(osDelete.str().c_str()))
        {
            LOGE("ContactTagManager::setTags delete error, userid: %lld, target: %lld",
                 (long long)userid, (long long)targetUserId);
            outErrorCode = error_code_server_internal;
            return false;
        }

        LOGI("clear contact tags ok, userid: %lld, target: %lld",
             (long long)userid, (long long)targetUserId);
        return true;
    }

    //删与插必须一起成败：中途失败若不回滚，用户看到的就是"改标签把原来的也弄丢了"。
    //走 CDbTransaction 守卫而不是自己发裸的事务控制语句，有三处好处：
    //  1. 两种后端开启事务的写法本就不同（MySQL 要用 START TRANSACTION，
    //     SQLite 要用 BEGIN IMMEDIATE 才能让 busy_timeout 生效），由驱动各自决定；
    //  2. 事务进行中连接断开时，连接层会拒绝重连重试并让本次操作整体失败，
    //     不会出现"重连之后剩下的语句各自提交"这种静默破坏原子性的情况；
    //  3. 下面每条错误路径直接 return 即可，守卫析构时自动回滚，不会漏掉。
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("ContactTagManager::setTags begin transaction error, userid: %lld, target: %lld",
             (long long)userid, (long long)targetUserId);
        outErrorCode = error_code_server_internal;
        return false;
    }

    if (!pConn->execute(osDelete.str().c_str()))
    {
        LOGE("ContactTagManager::setTags delete error, userid: %lld, target: %lld",
             (long long)userid, (long long)targetUserId);
        outErrorCode = error_code_server_internal;
        return false;
    }

    //逐条插入，下标即 f_order，于是读回来的顺序就是用户提交时的顺序。
    for (std::vector<std::string>::size_type index = 0; index < normalized.size(); ++index)
    {
        //标签是用户自由输入的文本，可能含单引号 / 反斜杠 / 中文，<u>必须</u>转义后再拼进 SQL。
        //这里用 dbapi::EscapeString 而不是 IDatabase::escapeString，与 ContactRemarkManager
        //保持一致；两者对本场景等价。
        const std::string escTag = dbapi::EscapeString(normalized[index]);

        //正常情况下上面刚把旧行删干净，插入不会撞主键；带 UPSERT 子句是为了兜住一种
        //归一挡不住的情形：MySQL 的 utf8_general_ci 除了大小写还会折叠重音符号，
        //Café 与 CAFÉ 在归一时算两条、递到 MySQL 却撞同一个主键。此时改为更新而非报错，
        //用户至少不会收到一个莫名其妙的服务器内部错误。
        std::ostringstream osInsert;
        osInsert << "INSERT INTO t_contact_tag(f_userid, f_target_userid, f_tag, f_order, f_update_time) VALUES("
                 << userid << ", " << targetUserId << ", '" << escTag << "', "
                 << static_cast<int>(index) << ", " << now << ")"
                 << dbapi::UpsertClause(CONTACT_TAG_CONFLICT_COLUMNS)
                 << "f_order=" << static_cast<int>(index) << ", f_update_time=" << now;

        if (!pConn->execute(osInsert.str().c_str()))
        {
            LOGE("ContactTagManager::setTags insert error, userid: %lld, target: %lld, order: %d",
                 (long long)userid, (long long)targetUserId, static_cast<int>(index));
            outErrorCode = error_code_server_internal;
            return false;
        }
    }

    //提交失败时守卫仍持有该事务，由它的析构补一次回滚，这里不必也不该自己再发 ROLLBACK
    if (!transaction.Commit())
    {
        LOGE("ContactTagManager::setTags commit error, userid: %lld, target: %lld",
             (long long)userid, (long long)targetUserId);
        outErrorCode = error_code_server_internal;
        return false;
    }

    LOGI("set contact tags ok, userid: %lld, target: %lld, count: %d",
         (long long)userid, (long long)targetUserId, static_cast<int>(normalized.size()));
    return true;
}

bool ContactTagManager::removeTagsForAll(int64_t target)
{
    if (target <= 0)
        return false;

    std::unique_ptr<IDatabase> pConn;
    pConn.reset(DbFactory::createConnection());
    if (!pConn)
    {
        LOGE("ContactTagManager::removeTagsForAll db init failed, target: %lld, db: %s",
             (long long)target, Singleton<CDbConfig>::Instance().describe().c_str());
        return false;
    }

    //本语句按 f_target_userid 过滤，用不上以 f_userid 打头的主键与 idx_user 索引，
    //是一次全表扫描。可以接受：它只在账号停用 / 成员移出企业这类罕见时刻执行。
    std::ostringstream osSql;
    osSql << "DELETE FROM t_contact_tag WHERE f_target_userid = " << target;
    if (!pConn->execute(osSql.str().c_str()))
    {
        LOGE("ContactTagManager::removeTagsForAll delete error, target: %lld",
             (long long)target);
        return false;
    }

    LOGI("remove contact tags for all ok, target: %lld", (long long)target);
    return true;
}
