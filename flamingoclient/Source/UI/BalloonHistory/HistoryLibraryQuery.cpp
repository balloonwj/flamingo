/**
 *  聊天记录管理器「文件」「图片」两页的查询纯逻辑实现，说明见 HistoryLibraryQuery.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "HistoryLibraryQuery.h"

#include <stdlib.h>
#include <memory>              // std::unique_ptr：jsoncpp 的解析器
#include "HistorySearchSyntax.h"   // ParseHistorySearch / SearchQueryHasCondition
#include "../../net/Protocol.h"   // ::message_type_*
#include "../../third-party/jsoncpp-1.9.0/json.h"

namespace balloonhistory {

namespace {

//会话类型：单聊、群聊（与 net::conversation_type 一致）
const int kCtypeSingle = 1;
const int kCtypeGroup  = 2;

//各类扩展名，与服务端 GroupFileProtocol.cpp 的同名表逐项一致
const char* const kDocExt[]     = { "doc", "docx", "xls", "xlsx", "ppt", "pptx", "pdf", "txt", "rtf", "csv", "md" };
const char* const kImageExt[]   = { "jpg", "jpeg", "png", "gif", "bmp", "webp" };
const char* const kVideoExt[]   = { "mp4", "avi", "mov", "mkv", "wmv", "flv" };
const char* const kAudioExt[]   = { "mp3", "wav", "aac", "flac", "ogg", "m4a" };
const char* const kArchiveExt[] = { "zip", "rar", "7z", "tar", "gz" };

//一类扩展名：表与条数
struct ExtGroup
{
    int                m_category;   //HistoryFileCategory 的 1～5
    const char* const* m_exts;       //扩展名表
    size_t             m_count;      //表里的条数
};

//全部已知的类别，按服务端判断的先后排列
const ExtGroup kExtGroups[] =
{
    { kHistoryFileCatDocument, kDocExt,     sizeof(kDocExt) / sizeof(kDocExt[0]) },
    { kHistoryFileCatImage,    kImageExt,   sizeof(kImageExt) / sizeof(kImageExt[0]) },
    { kHistoryFileCatVideo,    kVideoExt,   sizeof(kVideoExt) / sizeof(kVideoExt[0]) },
    { kHistoryFileCatAudio,    kAudioExt,   sizeof(kAudioExt) / sizeof(kAudioExt[0]) },
    { kHistoryFileCatArchive,  kArchiveExt, sizeof(kArchiveExt) / sizeof(kArchiveExt[0]) },
};
const size_t kExtGroupCount = sizeof(kExtGroups) / sizeof(kExtGroups[0]);

//聊天消息部分的 FROM：与覆盖区间表左连接，按覆盖区间判断可见（与 V2MsgQuery.cpp 的 kSearchFromWhere 同一口径）
const char* const kChatFrom =
    " FROM t_msg m LEFT JOIN t_cover c ON c.ctype=m.ctype AND c.cid=m.cid";
const char* const kChatVisible =
    " AND (c.ctype IS NULL OR c.reached_oldest<>0 OR m.msgid>=c.lo_msgid)";
//群文件部分的 FROM
const char* const kGroupFrom = " FROM t_group_file_cache g";
//同一个群文件又以文件消息发到了群里（GroupFileChatPoster）时只列聊天里那一条：排除 fileid 相同的群文件。
//内容不是合法 JSON 的老数据 json_extract 会报错，先用 json_valid 判断（CASE 保证先判断后取值）
const char* const kGroupDedupe =
    " AND NOT EXISTS (SELECT 1 FROM t_msg d WHERE d.ctype=2 AND d.cid=g.groupid AND d.msgtype=3"
    " AND d.recalled=0"
    " AND (CASE WHEN json_valid(d.content) THEN json_extract(d.content,'$.file.fileid') END)=g.fileid)";
//与我有关页判断 @ 与引用用的是原始 JSON（raw 列，服务端发来的整条消息）：@ 在 content.at，引用在 content.quote。
//先用 instr 粗筛出含这两个键名的行，只对这些行解析 JSON；raw 不是合法 JSON 的老数据经 json_valid 排除
//（CASE 保证先判断后取值，json_each 遇到非法 JSON 会报错）。userid 在 JSON 里按字符串编码，个别老数据可能是
//数值，两种都比较
const char* const kRawHasAt    = "instr(m.raw,'\"at\"')>0";
const char* const kRawHasQuote = "instr(m.raw,'\"quote\"')>0";

//帖子卡片图片部分的 FROM（docs/帖子群方案.md 14.5）：每条卡片消息按 content 的 threadcard.images[] 展开成每张图片一行。
//content 不是合法 JSON 时 json_each 会报错，故用 CASE 先判 json_valid，不合法时换成空对象（展开不出任何行）
const char* const kCardImageFrom =
    " FROM t_msg m LEFT JOIN t_cover c ON c.ctype=m.ctype AND c.cid=m.cid"
    " JOIN json_each(CASE WHEN json_valid(m.content) THEN m.content ELSE '{}' END,'$.threadcard.images') j";
//卡片图片部分的条件：只认对象形式、带 fileid 的图片，卡片不是已删除形态（同样先判 json_valid）
const char* const kCardImageValid =
    " AND j.type='object' AND IFNULL(json_extract(j.value,'$.fileid'),'')<>''"
    " AND (CASE WHEN json_valid(m.content) THEN IFNULL(json_extract(m.content,'$.threadcard.deleted'),0) ELSE 1 END)=0";

//整数转十进制文字
std::string IntText(int64_t v)
{
    return std::to_string((long long)v);
}

//ASCII 字母转小写，其它字节原样返回（UTF-8 多字节字符的字节都不小于 0x80，不受影响）
char LowerAsciiChar(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
    {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

//把一类扩展名拼成「名称以 .xxx 结尾」的条件，各项用 OR 连接，外面不带括号。
//扩展名都是 ASCII 字母与数字，不含 LIKE 的特殊字符，直接写进语句即可
std::string ExtLikeClause(const char* column, const ExtGroup& group)
{
    std::string clause;
    for (size_t i = 0; i < group.m_count; ++i)
    {
        if (!clause.empty())
        {
            clause += " OR ";
        }
        clause += column;
        clause += " LIKE '%.";
        clause += group.m_exts[i];
        clause += "'";
    }
    return clause;
}

//全部已知扩展名的条件（各类用 OR 连接），外面不带括号
std::string AllKnownExtClause(const char* column)
{
    std::string clause;
    for (size_t i = 0; i < kExtGroupCount; ++i)
    {
        if (!clause.empty())
        {
            clause += " OR ";
        }
        clause += ExtLikeClause(column, kExtGroups[i]);
    }
    return clause;
}

//时间条件。全部时间返回空串
std::string TimeClause(const char* column, const HistoryLibraryFilter& filter, int64_t nowMs)
{
    if (filter.m_range == kHistoryTimeAll)
    {
        return std::string();
    }
    int64_t minMs = 0;
    int64_t maxMs = 0;
    TimeRangeToBounds(filter.m_range, nowMs, filter.m_customMinMs, filter.m_customMaxMs, minMs, maxMs);
    std::string clause = " AND ";
    clause += column;
    clause += ">=";
    clause += IntText(minMs);
    clause += " AND ";
    clause += column;
    clause += "<=";
    clause += IntText(maxMs);
    return clause;
}

//方向条件：我发送的 = 发送人（上传者）是本人；我接收的 = 不是本人。本人 userid 未知或不限时返回空串
std::string DirectionClause(const char* column, const HistoryLibraryFilter& filter)
{
    if (filter.m_selfUserId <= 0 || filter.m_direction == kHistoryLibraryDirAll)
    {
        return std::string();
    }
    std::string clause = " AND ";
    clause += column;
    clause += (filter.m_direction == kHistoryLibraryDirSent) ? "=" : "<>";
    clause += IntText(filter.m_selfUserId);
    return clause;
}

//本人 userid 在 JSON 里可能的两种写法：字符串与数值，拼成 "IN ('10001',10001)" 的括号部分
std::string SelfIdInList(int64_t selfUserId)
{
    const std::string id = IntText(selfUserId);
    return "('" + id + "'," + id + ")";
}

//「@我的」条件（不含括号外的 AND）：别人发的，并且 content.at.all 为真或 content.at.users 里有本人。
//all 写成整数 1 或布尔 true，json_extract 对 true 也返回 1
std::string MentionCondition(int64_t selfUserId)
{
    return "(m.fromid<>" + IntText(selfUserId) + " AND " + kRawHasAt
         + " AND (CASE WHEN json_valid(m.raw) THEN"
           " (json_extract(m.raw,'$.content.at.all')=1"
           " OR EXISTS (SELECT 1 FROM json_each(m.raw,'$.content.at.users') u"
           " WHERE json_extract(u.value,'$.uid') IN " + SelfIdInList(selfUserId) + "))"
           " ELSE 0 END))";
}

//「回复我的」条件（不含括号外的 AND）：别人发的，并且 content.quote.from 是本人
std::string ReplyCondition(int64_t selfUserId)
{
    return "(m.fromid<>" + IntText(selfUserId) + " AND " + kRawHasQuote
         + " AND (CASE WHEN json_valid(m.raw) THEN"
           " json_extract(m.raw,'$.content.quote.from') IN " + SelfIdInList(selfUserId)
         + " ELSE 0 END))";
}

//与我有关页的条件：按筛选胶囊取 @我的、回复我的、我发的之一，「全部」取三者的并集。本人 userid 未知时没有结果
std::string RelationClause(const HistoryLibraryFilter& filter)
{
    if (filter.m_selfUserId <= 0)
    {
        return " AND 0";
    }
    const std::string mine = "m.fromid=" + IntText(filter.m_selfUserId);
    switch (filter.m_relation)
    {
    //@我的
    case kHistoryLibraryRelMention:
        return " AND " + MentionCondition(filter.m_selfUserId);
    //回复我的
    case kHistoryLibraryRelReply:
        return " AND " + ReplyCondition(filter.m_selfUserId);
    //我发的
    case kHistoryLibraryRelMine:
        return " AND " + mine;
    //全部：三者的并集
    case kHistoryLibraryRelAll:
    default:
        return " AND (" + mine + " OR " + MentionCondition(filter.m_selfUserId) + " OR "
             + ReplyCondition(filter.m_selfUserId) + ")";
    }
}

//分页结果里「关系」一列的表达式：与我有关页按 我发的 → 回复我的 → @我的 的先后判断（同时 @ 又回复记为回复）；
//其它页恒为 0
std::string RelationColumn(const HistoryLibraryFilter& filter)
{
    if (filter.m_kind != kHistoryLibraryMentions || filter.m_selfUserId <= 0)
    {
        return "0";
    }
    return "(CASE WHEN m.fromid=" + IntText(filter.m_selfUserId) + " THEN " + IntText(kHistoryLibraryRelMine)
         + " WHEN " + ReplyCondition(filter.m_selfUserId) + " THEN " + IntText(kHistoryLibraryRelReply)
         + " ELSE " + IntText(kHistoryLibraryRelMention) + " END)";
}

//聊天消息部分的类型条件：图片页是图片与截图；链接页是正文含链接的消息；与我有关页按筛选胶囊；
//文件页按类型筛选决定消息类型与扩展名
std::string ChatTypeClause(const HistoryLibraryFilter& filter)
{
    if (filter.m_kind == kHistoryLibraryImages)
    {
        return " AND m.msgtype IN (" + IntText(::message_type_image) + ","
             + IntText(::message_type_screenshot) + ")";
    }
    if (filter.m_kind == kHistoryLibraryLinks)
    {
        //is_link 在入库时按正文是否含 URL 预算好（只有文字消息会置 1）
        return " AND m.is_link=1";
    }
    if (filter.m_kind == kHistoryLibraryMentions)
    {
        return RelationClause(filter);
    }
    const std::string fileType = IntText(::message_type_file);
    const char* const nameCol = "IFNULL(m.search_text,'')";
    switch (filter.m_category)
    {
    //文件夹：只有文件夹传输记录
    case kHistoryFileCatFolder:
        return " AND m.msgtype=" + IntText(::message_type_folder);
    //其它：文件消息里扩展名不属于任何已知类别的
    case kHistoryFileCatOther:
        return " AND m.msgtype=" + fileType + " AND NOT (" + AllKnownExtClause(nameCol) + ")";
    //文档、图片、视频、音频、压缩包：文件消息里扩展名属于这一类的
    case kHistoryFileCatDocument:
    case kHistoryFileCatImage:
    case kHistoryFileCatVideo:
    case kHistoryFileCatAudio:
    case kHistoryFileCatArchive:
        for (size_t i = 0; i < kExtGroupCount; ++i)
        {
            if (kExtGroups[i].m_category == filter.m_category)
            {
                return " AND m.msgtype=" + fileType + " AND (" + ExtLikeClause(nameCol, kExtGroups[i]) + ")";
            }
        }
        return " AND 0";
    //全部类型：文件消息与文件夹传输记录
    case kHistoryFileCatAll:
    default:
        return " AND m.msgtype IN (" + fileType + "," + IntText(::message_type_folder) + ")";
    }
}

//聊天消息部分的 WHERE（含 "WHERE" 本身）。withConversation 为 false 时不加会话限定（按会话统计用）。
//关键字筛选生效时末尾带搜索语法的条件（LIKE / NOT LIKE 占位若干），likeBinds 加一（按部分计数）
std::string ChatWhereWithType(const HistoryLibraryFilter& filter, const std::string& typeClause, int64_t nowMs,
                              bool withConversation, int& likeBinds)
{
    std::string where = " WHERE m.recalled=0";
    where += typeClause;
    where += kChatVisible;
    //与我有关页由筛选胶囊决定发送人，不用「我发送的 / 我接收的」
    if (filter.m_kind != kHistoryLibraryMentions)
    {
        where += DirectionClause("m.fromid", filter);
    }
    if (withConversation)
    {
        if (filter.m_cid != 0)
        {
            where += " AND m.ctype=" + IntText(filter.m_ctype) + " AND m.cid=" + IntText(filter.m_cid);
        }
        else if (filter.m_ctypeOnly == kCtypeSingle || filter.m_ctypeOnly == kCtypeGroup)
        {
            where += " AND m.ctype=" + IntText(filter.m_ctypeOnly);
        }
    }
    where += TimeClause("m.timestamp", filter, nowMs);
    if (LibraryHasKeyword(filter))
    {
        //文件页只匹配文件名（search_text 存的就是文件名），只认词与排除词；链接页与与我有关页匹配消息正文，
        //按完整的搜索语法（另有 type、日期、from 条件）
        const bool termsOnly = (filter.m_kind == kHistoryLibraryFiles);
        where += BuildSearchSyntaxClause(filter.m_keyword, filter.m_wildcard, filter.m_syntaxFromIds,
                                         "m.search_text", termsOnly);
        ++likeBinds;
    }
    return where;
}

//聊天消息部分的 WHERE：类型条件按页面取 ChatTypeClause
std::string ChatWhere(const HistoryLibraryFilter& filter, int64_t nowMs, bool withConversation, int& likeBinds)
{
    return ChatWhereWithType(filter, ChatTypeClause(filter), nowMs, withConversation, likeBinds);
}

//帖子卡片图片部分是否参与这一次查询：只有图片页
bool CardImagePartIncluded(const HistoryLibraryFilter& filter)
{
    return filter.m_kind == kHistoryLibraryImages;
}

//帖子卡片图片部分的 WHERE（含 "WHERE" 本身）：帖子卡片与补充卡片，其余条件（会话、方向、时间、可见范围）与聊天消息部分相同。
//图片页没有关键字筛选，不会增加 likeBinds
std::string CardImageWhere(const HistoryLibraryFilter& filter, int64_t nowMs, bool withConversation, int& likeBinds)
{
    const std::string typeClause = " AND m.msgtype IN (" + IntText(::message_type_thread_post) + ","
                                 + IntText(::message_type_thread_reply) + ")";
    return ChatWhereWithType(filter, typeClause, nowMs, withConversation, likeBinds) + kCardImageValid;
}

//群文件部分的 WHERE（含 "WHERE" 本身）。参数含义同 ChatWhere
std::string GroupWhere(const HistoryLibraryFilter& filter, int64_t nowMs, bool withConversation, int& likeBinds)
{
    std::string where = " WHERE 1=1";
    if (filter.m_category >= kHistoryFileCatDocument && filter.m_category <= kHistoryFileCatOther)
    {
        where += " AND g.filetype=" + IntText(filter.m_category);
    }
    where += DirectionClause("g.uploaderid", filter);
    if (withConversation && filter.m_cid != 0)
    {
        where += " AND g.groupid=" + IntText(filter.m_cid);
    }
    where += TimeClause("g.uploadtime", filter, nowMs);
    if (LibraryHasKeyword(filter))
    {
        //群文件只出现在文件页，按文件名匹配，只认词与排除词
        where += BuildSearchSyntaxClause(filter.m_keyword, filter.m_wildcard, filter.m_syntaxFromIds,
                                         "g.name", true);
        ++likeBinds;
    }
    where += kGroupDedupe;
    return where;
}

//群文件部分是否参与这一次查询：LibraryIncludesGroupFiles，另外按会话统计时不看会话限定
bool GroupPartIncluded(const HistoryLibraryFilter& filter, bool withConversation)
{
    if (withConversation)
    {
        return LibraryIncludesGroupFiles(filter);
    }
    HistoryLibraryFilter unscoped = filter;
    unscoped.m_cid = 0;
    unscoped.m_ctypeOnly = 0;
    return LibraryIncludesGroupFiles(unscoped);
}

//取 JSON 对象里的整数：数值直接取，字符串按十进制解析（本地库的 size 存成了字符串）；都不是时返回 0
int64_t JsonInt64(const Json::Value& v)
{
    if (v.isIntegral())
    {
        return (int64_t)v.asInt64();
    }
    if (v.isString())
    {
        return (int64_t)_atoi64(v.asString().c_str());
    }
    return 0;
}

//取 JSON 对象里的字符串：字符串直接取，数值转成十进制文字；都不是时返回空串
std::string JsonText(const Json::Value& v)
{
    if (v.isString())
    {
        return v.asString();
    }
    if (v.isIntegral())
    {
        return IntText((int64_t)v.asInt64());
    }
    return std::string();
}

} // namespace

std::string FileExtFromName(const std::string& nameUtf8)
{
    const size_t dot = nameUtf8.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= nameUtf8.size())
    {
        return std::string();
    }
    std::string ext = nameUtf8.substr(dot + 1);
    for (size_t i = 0; i < ext.size(); ++i)
    {
        ext[i] = LowerAsciiChar(ext[i]);
    }
    return ext;
}

int FileCategoryFromName(const std::string& nameUtf8)
{
    const std::string ext = FileExtFromName(nameUtf8);
    if (ext.empty())
    {
        return kHistoryFileCatOther;
    }
    for (size_t g = 0; g < kExtGroupCount; ++g)
    {
        for (size_t i = 0; i < kExtGroups[g].m_count; ++i)
        {
            if (ext == kExtGroups[g].m_exts[i])
            {
                return kExtGroups[g].m_category;
            }
        }
    }
    return kHistoryFileCatOther;
}

bool LibraryIncludesChat(const HistoryLibraryFilter& filter)
{
    //来源筛选只有文件页有；其它页都只查聊天消息
    return filter.m_kind != kHistoryLibraryFiles || filter.m_source != kHistoryLibrarySourceGroup;
}

bool LibraryIncludesGroupFiles(const HistoryLibraryFilter& filter)
{
    if (filter.m_kind != kHistoryLibraryFiles || filter.m_source == kHistoryLibrarySourceChat
        || filter.m_category == kHistoryFileCatFolder)
    {
        return false;
    }
    //限定到单聊：只看某个单聊，或者全部会话里只看单聊
    if (filter.m_cid != 0 && filter.m_ctype != kCtypeGroup)
    {
        return false;
    }
    if (filter.m_cid == 0 && filter.m_ctypeOnly == kCtypeSingle)
    {
        return false;
    }
    return true;
}

bool LibraryHasKeyword(const HistoryLibraryFilter& filter)
{
    if (filter.m_kind == kHistoryLibraryImages || filter.m_keyword.empty())
    {
        return false;
    }
    const HistorySearchQuery q = ParseHistorySearch(filter.m_keyword, filter.m_wildcard);
    if (filter.m_kind == kHistoryLibraryFiles)
    {
        //文件名只认词与排除词，其余限定词写了也忽略
        return !q.m_terms.empty() || !q.m_excludes.empty();
    }
    return SearchQueryHasCondition(q);
}

std::string BuildLibraryCountSql(const HistoryLibraryFilter& filter, int64_t nowMs, int& outLikeBinds)
{
    outLikeBinds = 0;
    const bool chat = LibraryIncludesChat(filter);
    const bool group = GroupPartIncluded(filter, true);
    if (!chat && !group)
    {
        return std::string();
    }
    std::string sql = "SELECT 0";
    if (chat)
    {
        sql += "+(SELECT COUNT(*)";
        sql += kChatFrom;
        sql += ChatWhere(filter, nowMs, true, outLikeBinds);
        sql += ")";
    }
    if (group)
    {
        sql += "+(SELECT COUNT(*)";
        sql += kGroupFrom;
        sql += GroupWhere(filter, nowMs, true, outLikeBinds);
        sql += ")";
    }
    //图片页：帖子卡片里的图片每张算一项
    if (CardImagePartIncluded(filter))
    {
        sql += "+(SELECT COUNT(*)";
        sql += kCardImageFrom;
        sql += CardImageWhere(filter, nowMs, true, outLikeBinds);
        sql += ")";
    }
    return sql;
}

std::string BuildLibraryPageSql(const HistoryLibraryFilter& filter, int64_t nowMs, int& outLikeBinds)
{
    outLikeBinds = 0;
    const bool chat = LibraryIncludesChat(filter);
    const bool group = GroupPartIncluded(filter, true);
    if (!chat && !group)
    {
        return std::string();
    }
    std::string sql = "SELECT src,ctype,cid,msgid,entryid,msgtype,fromid,uname,name,size,fileid,md5,ts,"
                      "content,lpath,ftype,rel FROM (";
    if (chat)
    {
        sql += "SELECT 0 AS src,m.ctype AS ctype,m.cid AS cid,m.msgid AS msgid,'' AS entryid,"
               "m.msgtype AS msgtype,m.fromid AS fromid,'' AS uname,IFNULL(m.search_text,'') AS name,"
               "0 AS size,'' AS fileid,'' AS md5,m.timestamp AS ts,IFNULL(m.content,'') AS content,"
               "'' AS lpath,0 AS ftype,";
        sql += RelationColumn(filter);
        sql += " AS rel,0 AS ord";
        sql += kChatFrom;
        sql += ChatWhere(filter, nowMs, true, outLikeBinds);
    }
    if (group)
    {
        if (chat)
        {
            sql += " UNION ALL ";
        }
        sql += "SELECT 1 AS src,2 AS ctype,g.groupid AS cid,0 AS msgid,g.entryid AS entryid,"
               + IntText(::message_type_file) + " AS msgtype,g.uploaderid AS fromid,"
               "IFNULL(g.uploadername,'') AS uname,IFNULL(g.name,'') AS name,g.size AS size,"
               "IFNULL(g.fileid,'') AS fileid,IFNULL(g.md5,'') AS md5,g.uploadtime AS ts,'' AS content,"
               "IFNULL(g.local_path,'') AS lpath,g.filetype AS ftype,0 AS rel,0 AS ord";
        sql += kGroupFrom;
        sql += GroupWhere(filter, nowMs, true, outLikeBinds);
    }
    //图片页：帖子卡片里的每张图片一行，entryid 为图片在卡片里的序号，ord 让同一张卡片的图片按原顺序排
    if (CardImagePartIncluded(filter))
    {
        sql += " UNION ALL ";
        sql += "SELECT " + IntText(kHistoryLibrarySrcCardImage) + " AS src,m.ctype AS ctype,m.cid AS cid,"
               "m.msgid AS msgid,CAST(j.key AS TEXT) AS entryid,m.msgtype AS msgtype,m.fromid AS fromid,"
               "'' AS uname,'' AS name,IFNULL(json_extract(j.value,'$.size'),0) AS size,"
               "IFNULL(json_extract(j.value,'$.fileid'),'') AS fileid,IFNULL(json_extract(j.value,'$.md5'),'') AS md5,"
               "m.timestamp AS ts,IFNULL(m.content,'') AS content,'' AS lpath,0 AS ftype,0 AS rel,j.key AS ord";
        sql += kCardImageFrom;
        sql += CardImageWhere(filter, nowMs, true, outLikeBinds);
    }
    const int page = (filter.m_page >= 1) ? filter.m_page : 1;
    const int pageSize = (filter.m_pageSize > 0) ? filter.m_pageSize : 1;
    const int64_t offset = (int64_t)(page - 1) * (int64_t)pageSize;
    sql += ") ORDER BY ts DESC, src ASC, msgid DESC, ord ASC, entryid DESC LIMIT ";
    sql += IntText(pageSize);
    sql += " OFFSET ";
    sql += IntText(offset);
    return sql;
}

std::string BuildLibraryConvCountSql(const HistoryLibraryFilter& filter, int64_t nowMs, int& outLikeBinds)
{
    outLikeBinds = 0;
    const bool chat = LibraryIncludesChat(filter);
    const bool group = GroupPartIncluded(filter, false);
    if (!chat && !group)
    {
        return std::string();
    }
    std::string sql = "SELECT ctype,cid,SUM(n) FROM (";
    if (chat)
    {
        sql += "SELECT m.ctype AS ctype,m.cid AS cid,COUNT(*) AS n";
        sql += kChatFrom;
        sql += ChatWhere(filter, nowMs, false, outLikeBinds);
        sql += " GROUP BY m.ctype,m.cid";
    }
    if (group)
    {
        if (chat)
        {
            sql += " UNION ALL ";
        }
        sql += "SELECT 2 AS ctype,g.groupid AS cid,COUNT(*) AS n";
        sql += kGroupFrom;
        sql += GroupWhere(filter, nowMs, false, outLikeBinds);
        sql += " GROUP BY g.groupid";
    }
    //图片页：帖子卡片里的图片每张算一项
    if (CardImagePartIncluded(filter))
    {
        sql += " UNION ALL ";
        sql += "SELECT m.ctype AS ctype,m.cid AS cid,COUNT(*) AS n";
        sql += kCardImageFrom;
        sql += CardImageWhere(filter, nowMs, false, outLikeBinds);
        sql += " GROUP BY m.ctype,m.cid";
    }
    sql += ") GROUP BY ctype,cid ORDER BY ctype,cid";
    return sql;
}

void FillLibraryItemFromContent(HistoryLibraryItem& item)
{
    if (item.m_isGroupFile || item.m_content.empty())
    {
        return;
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    const char* begin = item.m_content.c_str();
    if (!reader->parse(begin, begin + item.m_content.size(), &root, &errs) || !root.isObject())
    {
        return;
    }
    //文件消息
    if (root["file"].isObject())
    {
        const Json::Value& file = root["file"];
        if (item.m_name.empty())
        {
            item.m_name = JsonText(file["name"]);
        }
        item.m_size = JsonInt64(file["size"]);
        item.m_fileid = JsonText(file["fileid"]);
        item.m_md5 = JsonText(file["md5"]);
        item.m_direct = (JsonInt64(file["direct"]) != 0);
        return;
    }
    //文件夹传输记录
    if (root["folder"].isObject())
    {
        const Json::Value& folder = root["folder"];
        if (item.m_name.empty())
        {
            item.m_name = JsonText(folder["foldername"]);
        }
        item.m_size = JsonInt64(folder["totalsize"]);
        return;
    }
    //图片与截图
    if (root["image"].isObject())
    {
        const Json::Value& image = root["image"];
        item.m_size = JsonInt64(image["size"]);
        item.m_fileid = JsonText(image["fileid"]);
        item.m_md5 = JsonText(image["md5"]);
    }
}

HistoryRecord RecordFromLibraryItem(const HistoryLibraryItem& item)
{
    HistoryRecord rec;
    rec.m_msgid = item.m_msgid;
    rec.m_ctype = item.m_ctype;
    rec.m_cid = item.m_cid;
    rec.m_fromid = item.m_fromid;
    rec.m_msgtype = item.m_msgtype;
    rec.m_content = item.m_content;
    rec.m_searchText = item.m_name;
    rec.m_timestamp = item.m_timestamp;
    return rec;
}

} // namespace balloonhistory
