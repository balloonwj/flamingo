/**
 *  v2 历史消息本地库——SQLite 读写封装实现，V2MsgStore.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "V2MsgStore.h"
#include "HistoryWildcard.h"   //通配符搜索：LIKE 匹配串、全文索引查询串（docs/聊天记录管理器方案.md §11.4）
#include "HistoryTimeline.h"   //BuildLocalMonthRanges：时间轴按月统计时算出各月的本地起止
#include "HistorySearchSyntax.h"   //ParseHistorySearch：左栏跨会话搜索按搜索语法解释关键字
#include "sqlite3.h"
#include "../../net/Protocol.h"   //message_type_voice：LoadUnplayedVoiceIds 只查语音
#include <algorithm>   //QueryLatest：倒序取出后翻成正序
#include <map>         //QueryConversationList：按会话查覆盖区间

namespace balloonhistory {

//建表 SQL：列与 V2MsgQuery 的 kColumns 一致；(ctype,cid,msgid) 主键实现去重。
static const char* const kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS t_msg ("
    "msgid INTEGER NOT NULL,"
    "clientmsgid TEXT,"
    "ctype INTEGER NOT NULL,"
    "cid INTEGER NOT NULL,"
    "fromid INTEGER NOT NULL,"
    "msgtype INTEGER NOT NULL,"
    "content TEXT,"
    "search_text TEXT,"
    "is_link INTEGER NOT NULL DEFAULT 0,"
    "timestamp INTEGER NOT NULL,"
    "recalled INTEGER NOT NULL DEFAULT 0,"
    "pinned INTEGER NOT NULL DEFAULT 0,"
    "pin_time INTEGER NOT NULL DEFAULT 0,"
    "raw TEXT NOT NULL DEFAULT '',"
    "played INTEGER NOT NULL DEFAULT 1,"
    "PRIMARY KEY (ctype,cid,msgid))";

//一张表里的一个列，用于"存量库缺列自动补齐"。
//建表语句是 CREATE TABLE IF NOT EXISTS：库文件已存在时它整条跳过，后加的列<u>永远不会</u>
//出现在存量库里，随后所有带新列的 SQL 全部报 no such column，"消息记录"窗直接空白。
//形态照服务端 sqliteapi/SqliteSchema.h 的 SchemaColumn，但客户端只有一张表，
//内嵌这张小表即可，不必引入生成脚本。
struct SchemaColumn
{
    const char* m_column;     //列名
    const char* m_addClause;  //ALTER TABLE t_msg ADD COLUMN <列名> 之后要跟的定义片段
};

//t_msg 全部需要补齐的列。主键列不列在这里 —— 主键都缺了的库已经没救，补一列也修不回来。
//新增列时在此追加一行，Open 会自动给存量库补上。
static const SchemaColumn kSchemaColumns[] =
{
    { "pinned",   "INTEGER NOT NULL DEFAULT 0" },
    { "pin_time", "INTEGER NOT NULL DEFAULT 0" },
    //整条消息的原始 JSON，打开聊天窗时据此还原消息先行显示（2026-10-01 新增）
    { "raw",      "TEXT NOT NULL DEFAULT ''" },
    //语音消息是否已听（语音消息第 3a 步）：存量库补列时默认 1，加列之前已在库里的历史语音一律算已听
    { "played",   "INTEGER NOT NULL DEFAULT 1" },
};
static const int kSchemaColumnCount = (int)(sizeof(kSchemaColumns) / sizeof(kSchemaColumns[0]));

//按会话 + 时间建索引，支撑翻页与时间区间过滤。
static const char* const kCreateIndexTimeSql =
    "CREATE INDEX IF NOT EXISTS idx_conv_time ON t_msg (ctype,cid,timestamp)";
//按会话 + 类型建索引，支撑类型筛选。
static const char* const kCreateIndexTypeSql =
    "CREATE INDEX IF NOT EXISTS idx_conv_type ON t_msg (ctype,cid,msgtype)";
//按类型 + 时间建索引，支撑主面板全局搜索「文件」标签的跨会话查询（只查文件、文件夹传输记录、
//文件共享卡片三类）。没有它时这条查询要把整张表扫一遍，20 万条记录约 300 毫秒；有了它只扫这
//三类的行，约 40～60 毫秒（据实证：tmp/brand/bench_search.py，2026-10-03）。存量库在下一次
//打开时自动补建。
static const char* const kCreateIndexTypeTimeSql =
    "CREATE INDEX IF NOT EXISTS idx_type_time ON t_msg (msgtype,timestamp)";

//切到 WAL 日志模式。WAL 下读与写互不阻塞：后台线程用另一条连接做跨会话搜索时，界面线程照常
//写库，不必等那次查询结束（2026-10-04 之前共用一条连接，写库要排在查询之后）。日志
//模式记在库文件里，切过一次之后其它连接打开这个库自动就是 WAL。内存库不支持 WAL，这条语句
//对它不起作用，也不报错。
static const char* const kJournalWalSql = "PRAGMA journal_mode=WAL";
//连接遇到库被锁住时最多等多久（毫秒）再返回 SQLITE_BUSY。WAL 下只有写与写、以及检查点之间
//会互相等待，正常情况下等不到这么久。
static const int kBusyTimeoutMs = 3000;

//==================== 全文索引 ====================
//
//t_msg_fts：search_text 列的 FTS5 全文索引。
//  · tokenize='trigram'：按连续三个字切片建索引，支持任意位置的子串匹配（中文没有空格分词，
//    普通分词器搜不到词中间的字）。不区分大小写，范围比 LIKE 宽，查询时仍按 LIKE 复核。
//  · content=''：不另存一份文字，只存索引，库文件增大约为 search_text 本身的两到三倍。
//  · contentless_delete=1：可以按 rowid 删除、用 INSERT OR REPLACE 覆盖（SQLite 3.43 起支持）。
//    普通的无内容表删除时要提供原文，原文与索引对不上会把索引弄坏；这里只需 rowid，回填与触发器
//    重复写同一行也只是覆盖，不会出现重复的索引项。
//t_msg 是普通 rowid 表（主键是三列复合键），rowid 在行的生命周期内不变；本库不执行 VACUUM，
//也没有 REPLACE INTO t_msg 这种会换 rowid 的写法。
static const char* const kCreateFtsSql =
    "CREATE VIRTUAL TABLE IF NOT EXISTS t_msg_fts USING fts5("
    "search_text, content='', contentless_delete=1, tokenize='trigram')";
//回填进度：backfill_next 为下一批的起始 rowid，backfill_done 为 1 表示存量数据已全部进索引。
static const char* const kCreateFtsMetaSql =
    "CREATE TABLE IF NOT EXISTS t_fts_meta (k TEXT PRIMARY KEY, v INTEGER NOT NULL)";
static const char* const kInitFtsNextSql =
    "INSERT OR IGNORE INTO t_fts_meta (k,v) VALUES ('backfill_next',0)";
//第一次建索引时表里没有消息（新库）就不必回填；有消息（存量库）则从头回填
static const char* const kInitFtsDoneSql =
    "INSERT OR IGNORE INTO t_fts_meta (k,v) VALUES "
    "('backfill_done', CASE WHEN EXISTS (SELECT 1 FROM t_msg) THEN 0 ELSE 1 END)";
//三个触发器：新增、删除、改 search_text 时同步索引。
//改 search_text 时显式先删后插，不写 INSERT OR REPLACE：触发器里语句的冲突处理方式会被外层语句的
//覆盖，Upsert 撞主键走 DO UPDATE 时外层是 ABORT，OR REPLACE 不起作用，旧文字的索引项会留下来
//（据实证：单测 FTS4，2026-10-04；SQLite 的 codeTriggerProgram 按 pParse->eOrconf 覆盖）。
static const char* const kCreateFtsInsertTriggerSql =
    "CREATE TRIGGER IF NOT EXISTS t_msg_fts_ins AFTER INSERT ON t_msg BEGIN "
    "INSERT INTO t_msg_fts (rowid,search_text) VALUES (new.rowid,new.search_text); END";
static const char* const kCreateFtsUpdateTriggerSql =
    "CREATE TRIGGER IF NOT EXISTS t_msg_fts_upd AFTER UPDATE OF search_text ON t_msg BEGIN "
    "DELETE FROM t_msg_fts WHERE rowid=old.rowid; "
    "INSERT INTO t_msg_fts (rowid,search_text) VALUES (new.rowid,new.search_text); END";
static const char* const kCreateFtsDeleteTriggerSql =
    "CREATE TRIGGER IF NOT EXISTS t_msg_fts_del AFTER DELETE ON t_msg BEGIN "
    "DELETE FROM t_msg_fts WHERE rowid=old.rowid; END";
//开发期间短暂用过的三个触发器名（写法依赖 OR REPLACE，见上），存在就删掉
static const char* const kDropOldFtsTriggersSql =
    "DROP TRIGGER IF EXISTS t_msg_fts_ai; DROP TRIGGER IF EXISTS t_msg_fts_au; DROP TRIGGER IF EXISTS t_msg_fts_ad";
//进度回调的调用间隔（虚拟机指令条数）。整表扫描每行几十条指令，取这个值时中断请求在一毫秒
//之内生效，回调本身的开销可以忽略。
static const int kProgressHandlerOps = 1000;
static const char* const kReadFtsMetaSql = "SELECT v FROM t_fts_meta WHERE k=?";
static const char* const kWriteFtsMetaSql = "UPDATE t_fts_meta SET v=? WHERE k=?";
//回填一批：rowid 落在 [?, ?) 的行写进索引（已在索引里的覆盖，见上文 contentless_delete）
static const char* const kBackfillFtsSql =
    "INSERT OR REPLACE INTO t_msg_fts (rowid,search_text) "
    "SELECT rowid,search_text FROM t_msg WHERE rowid>=? AND rowid<?";
static const char* const kMaxRowidSql = "SELECT IFNULL(MAX(rowid),0) FROM t_msg";

//Upsert 用的固定语句；占位符顺序与下方绑定一一对应。
//
//这里<u>不能</u>用 INSERT OR REPLACE：那是"整行替换"，撞主键时会把 pinned / pin_time
//一并冲成默认值。同一条消息重复落库是常态（同步回填与实时推送撞上同一条），于是
//"钉住的消息过一会儿自己没了"。改成显式 UPSERT，冲突时只更新内容类字段。
//依赖 SQLite 3.24+ 的 ON CONFLICT 语法；本工程编入的是 3.53.1（见 Flamingo.vcxproj
//里的 sqlite-src-3530100），换回更老的 amalgamation 会让本语句 prepare 失败、
//Upsert 恒返回 false。
static const char* const kUpsertSql =
    "INSERT INTO t_msg "
    "(msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled,raw,played) "
    "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?) "
    //played 只在新插入时写入；撞主键更新时不改，用户听过的状态不会被重新同步覆盖
    "ON CONFLICT(ctype,cid,msgid) DO UPDATE SET "
    "clientmsgid=excluded.clientmsgid,fromid=excluded.fromid,msgtype=excluded.msgtype,"
    "content=excluded.content,search_text=excluded.search_text,is_link=excluded.is_link,"
    "timestamp=excluded.timestamp,recalled=excluded.recalled,"
    //原始 JSON 只在新值非空时覆盖：不带原始 JSON 的写入路径（手工构造的消息等）不能把已存的冲掉
    "raw=CASE WHEN excluded.raw<>'' THEN excluded.raw ELSE raw END";

//MarkVoicePlayed 用：把一条语音消息标为已听，按主键定位
static const char* const kMarkVoicePlayedSql =
    "UPDATE t_msg SET played=1 WHERE ctype=? AND cid=? AND msgid=?";

//LoadUnplayedVoiceIds 用：一个会话里未听、未撤回的语音消息的 msgid（走主键索引的会话前缀）
static const char* const kLoadUnplayedVoiceSql =
    "SELECT msgid FROM t_msg WHERE ctype=? AND cid=? AND msgtype=? AND played=0 AND recalled=0 ORDER BY msgid";

//LoadPlayedVoiceIds 用：按主键取一条消息的 played，一批 msgid 逐个复用同一条语句
static const char* const kLoadPlayedOneSql =
    "SELECT played FROM t_msg WHERE ctype=? AND cid=? AND msgid=?";

//MarkRecalled 用的固定 UPDATE 语句；WHERE 按主键 (ctype,cid,msgid) 精确定位一行，
//保证不同会话里出现相同 msgid 时互不影响。占位符顺序与下方绑定一一对应。
static const char* const kMarkRecalledSql =
    "UPDATE t_msg SET recalled=1 WHERE ctype=? AND cid=? AND msgid=?";

//SetPinned 用的固定 UPDATE 语句；定位方式与 kMarkRecalledSql 同理。
static const char* const kSetPinnedSql =
    "UPDATE t_msg SET pinned=?, pin_time=? WHERE ctype=? AND cid=? AND msgid=?";

//ReplacePinnedSet 第一步：把该会话中 timestamp 不早于给定下界的行 pinned 清零。
//下界的意义见 CV2MsgStore::ReplacePinnedSet 的注释。
static const char* const kClearPinnedSql =
    "UPDATE t_msg SET pinned=0, pin_time=0 WHERE ctype=? AND cid=? AND timestamp>=?";

//覆盖区间表：每个会话一行，记"服务端上 msgid 落在 [lo_msgid, hi_msgid] 里的消息本地一条不缺"，
//reached_oldest 为 1 表示已拉到服务端最早一条。规则见 HistoryCoverage.h。
static const char* const kCreateCoverTableSql =
    "CREATE TABLE IF NOT EXISTS t_cover ("
    "ctype INTEGER NOT NULL,"
    "cid INTEGER NOT NULL,"
    "lo_msgid INTEGER NOT NULL,"
    "hi_msgid INTEGER NOT NULL,"
    "reached_oldest INTEGER NOT NULL DEFAULT 0,"
    "PRIMARY KEY (ctype,cid))";

//群文件信息缓存表（docs/聊天记录管理器方案.md §11.5）：本人所在各群的群文件列表的本地副本，由主面板用
//cmd_get_group_files 按群拉齐、按 cmd_group_files_changed_notify 增量更新，聊天记录管理器的文件页据此把
//群文件与聊天里的文件汇总在一起。local_path 是本机下载后的落地路径，只由本机写入，拉齐时保留。
//(groupid, entryid) 主键：entryid 本身全局唯一，带上 groupid 便于按群整体替换与删除
static const char* const kCreateGroupFileCacheSql =
    "CREATE TABLE IF NOT EXISTS t_group_file_cache ("
    "groupid INTEGER NOT NULL,"
    "entryid TEXT NOT NULL,"
    "fileid TEXT NOT NULL DEFAULT '',"
    "name TEXT NOT NULL DEFAULT '',"
    "ext TEXT NOT NULL DEFAULT '',"
    "size INTEGER NOT NULL DEFAULT 0,"
    "md5 TEXT NOT NULL DEFAULT '',"
    "filetype INTEGER NOT NULL DEFAULT 0,"
    "uploaderid INTEGER NOT NULL DEFAULT 0,"
    "uploadername TEXT NOT NULL DEFAULT '',"
    "uploadtime INTEGER NOT NULL DEFAULT 0,"
    "local_path TEXT NOT NULL DEFAULT '',"
    "PRIMARY KEY (groupid,entryid))";
//撤回快照表（docs/撤回后重新编辑方案.md §3.1）：本人撤回一条文字类消息的那一刻，把原文存成「重新编辑」用的
//快照，撤回提示行据此在 5 分钟内显示「编辑」。与 t_msg 分开存：本人刚发出的单聊消息在 t_msg 里可能还没有这一行，
//而且同步历史时的 upsert 会改写 t_msg。表里通常只有几行，过期的在写入时与打开库时删除。
static const char* const kCreateRecallDraftSql =
    "CREATE TABLE IF NOT EXISTS t_recall_draft ("
    "ctype INTEGER NOT NULL,"
    "cid INTEGER NOT NULL,"
    "msgid INTEGER NOT NULL,"
    "recall_time INTEGER NOT NULL,"
    "payload TEXT NOT NULL,"
    "PRIMARY KEY (ctype,cid,msgid))";
//同步进度表（docs/消息改写离线同步方案.md 3.3）：按键存同步用的整数进度，目前只有 rewritten_rev
//（已同步到的改写序号）。不放进 t_fts_meta：那张表建在全文索引的初始化事务里，SQLite 不带 FTS5 时会整组回滚，
//表就不存在了
static const char* const kCreateSyncMetaSql =
    "CREATE TABLE IF NOT EXISTS t_sync_meta (k TEXT PRIMARY KEY, v INTEGER NOT NULL)";
//ReadSyncValue 用
static const char* const kReadSyncMetaSql = "SELECT v FROM t_sync_meta WHERE k=?";
//WriteSyncValue 用：已有时整项覆盖
static const char* const kWriteSyncMetaSql = "INSERT OR REPLACE INTO t_sync_meta (k,v) VALUES (?,?)";
//HasAnyMessage 用：有一行就够，不数总数
static const char* const kAnyMessageSql = "SELECT 1 FROM t_msg LIMIT 1";
//SaveRecallDraft 用：同一主键已有时整行覆盖
static const char* const kSaveRecallDraftSql =
    "INSERT OR REPLACE INTO t_recall_draft (ctype,cid,msgid,recall_time,payload) VALUES (?,?,?,?,?)";
//QueryRecallDrafts 用：按主键逐个取，一批 msgid 复用同一条语句
static const char* const kQueryRecallDraftSql =
    "SELECT recall_time,payload FROM t_recall_draft WHERE ctype=? AND cid=? AND msgid=?";
//PurgeRecallDrafts 用：删掉撤回时间早于给定时刻的行
static const char* const kPurgeRecallDraftSql =
    "DELETE FROM t_recall_draft WHERE recall_time<?";

//按登记时间建索引，文件页按时间倒序分页
static const char* const kCreateGroupFileCacheTimeIndexSql =
    "CREATE INDEX IF NOT EXISTS idx_gfc_time ON t_group_file_cache (uploadtime)";
//写入或更新一个群文件，不改动本机落地路径
static const char* const kUpsertGroupFileSql =
    "INSERT INTO t_group_file_cache (groupid,entryid,fileid,name,ext,size,md5,filetype,uploaderid,"
    "uploadername,uploadtime) VALUES (?,?,?,?,?,?,?,?,?,?,?) "
    "ON CONFLICT(groupid,entryid) DO UPDATE SET fileid=excluded.fileid,name=excluded.name,"
    "ext=excluded.ext,size=excluded.size,md5=excluded.md5,filetype=excluded.filetype,"
    "uploaderid=excluded.uploaderid,uploadername=excluded.uploadername,uploadtime=excluded.uploadtime";
//取某群缓存里的全部条目 ID（整体替换时据此找出服务端已经没有的条目）
static const char* const kGroupFileEntryIdsSql =
    "SELECT entryid FROM t_group_file_cache WHERE groupid=?";
//删除一个群文件、删除一个群的全部群文件
static const char* const kDeleteGroupFileSql =
    "DELETE FROM t_group_file_cache WHERE groupid=? AND entryid=?";
static const char* const kDeleteGroupFilesOfGroupSql =
    "DELETE FROM t_group_file_cache WHERE groupid=?";
//登记本机落地路径
static const char* const kSetGroupFileLocalPathSql =
    "UPDATE t_group_file_cache SET local_path=? WHERE groupid=? AND entryid=?";
//按主键取一个群文件；列序与 ReadGroupFileRow 一致
static const char* const kQueryGroupFileSql =
    "SELECT groupid,entryid,fileid,name,ext,size,md5,filetype,uploaderid,uploadername,uploadtime,local_path "
    "FROM t_group_file_cache WHERE groupid=? AND entryid=?";
//取某群的全部群文件，按登记时间倒序；列序同上
static const char* const kQueryGroupFilesOfGroupSql =
    "SELECT groupid,entryid,fileid,name,ext,size,md5,filetype,uploaderid,uploadername,uploadtime,local_path "
    "FROM t_group_file_cache WHERE groupid=? ORDER BY uploadtime DESC, entryid DESC";

//覆盖区间的读、写、删与全部清空
static const char* const kGetCoverSql =
    "SELECT lo_msgid, hi_msgid, reached_oldest FROM t_cover WHERE ctype=? AND cid=?";
static const char* const kSetCoverSql =
    "INSERT INTO t_cover(ctype,cid,lo_msgid,hi_msgid,reached_oldest) VALUES(?,?,?,?,?) "
    "ON CONFLICT(ctype,cid) DO UPDATE SET lo_msgid=excluded.lo_msgid, "
    "hi_msgid=excluded.hi_msgid, reached_oldest=excluded.reached_oldest";
static const char* const kDeleteCoverSql =
    "DELETE FROM t_cover WHERE ctype=? AND cid=?";
static const char* const kClearAllCoverSql =
    "DELETE FROM t_cover";

//对账用：取某会话里 msgid 落在 [?, ?] 内的全部 msgid
static const char* const kQueryMsgidsInRangeSql =
    "SELECT msgid FROM t_msg WHERE ctype=? AND cid=? AND msgid>=? AND msgid<=?";

//取某会话里 msgid 不小于 ? 的消息中最早的时间（判断覆盖区间是否已盖住时间筛选的下界）
static const char* const kQueryOldestTimestampFromSql =
    "SELECT MIN(timestamp) FROM t_msg WHERE ctype=? AND cid=? AND msgid>=?";

//QuerySenders 用：某会话里发过言的人，按各自最近一条消息的时间倒序；同一时间再按 userid
//升序，保证顺序稳定。走索引 idx_conv_time (ctype,cid,timestamp)
static const char* const kQuerySendersSql =
    "SELECT fromid FROM t_msg WHERE ctype=? AND cid=? AND recalled=0 AND msgid>=? AND fromid>0 "
    "GROUP BY fromid ORDER BY MAX(timestamp) DESC, fromid ASC";

//QueryAllSenders 用：所有会话里发过言的人（不看覆盖区间：只用来把 from:名字 换成 userid，多几个人不影响结果）
static const char* const kQueryAllSendersSql =
    "SELECT DISTINCT fromid FROM t_msg WHERE recalled=0 AND fromid>0 ORDER BY fromid";

//QueryCacheRefs 用：图片（2）、截图（9）、文件（3）、语音（4）消息的会话、类型与内容；按 msgtype 走 idx_type_time
static const char* const kQueryCacheRefsSql =
    "SELECT ctype,cid,msgtype,content FROM t_msg WHERE msgtype IN (2,3,4,9) AND recalled=0";

//QueryLatest 用：按 msgid 倒序取某会话最近的若干条（含已撤回的），走主键 (ctype,cid,msgid)
static const char* const kQueryLatestSql =
    "SELECT msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,"
    "recalled,pinned,pin_time,raw FROM t_msg WHERE ctype=? AND cid=? ORDER BY msgid DESC LIMIT ?";

//QueryRecord 用：按主键取一行完整记录，列序与 kQueryLatestSql 相同，读取共用 ReadRecordRow
static const char* const kQueryRecordSql =
    "SELECT msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,"
    "recalled,pinned,pin_time,raw FROM t_msg WHERE ctype=? AND cid=? AND msgid=?";

//UpdateContent 用：按主键只改 content 列
static const char* const kUpdateContentSql =
    "UPDATE t_msg SET content=? WHERE ctype=? AND cid=? AND msgid=?";

//DeleteMessages 用：按主键删一条。主键含会话，别的会话里 msgid 相同的行不受影响。
static const char* const kDeleteMsgSql =
    "DELETE FROM t_msg WHERE ctype=? AND cid=? AND msgid=?";

//DeleteConversationUpTo 用：删除某会话里 msgid 不大于锚点的消息，走 idx_conv_time 索引。
static const char* const kDeleteConversationMsgSql =
    "DELETE FROM t_msg WHERE ctype=? AND cid=? AND msgid<=?";

//DeleteByTimeRangeBatch 用：删除全部会话里时间落在 [?, ?] 内的至多 ? 行。先按 rowid 取一批
//再删，而不是直接 DELETE ... LIMIT —— 后者要求 SQLite 以 SQLITE_ENABLE_UPDATE_DELETE_LIMIT
//编译，第三方 amalgamation 默认不开。
static const char* const kDeleteRangeBatchSql =
    "DELETE FROM t_msg WHERE rowid IN "
    "(SELECT rowid FROM t_msg WHERE timestamp>=? AND timestamp<=? LIMIT ?)";

//给三个跨会话搜索语句绑定关键字。走全文索引时第一个占位是 MATCH 查询串、第二个是 LIKE 匹配串；
//不走时只有一个 LIKE 占位（语句形式见 V2MsgQuery.h 的 BuildSearchConversationsSql）。
//wildcard 为 true 时关键字按通配符解释：LIKE 匹配串由 MakeWildcardLikePattern 生成，MATCH 查询串是
//BuildWildcardFtsQuery 切出的各段（调用方只在 UseFullTextLocked 判定可用时才传 fullText=true）。
static void BindSearchKeyword(sqlite3_stmt* stmt, const std::string& keywordUtf8, bool fullText,
                              bool wildcard = false)
{
    const std::string like = MakeSearchLikePattern(keywordUtf8, wildcard);
    if (fullText)
    {
        std::string match;
        if (wildcard)
        {
            BuildWildcardFtsQuery(keywordUtf8, match);
        }
        else
        {
            match = MakeFtsPhrase(keywordUtf8);
        }
        sqlite3_bind_text(stmt, 1, match.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, like.c_str(), -1, SQLITE_TRANSIENT);
    }
    else
    {
        sqlite3_bind_text(stmt, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    }
}

CV2MsgStore::CV2MsgStore()
    : m_db(NULL)
    , m_ftsAvailable(false)
    , m_ftsReady(false)
    , m_ftsAllowed(true)
    , m_interruptRequested(false)
{
}

CV2MsgStore::~CV2MsgStore()
{
    Close();
}

bool CV2MsgStore::Open(const wchar_t* dbPathUtf16)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db != NULL)
    {
        return true;
    }
    if (dbPathUtf16 == NULL)
    {
        return false;
    }
    sqlite3* db = NULL;
    int rc = sqlite3_open16(dbPathUtf16, &db);
    if (rc != SQLITE_OK)
    {
        if (db != NULL)
        {
            sqlite3_close(db);
        }
        return false;
    }
    m_db = db;
    sqlite3_busy_timeout(m_db, kBusyTimeoutMs);
    sqlite3_progress_handler(m_db, kProgressHandlerOps, &CV2MsgStore::OnProgress, this);
    //切换失败（库只读等）时仍按原来的日志模式工作，只是读写会互相等待，不据此判打开失败
    ExecLocked(kJournalWalSql);
    //建表与索引；建表失败则回退到未打开态，避免半初始化。
    if (!ExecLocked(kCreateTableSql))
    {
        sqlite3_close(m_db);
        m_db = NULL;
        return false;
    }

    //顺序不能反：先建表、再补列、最后建索引。结构落后的存量库里，索引要用到的列
    //可能还没补上，先建索引会直接报 no such column。
    EnsureColumnsLocked();

    ExecLocked(kCreateIndexTimeSql);
    ExecLocked(kCreateIndexTypeSql);
    ExecLocked(kCreateIndexTypeTimeSql);

    //覆盖区间表建不出来时不影响消息本身的读写，只是消息记录窗退化成"等回填到达才显示"，
    //故不据此判打开失败
    ExecLocked(kCreateCoverTableSql);

    //群文件信息缓存表建不出来时文件页只列聊天里的文件，同样不据此判打开失败
    ExecLocked(kCreateGroupFileCacheSql);
    ExecLocked(kCreateGroupFileCacheTimeIndexSql);

    //撤回快照表建不出来时撤回提示行只是不显示「编辑」，同样不据此判打开失败
    ExecLocked(kCreateRecallDraftSql);

    //同步进度表建不出来时按改写序号同步每次都从头取（读进度失败按 0 处理），同样不据此判打开失败
    ExecLocked(kCreateSyncMetaSql);

    //全文索引建不出来时跨会话搜索退回 LIKE，同样不据此判打开失败
    InitFullTextLocked();
    m_dbPath = dbPathUtf16;
    return true;
}

void CV2MsgStore::InitFullTextLocked()
{
    m_ftsAvailable = false;
    m_ftsReady = false;
    if (m_db == NULL || !ExecLocked("BEGIN IMMEDIATE"))
    {
        return;
    }
    const bool ok = ExecLocked(kCreateFtsSql)
                 && ExecLocked(kDropOldFtsTriggersSql)
                 && ExecLocked(kCreateFtsMetaSql)
                 && ExecLocked(kInitFtsNextSql)
                 && ExecLocked(kInitFtsDoneSql)
                 && ExecLocked(kCreateFtsInsertTriggerSql)
                 && ExecLocked(kCreateFtsUpdateTriggerSql)
                 && ExecLocked(kCreateFtsDeleteTriggerSql);
    if (!ok || !ExecLocked("COMMIT"))
    {
        ExecLocked("ROLLBACK");
        return;
    }
    m_ftsAvailable = true;
    int64_t done = 0;
    m_ftsReady = ReadFtsMetaLocked("backfill_done", done) && done != 0;
}

bool CV2MsgStore::ReadFtsMetaLocked(const char* key, int64_t& out)
{
    out = 0;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kReadFtsMetaSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out = sqlite3_column_int64(stmt, 0);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

bool CV2MsgStore::UseFullTextLocked(const std::string& keywordUtf8, bool wildcard) const
{
    if (!m_ftsAvailable || !m_ftsReady || !m_ftsAllowed)
    {
        return false;
    }
    //通配符：切出的段里至少有一段不少于三个字，才有可交给全文索引的查询串
    if (wildcard)
    {
        std::string query;
        return BuildWildcardFtsQuery(keywordUtf8, query);
    }
    return CanUseTrigramSearch(keywordUtf8);
}

//会话内的查询（Count、CountBefore、QueryPage 与时间轴的几项统计）绑定关键字：关键字筛选生效时（HasKeywordFilter），
//BuildWhereClause 按搜索语法为每个正向词、排除词各拼一个 LIKE / NOT LIKE 占位（BuildSearchSyntaxClause），这里
//按同一顺序依次绑定（SearchSyntaxLikeBinds）；不生效时语句里没有占位，什么也不绑定。
static void BindFilterKeyword(sqlite3_stmt* stmt, const HistoryFilter& filter)
{
    if (!HasKeywordFilter(filter))
    {
        return;
    }
    const std::vector<std::string> binds = SearchSyntaxLikeBinds(filter.m_keyword, filter.m_wildcard);
    for (size_t i = 0; i < binds.size(); ++i)
    {
        sqlite3_bind_text(stmt, (int)i + 1, binds[i].c_str(), -1, SQLITE_TRANSIENT);
    }
}

bool CV2MsgStore::IsFullTextReady()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_db != NULL && m_ftsAvailable && m_ftsReady;
}

void CV2MsgStore::SetFullTextAllowed(bool allowed)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_ftsAllowed = allowed;
}

int CV2MsgStore::BackfillFullTextStep(int maxRows)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL || !m_ftsAvailable || maxRows <= 0)
    {
        return -1;
    }
    if (m_ftsReady)
    {
        return 0;
    }
    if (!ExecLocked("BEGIN IMMEDIATE"))
    {
        return -1;
    }

    //进度在事务里读：另一条连接（另一个实例）同时回填时，各自读到的都是提交后的最新值
    int64_t next = 0;
    bool ok = ReadFtsMetaLocked("backfill_next", next);
    const int64_t end = next + maxRows;
    int inserted = 0;
    if (ok)
    {
        sqlite3_stmt* stmt = NULL;
        ok = (sqlite3_prepare_v2(m_db, kBackfillFtsSql, -1, &stmt, NULL) == SQLITE_OK);
        if (ok)
        {
            sqlite3_bind_int64(stmt, 1, next);
            sqlite3_bind_int64(stmt, 2, end);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            inserted = sqlite3_changes(m_db);
        }
        sqlite3_finalize(stmt);
    }

    int64_t maxRowid = 0;
    if (ok)
    {
        sqlite3_stmt* stmt = NULL;
        ok = (sqlite3_prepare_v2(m_db, kMaxRowidSql, -1, &stmt, NULL) == SQLITE_OK)
          && (sqlite3_step(stmt) == SQLITE_ROW);
        if (ok)
        {
            maxRowid = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    //下一批从 end 开始；end 已经超过当前最大 rowid，说明存量数据都进了索引（之后新增的行由触发器负责）
    const bool done = ok && end > maxRowid;
    if (ok)
    {
        sqlite3_stmt* stmt = NULL;
        ok = (sqlite3_prepare_v2(m_db, kWriteFtsMetaSql, -1, &stmt, NULL) == SQLITE_OK);
        if (ok)
        {
            sqlite3_bind_int64(stmt, 1, end);
            sqlite3_bind_text(stmt, 2, "backfill_next", -1, SQLITE_STATIC);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_reset(stmt);
            if (ok && done)
            {
                sqlite3_bind_int64(stmt, 1, 1);
                sqlite3_bind_text(stmt, 2, "backfill_done", -1, SQLITE_STATIC);
                ok = (sqlite3_step(stmt) == SQLITE_DONE);
            }
        }
        sqlite3_finalize(stmt);
    }

    if (!ok || !ExecLocked("COMMIT"))
    {
        ExecLocked("ROLLBACK");
        return -1;
    }
    if (done)
    {
        m_ftsReady = true;
    }
    return inserted;
}

void CV2MsgStore::EnsureColumnsLocked()
{
    if (m_db == NULL)
    {
        return;
    }

    //先把现有列名读出来。用 PRAGMA table_info 而不是"直接 ALTER、失败就当已存在"：
    //后者会把真正的失败（磁盘只读、库损坏）也一并吞掉，出问题时无从分辨。
    std::vector<std::string> existing;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(t_msg)", -1, &stmt, NULL) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            //PRAGMA table_info 的结果集列序：0 cid(序号) 1 name 2 type 3 notnull 4 dflt_value 5 pk
            const unsigned char* name = sqlite3_column_text(stmt, 1);
            if (name != NULL)
            {
                existing.push_back((const char*)name);
            }
        }
    }
    sqlite3_finalize(stmt);

    //读不到任何列说明这张表压根没建成，此时 ALTER 只会连着报错，直接返回。
    if (existing.empty())
    {
        return;
    }

    for (int i = 0; i < kSchemaColumnCount; ++i)
    {
        bool found = false;
        for (size_t j = 0; j < existing.size(); ++j)
        {
            if (existing[j] == kSchemaColumns[i].m_column)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            continue;
        }

        std::string sql = "ALTER TABLE t_msg ADD COLUMN ";
        sql += kSchemaColumns[i].m_column;
        sql += " ";
        sql += kSchemaColumns[i].m_addClause;
        ExecLocked(sql.c_str());
    }
}

void CV2MsgStore::Close()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db != NULL)
    {
        sqlite3_close(m_db);
        m_db = NULL;
    }
    m_dbPath.clear();
    m_ftsAvailable = false;
    m_ftsReady = false;
}

std::wstring CV2MsgStore::DbPath()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_dbPath;
}

void CV2MsgStore::Interrupt()
{
    //不加锁，理由见头文件。先置标志：sqlite3_interrupt 只对此刻正在执行的语句有效，语句还没
    //开始时靠进度回调看到这个标志
    m_interruptRequested.store(true);
    sqlite3* db = m_db;
    if (db != NULL)
    {
        sqlite3_interrupt(db);
    }
}

void CV2MsgStore::ClearInterrupt()
{
    m_interruptRequested.store(false);
}

int CV2MsgStore::OnProgress(void* self)
{
    const CV2MsgStore* store = static_cast<const CV2MsgStore*>(self);
    return (store != NULL && store->m_interruptRequested.load()) ? 1 : 0;
}

bool CV2MsgStore::IsOpen() const
{
    //仅读取指针做“是否需要 Open”的粗判，不加锁可接受。
    return m_db != NULL;
}

bool CV2MsgStore::ExecLocked(const char* sql)
{
    if (m_db == NULL || sql == NULL)
    {
        return false;
    }
    char* err = NULL;
    int rc = sqlite3_exec(m_db, sql, NULL, NULL, &err);
    if (err != NULL)
    {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

bool CV2MsgStore::Upsert(const HistoryRecord& rec)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kUpsertSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    //文本字段用 SQLITE_TRANSIENT：让 SQLite 立即拷贝，避免依赖临时串生命周期。
    sqlite3_bind_int64(stmt, 1, rec.m_msgid);
    sqlite3_bind_text(stmt, 2, rec.m_clientmsgid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, rec.m_ctype);
    sqlite3_bind_int64(stmt, 4, rec.m_cid);
    sqlite3_bind_int64(stmt, 5, rec.m_fromid);
    sqlite3_bind_int(stmt, 6, rec.m_msgtype);
    sqlite3_bind_text(stmt, 7, rec.m_content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, rec.m_searchText.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, rec.m_isLink ? 1 : 0);
    sqlite3_bind_int64(stmt, 10, rec.m_timestamp);
    sqlite3_bind_int(stmt, 11, rec.m_recalled ? 1 : 0);
    sqlite3_bind_text(stmt, 12, rec.m_raw.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 13, rec.m_played ? 1 : 0);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::MarkVoicePlayed(int ctype, int64_t cid, int64_t msgid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL || msgid <= 0)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kMarkVoicePlayedSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int64(stmt, 3, msgid);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::LoadUnplayedVoiceIds(int ctype, int64_t cid, std::vector<int64_t>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kLoadUnplayedVoiceSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int(stmt, 3, ::message_type_voice);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        out.push_back(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::LoadPlayedVoiceIds(int ctype, int64_t cid, const std::vector<int64_t>& candidates,
                                     std::set<int64_t>& played)
{
    played.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    if (candidates.empty())
    {
        return true;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kLoadPlayedOneSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, ctype);
        sqlite3_bind_int64(stmt, 2, cid);
        sqlite3_bind_int64(stmt, 3, candidates[i]);
        const int rc = sqlite3_step(stmt);
        //查到这一行并且已听才算；没有这一行（还没入库）返回 SQLITE_DONE，调用方按未听处理
        if (rc == SQLITE_ROW)
        {
            if (sqlite3_column_int(stmt, 0) != 0)
            {
                played.insert(candidates[i]);
            }
        }
        else if (rc != SQLITE_DONE)
        {
            ok = false;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool CV2MsgStore::UpsertBatch(const std::vector<HistoryRecord>& recs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    if (recs.empty())
    {
        return true;
    }

    if (!ExecLocked("BEGIN IMMEDIATE"))
    {
        return false;
    }

    //语句只准备一次，逐条绑定参数、执行、复位后复用，省掉每条都重新解析 SQL 的开销。
    sqlite3_stmt* stmt = NULL;
    bool ok = (sqlite3_prepare_v2(m_db, kUpsertSql, -1, &stmt, NULL) == SQLITE_OK);
    for (size_t i = 0; ok && i < recs.size(); ++i)
    {
        const HistoryRecord& rec = recs[i];
        //服务器尚未分配 msgid 的消息没有可去重的主键，与调用方逐条写入时的做法一致，跳过
        if (rec.m_msgid <= 0)
        {
            continue;
        }
        //字段绑定顺序与 Upsert 完全一致，两处共用同一条 kUpsertSql。
        sqlite3_bind_int64(stmt, 1, rec.m_msgid);
        sqlite3_bind_text(stmt, 2, rec.m_clientmsgid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, rec.m_ctype);
        sqlite3_bind_int64(stmt, 4, rec.m_cid);
        sqlite3_bind_int64(stmt, 5, rec.m_fromid);
        sqlite3_bind_int(stmt, 6, rec.m_msgtype);
        sqlite3_bind_text(stmt, 7, rec.m_content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, rec.m_searchText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 9, rec.m_isLink ? 1 : 0);
        sqlite3_bind_int64(stmt, 10, rec.m_timestamp);
        sqlite3_bind_int(stmt, 11, rec.m_recalled ? 1 : 0);
        sqlite3_bind_text(stmt, 12, rec.m_raw.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 13, rec.m_played ? 1 : 0);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);

    if (!ok)
    {
        ExecLocked("ROLLBACK");
        return false;
    }
    return ExecLocked("COMMIT");
}

//把 kQueryLatestSql / kQueryRecordSql 取到的当前行读成一条记录。列序与两条 SELECT 一一对应，
//改列时两条语句与本函数要一起改
static void ReadRecordRow(sqlite3_stmt* stmt, HistoryRecord& rec)
{
    rec.m_msgid = sqlite3_column_int64(stmt, 0);
    const unsigned char* clientmsgid = sqlite3_column_text(stmt, 1);
    rec.m_clientmsgid = (clientmsgid != NULL) ? (const char*)clientmsgid : "";
    rec.m_ctype = sqlite3_column_int(stmt, 2);
    rec.m_cid = sqlite3_column_int64(stmt, 3);
    rec.m_fromid = sqlite3_column_int64(stmt, 4);
    rec.m_msgtype = sqlite3_column_int(stmt, 5);
    const unsigned char* content = sqlite3_column_text(stmt, 6);
    rec.m_content = (content != NULL) ? (const char*)content : "";
    const unsigned char* searchText = sqlite3_column_text(stmt, 7);
    rec.m_searchText = (searchText != NULL) ? (const char*)searchText : "";
    rec.m_isLink = (sqlite3_column_int(stmt, 8) != 0);
    rec.m_timestamp = sqlite3_column_int64(stmt, 9);
    rec.m_recalled = (sqlite3_column_int(stmt, 10) != 0);
    rec.m_pinned = (sqlite3_column_int(stmt, 11) != 0);
    rec.m_pinTime = sqlite3_column_int64(stmt, 12);
    const unsigned char* rawJson = sqlite3_column_text(stmt, 13);
    rec.m_raw = (rawJson != NULL) ? (const char*)rawJson : "";
}

bool CV2MsgStore::QueryRecord(int ctype, int64_t cid, int64_t msgid, HistoryRecord& out, bool& found)
{
    found = false;
    if (msgid <= 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryRecordSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int64(stmt, 3, msgid);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        ReadRecordRow(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool CV2MsgStore::UpdateContent(int ctype, int64_t cid, int64_t msgid, const std::string& contentJson,
                                bool& outHit)
{
    outHit = false;
    if (msgid <= 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kUpdateContentSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text(stmt, 1, contentJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, ctype);
    sqlite3_bind_int64(stmt, 3, cid);
    sqlite3_bind_int64(stmt, 4, msgid);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        return false;
    }

    //命中 0 行不算失败：本机没同步过这张卡片属正常，下次同步会带回合并后的最新内容
    outHit = (sqlite3_changes(m_db) > 0);
    return true;
}

bool CV2MsgStore::QueryLatest(int ctype, int64_t cid, int limit, std::vector<HistoryRecord>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL || limit <= 0)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryLatestSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int(stmt, 3, limit);

    //列序与 kQueryLatestSql 的 SELECT 一一对应，读取见 ReadRecordRow
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        HistoryRecord rec;
        ReadRecordRow(stmt, rec);
        out.push_back(rec);
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        out.clear();
        return false;
    }

    //查询按 msgid 倒序取最近的若干条，返回前翻成正序，与服务端同步应答的顺序一致
    std::reverse(out.begin(), out.end());
    return true;
}

bool CV2MsgStore::MarkRecalled(int ctype, int64_t cid, int64_t msgid)
{
    //msgid 非正说明服务器还没给这条消息分配 ID，本地也不可能按主键存着它。
    if (msgid <= 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kMarkRecalledSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int64(stmt, 3, msgid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    //UPDATE 命中 0 行也算成功：本端可能压根没同步过这条历史消息（比如在别的设备上
    //发出、本机从未拉取过），这属于正常情况，不应让调用方当成失败去重试。
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::SetPinned(int ctype, int64_t cid, int64_t msgid, bool pinned,
                            int64_t pinTimeMs, bool& outHit)
{
    outHit = false;

    //msgid 非正说明服务器还没给这条消息分配 ID，本地也不可能按主键存着它。
    if (msgid <= 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kSetPinnedSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    //取消 Pin 时把 pin_time 一并归零，免得留下"没钉住却有 Pin 时间"这种半截状态，
    //日后按 pin_time 排序或排查时会被它误导。
    sqlite3_bind_int(stmt, 1, pinned ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, pinned ? pinTimeMs : 0);
    sqlite3_bind_int(stmt, 3, ctype);
    sqlite3_bind_int64(stmt, 4, cid);
    sqlite3_bind_int64(stmt, 5, msgid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        return false;
    }

    //命中 0 行不算失败（理由同 MarkRecalled），但要如实告诉调用方 —— 与撤回不同，
    //Pin 的这一情形<u>需要后续动作</u>：本地没有这条消息就意味着它进不了 Pin 分类，
    //调用方据此向服务端补拉一次消息体。
    outHit = (sqlite3_changes(m_db) > 0);
    return true;
}

bool CV2MsgStore::ReplacePinnedSet(int ctype, int64_t cid, const std::vector<PinnedItem>& items,
                                   int64_t minTimestampMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    //整体对齐必须是原子的：清零与置位之间若被读到，界面上会闪一下"所有 Pin 都没了"。
    if (!ExecLocked("BEGIN IMMEDIATE"))
    {
        return false;
    }

    bool ok = true;

    //第一步：把该会话中不早于下界的行 pinned 清零。下界的由来见头文件注释 ——
    //服务端应答被截断时，更老的那些 Pin 不在 msgids 里，全量清零会把它们错杀。
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kClearPinnedSql, -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, ctype);
        sqlite3_bind_int64(stmt, 2, cid);
        sqlite3_bind_int64(stmt, 3, minTimestampMs);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
    }
    else
    {
        ok = false;
    }
    sqlite3_finalize(stmt);

    //第二步：把本次应置为已 Pin 的各条按其 Pin 时间置位。
    //时间必须一起写：上一步清零时把 pin_time 也归了零（不留"没钉住却有 Pin 时间"的
    //半截状态），这里不写回去的话，对账一次就把所有 Pin 时间抹平了。
    for (size_t i = 0; ok && i < items.size(); ++i)
    {
        if (items[i].m_msgid <= 0)
        {
            continue;
        }

        sqlite3_stmt* setStmt = NULL;
        if (sqlite3_prepare_v2(m_db, kSetPinnedSql, -1, &setStmt, NULL) != SQLITE_OK)
        {
            ok = false;
            sqlite3_finalize(setStmt);
            break;
        }
        sqlite3_bind_int(setStmt, 1, 1);
        sqlite3_bind_int64(setStmt, 2, items[i].m_pinTime);
        sqlite3_bind_int(setStmt, 3, ctype);
        sqlite3_bind_int64(setStmt, 4, cid);
        sqlite3_bind_int64(setStmt, 5, items[i].m_msgid);
        ok = (sqlite3_step(setStmt) == SQLITE_DONE);
        sqlite3_finalize(setStmt);
    }

    if (!ok)
    {
        ExecLocked("ROLLBACK");
        return false;
    }
    return ExecLocked("COMMIT");
}

bool CV2MsgStore::DeleteMessages(int ctype, int64_t cid, const std::vector<int64_t>& msgids,
                                 int& outDeleted)
{
    outDeleted = 0;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    //整批放在一个事务里：逐条各自提交时，每条都要单独落一次盘
    if (!ExecLocked("BEGIN IMMEDIATE"))
    {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; ok && i < msgids.size(); ++i)
    {
        //msgid 非正说明服务器还没给这条消息分配 ID，本地不可能按主键存着它
        if (msgids[i] <= 0)
        {
            continue;
        }

        sqlite3_stmt* stmt = NULL;
        if (sqlite3_prepare_v2(m_db, kDeleteMsgSql, -1, &stmt, NULL) != SQLITE_OK)
        {
            ok = false;
            sqlite3_finalize(stmt);
            break;
        }
        sqlite3_bind_int(stmt, 1, ctype);
        sqlite3_bind_int64(stmt, 2, cid);
        sqlite3_bind_int64(stmt, 3, msgids[i]);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (ok)
        {
            //本地没有这一行时删 0 行，不算失败，只是不计入
            outDeleted += sqlite3_changes(m_db);
        }
    }

    if (!ok)
    {
        ExecLocked("ROLLBACK");
        outDeleted = 0;
        return false;
    }
    return ExecLocked("COMMIT");
}

bool CV2MsgStore::DeleteConversation(int ctype, int64_t cid, int& outDeleted)
{
    //删除全部消息等于锚点取最大值的清空（本人离开该群时用）
    return DeleteConversationUpTo(ctype, cid, INT64_MAX, outDeleted);
}

bool CV2MsgStore::DeleteConversationUpTo(int ctype, int64_t cid, int64_t maxMsgid, int& outDeleted)
{
    outDeleted = 0;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    //消息与覆盖区间一起成败：只删了消息而覆盖区间还在，消息记录窗会把空区间当成"已验证完整"
    if (!ExecLocked("BEGIN IMMEDIATE"))
    {
        return false;
    }

    int deleted = 0;
    bool ok = false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kDeleteConversationMsgSql, -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, ctype);
        sqlite3_bind_int64(stmt, 2, cid);
        sqlite3_bind_int64(stmt, 3, maxMsgid);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (ok)
        {
            deleted = sqlite3_changes(m_db);
        }
    }
    sqlite3_finalize(stmt);

    if (ok)
    {
        stmt = NULL;
        ok = false;
        if (sqlite3_prepare_v2(m_db, kDeleteCoverSql, -1, &stmt, NULL) == SQLITE_OK)
        {
            sqlite3_bind_int(stmt, 1, ctype);
            sqlite3_bind_int64(stmt, 2, cid);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
        }
        sqlite3_finalize(stmt);
    }

    if (!ok)
    {
        ExecLocked("ROLLBACK");
        return false;
    }
    if (!ExecLocked("COMMIT"))
    {
        return false;
    }
    outDeleted = deleted;
    return true;
}

bool CV2MsgStore::DeleteByTimeRangeBatch(int64_t beginMs, int64_t endMs, int batchSize,
                                         int& outDeleted)
{
    outDeleted = 0;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    //批大小非正：什么都不删，按"已经没有剩余的行"返回，调用方的循环据此结束
    if (batchSize <= 0)
    {
        return true;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kDeleteRangeBatchSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, beginMs);
    sqlite3_bind_int64(stmt, 2, endMs);
    sqlite3_bind_int(stmt, 3, batchSize);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
    {
        return false;
    }

    outDeleted = sqlite3_changes(m_db);
    return true;
}

bool CV2MsgStore::GetCoverage(int ctype, int64_t cid, CoverRange& out)
{
    out = CoverRange();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kGetCoverSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        out.m_valid = true;
        out.m_lo = sqlite3_column_int64(stmt, 0);
        out.m_hi = sqlite3_column_int64(stmt, 1);
        out.m_reachedOldest = (sqlite3_column_int(stmt, 2) != 0);
    }
    sqlite3_finalize(stmt);

    //没有记录（SQLITE_DONE）也算查询成功，out 保持为未建立
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool CV2MsgStore::SetCoverage(int ctype, int64_t cid, const CoverRange& cover)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    //未建立的区间不存行：读回时没有记录即表示未建立，两种表示不能并存
    const char* sql = cover.m_valid ? kSetCoverSql : kDeleteCoverSql;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    if (cover.m_valid)
    {
        sqlite3_bind_int64(stmt, 3, cover.m_lo);
        sqlite3_bind_int64(stmt, 4, cover.m_hi);
        sqlite3_bind_int(stmt, 5, cover.m_reachedOldest ? 1 : 0);
    }
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::ClearAllCoverage()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    return ExecLocked(kClearAllCoverSql);
}

bool CV2MsgStore::QueryMsgidsInRange(int ctype, int64_t cid, int64_t loMsgid, int64_t hiMsgid,
                                     std::vector<int64_t>& out)
{
    out.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryMsgidsInRangeSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int64(stmt, 3, loMsgid);
    sqlite3_bind_int64(stmt, 4, hiMsgid);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        out.push_back(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::QuerySenders(int ctype, int64_t cid, int64_t minMsgid, std::vector<int64_t>& out)
{
    out.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQuerySendersSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int64(stmt, 3, minMsgid);
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        out.push_back(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::QueryAllSenders(std::vector<int64_t>& out)
{
    out.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryAllSendersSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        out.push_back(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::QueryCacheRefs(std::vector<HistoryCacheRef>& out)
{
    out.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryCacheRefsSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        HistoryCacheRef ref;
        ref.m_ctype = sqlite3_column_int(stmt, 0);
        ref.m_cid = sqlite3_column_int64(stmt, 1);
        const int msgtype = sqlite3_column_int(stmt, 2);
        const unsigned char* content = sqlite3_column_text(stmt, 3);
        const std::string contentJson = (content != NULL) ? (const char*)content : "";
        if (CacheRefFromContent(msgtype, contentJson, ref))
        {
            out.push_back(ref);
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::QueryOldestTimestampFrom(int ctype, int64_t cid, int64_t minMsgid,
                                           int64_t& outTimestampMs, bool& outFound)
{
    outTimestampMs = 0;
    outFound = false;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryOldestTimestampFromSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int64(stmt, 3, minMsgid);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
    {
        //聚合函数在没有任何行时仍返回一行，值为 NULL，据此判断"没有这样的行"
        outTimestampMs = sqlite3_column_int64(stmt, 0);
        outFound = true;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool CV2MsgStore::CountBefore(const HistoryFilter& filter, int64_t nowMs, int64_t timestamp, int64_t msgid,
                              int64_t& out)
{
    out = 0;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    std::string sql = BuildCountBeforeSql(filter, nowMs, timestamp, msgid);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    //与 Count 相同：关键字筛选生效时语句里有一个 LIKE ? 占位
    BindFilterKeyword(stmt, filter);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        out = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

int64_t CV2MsgStore::Count(const HistoryFilter& filter, int64_t nowMs)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return 0;
    }
    std::string sql = BuildCountSql(filter, nowMs);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return 0;
    }
    //关键字筛选生效时 BuildCountSql 末尾会带一个 LIKE ? 占位，这里绑定转义过的匹配串。
    BindFilterKeyword(stmt, filter);
    int64_t total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        total = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return total;
}

bool CV2MsgStore::QueryPage(const HistoryFilter& filter, int64_t nowMs,
                            std::vector<HistoryRecord>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    std::string sql = BuildPageSql(filter, nowMs);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindFilterKeyword(stmt, filter);
    //列读取顺序须与 V2MsgQuery::kColumns 一致：
    //0 msgid,1 clientmsgid,2 ctype,3 cid,4 fromid,5 msgtype,6 content,7 search_text,
    //8 is_link,9 timestamp,10 recalled,11 pinned,12 pin_time
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        HistoryRecord rec;
        rec.m_msgid = sqlite3_column_int64(stmt, 0);
        const unsigned char* clientmsgid = sqlite3_column_text(stmt, 1);
        rec.m_clientmsgid = (clientmsgid != NULL) ? (const char*)clientmsgid : "";
        rec.m_ctype = sqlite3_column_int(stmt, 2);
        rec.m_cid = sqlite3_column_int64(stmt, 3);
        rec.m_fromid = sqlite3_column_int64(stmt, 4);
        rec.m_msgtype = sqlite3_column_int(stmt, 5);
        const unsigned char* content = sqlite3_column_text(stmt, 6);
        rec.m_content = (content != NULL) ? (const char*)content : "";
        const unsigned char* searchText = sqlite3_column_text(stmt, 7);
        rec.m_searchText = (searchText != NULL) ? (const char*)searchText : "";
        rec.m_isLink = sqlite3_column_int(stmt, 8) != 0;
        rec.m_timestamp = sqlite3_column_int64(stmt, 9);
        rec.m_recalled = sqlite3_column_int(stmt, 10) != 0;
        rec.m_pinned = sqlite3_column_int(stmt, 11) != 0;
        rec.m_pinTime = sqlite3_column_int64(stmt, 12);
        out.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return true;
}

//执行一条「分段键, 条数」两列的分组统计语句（BuildMonthCountSql / BuildDayCountSql 拼出的），调用方已持锁
static bool RunPeriodCountSql(sqlite3* db, const std::string& sql, const HistoryFilter& filter,
                              std::vector<HistoryPeriodCount>& out)
{
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    //与 Count 相同：关键字筛选生效时语句里有一个 LIKE ? 占位
    BindFilterKeyword(stmt, filter);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        HistoryPeriodCount c;
        c.m_key = sqlite3_column_int64(stmt, 0);
        c.m_count = sqlite3_column_int64(stmt, 1);
        out.push_back(c);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

//取符合条件的最早或最晚一条消息的时间（BuildEdgeTimestampSql），调用方已持锁
//@param found  [出参] 有没有符合条件的消息
//@return 查询成功返回 true（即使没有符合条件的消息）
static bool QueryEdgeTimestamp(sqlite3* db, const HistoryFilter& filter, int64_t nowMs, bool newest,
                               int64_t& outMs, bool& found)
{
    outMs = 0;
    found = false;
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, BuildEdgeTimestampSql(filter, nowMs, newest).c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindFilterKeyword(stmt, filter);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        outMs = sqlite3_column_int64(stmt, 0);
        found = true;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool CV2MsgStore::CountByLocalMonth(const HistoryFilter& filter, int64_t nowMs, std::vector<HistoryPeriodCount>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    //先取最早、最晚一条的时间（各走一次索引），在程序里算出中间各月的本地起止，再按区间计数；不对每一行
    //做本地时间换算（逐行 strftime 时 10 万条的会话要两三百毫秒）
    int64_t oldestMs = 0;
    int64_t newestMs = 0;
    bool found = false;
    if (!QueryEdgeTimestamp(m_db, filter, nowMs, false, oldestMs, found))
    {
        return false;
    }
    if (!found)
    {
        return true;
    }
    if (!QueryEdgeTimestamp(m_db, filter, nowMs, true, newestMs, found) || !found)
    {
        return false;
    }
    const std::vector<HistoryMonthRange> ranges = BuildLocalMonthRanges(oldestMs, newestMs);
    //月份多于一条语句能放下的个数时分批；各批都按月份倒序，依次拼起来仍是倒序
    for (size_t begin = 0; begin < ranges.size(); begin += kMonthRangeSqlMaxMonths)
    {
        const size_t end = (begin + kMonthRangeSqlMaxMonths < ranges.size()) ? (begin + kMonthRangeSqlMaxMonths)
                                                                               : ranges.size();
        const std::vector<HistoryMonthRange> chunk(ranges.begin() + begin, ranges.begin() + end);
        if (!RunPeriodCountSql(m_db, BuildMonthRangeCountSql(filter, nowMs, chunk), filter, out))
        {
            out.clear();
            return false;
        }
    }
    return true;
}

bool CV2MsgStore::CountByLocalDay(const HistoryFilter& filter, int64_t nowMs, int64_t fromMs, int64_t toMs,
                                  std::vector<HistoryPeriodCount>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    return RunPeriodCountSql(m_db, BuildDayCountSql(filter, nowMs, fromMs, toMs), filter, out);
}

bool CV2MsgStore::QueryPositions(const HistoryFilter& filter, int64_t nowMs, int limit,
                                 std::vector<HistoryPosition>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    const std::string sql = BuildPositionsSql(filter, nowMs, limit);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindFilterKeyword(stmt, filter);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        HistoryPosition p;
        p.m_timestamp = sqlite3_column_int64(stmt, 0);
        p.m_msgid = sqlite3_column_int64(stmt, 1);
        out.push_back(p);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::SearchConversations(const std::string& keywordUtf8, int limit,
                                      std::vector<ConversationHit>& out)
{
    out.clear();
    if (limit <= 0)
    {
        return false;
    }
    //空关键字不当作「全部命中」：搜索框清空时下拉不该列出所有会话
    if (keywordUtf8.empty())
    {
        return true;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    const bool fullText = UseFullTextLocked(keywordUtf8);
    const std::string sql = BuildSearchConversationsSql(limit, fullText);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindSearchKeyword(stmt, keywordUtf8, fullText);

    //列顺序见 BuildSearchConversationsSql：0 ctype，1 cid，2 命中条数，3 最近命中时间，
    //4 最近命中的 msgid，5 最近命中的 search_text
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        ConversationHit hit;
        hit.m_ctype = sqlite3_column_int(stmt, 0);
        hit.m_cid = sqlite3_column_int64(stmt, 1);
        hit.m_hitCount = sqlite3_column_int(stmt, 2);
        hit.m_latestTimestamp = sqlite3_column_int64(stmt, 3);
        hit.m_latestMsgid = sqlite3_column_int64(stmt, 4);
        const unsigned char* text = sqlite3_column_text(stmt, 5);
        hit.m_latestText = (text != NULL) ? (const char*)text : "";
        out.push_back(hit);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::SearchFileMessages(const std::string& keywordUtf8, int limit,
                                     std::vector<HistoryRecord>& out)
{
    out.clear();
    if (limit <= 0)
    {
        return false;
    }
    if (keywordUtf8.empty())
    {
        return true;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    const bool fullText = UseFullTextLocked(keywordUtf8);
    const std::string sql = BuildSearchFileMessagesSql(limit, fullText);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindSearchKeyword(stmt, keywordUtf8, fullText);

    //列顺序与 ReadRecordRow 一致（kColumns 加 raw，见 BuildSearchFileMessagesSql）
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        HistoryRecord rec;
        ReadRecordRow(stmt, rec);
        out.push_back(rec);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

//==================== 聊天记录管理器 ====================

//QueryConversationList 用的四条语句。
//跳到下一个会话分两步：先在同一会话类型里找下一个 cid，找不到再跳到下一个会话类型的第一个会话。
//每步都只做一次索引定位，与会话里有多少条消息无关。不写成行值比较 (ctype,cid)>(?,?)：SQLite 对它
//只按第一列定位起点、其余逐行过滤，2026-10-03 实测 500 个会话 × 1000 条时每跳一次要 0.5 毫秒
//（tmp/histmgr/perf/probe.py、probe2.py）
static const char* const kNextCidSql =
    "SELECT cid FROM t_msg WHERE ctype=? AND cid>? ORDER BY cid LIMIT 1";
static const char* const kNextCtypeSql =
    "SELECT ctype,cid FROM t_msg WHERE ctype>? ORDER BY ctype,cid LIMIT 1";
//某会话 msgid 最大的一条未撤回消息，走主键倒序
static const char* const kLatestUnrecalledSql =
    "SELECT msgid,timestamp,fromid,msgtype,content FROM t_msg "
    "WHERE ctype=? AND cid=? AND recalled=0 ORDER BY msgid DESC LIMIT 1";
//全部会话的覆盖区间；每个会话至多一行，行数与会话数同一量级
static const char* const kAllCoverSql =
    "SELECT ctype,cid,lo_msgid,hi_msgid,reached_oldest FROM t_cover";

bool CV2MsgStore::QueryConversationList(std::vector<LocalConversationBrief>& out)
{
    out.clear();

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }

    //整轮查询放进一个读事务。自动提交模式下每条语句都要单独开、关一次读事务（加文件锁、核对文件头），
    //每条约多出 0.1 毫秒，这里每个会话要执行两三条语句，几百个会话就是几十到上百毫秒。2026-10-03 实测
    //（tmp/histmgr/perf/probe5.py）：500 个会话从 240 毫秒降到 64 毫秒。开事务失败时照常按自动提交执行
    const bool inTxn = ExecLocked("BEGIN");
    const bool ok = QueryConversationListLocked(out);
    if (inTxn)
    {
        ExecLocked("COMMIT");
    }
    if (!ok)
    {
        out.clear();
        return false;
    }
    SortLocalConversationBriefs(out);
    return true;
}

bool CV2MsgStore::QueryConversationListLocked(std::vector<LocalConversationBrief>& out)
{
    //1) 覆盖区间全部读进内存，下面逐个会话判断最后一条是否可见时查它
    std::map<std::pair<int, int64_t>, CoverRange> covers;
    sqlite3_stmt* coverStmt = NULL;
    if (sqlite3_prepare_v2(m_db, kAllCoverSql, -1, &coverStmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(coverStmt)) == SQLITE_ROW)
    {
        CoverRange cover;
        cover.m_valid = true;
        cover.m_lo = sqlite3_column_int64(coverStmt, 2);
        cover.m_hi = sqlite3_column_int64(coverStmt, 3);
        cover.m_reachedOldest = (sqlite3_column_int(coverStmt, 4) != 0);
        const std::pair<int, int64_t> key(sqlite3_column_int(coverStmt, 0),
                                          sqlite3_column_int64(coverStmt, 1));
        covers[key] = cover;
    }
    sqlite3_finalize(coverStmt);
    if (rc != SQLITE_DONE)
    {
        return false;
    }

    //2) 逐个会话取最后一条未撤回消息
    sqlite3_stmt* nextCidStmt = NULL;
    sqlite3_stmt* nextCtypeStmt = NULL;
    sqlite3_stmt* latestStmt = NULL;
    if (sqlite3_prepare_v2(m_db, kNextCidSql, -1, &nextCidStmt, NULL) != SQLITE_OK
        || sqlite3_prepare_v2(m_db, kNextCtypeSql, -1, &nextCtypeStmt, NULL) != SQLITE_OK
        || sqlite3_prepare_v2(m_db, kLatestUnrecalledSql, -1, &latestStmt, NULL) != SQLITE_OK)
    {
        sqlite3_finalize(nextCidStmt);
        sqlite3_finalize(nextCtypeStmt);
        sqlite3_finalize(latestStmt);
        return false;
    }

    bool ok = true;
    bool started = false;   //是否已经定位到过一个会话；第一次直接跳到排在最前的会话类型
    int ctype = 0;
    int64_t cid = 0;
    while (true)
    {
        //先在同一会话类型里找下一个 cid
        bool found = false;
        if (started)
        {
            sqlite3_bind_int(nextCidStmt, 1, ctype);
            sqlite3_bind_int64(nextCidStmt, 2, cid);
            rc = sqlite3_step(nextCidStmt);
            if (rc == SQLITE_ROW)
            {
                cid = sqlite3_column_int64(nextCidStmt, 0);
                found = true;
            }
            sqlite3_reset(nextCidStmt);
            if (rc != SQLITE_ROW && rc != SQLITE_DONE)
            {
                ok = false;
                break;
            }
        }
        //同一类型里已经没有了（或是第一次）：跳到下一个会话类型的第一个会话
        if (!found)
        {
            sqlite3_bind_int64(nextCtypeStmt, 1, started ? (int64_t)ctype : INT64_MIN);
            rc = sqlite3_step(nextCtypeStmt);
            if (rc == SQLITE_ROW)
            {
                ctype = sqlite3_column_int(nextCtypeStmt, 0);
                cid = sqlite3_column_int64(nextCtypeStmt, 1);
            }
            sqlite3_reset(nextCtypeStmt);
            if (rc == SQLITE_DONE)
            {
                break;
            }
            if (rc != SQLITE_ROW)
            {
                ok = false;
                break;
            }
        }
        started = true;

        sqlite3_bind_int(latestStmt, 1, ctype);
        sqlite3_bind_int64(latestStmt, 2, cid);
        rc = sqlite3_step(latestStmt);
        if (rc == SQLITE_ROW)
        {
            LocalConversationBrief brief;
            brief.m_ctype = ctype;
            brief.m_cid = cid;
            brief.m_lastMsgid = sqlite3_column_int64(latestStmt, 0);
            brief.m_lastTimestamp = sqlite3_column_int64(latestStmt, 1);
            brief.m_lastFromid = sqlite3_column_int64(latestStmt, 2);
            brief.m_lastMsgtype = sqlite3_column_int(latestStmt, 3);
            const unsigned char* content = sqlite3_column_text(latestStmt, 4);
            brief.m_lastContent = (content != NULL) ? (const char*)content : "";

            //最后一条在覆盖区间下界以下：更早的行 msgid 更小，同样不可见，整个会话都不列出
            CoverRange cover;
            std::map<std::pair<int, int64_t>, CoverRange>::const_iterator it =
                covers.find(std::pair<int, int64_t>(ctype, cid));
            if (it != covers.end())
            {
                cover = it->second;
            }
            if (brief.m_lastMsgid >= CoverageMinVisibleMsgid(cover, false))
            {
                out.push_back(brief);
            }
        }
        sqlite3_reset(latestStmt);
        //SQLITE_DONE：该会话只剩撤回的消息，不列出
        if (rc != SQLITE_ROW && rc != SQLITE_DONE)
        {
            ok = false;
            break;
        }
    }
    sqlite3_finalize(nextCidStmt);
    sqlite3_finalize(nextCtypeStmt);
    sqlite3_finalize(latestStmt);
    return ok;
}

bool CV2MsgStore::SearchConversationsForManager(const std::string& keywordUtf8, int limit,
                                                const std::vector<HistoryConversationKey>& scope,
                                                std::vector<ConversationHit>& out, bool wildcard,
                                                const std::vector<int64_t>& fromIds)
{
    out.clear();
    if (limit <= 0)
    {
        return false;
    }
    //空关键字不当作「全部命中」，与 SearchConversations 一致；按搜索语法解析后什么条件都没有（只有空白、勾选通配符时
    //只输了 '*'）也按空关键字处理
    if (!SearchQueryHasCondition(ParseHistorySearch(keywordUtf8, wildcard)))
    {
        return true;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    //全文索引：取各正向词里够长的部分（搜索语法，docs/聊天记录管理器方案.md §12.3）
    std::string match;
    const bool fullText = m_ftsAvailable && m_ftsReady && m_ftsAllowed
                       && BuildSearchSyntaxFtsQuery(keywordUtf8, wildcard, match);
    const std::string sql = BuildManagerSearchSql(limit, scope, keywordUtf8, wildcard, fromIds, fullText);
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    int index = 1;
    if (fullText)
    {
        sqlite3_bind_text(stmt, index++, match.c_str(), -1, SQLITE_TRANSIENT);
    }
    const std::vector<std::string> binds = SearchSyntaxLikeBinds(keywordUtf8, wildcard);
    for (size_t i = 0; i < binds.size(); ++i)
    {
        sqlite3_bind_text(stmt, index++, binds[i].c_str(), -1, SQLITE_TRANSIENT);
    }

    //列顺序见 BuildManagerSearchSql：0 ctype，1 cid，2 命中条数，3 最近命中时间，
    //4 最近命中的 msgid，5 最近命中的 search_text
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        ConversationHit hit;
        hit.m_ctype = sqlite3_column_int(stmt, 0);
        hit.m_cid = sqlite3_column_int64(stmt, 1);
        hit.m_hitCount = sqlite3_column_int(stmt, 2);
        hit.m_latestTimestamp = sqlite3_column_int64(stmt, 3);
        hit.m_latestMsgid = sqlite3_column_int64(stmt, 4);
        const unsigned char* text = sqlite3_column_text(stmt, 5);
        hit.m_latestText = (text != NULL) ? (const char*)text : "";
        out.push_back(hit);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

//==================== 聊天记录管理器的文件页与图片页（docs/聊天记录管理器方案.md §11.5、§11.6） ====================

//取一列文字；NULL 按空串处理
static std::string ColumnText(sqlite3_stmt* stmt, int col)
{
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return (text != NULL) ? (const char*)text : std::string();
}

//给 kUpsertGroupFileSql 绑定一个群文件的各列（本机落地路径不在其中）
static void BindGroupFile(sqlite3_stmt* stmt, const GroupFileCacheItem& item)
{
    sqlite3_bind_int64(stmt, 1, item.m_groupid);
    sqlite3_bind_text(stmt, 2, item.m_entryid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, item.m_fileid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, item.m_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, item.m_ext.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, item.m_size);
    sqlite3_bind_text(stmt, 7, item.m_md5.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, item.m_filetype);
    sqlite3_bind_int64(stmt, 9, item.m_uploaderid);
    sqlite3_bind_text(stmt, 10, item.m_uploaderName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 11, item.m_uploadTime);
}

//把 kQueryGroupFileSql / kQueryGroupFilesOfGroupSql 的当前行读成一个群文件
static void ReadGroupFileRow(sqlite3_stmt* stmt, GroupFileCacheItem& item)
{
    item.m_groupid = sqlite3_column_int64(stmt, 0);
    item.m_entryid = ColumnText(stmt, 1);
    item.m_fileid = ColumnText(stmt, 2);
    item.m_name = ColumnText(stmt, 3);
    item.m_ext = ColumnText(stmt, 4);
    item.m_size = sqlite3_column_int64(stmt, 5);
    item.m_md5 = ColumnText(stmt, 6);
    item.m_filetype = sqlite3_column_int(stmt, 7);
    item.m_uploaderid = sqlite3_column_int64(stmt, 8);
    item.m_uploaderName = ColumnText(stmt, 9);
    item.m_uploadTime = sqlite3_column_int64(stmt, 10);
    item.m_localPath = ColumnText(stmt, 11);
}

//给文件 / 图片 / 链接 / 与我有关页的语句绑定关键字：likeBinds 是带关键字条件的部分个数（聊天消息部分、群文件
//部分各算一个），每个部分按搜索语法有若干个 LIKE / NOT LIKE 占位（SearchSyntaxLikeBinds 的顺序），按部分在语句里
//的先后依次绑定。占位序号从 1 起
static void BindLibraryKeyword(sqlite3_stmt* stmt, const HistoryLibraryFilter& filter, int likeBinds)
{
    if (likeBinds <= 0)
    {
        return;
    }
    const std::vector<std::string> binds = SearchSyntaxLikeBinds(filter.m_keyword, filter.m_wildcard);
    int index = 1;
    for (int part = 0; part < likeBinds; ++part)
    {
        for (size_t i = 0; i < binds.size(); ++i)
        {
            sqlite3_bind_text(stmt, index++, binds[i].c_str(), -1, SQLITE_TRANSIENT);
        }
    }
}

bool CV2MsgStore::UpsertGroupFileCache(const GroupFileCacheItem& item)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL || item.m_groupid <= 0 || item.m_entryid.empty())
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kUpsertGroupFileSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindGroupFile(stmt, item);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool CV2MsgStore::ReplaceGroupFileCache(int64_t groupid, const std::vector<GroupFileCacheItem>& items)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL || groupid <= 0)
    {
        return false;
    }
    if (!ExecLocked("BEGIN IMMEDIATE"))
    {
        return false;
    }

    //先读出缓存里这个群现有的条目，写完新列表后删掉新列表里没有的那些
    std::set<std::string> stale;
    sqlite3_stmt* stmt = NULL;
    bool ok = (sqlite3_prepare_v2(m_db, kGroupFileEntryIdsSql, -1, &stmt, NULL) == SQLITE_OK);
    if (ok)
    {
        sqlite3_bind_int64(stmt, 1, groupid);
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            stale.insert(ColumnText(stmt, 0));
        }
        ok = (rc == SQLITE_DONE);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    //写入新列表：已有的条目只更新服务端给的字段，本机落地路径保留
    if (ok)
    {
        ok = (sqlite3_prepare_v2(m_db, kUpsertGroupFileSql, -1, &stmt, NULL) == SQLITE_OK);
    }
    for (size_t i = 0; ok && i < items.size(); ++i)
    {
        //列表里的条目都应属于这个群；对不上的、没有条目 ID 的跳过，不让它们写进别的群
        if (items[i].m_groupid != groupid || items[i].m_entryid.empty())
        {
            continue;
        }
        BindGroupFile(stmt, items[i]);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        stale.erase(items[i].m_entryid);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    //删掉服务端已经没有的条目
    if (ok && !stale.empty())
    {
        ok = (sqlite3_prepare_v2(m_db, kDeleteGroupFileSql, -1, &stmt, NULL) == SQLITE_OK);
        for (std::set<std::string>::const_iterator it = stale.begin(); ok && it != stale.end(); ++it)
        {
            sqlite3_bind_int64(stmt, 1, groupid);
            sqlite3_bind_text(stmt, 2, it->c_str(), -1, SQLITE_TRANSIENT);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
        sqlite3_finalize(stmt);
    }

    if (!ok)
    {
        ExecLocked("ROLLBACK");
        return false;
    }
    return ExecLocked("COMMIT");
}

bool CV2MsgStore::DeleteGroupFileCacheEntry(int64_t groupid, const std::string& entryid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kDeleteGroupFileSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, groupid);
    sqlite3_bind_text(stmt, 2, entryid.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool CV2MsgStore::DeleteGroupFileCacheOfGroup(int64_t groupid)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kDeleteGroupFilesOfGroupSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, groupid);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool CV2MsgStore::SetGroupFileLocalPath(int64_t groupid, const std::string& entryid,
                                        const std::string& localPathUtf8, bool& outHit)
{
    outHit = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kSetGroupFileLocalPathSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text(stmt, 1, localPathUtf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, groupid);
    sqlite3_bind_text(stmt, 3, entryid.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    outHit = ok && (sqlite3_changes(m_db) > 0);
    sqlite3_finalize(stmt);
    return ok;
}

bool CV2MsgStore::QueryGroupFileCache(int64_t groupid, const std::string& entryid, GroupFileCacheItem& out,
                                      bool& found)
{
    found = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryGroupFileSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, groupid);
    sqlite3_bind_text(stmt, 2, entryid.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        ReadGroupFileRow(stmt, out);
        found = true;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool CV2MsgStore::QueryGroupFileCacheOfGroup(int64_t groupid, std::vector<GroupFileCacheItem>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryGroupFilesOfGroupSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, groupid);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        GroupFileCacheItem item;
        ReadGroupFileRow(stmt, item);
        out.push_back(item);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::CountLibrary(const HistoryLibraryFilter& filter, int64_t nowMs, int64_t& out)
{
    out = 0;
    int likeBinds = 0;
    const std::string sql = BuildLibraryCountSql(filter, nowMs, likeBinds);
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    //两部分都不用查：总数就是 0
    if (sql.empty())
    {
        return true;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindLibraryKeyword(stmt, filter, likeBinds);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        out = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

bool CV2MsgStore::QueryLibraryPage(const HistoryLibraryFilter& filter, int64_t nowMs,
                                   std::vector<HistoryLibraryItem>& out)
{
    out.clear();
    int likeBinds = 0;
    const std::string sql = BuildLibraryPageSql(filter, nowMs, likeBinds);
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    if (sql.empty())
    {
        return true;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindLibraryKeyword(stmt, filter, likeBinds);
    //列序见 BuildLibraryPageSql
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        HistoryLibraryItem item;
        const int source = sqlite3_column_int(stmt, 0);
        item.m_isGroupFile = (source == kHistoryLibrarySrcGroupFile);
        item.m_ctype = sqlite3_column_int(stmt, 1);
        item.m_cid = sqlite3_column_int64(stmt, 2);
        item.m_msgid = sqlite3_column_int64(stmt, 3);
        item.m_entryid = ColumnText(stmt, 4);
        item.m_msgtype = sqlite3_column_int(stmt, 5);
        item.m_fromid = sqlite3_column_int64(stmt, 6);
        item.m_uploaderName = ColumnText(stmt, 7);
        item.m_name = ColumnText(stmt, 8);
        item.m_size = sqlite3_column_int64(stmt, 9);
        item.m_fileid = ColumnText(stmt, 10);
        item.m_md5 = ColumnText(stmt, 11);
        item.m_timestamp = sqlite3_column_int64(stmt, 12);
        item.m_content = ColumnText(stmt, 13);
        item.m_localPath = ColumnText(stmt, 14);
        item.m_filetype = sqlite3_column_int(stmt, 15);
        item.m_relation = sqlite3_column_int(stmt, 16);
        //帖子卡片里的一张图片：序号在 entryid 列，文件 ID、MD5、大小已由查询从那张图片取出，不再按内容 JSON 填
        if (source == kHistoryLibrarySrcCardImage)
        {
            item.m_cardImageIndex = atoi(item.m_entryid.c_str());
            item.m_entryid.clear();
            out.push_back(item);
            continue;
        }
        //聊天消息的大小、文件 ID、MD5 在内容 JSON 里
        FillLibraryItemFromContent(item);
        out.push_back(item);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::CountLibraryByConversation(const HistoryLibraryFilter& filter, int64_t nowMs,
                                             std::vector<HistoryLibraryConvCount>& out)
{
    out.clear();
    int likeBinds = 0;
    const std::string sql = BuildLibraryConvCountSql(filter, nowMs, likeBinds);
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    if (sql.empty())
    {
        return true;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    BindLibraryKeyword(stmt, filter, likeBinds);
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        HistoryLibraryConvCount c;
        c.m_ctype = sqlite3_column_int(stmt, 0);
        c.m_cid = sqlite3_column_int64(stmt, 1);
        c.m_count = sqlite3_column_int(stmt, 2);
        out.push_back(c);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::ReadSyncValue(const char* key, int64_t& out, bool& found)
{
    out = 0;
    found = false;
    if (key == NULL)
    {
        return false;
    }
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kReadSyncMetaSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        out = sqlite3_column_int64(stmt, 0);
        found = true;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool CV2MsgStore::WriteSyncValue(const char* key, int64_t value)
{
    if (key == NULL)
    {
        return false;
    }
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kWriteSyncMetaSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, value);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::HasAnyMessage(bool& out)
{
    out = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kAnyMessageSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    const int rc = sqlite3_step(stmt);
    out = (rc == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool CV2MsgStore::SaveRecallDraft(int ctype, int64_t cid, int64_t msgid, int64_t recallTimeMs,
                                  const std::string& payload)
{
    if (msgid <= 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kSaveRecallDraftSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int(stmt, 1, ctype);
    sqlite3_bind_int64(stmt, 2, cid);
    sqlite3_bind_int64(stmt, 3, msgid);
    sqlite3_bind_int64(stmt, 4, recallTimeMs);
    sqlite3_bind_text(stmt, 5, payload.c_str(), (int)payload.size(), SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool CV2MsgStore::QueryRecallDrafts(int ctype, int64_t cid, const std::vector<int64_t>& msgids,
                                    std::vector<RecallDraftRow>& out)
{
    out.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kQueryRecallDraftSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        if (msgids[i] <= 0)
        {
            continue;
        }
        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, ctype);
        sqlite3_bind_int64(stmt, 2, cid);
        sqlite3_bind_int64(stmt, 3, msgids[i]);
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        {
            RecallDraftRow row;
            row.m_msgid = msgids[i];
            row.m_recallTimeMs = sqlite3_column_int64(stmt, 0);
            const unsigned char* text = sqlite3_column_text(stmt, 1);
            const int bytes = sqlite3_column_bytes(stmt, 1);
            if (text != NULL && bytes > 0)
            {
                row.m_payload.assign(reinterpret_cast<const char*>(text), (size_t)bytes);
            }
            out.push_back(row);
        }
        else if (rc != SQLITE_DONE)
        {
            ok = false;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool CV2MsgStore::PurgeRecallDrafts(int64_t olderThanMs, int& deleted)
{
    deleted = 0;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_db == NULL)
    {
        return false;
    }
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(m_db, kPurgeRecallDraftSql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return false;
    }
    sqlite3_bind_int64(stmt, 1, olderThanMs);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
    {
        deleted = sqlite3_changes(m_db);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

} // namespace balloonhistory
