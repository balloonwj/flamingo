/**
 *  企业级敏感词库管理器的实现，SensitiveWordManager.cpp
 *
 *  说明见 SensitiveWordManager.h；方案见 docs/敏感词方案.md。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "SensitiveWordManager.h"

#include <memory>
#include <set>
#include <sstream>

#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

using namespace chatserver;

namespace {

// ---- 分类名称 ----

//分类取值与中文名称的对照，导入导出的文本里写的是名称。顺序即取值顺序。
struct CategoryNameItem
{
    int         m_category;   //分类取值
    const char* m_name;       //中文名称（UTF-8）
};

const CategoryNameItem kCategoryNames[] =
{
    { sensitive_category_illegal, u8"违法违规" },
    { sensitive_category_vulgar,  u8"色情低俗" },
    { sensitive_category_abuse,   u8"攻击辱骂" },
    { sensitive_category_secret,  u8"商业机密" },
    { sensitive_category_ad,      u8"广告引流" },
    { sensitive_category_other,   u8"其他" },
};

const size_t kCategoryCount = sizeof(kCategoryNames) / sizeof(kCategoryNames[0]);

// ---- 文本处理用到的常量 ----

//导入导出文本里列与列之间的分隔符。
const char kColumnSeparator = '\t';

//全角空格 U+3000 的 UTF-8 编码，去首尾空白时与 ASCII 空白一并去掉。
const char* const kIdeographicSpace = "\xE3\x80\x80";
const size_t kIdeographicSpaceBytes = 3;

//U+FFFD 的 UTF-8 编码，4 字节字符与非法字节都替换成它。
const char* const kReplacementChar = "\xEF\xBF\xBD";

//UTF-8 首字节判定：两字节、三字节、四字节序列首字节的取值范围（含两端），与后续字节的形式。
const unsigned char kLead2First = 0xC2;
const unsigned char kLead2Last  = 0xDF;
const unsigned char kLead3First = 0xE0;
const unsigned char kLead3Last  = 0xEF;
const unsigned char kLead4First = 0xF0;
const unsigned char kLead4Last  = 0xF4;
const unsigned char kContinuationMask  = 0xC0;
const unsigned char kContinuationValue = 0x80;
const unsigned char kAsciiLimit = 0x80;

//拦截记录内容摘要最多的字符数，与 t_sensitive_hit_log.f_snippet 的 varchar(128) 一致。
const int kSnippetMaxChars = 128;

//LIKE 模糊匹配用的转义字符：关键字里的 %、_ 与它本身前面加上它，按字面匹配。
const char kLikeEscape = '!';

// ---- 文本处理 ----

//判断一个字节是否是 ASCII 空白（含 \r、\n、\t 等控制空白）。
bool IsAsciiSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

//去掉首尾的 ASCII 空白与全角空格。
std::string TrimSpaces(const std::string& s)
{
    size_t first = 0;
    size_t last = s.size();
    while (first < last)
    {
        if (IsAsciiSpace(s[first]))
            ++first;
        else if (last - first >= kIdeographicSpaceBytes
                 && s.compare(first, kIdeographicSpaceBytes, kIdeographicSpace) == 0)
            first += kIdeographicSpaceBytes;
        else
            break;
    }
    while (last > first)
    {
        if (IsAsciiSpace(s[last - 1]))
            --last;
        else if (last - first >= kIdeographicSpaceBytes
                 && s.compare(last - kIdeographicSpaceBytes, kIdeographicSpaceBytes, kIdeographicSpace) == 0)
            last -= kIdeographicSpaceBytes;
        else
            break;
    }
    return s.substr(first, last - first);
}

//按字符截掉超出 maxChars 的部分，不截断多字节字符。
std::string TruncateChars(const std::string& s, int maxChars)
{
    int chars = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        //后续字节不是一个字符的开头
        if ((static_cast<unsigned char>(s[i]) & kContinuationMask) == kContinuationValue)
            continue;
        if (chars == maxChars)
            return s.substr(0, i);
        ++chars;
    }
    return s;
}

//把制表符、回车、换行替换为空格，保证导出的一列不会把一行拆开。
std::string FlattenColumn(const std::string& s)
{
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] == '\t' || out[i] == '\r' || out[i] == '\n')
            out[i] = ' ';
    }
    return out;
}

//分类取值不在范围内时按「其他」处理。
int NormalizeCategory(int category)
{
    for (size_t i = 0; i < kCategoryCount; ++i)
    {
        if (kCategoryNames[i].m_category == category)
            return category;
    }
    return sensitive_category_other;
}

// ---- SQL 拼接 ----

//对一段文字做 SQL 字符串转义（引号等），结果可直接放进单引号里。
std::string Escape(IDatabase* pConn, const std::string& s)
{
    //转义后最坏情况每个字符变两个，再留一个结束符
    std::vector<char> buf(s.size() * 2 + 1, '\0');
    pConn->escapeString(&buf[0], s.c_str(), static_cast<uint32_t>(s.size()));
    return std::string(&buf[0]);
}

//拼出 "列 LIKE '%关键字%' ESCAPE '!'"：关键字里的 %、_ 与转义字符本身按字面匹配。
std::string LikeClause(IDatabase* pConn, const char* column, const std::string& keyword)
{
    std::string pattern;
    for (size_t i = 0; i < keyword.size(); ++i)
    {
        const char c = keyword[i];
        if (c == '%' || c == '_' || c == kLikeEscape)
            pattern += kLikeEscape;
        pattern += c;
    }
    std::ostringstream os;
    os << column << " LIKE '%" << Escape(pConn, pattern) << "%' ESCAPE '" << kLikeEscape << "'";
    return os.str();
}

//把页码、每页条数夹回合法范围，算出 LIMIT 与 OFFSET。
void PageArgs(int page, int pageSize, int& limit, int& offset)
{
    if (pageSize <= 0)
        pageSize = SENSITIVE_DEFAULT_PAGE_SIZE;
    if (pageSize > SENSITIVE_MAX_PAGE_SIZE)
        pageSize = SENSITIVE_MAX_PAGE_SIZE;
    if (page < 1)
        page = 1;
    limit = pageSize;
    offset = (page - 1) * pageSize;
}

//执行一条只返回一个整数的查询（COUNT 等）。
bool QueryInt(IDatabase* pConn, const std::string& sql, int64_t& value)
{
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return false;
    value = 0;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        value = static_cast<int64_t>(pRow[0].getUInt64());
    pResult->endQuery();
    delete pResult;
    return true;
}

//查某企业里是否已有归一化形式为 norm 的词条；excludeId 非 0 时不算这一条（修改时排除自身）。
//@return 数据库访问成功返回 true，结果写入 exists
bool NormExists(IDatabase* pConn, int64_t enterpriseId, const std::string& norm, int64_t excludeId,
                bool& exists)
{
    std::ostringstream os;
    os << "SELECT COUNT(*) FROM t_sensitive_word WHERE f_enterprise_id = " << enterpriseId
       << " AND f_word_norm = '" << Escape(pConn, norm) << "'";
    if (excludeId != 0)
        os << " AND f_id <> " << excludeId;
    int64_t count = 0;
    if (!QueryInt(pConn, os.str(), count))
        return false;
    exists = (count > 0);
    return true;
}

//统计某企业的词条数。
bool CountWordsOn(IDatabase* pConn, int64_t enterpriseId, int64_t& count)
{
    std::ostringstream os;
    os << "SELECT COUNT(*) FROM t_sensitive_word WHERE f_enterprise_id = " << enterpriseId;
    return QueryInt(pConn, os.str(), count);
}

//插入一条词条。调用方须已校验、判重并检查过上限。
bool InsertWord(IDatabase* pConn, int64_t enterpriseId, const std::string& word, const std::string& norm,
                int category, const std::string& remark, int64_t creatorId, const std::string& creatorName,
                int64_t now)
{
    std::ostringstream os;
    os << "INSERT INTO t_sensitive_word(f_enterprise_id, f_word, f_word_norm, f_category, f_remark, "
          "f_creator_id, f_creator_name, f_create_time, f_update_time) VALUES("
       << enterpriseId << ", '" << Escape(pConn, word) << "', '" << Escape(pConn, norm) << "', "
       << NormalizeCategory(category) << ", '"
       << Escape(pConn, TruncateChars(remark, SENSITIVE_REMARK_MAX_CHARS)) << "', "
       << creatorId << ", '" << Escape(pConn, TruncateChars(creatorName, SENSITIVE_NAME_MAX_CHARS)) << "', "
       << now << ", " << now << ")";
    return pConn->execute(os.str().c_str());
}

//校验一个准备入库的词条：去掉首尾空白后按匹配引擎的规则检查。
//@return 取值见 sensitive_word_op_result；合格时 trimmed 与 norm 写入去空白后的原文与归一化形式
int ValidateWord(const std::string& word, std::string& trimmed, std::string& norm)
{
    trimmed = TrimSpaces(word);
    const int check = SensitiveWordMatcher::CheckWord(trimmed, &norm);
    if (check == sensitive_word_check_empty)
        return sensitive_word_op_empty;
    if (check == sensitive_word_check_too_long)
        return sensitive_word_op_too_long;
    return sensitive_word_op_ok;
}

//建立一条数据库连接，失败时记日志。
IDatabase* Connect(const char* where)
{
    IDatabase* pConn = DbFactory::createConnection();
    if (pConn == NULL)
        LOGE("%s db init failed, db: %s", where, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

} // anonymous namespace

SensitiveWordManager::SensitiveWordManager()
{
}

SensitiveWordManager::~SensitiveWordManager()
{
}

bool SensitiveWordManager::init()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_cache.clear();
    return true;
}

// ====================================================================
// 纯函数
// ====================================================================

std::string SensitiveWordManager::CategoryName(int category)
{
    const int normalized = NormalizeCategory(category);
    for (size_t i = 0; i < kCategoryCount; ++i)
    {
        if (kCategoryNames[i].m_category == normalized)
            return kCategoryNames[i].m_name;
    }
    return u8"其他";
}

int SensitiveWordManager::ParseCategoryName(const std::string& name)
{
    const std::string trimmed = TrimSpaces(name);
    for (size_t i = 0; i < kCategoryCount; ++i)
    {
        if (trimmed == kCategoryNames[i].m_name)
            return kCategoryNames[i].m_category;
    }
    return 0;
}

bool SensitiveWordManager::ParseImportLine(const std::string& line, int defaultCategory,
                                           std::string& word, int& category, std::string& remark)
{
    word.clear();
    remark.clear();
    category = NormalizeCategory(defaultCategory);

    if (TrimSpaces(line).empty())
        return false;

    //按制表符切成至多三列：词条、分类名、备注（备注列里若还有制表符，一并留在备注里）
    const size_t tab1 = line.find(kColumnSeparator);
    if (tab1 == std::string::npos)
    {
        word = TrimSpaces(line);
        return true;
    }
    word = TrimSpaces(line.substr(0, tab1));

    const size_t tab2 = line.find(kColumnSeparator, tab1 + 1);
    const std::string categoryColumn = (tab2 == std::string::npos)
                                     ? line.substr(tab1 + 1)
                                     : line.substr(tab1 + 1, tab2 - tab1 - 1);
    const int parsed = ParseCategoryName(categoryColumn);
    if (parsed != 0)
        category = parsed;
    if (tab2 != std::string::npos)
        remark = TrimSpaces(line.substr(tab2 + 1));
    return true;
}

std::string SensitiveWordManager::FormatExportLine(const std::string& word, int category,
                                                   const std::string& remark)
{
    std::string line = FlattenColumn(word);
    line += kColumnSeparator;
    line += CategoryName(category);
    line += kColumnSeparator;
    line += FlattenColumn(remark);
    return line;
}

std::string SensitiveWordManager::ReplaceFourByteChars(const std::string& utf8)
{
    //逐个字符扫描：合法的 1～3 字节字符原样保留；4 字节字符与非法字节替换为 U+FFFD
    std::string out;
    out.reserve(utf8.size());
    const size_t n = utf8.size();
    size_t i = 0;
    while (i < n)
    {
        const unsigned char b0 = static_cast<unsigned char>(utf8[i]);
        size_t length = 0;
        if (b0 < kAsciiLimit)
            length = 1;
        else if (b0 >= kLead2First && b0 <= kLead2Last)
            length = 2;
        else if (b0 >= kLead3First && b0 <= kLead3Last)
            length = 3;
        else if (b0 >= kLead4First && b0 <= kLead4Last)
            length = 4;

        //首字节非法，或后续字节不足 / 形式不对：当作一个非法字节
        bool valid = (length > 0 && i + length <= n);
        for (size_t k = 1; valid && k < length; ++k)
        {
            if ((static_cast<unsigned char>(utf8[i + k]) & kContinuationMask) != kContinuationValue)
                valid = false;
        }
        if (!valid)
        {
            out += kReplacementChar;
            ++i;
            continue;
        }

        if (length == 4)
            out += kReplacementChar;
        else
            out.append(utf8, i, length);
        i += length;
    }
    return out;
}

// ====================================================================
// 词库：管理后台用
// ====================================================================

bool SensitiveWordManager::listWords(const SensitiveWordQuery& query,
                                     std::vector<SensitiveWordRecord>& out, int& total)
{
    out.clear();
    total = 0;
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::listWords"));
    if (!pConn)
        return false;

    std::ostringstream where;
    where << " WHERE f_enterprise_id = " << query.m_enterpriseId;
    if (!query.m_keyword.empty())
        where << " AND " << LikeClause(pConn.get(), "f_word", query.m_keyword);
    if (query.m_category != 0)
        where << " AND f_category = " << query.m_category;

    int64_t count = 0;
    if (!QueryInt(pConn.get(), "SELECT COUNT(*) FROM t_sensitive_word" + where.str(), count))
        return false;
    total = static_cast<int>(count);

    int limit = 0;
    int offset = 0;
    PageArgs(query.m_page, query.m_pageSize, limit, offset);
    std::ostringstream os;
    os << "SELECT f_id, f_enterprise_id, f_word, f_category, f_remark, f_creator_id, f_creator_name, "
          "f_create_time, f_update_time FROM t_sensitive_word" << where.str()
       << " ORDER BY f_id DESC LIMIT " << limit << " OFFSET " << offset;
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("SensitiveWordManager::listWords query error, enterpriseid: %lld", (long long)query.m_enterpriseId);
        return false;
    }
    Field* pRow = pResult->fetch();
    while (pRow != NULL)
    {
        SensitiveWordRecord r;
        r.m_id           = static_cast<int64_t>(pRow[0].getUInt64());
        r.m_enterpriseId = static_cast<int64_t>(pRow[1].getUInt64());
        r.m_word         = pRow[2].getCppString();
        r.m_category     = pRow[3].getInt32();
        r.m_remark       = pRow[4].getCppString();
        r.m_creatorId    = static_cast<int64_t>(pRow[5].getUInt64());
        r.m_creatorName  = pRow[6].getCppString();
        r.m_createTime   = static_cast<int64_t>(pRow[7].getUInt64());
        r.m_updateTime   = static_cast<int64_t>(pRow[8].getUInt64());
        out.push_back(r);
        if (!pResult->nextRow())
            break;
        pRow = pResult->fetch();
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool SensitiveWordManager::countWords(int64_t enterpriseId, int& count)
{
    count = 0;
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::countWords"));
    if (!pConn)
        return false;
    int64_t value = 0;
    if (!CountWordsOn(pConn.get(), enterpriseId, value))
        return false;
    count = static_cast<int>(value);
    return true;
}

int SensitiveWordManager::addWord(int64_t enterpriseId, const std::string& word, int category,
                                  const std::string& remark, int64_t creatorId,
                                  const std::string& creatorName, int64_t now, int64_t* newId)
{
    std::string trimmed;
    std::string norm;
    const int valid = ValidateWord(word, trimmed, norm);
    if (valid != sensitive_word_op_ok)
        return valid;

    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::addWord"));
    if (!pConn)
        return sensitive_word_op_db_error;

    int64_t count = 0;
    if (!CountWordsOn(pConn.get(), enterpriseId, count))
        return sensitive_word_op_db_error;
    if (count >= SENSITIVE_WORDS_PER_ENTERPRISE)
        return sensitive_word_op_full;

    bool exists = false;
    if (!NormExists(pConn.get(), enterpriseId, norm, 0, exists))
        return sensitive_word_op_db_error;
    if (exists)
        return sensitive_word_op_exist;

    if (!InsertWord(pConn.get(), enterpriseId, trimmed, norm, category, remark, creatorId, creatorName, now))
    {
        //两个管理员几乎同时添加同一个词时，后到的那次撞上唯一键：再查一次，确认是重复还是库出错
        if (NormExists(pConn.get(), enterpriseId, norm, 0, exists) && exists)
            return sensitive_word_op_exist;
        LOGE("SensitiveWordManager::addWord insert error, enterpriseid: %lld", (long long)enterpriseId);
        return sensitive_word_op_db_error;
    }
    if (newId != NULL)
        *newId = static_cast<int64_t>(pConn->getInsertID());

    invalidate(enterpriseId);
    LOGI("sensitive word added, enterpriseid: %lld, category: %d, creator: %lld",
         (long long)enterpriseId, NormalizeCategory(category), (long long)creatorId);
    return sensitive_word_op_ok;
}

int SensitiveWordManager::updateWord(int64_t enterpriseId, int64_t id, const std::string& word,
                                     int category, const std::string& remark, int64_t now)
{
    std::string trimmed;
    std::string norm;
    const int valid = ValidateWord(word, trimmed, norm);
    if (valid != sensitive_word_op_ok)
        return valid;

    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::updateWord"));
    if (!pConn)
        return sensitive_word_op_db_error;

    //词条须存在且属于该企业
    std::ostringstream osOwn;
    osOwn << "SELECT COUNT(*) FROM t_sensitive_word WHERE f_id = " << id
          << " AND f_enterprise_id = " << enterpriseId;
    int64_t own = 0;
    if (!QueryInt(pConn.get(), osOwn.str(), own))
        return sensitive_word_op_db_error;
    if (own == 0)
        return sensitive_word_op_not_exist;

    bool exists = false;
    if (!NormExists(pConn.get(), enterpriseId, norm, id, exists))
        return sensitive_word_op_db_error;
    if (exists)
        return sensitive_word_op_exist;

    std::ostringstream os;
    os << "UPDATE t_sensitive_word SET f_word = '" << Escape(pConn.get(), trimmed)
       << "', f_word_norm = '" << Escape(pConn.get(), norm)
       << "', f_category = " << NormalizeCategory(category)
       << ", f_remark = '" << Escape(pConn.get(), TruncateChars(remark, SENSITIVE_REMARK_MAX_CHARS))
       << "', f_update_time = " << now
       << " WHERE f_id = " << id << " AND f_enterprise_id = " << enterpriseId;
    if (!pConn->execute(os.str().c_str()))
    {
        LOGE("SensitiveWordManager::updateWord error, enterpriseid: %lld, id: %lld",
             (long long)enterpriseId, (long long)id);
        return sensitive_word_op_db_error;
    }

    invalidate(enterpriseId);
    LOGI("sensitive word updated, enterpriseid: %lld, id: %lld", (long long)enterpriseId, (long long)id);
    return sensitive_word_op_ok;
}

bool SensitiveWordManager::deleteWords(int64_t enterpriseId, const std::vector<int64_t>& ids, int& deleted)
{
    deleted = 0;
    if (ids.empty())
        return true;

    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::deleteWords"));
    if (!pConn)
        return false;

    //条件里带上企业ID：不属于该企业的ID不会被删到
    std::ostringstream os;
    os << "DELETE FROM t_sensitive_word WHERE f_enterprise_id = " << enterpriseId << " AND f_id IN (";
    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (i > 0)
            os << ", ";
        os << ids[i];
    }
    os << ")";
    uint32_t affected = 0;
    int dbErrno = 0;
    if (!pConn->execute(os.str().c_str(), affected, dbErrno))
    {
        LOGE("SensitiveWordManager::deleteWords error, enterpriseid: %lld, errno: %d",
             (long long)enterpriseId, dbErrno);
        return false;
    }
    deleted = static_cast<int>(affected);

    invalidate(enterpriseId);
    LOGI("sensitive words deleted, enterpriseid: %lld, requested: %u, deleted: %d",
         (long long)enterpriseId, static_cast<unsigned>(ids.size()), deleted);
    return true;
}

bool SensitiveWordManager::importWords(int64_t enterpriseId, const std::vector<std::string>& lines,
                                       int defaultCategory, int64_t creatorId,
                                       const std::string& creatorName, int64_t now,
                                       int& added, std::vector<SensitiveImportSkip>& skipped)
{
    added = 0;
    skipped.clear();

    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::importWords"));
    if (!pConn)
        return false;

    //整批在一个事务里写：既保证失败时不留半批数据，SQLite 下也比逐条提交快得多
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
        return false;

    int64_t current = 0;
    if (!CountWordsOn(pConn.get(), enterpriseId, current))
        return false;

    std::set<std::string> batchNorms;   //本批已加入的归一化形式，判断本批之内的重复
    int insertedCount = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string word;
        int category = 0;
        std::string remark;
        if (!SensitiveWordManager::ParseImportLine(lines[i], defaultCategory, word, category, remark))
            continue;

        SensitiveImportSkip skip;
        skip.m_line = static_cast<int>(i) + 1;
        skip.m_word = word;

        std::string trimmed;
        std::string norm;
        const int valid = ValidateWord(word, trimmed, norm);
        if (valid != sensitive_word_op_ok)
        {
            skip.m_reason = valid;
            skipped.push_back(skip);
            continue;
        }

        bool exists = (batchNorms.find(norm) != batchNorms.end());
        if (!exists && !NormExists(pConn.get(), enterpriseId, norm, 0, exists))
            return false;
        if (exists)
        {
            skip.m_reason = sensitive_word_op_exist;
            skipped.push_back(skip);
            continue;
        }

        if (current >= SENSITIVE_WORDS_PER_ENTERPRISE)
        {
            skip.m_reason = sensitive_word_op_full;
            skipped.push_back(skip);
            continue;
        }

        if (!InsertWord(pConn.get(), enterpriseId, trimmed, norm, category, remark, creatorId, creatorName, now))
        {
            LOGE("SensitiveWordManager::importWords insert error, enterpriseid: %lld, line: %d",
                 (long long)enterpriseId, skip.m_line);
            skipped.clear();
            return false;
        }
        batchNorms.insert(norm);
        ++current;
        ++insertedCount;
    }

    if (!transaction.Commit())
    {
        skipped.clear();
        return false;
    }
    added = insertedCount;

    invalidate(enterpriseId);
    LOGI("sensitive words imported, enterpriseid: %lld, lines: %u, added: %d, skipped: %u",
         (long long)enterpriseId, static_cast<unsigned>(lines.size()), added,
         static_cast<unsigned>(skipped.size()));
    return true;
}

bool SensitiveWordManager::exportWords(int64_t enterpriseId, std::string& text)
{
    text.clear();
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::exportWords"));
    if (!pConn)
        return false;

    std::ostringstream os;
    os << "SELECT f_word, f_category, f_remark FROM t_sensitive_word WHERE f_enterprise_id = "
       << enterpriseId << " ORDER BY f_id ASC";
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("SensitiveWordManager::exportWords query error, enterpriseid: %lld", (long long)enterpriseId);
        return false;
    }
    Field* pRow = pResult->fetch();
    while (pRow != NULL)
    {
        if (!text.empty())
            text += '\n';
        text += FormatExportLine(pRow[0].getCppString(), pRow[1].getInt32(), pRow[2].getCppString());
        if (!pResult->nextRow())
            break;
        pRow = pResult->fetch();
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool SensitiveWordManager::getEnabled(int64_t enterpriseId, bool& enabled)
{
    enabled = false;
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::getEnabled"));
    if (!pConn)
        return false;

    std::ostringstream os;
    os << "SELECT f_sensitive_on FROM t_enterprise WHERE f_enterprise_id = " << enterpriseId;
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
        return false;
    Field* pRow = pResult->fetch();
    const bool found = (pRow != NULL);
    if (found)
        enabled = (pRow[0].getInt32() != 0);
    pResult->endQuery();
    delete pResult;
    return found;
}

bool SensitiveWordManager::setEnabled(int64_t enterpriseId, bool enabled)
{
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::setEnabled"));
    if (!pConn)
        return false;

    //先确认企业存在：MySQL 在新值与旧值相同时受影响行数为 0，不能据它判断企业是否存在
    std::ostringstream osCheck;
    osCheck << "SELECT COUNT(*) FROM t_enterprise WHERE f_enterprise_id = " << enterpriseId;
    int64_t count = 0;
    if (!QueryInt(pConn.get(), osCheck.str(), count) || count == 0)
        return false;

    std::ostringstream os;
    os << "UPDATE t_enterprise SET f_sensitive_on = " << (enabled ? 1 : 0)
       << " WHERE f_enterprise_id = " << enterpriseId;
    if (!pConn->execute(os.str().c_str()))
    {
        LOGE("SensitiveWordManager::setEnabled error, enterpriseid: %lld", (long long)enterpriseId);
        return false;
    }

    invalidate(enterpriseId);
    LOGI("sensitive word switch changed, enterpriseid: %lld, enabled: %d",
         (long long)enterpriseId, enabled ? 1 : 0);
    return true;
}

// ====================================================================
// 检查
// ====================================================================

bool SensitiveWordManager::checkTexts(int64_t enterpriseId, const std::vector<std::string>& texts,
                                      SensitiveCheckResult* result)
{
    if (enterpriseId <= 0)
        return false;

    std::shared_ptr<const WordSet> wordSet = getWordSet(enterpriseId);
    if (!wordSet || !wordSet->m_enabled || wordSet->m_matcher.WordCount() == 0)
        return false;

    for (size_t i = 0; i < texts.size(); ++i)
    {
        if (texts[i].empty())
            continue;
        SensitiveHit hit;
        if (!wordSet->m_matcher.Match(texts[i], &hit))
            continue;

        if (result != NULL)
        {
            result->m_textIndex = i;
            result->m_wordId = hit.m_wordId;
            result->m_category = hit.m_category;
            std::map<int64_t, std::string>::const_iterator it = wordSet->m_words.find(hit.m_wordId);
            result->m_word = (it != wordSet->m_words.end()) ? it->second : std::string();
            result->m_snippet = SensitiveWordMatcher::MakeSnippet(texts[i], hit.m_beginChar, hit.m_endChar,
                                                                  SENSITIVE_SNIPPET_CONTEXT_CHARS);
        }
        return true;
    }
    return false;
}

void SensitiveWordManager::invalidate(int64_t enterpriseId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_cache.erase(enterpriseId);
    ++m_generation[enterpriseId];
}

std::shared_ptr<const SensitiveWordManager::WordSet> SensitiveWordManager::getWordSet(int64_t enterpriseId)
{
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<int64_t, std::shared_ptr<const WordSet> >::const_iterator it = m_cache.find(enterpriseId);
        if (it != m_cache.end())
            return it->second;
        generation = m_generation[enterpriseId];
    }

    //缓存里没有：在锁外查库、构建匹配器
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::getWordSet"));
    if (!pConn)
        return std::shared_ptr<const WordSet>();

    std::shared_ptr<WordSet> wordSet(new WordSet());

    //企业不存在时按"开关关闭"处理，同样放入缓存，避免每条消息都为它查一次库
    std::ostringstream osSwitch;
    osSwitch << "SELECT f_sensitive_on FROM t_enterprise WHERE f_enterprise_id = " << enterpriseId;
    QueryResult* pSwitch = pConn->query(osSwitch.str().c_str());
    if (pSwitch == NULL)
        return std::shared_ptr<const WordSet>();
    Field* pSwitchRow = pSwitch->fetch();
    wordSet->m_enabled = (pSwitchRow != NULL && pSwitchRow[0].getInt32() != 0);
    pSwitch->endQuery();
    delete pSwitch;

    std::vector<SensitiveWordEntry> entries;
    std::ostringstream os;
    os << "SELECT f_id, f_word, f_category FROM t_sensitive_word WHERE f_enterprise_id = "
       << enterpriseId << " ORDER BY f_id ASC";
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("SensitiveWordManager::getWordSet query error, enterpriseid: %lld", (long long)enterpriseId);
        return std::shared_ptr<const WordSet>();
    }
    Field* pRow = pResult->fetch();
    while (pRow != NULL)
    {
        SensitiveWordEntry e;
        e.m_id = static_cast<int64_t>(pRow[0].getUInt64());
        e.m_word = pRow[1].getCppString();
        e.m_category = pRow[2].getInt32();
        wordSet->m_words[e.m_id] = e.m_word;
        entries.push_back(e);
        if (!pResult->nextRow())
            break;
        pRow = pResult->fetch();
    }
    pResult->endQuery();
    delete pResult;

    const size_t built = wordSet->m_matcher.Build(entries);

    //装载期间没有写入丢弃过缓存，才把这份快照放进去；否则它可能已经过时，只用于本次检查
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_generation[enterpriseId] == generation)
            m_cache[enterpriseId] = wordSet;
    }
    LOGI("sensitive word set loaded, enterpriseid: %lld, enabled: %d, words: %u",
         (long long)enterpriseId, wordSet->m_enabled ? 1 : 0, static_cast<unsigned>(built));
    return wordSet;
}

// ====================================================================
// 拦截记录
// ====================================================================

bool SensitiveWordManager::writeHit(const SensitiveHitRecord& record)
{
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::writeHit"));
    if (!pConn)
        return false;

    const std::string userName = TruncateChars(ReplaceFourByteChars(record.m_userName), SENSITIVE_NAME_MAX_CHARS);
    const std::string targetName = TruncateChars(ReplaceFourByteChars(record.m_targetName), SENSITIVE_NAME_MAX_CHARS);
    const std::string word = TruncateChars(ReplaceFourByteChars(record.m_word), SENSITIVE_WORD_MAX_CHARS);
    const std::string snippet = TruncateChars(ReplaceFourByteChars(record.m_snippet), kSnippetMaxChars);

    std::ostringstream os;
    os << "INSERT INTO t_sensitive_hit_log(f_enterprise_id, f_user_id, f_user_name, f_source, f_ctype, "
          "f_cid, f_target_name, f_word_id, f_word, f_category, f_snippet, f_create_time) VALUES("
       << record.m_enterpriseId << ", " << record.m_userId << ", '" << Escape(pConn.get(), userName) << "', "
       << record.m_source << ", " << record.m_ctype << ", " << record.m_cid << ", '"
       << Escape(pConn.get(), targetName) << "', " << record.m_wordId << ", '"
       << Escape(pConn.get(), word) << "', " << NormalizeCategory(record.m_category) << ", '"
       << Escape(pConn.get(), snippet) << "', " << record.m_createTime << ")";
    if (!pConn->execute(os.str().c_str()))
    {
        LOGE("SensitiveWordManager::writeHit error, enterpriseid: %lld, userid: %lld, source: %d",
             (long long)record.m_enterpriseId, (long long)record.m_userId, record.m_source);
        return false;
    }
    return true;
}

bool SensitiveWordManager::listHits(const SensitiveHitQuery& query,
                                    std::vector<SensitiveHitRecord>& out, int& total)
{
    out.clear();
    total = 0;
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::listHits"));
    if (!pConn)
        return false;

    std::ostringstream where;
    where << " WHERE f_enterprise_id = " << query.m_enterpriseId;
    if (query.m_beginTime > 0)
        where << " AND f_create_time >= " << query.m_beginTime;
    if (query.m_endTime > 0)
        where << " AND f_create_time <= " << query.m_endTime;
    if (!query.m_keyword.empty())
        where << " AND " << LikeClause(pConn.get(), "f_user_name", query.m_keyword);
    if (query.m_source != 0)
        where << " AND f_source = " << query.m_source;
    if (query.m_category != 0)
        where << " AND f_category = " << query.m_category;

    int64_t count = 0;
    if (!QueryInt(pConn.get(), "SELECT COUNT(*) FROM t_sensitive_hit_log" + where.str(), count))
        return false;
    total = static_cast<int>(count);

    int limit = 0;
    int offset = 0;
    PageArgs(query.m_page, query.m_pageSize, limit, offset);
    std::ostringstream os;
    os << "SELECT f_id, f_enterprise_id, f_user_id, f_user_name, f_source, f_ctype, f_cid, f_target_name, "
          "f_word_id, f_word, f_category, f_snippet, f_create_time FROM t_sensitive_hit_log" << where.str()
       << " ORDER BY f_create_time DESC, f_id DESC LIMIT " << limit << " OFFSET " << offset;
    QueryResult* pResult = pConn->query(os.str().c_str());
    if (pResult == NULL)
    {
        LOGE("SensitiveWordManager::listHits query error, enterpriseid: %lld", (long long)query.m_enterpriseId);
        return false;
    }
    Field* pRow = pResult->fetch();
    while (pRow != NULL)
    {
        SensitiveHitRecord r;
        r.m_id           = static_cast<int64_t>(pRow[0].getUInt64());
        r.m_enterpriseId = static_cast<int64_t>(pRow[1].getUInt64());
        r.m_userId       = static_cast<int64_t>(pRow[2].getUInt64());
        r.m_userName     = pRow[3].getCppString();
        r.m_source       = pRow[4].getInt32();
        r.m_ctype        = pRow[5].getInt32();
        r.m_cid          = static_cast<int64_t>(pRow[6].getUInt64());
        r.m_targetName   = pRow[7].getCppString();
        r.m_wordId       = static_cast<int64_t>(pRow[8].getUInt64());
        r.m_word         = pRow[9].getCppString();
        r.m_category     = pRow[10].getInt32();
        r.m_snippet      = pRow[11].getCppString();
        r.m_createTime   = static_cast<int64_t>(pRow[12].getUInt64());
        out.push_back(r);
        if (!pResult->nextRow())
            break;
        pRow = pResult->fetch();
    }
    pResult->endQuery();
    delete pResult;
    return true;
}

bool SensitiveWordManager::purgeHitsBefore(int64_t cutoffMs, int& deleted)
{
    deleted = 0;
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::purgeHitsBefore"));
    if (!pConn)
        return false;

    std::ostringstream os;
    os << "DELETE FROM t_sensitive_hit_log WHERE f_create_time < " << cutoffMs;
    uint32_t affected = 0;
    int dbErrno = 0;
    if (!pConn->execute(os.str().c_str(), affected, dbErrno))
    {
        LOGE("SensitiveWordManager::purgeHitsBefore error, cutoff: %lld, errno: %d",
             (long long)cutoffMs, dbErrno);
        return false;
    }
    //成功时不在这里记日志：定时清理每小时调用一次，要不要记由调用方按删除条数决定
    deleted = static_cast<int>(affected);
    return true;
}

int64_t SensitiveWordManager::HitRetentionCutoff(int64_t nowMs)
{
    //一天的毫秒数
    const int64_t kMillisPerDay = 24LL * 3600 * 1000;
    return nowMs - static_cast<int64_t>(SENSITIVE_HIT_RETENTION_DAYS) * kMillisPerDay;
}

bool SensitiveWordManager::deleteEnterpriseData(int64_t enterpriseId)
{
    std::unique_ptr<IDatabase> pConn(Connect("SensitiveWordManager::deleteEnterpriseData"));
    if (!pConn)
        return false;

    //两张表在一个事务里删：任一条失败时整体回滚，不会只删掉词库而留下拦截记录
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
        return false;

    std::ostringstream osWords;
    osWords << "DELETE FROM t_sensitive_word WHERE f_enterprise_id = " << enterpriseId;
    uint32_t deletedWords = 0;
    int dbErrno = 0;
    if (!pConn->execute(osWords.str().c_str(), deletedWords, dbErrno))
    {
        LOGE("SensitiveWordManager::deleteEnterpriseData words error, enterpriseid: %lld, errno: %d",
             (long long)enterpriseId, dbErrno);
        return false;
    }

    std::ostringstream osHits;
    osHits << "DELETE FROM t_sensitive_hit_log WHERE f_enterprise_id = " << enterpriseId;
    uint32_t deletedHits = 0;
    if (!pConn->execute(osHits.str().c_str(), deletedHits, dbErrno))
    {
        LOGE("SensitiveWordManager::deleteEnterpriseData hits error, enterpriseid: %lld, errno: %d",
             (long long)enterpriseId, dbErrno);
        return false;
    }

    if (!transaction.Commit())
        return false;

    invalidate(enterpriseId);
    LOGI("sensitive data of enterprise deleted, enterpriseid: %lld, words: %u, hits: %u",
         (long long)enterpriseId, deletedWords, deletedHits);
    return true;
}
