/**
 *  企业级敏感词库管理器，SensitiveWordManager.h
 *
 *  负责三件事：
 *    · 词库的增删改查、批量导入导出、企业总开关（t_sensitive_word、t_enterprise.f_sensitive_on），
 *      供管理后台接口调用；
 *    · 按企业缓存匹配器，供会话层检查用户提交的文字（缓存在任何一次写入后丢弃，改动立即生效）；
 *    · 拦截记录的写入、查询与过期清理（t_sensitive_hit_log）。
 *  数据库访问与 GroupManager 等同一风格：每次操作单独建立一条连接。方案见 docs/敏感词方案.md。
 *
 *  典型用法：
 *      SensitiveWordManager& mgr = Singleton<SensitiveWordManager>::Instance();
 *      SensitiveCheckResult r;
 *      if (mgr.checkTexts(enterpriseId, texts, &r))
 *      {
 *          //拒绝本次请求，并用 r 写一条拦截记录
 *      }
 *
 *  线程安全：各方法可以在任意线程并发调用。匹配器缓存由 m_mutex 保护，检查时只在锁内取出
 *  该企业匹配器的共享指针，匹配本身在锁外进行。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "SensitiveWordDefs.h"
#include "SensitiveWordMatcher.h"

//词库里的一条词条，对应 t_sensitive_word 的一行。
struct SensitiveWordRecord
{
    int64_t     m_id;            //词条ID
    int64_t     m_enterpriseId;  //所属企业ID
    std::string m_word;          //词条原文（去掉首尾空白）
    int         m_category;      //分类，取值见 chatserver::sensitive_word_category
    std::string m_remark;        //备注，可为空
    int64_t     m_creatorId;     //添加者管理员ID
    std::string m_creatorName;   //添加者姓名快照
    int64_t     m_createTime;    //添加时间，Unix毫秒
    int64_t     m_updateTime;    //最后修改时间，Unix毫秒；从未修改过时等于添加时间

    SensitiveWordRecord()
        : m_id(0)
        , m_enterpriseId(0)
        , m_category(chatserver::sensitive_category_other)
        , m_creatorId(0)
        , m_createTime(0)
        , m_updateTime(0)
    {
    }
};

//词库列表的查询条件。
struct SensitiveWordQuery
{
    int64_t     m_enterpriseId;  //企业ID
    std::string m_keyword;       //按词条原文模糊匹配；为空时不筛选
    int         m_category;      //只看某一分类；0 表示全部
    int         m_page;          //页码，从 1 起；小于 1 时按 1 处理
    int         m_pageSize;      //每页条数；不大于 0 时取 SENSITIVE_DEFAULT_PAGE_SIZE，超过上限时按上限处理

    SensitiveWordQuery()
        : m_enterpriseId(0)
        , m_category(0)
        , m_page(1)
        , m_pageSize(0)
    {
    }
};

//导入时被跳过的一行。
struct SensitiveImportSkip
{
    int         m_line;    //行号，从 1 起，按传入的行列表计（空行也计入行号）
    std::string m_word;    //该行解析出的词条原文
    int         m_reason;  //跳过原因，取值见 chatserver::sensitive_word_op_result

    SensitiveImportSkip()
        : m_line(0)
        , m_reason(chatserver::sensitive_word_op_ok)
    {
    }
};

//一条拦截记录，对应 t_sensitive_hit_log 的一行。
struct SensitiveHitRecord
{
    int64_t     m_id;            //记录ID；写入时忽略
    int64_t     m_enterpriseId;  //提交者所属企业ID
    int64_t     m_userId;        //提交者userid；自助注册时尚无账号，为 0
    std::string m_userName;      //提交者账号或昵称快照
    int         m_source;        //来源，取值见 chatserver::sensitive_hit_source
    int         m_ctype;         //会话类型：0 与会话无关，1 单聊，2 群聊
    int64_t     m_cid;           //会话ID：单聊为对方userid，群聊为群ID；与会话无关时为 0
    std::string m_targetName;    //会话名称快照（对方姓名或群名）
    int64_t     m_wordId;        //命中的词条ID
    std::string m_word;          //命中的词条原文快照
    int         m_category;      //命中词条的分类快照
    std::string m_snippet;       //内容摘要：命中位置前后各 SENSITIVE_SNIPPET_CONTEXT_CHARS 个字符
    int64_t     m_createTime;    //拦截时间，Unix毫秒

    SensitiveHitRecord()
        : m_id(0)
        , m_enterpriseId(0)
        , m_userId(0)
        , m_source(chatserver::sensitive_source_text)
        , m_ctype(0)
        , m_cid(0)
        , m_wordId(0)
        , m_category(chatserver::sensitive_category_other)
        , m_createTime(0)
    {
    }
};

//拦截记录的查询条件。
struct SensitiveHitQuery
{
    int64_t     m_enterpriseId;  //企业ID
    int64_t     m_beginTime;     //拦截时间下限（含），Unix毫秒；0 表示不限
    int64_t     m_endTime;       //拦截时间上限（含），Unix毫秒；0 表示不限
    std::string m_keyword;       //按提交者名称模糊匹配；为空时不筛选
    int         m_source;        //只看某一来源；0 表示全部
    int         m_category;      //只看某一分类；0 表示全部
    int         m_page;          //页码，从 1 起
    int         m_pageSize;      //每页条数，规则同 SensitiveWordQuery::m_pageSize

    SensitiveHitQuery()
        : m_enterpriseId(0)
        , m_beginTime(0)
        , m_endTime(0)
        , m_source(0)
        , m_category(0)
        , m_page(1)
        , m_pageSize(0)
    {
    }
};

//一次检查的命中结果。
struct SensitiveCheckResult
{
    size_t      m_textIndex;   //命中的是传入的第几段文字（从 0 起）
    int64_t     m_wordId;      //命中的词条ID
    std::string m_word;        //命中的词条原文
    int         m_category;    //命中词条的分类
    std::string m_snippet;     //命中位置前后各 SENSITIVE_SNIPPET_CONTEXT_CHARS 个字符的内容摘要

    SensitiveCheckResult()
        : m_textIndex(0)
        , m_wordId(0)
        , m_category(chatserver::sensitive_category_other)
    {
    }
};

//企业级敏感词库管理器；全局单例，经 Singleton<SensitiveWordManager> 访问
class SensitiveWordManager final
{
public:
    SensitiveWordManager();
    ~SensitiveWordManager();

    SensitiveWordManager(const SensitiveWordManager& rhs) = delete;
    SensitiveWordManager& operator=(const SensitiveWordManager& rhs) = delete;

    /**
     *  初始化：清空匹配器缓存（词库按需装载，不预加载）。
     *  @return 恒返回 true
     */
    bool init();

    // ================================================================
    // 纯函数：分类名称、导入导出的文本格式
    // ================================================================
    //
    // 导入导出的文本每行一条：词条[<Tab>分类名[<Tab>备注]]。

    /**
     *  取分类的中文名称。
     *  @param category 分类，取值见 sensitive_word_category；不认识的取值按「其他」
     *  @return 分类名称（UTF-8）
     */
    static std::string CategoryName(int category);

    /**
     *  按中文名称查分类。
     *  @param name 分类名称（UTF-8），首尾空白不计
     *  @return 分类取值；名称不认识时返回 0
     */
    static int ParseCategoryName(const std::string& name);

    /**
     *  解析导入文本的一行。各列去掉首尾空白；分类列缺失或名称不认识时取 defaultCategory。
     *  @param line            一行文字（UTF-8，不含换行符；行尾的 \r 当作空白去掉）
     *  @param defaultCategory 默认分类
     *  @param word            [出参] 词条原文；该列为空时为空串（由调用方按"为空"跳过）
     *  @param category        [出参] 分类
     *  @param remark          [出参] 备注
     *  @return 整行只有空白时返回 false（调用方直接忽略该行），否则返回 true
     */
    static bool ParseImportLine(const std::string& line, int defaultCategory,
                                std::string& word, int& category, std::string& remark);

    /**
     *  按导入格式拼出一行（不含换行符）。词条与备注里的制表符、回车、换行替换为空格，
     *  保证导出的文件可以原样再导入。
     *  @param word     词条原文
     *  @param category 分类
     *  @param remark   备注，可为空（为空时仍输出分类列，备注列为空）
     *  @return 一行文字（UTF-8）
     */
    static std::string FormatExportLine(const std::string& word, int category, const std::string& remark);

    /**
     *  把 UTF-8 文字里的 4 字节字符（多数 emoji）替换为 U+FFFD。MySQL 的 utf8 列存不下 4 字节
     *  字符，拦截记录里的摘要与名称写库前都经过这一步；两种后端一视同仁，结果一致。
     *  @param utf8 文字（UTF-8）
     *  @return 替换后的文字
     */
    static std::string ReplaceFourByteChars(const std::string& utf8);

    /**
     *  算出拦截记录的保留截止时刻：早于它的记录应当清理（保留期见 SENSITIVE_HIT_RETENTION_DAYS）。
     *  @param nowMs 当前时刻，Unix毫秒
     *  @return 截止时刻，Unix毫秒；交给 purgeHitsBefore 使用
     */
    static int64_t HitRetentionCutoff(int64_t nowMs);

    // ================================================================
    // 词库：管理后台用
    // ================================================================

    /**
     *  分页查询某企业的词库，按添加先后倒序（最新的在前）。
     *  @param query 查询条件
     *  @param out   [出参] 本页词条，调用前会被清空
     *  @param total [出参] 满足条件的总条数（不受分页限制）
     *  @return 数据库访问成功返回 true
     */
    bool listWords(const SensitiveWordQuery& query, std::vector<SensitiveWordRecord>& out, int& total);

    /**
     *  统计某企业的词条总数（管理端显示「已用 N / 10000」）。
     *  @param enterpriseId 企业ID
     *  @param count        [出参] 词条数
     *  @return 数据库访问成功返回 true
     */
    bool countWords(int64_t enterpriseId, int& count);

    /**
     *  添加一条词条。成功后丢弃该企业的匹配器缓存。
     *  @param enterpriseId 企业ID
     *  @param word         词条原文（UTF-8），首尾空白去掉后入库
     *  @param category     分类；不在 sensitive_word_category 范围内时按「其他」
     *  @param remark       备注，超过 SENSITIVE_REMARK_MAX_CHARS 个字符的部分截掉
     *  @param creatorId    添加者管理员ID
     *  @param creatorName  添加者姓名
     *  @param now          当前时间，Unix毫秒
     *  @param newId        [出参，可为 NULL] 成功时写入新词条ID
     *  @return 取值见 sensitive_word_op_result
     */
    int addWord(int64_t enterpriseId, const std::string& word, int category, const std::string& remark,
                int64_t creatorId, const std::string& creatorName, int64_t now, int64_t* newId);

    /**
     *  修改一条词条的原文、分类与备注。成功后丢弃该企业的匹配器缓存。
     *  @param enterpriseId 企业ID；词条须属于该企业
     *  @param id           词条ID
     *  @param word         新的词条原文
     *  @param category     新的分类
     *  @param remark       新的备注
     *  @param now          当前时间，Unix毫秒，写入最后修改时间
     *  @return 取值见 sensitive_word_op_result；词条不存在或不属于该企业时为 sensitive_word_op_not_exist
     */
    int updateWord(int64_t enterpriseId, int64_t id, const std::string& word, int category,
                   const std::string& remark, int64_t now);

    /**
     *  删除一条或多条词条。不属于该企业的ID直接忽略。成功后丢弃该企业的匹配器缓存。
     *  @param enterpriseId 企业ID
     *  @param ids          词条ID列表
     *  @param deleted      [出参] 实际删除的条数
     *  @return 数据库访问成功返回 true
     */
    bool deleteWords(int64_t enterpriseId, const std::vector<int64_t>& ids, int& deleted);

    /**
     *  批量导入。每行按 ParseImportLine 解析，空行忽略；其余行逐行校验，不合格、重复或已达
     *  上限的行跳过并记下原因。全部写入在一个事务里完成。成功后丢弃该企业的匹配器缓存。
     *  @param enterpriseId    企业ID
     *  @param lines           导入的行（UTF-8，各行不含换行符）
     *  @param defaultCategory 没写分类或分类名不认识的行使用的分类
     *  @param creatorId       添加者管理员ID
     *  @param creatorName     添加者姓名
     *  @param now             当前时间，Unix毫秒
     *  @param added           [出参] 成功添加的条数
     *  @param skipped         [出参] 被跳过的行，调用前会被清空
     *  @return 数据库访问成功返回 true；失败时整批回滚，added 为 0
     */
    bool importWords(int64_t enterpriseId, const std::vector<std::string>& lines, int defaultCategory,
                     int64_t creatorId, const std::string& creatorName, int64_t now,
                     int& added, std::vector<SensitiveImportSkip>& skipped);

    /**
     *  导出某企业的全部词条，按添加先后排列，每行一条（格式见 FormatExportLine），行间用 \n 分隔。
     *  @param enterpriseId 企业ID
     *  @param text         [出参] 导出的文字（UTF-8）；没有词条时为空串
     *  @return 数据库访问成功返回 true
     */
    bool exportWords(int64_t enterpriseId, std::string& text);

    /**
     *  读取某企业的敏感词过滤总开关。
     *  @param enterpriseId 企业ID
     *  @param enabled      [出参] 开关状态
     *  @return 企业存在且数据库访问成功返回 true；企业不存在时返回 false
     */
    bool getEnabled(int64_t enterpriseId, bool& enabled);

    /**
     *  设置某企业的敏感词过滤总开关。成功后丢弃该企业的匹配器缓存。
     *  @param enterpriseId 企业ID
     *  @param enabled      开关状态
     *  @return 企业存在且数据库访问成功返回 true；企业不存在时返回 false
     */
    bool setEnabled(int64_t enterpriseId, bool enabled);

    // ================================================================
    // 检查：会话层用
    // ================================================================

    /**
     *  按某企业的词库逐段检查文字，任意一段命中即返回 true。企业不存在、总开关关闭或词库为空
     *  时返回 false。首次检查某企业时装载其词库并构建匹配器，之后直到下一次写入都复用缓存。
     *  @param enterpriseId 企业ID（取提交者所属企业）
     *  @param texts        待检查的文字（UTF-8），空串跳过
     *  @param result       [出参，可为 NULL] 命中时写入命中结果；未命中时不改动
     *  @return 命中返回 true
     */
    bool checkTexts(int64_t enterpriseId, const std::vector<std::string>& texts, SensitiveCheckResult* result);

    /**
     *  丢弃某企业的匹配器缓存，下次检查时重新装载。各写入方法内部已经调用，删除企业等外部
     *  改动词库的场合也要调用。
     *  @param enterpriseId 企业ID
     */
    void invalidate(int64_t enterpriseId);

    // ================================================================
    // 拦截记录
    // ================================================================

    /**
     *  写一条拦截记录。名称与摘要中的 4 字节字符替换为 U+FFFD，超长部分按字符截掉。
     *  @param record 记录内容（m_id 忽略）
     *  @return 数据库访问成功返回 true
     */
    bool writeHit(const SensitiveHitRecord& record);

    /**
     *  分页查询拦截记录，按拦截时间倒序（最新的在前）。
     *  @param query 查询条件
     *  @param out   [出参] 本页记录，调用前会被清空
     *  @param total [出参] 满足条件的总条数
     *  @return 数据库访问成功返回 true
     */
    bool listHits(const SensitiveHitQuery& query, std::vector<SensitiveHitRecord>& out, int& total);

    /**
     *  删除拦截时间早于 cutoffMs 的全部拦截记录（不分企业）。
     *  @param cutoffMs 截止时间，Unix毫秒；早于它的记录被删除
     *  @param deleted  [出参] 删除的条数
     *  @return 数据库访问成功返回 true
     */
    bool purgeHitsBefore(int64_t cutoffMs, int& deleted);

    /**
     *  删除某企业的全部敏感词数据：词库（t_sensitive_word）与拦截记录（t_sensitive_hit_log），
     *  在一个事务里完成，随后丢弃该企业的匹配器缓存。删除企业（EnterpriseManager::deleteEnterprise）
     *  时调用。该企业没有任何数据时同样返回成功。
     *  @param enterpriseId 企业ID
     *  @return 数据库访问成功返回 true；失败时两张表都保持原样，缓存不受影响
     */
    bool deleteEnterpriseData(int64_t enterpriseId);

private:
    //一个企业的匹配器快照，构建后只读，经共享指针在多个检查线程之间共享。
    struct WordSet
    {
        bool                             m_enabled;   //总开关
        chatserver::SensitiveWordMatcher m_matcher;   //按该企业全部词条构建的匹配器
        std::map<int64_t, std::string>   m_words;     //词条ID -> 原文，命中时取原文写拦截记录

        WordSet()
            : m_enabled(false)
        {
        }
    };

    //取某企业的匹配器快照：缓存里有就直接返回，没有时装载、构建并放入缓存。装载失败返回空指针。
    std::shared_ptr<const WordSet> getWordSet(int64_t enterpriseId);

    std::map<int64_t, std::shared_ptr<const WordSet> > m_cache;        //企业ID -> 匹配器快照；写入时整条丢弃
    //企业ID -> 缓存的代次，每次 invalidate 加一。装载词库要查库、在锁外进行，期间若有写入把缓存
    //丢弃了，装载前后的代次就对不上，这份可能过时的快照便不放入缓存，避免旧词库一直留在缓存里。
    std::map<int64_t, uint64_t>                        m_generation;
    std::mutex                                         m_mutex;        //保护 m_cache 与 m_generation
};
