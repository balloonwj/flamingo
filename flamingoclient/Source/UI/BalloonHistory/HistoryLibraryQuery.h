/**
 *  聊天记录管理器「文件」「图片」「链接」「与我有关」四页的查询纯逻辑（不依赖 SQLite），HistoryLibraryQuery.h
 *
 *  聊天记录管理器左栏顶部有「聊天 | 文件 | 图片 | 链接」四个标签（docs/聊天记录管理器方案.md §11.5、§11.6、
 *  §12.2）。文件页把本地库里的文件消息、文件夹传输记录与群文件信息缓存表（t_group_file_cache）里的群文件
 *  汇总到一起；图片页汇总聊天里的图片与截图；链接页汇总正文含链接的消息（§12.4）；「聊天」标签左栏第一行
 *  「与我有关」汇总 @我的、回复我的与我发的消息（§12.5）。各页都可以按会话、时间筛选，文件、图片、链接页
 *  另可按「全部 / 我发送的 / 我接收的」筛选，与我有关页改按「全部 / @我的 / 回复我的 / 我发的」筛选；文件页
 *  另可按来源、类型、文件名筛选，链接页与与我有关页可按正文筛选（支持搜索语法与通配符）。
 *
 *  本文件提供：筛选条件与结果行的结构、按扩展名归类、统计 / 分页 / 按会话统计三种 SQL 的拼装、从内容
 *  JSON 补齐文件信息。SQLite 读写在 CV2MsgStore（V2MsgStore.h），本文件只拼语句，便于脱离数据库单测
 *  （Tests/HistoryLibraryTests.cpp，标签 [histfiles] / [histimages] / [histlinks] / [histmentions]）。
 *  在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法：
 *      HistoryLibraryFilter f;
 *      f.m_kind = kHistoryLibraryFiles;
 *      f.m_selfUserId = self;
 *      int likeBinds = 0;
 *      std::string sql = BuildLibraryPageSql(f, nowMs, likeBinds);
 *      // 带关键字条件的部分有 likeBinds 个，每个部分依次绑定 SearchSyntaxLikeBinds(f.m_keyword, f.m_wildcard)
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "V2MsgQuery.h"   // HistoryTimeRange / HistoryRecord

namespace balloonhistory {

//汇总的是哪一类内容（左栏标签「文件」「图片」「链接」，以及「聊天」标签会话列表第一行「与我有关」）
enum HistoryLibraryKind
{
    kHistoryLibraryFiles    = 0,   //文件页：文件消息、文件夹传输记录、群文件
    kHistoryLibraryImages   = 1,   //图片页：聊天里的图片与截图
    kHistoryLibraryLinks    = 2,   //链接页：正文含链接的消息（docs/聊天记录管理器方案.md §12.4）
    kHistoryLibraryMentions = 3,   //与我有关页：@我的、回复我的、我发的消息（§12.5）
};

//与我有关页的筛选胶囊（取值与胶囊下标一一对应）
enum HistoryLibraryRelation
{
    kHistoryLibraryRelAll     = 0,   //全部：下面三类的并集
    kHistoryLibraryRelMention = 1,   //@我的：content.at 里有本人，或 @所有人；不含本人自己发的
    kHistoryLibraryRelReply   = 2,   //回复我的：content.quote.from 是本人；不含本人自己发的
    kHistoryLibraryRelMine    = 3,   //我发的：发送人是本人
};

//第一级分类：谁发的
enum HistoryLibraryDirection
{
    kHistoryLibraryDirAll      = 0,   //全部
    kHistoryLibraryDirSent     = 1,   //我发送的：发送人是本人；群文件为本人上传的
    kHistoryLibraryDirReceived = 2,   //我接收的：发送人不是本人；群文件为别人上传的
};

//文件页的来源筛选（下拉框，取值与下拉项下标一一对应）
enum HistoryLibrarySource
{
    kHistoryLibrarySourceAll   = 0,   //聊天文件和群文件
    kHistoryLibrarySourceChat  = 1,   //只看聊天里的文件消息与文件夹传输记录
    kHistoryLibrarySourceGroup = 2,   //只看群文件
};

//文件页的类型筛选（下拉框，取值与下拉项下标一一对应）。1～6 与服务端 group_file_type 的取值相同，
//扩展名归类规则也与服务端 GroupFileProtocol.cpp 的那张表一致，聊天文件与群文件因此按同一口径归类
enum HistoryFileCategory
{
    kHistoryFileCatAll      = 0,   //全部类型
    kHistoryFileCatDocument = 1,   //文档：doc/docx/xls/xlsx/ppt/pptx/pdf/txt/rtf/csv/md
    kHistoryFileCatImage    = 2,   //图片：jpg/jpeg/png/gif/bmp/webp（以文件形式发送的图片）
    kHistoryFileCatVideo    = 3,   //视频：mp4/avi/mov/mkv/wmv/flv
    kHistoryFileCatAudio    = 4,   //音频：mp3/wav/aac/flac/ogg/m4a
    kHistoryFileCatArchive  = 5,   //压缩包：zip/rar/7z/tar/gz
    kHistoryFileCatOther    = 6,   //其它：扩展名不在以上各类里的文件
    kHistoryFileCatFolder   = 7,   //文件夹：单聊里的文件夹传输记录（群文件没有这一类）
};

//文件 / 图片页的一次查询条件
struct HistoryLibraryFilter
{
    int               m_kind;          //汇总哪一类，取值见 HistoryLibraryKind
    int               m_direction;     //谁发的，取值见 HistoryLibraryDirection
    int               m_source;        //来源，取值见 HistoryLibrarySource；图片页不使用
    int               m_category;      //文件类型，取值见 HistoryFileCategory；图片页不使用
    HistoryTimeRange  m_range;         //时间范围
    int64_t           m_customMinMs;   //自定义时间起（含），Unix 毫秒；仅 m_range 为自定义时有效
    int64_t           m_customMaxMs;   //自定义时间止（含），Unix 毫秒；仅 m_range 为自定义时有效
    //关键字（UTF-8）；空串表示不筛选。文件页匹配文件名，只认多个词、排除词与引号；链接页与与我有关页匹配消息正文，
    //按完整的搜索语法解释（HistorySearchSyntax.h）。图片页不使用
    std::string       m_keyword;
    bool              m_wildcard;      //关键字是否按通配符解释（规则见 HistoryWildcard.h）
    //关键字里 from:名字 解析出的 userid（链接页与与我有关页用，含义同 HistoryFilter::m_syntaxFromIds）
    std::vector<int64_t> m_syntaxFromIds;
    int               m_relation;      //与我有关页的筛选胶囊，取值见 HistoryLibraryRelation；其它页不使用
    int64_t           m_selfUserId;    //本人 userid，判断「我发送的 / 我接收的」用；不大于 0 时方向筛选不生效
    //只看某一个会话：m_cid 不为 0 时只查 (m_ctype, m_cid) 这个会话（左栏点了某个会话）；为 0 表示全部会话
    int               m_ctype;
    int64_t           m_cid;
    //全部会话时只看单聊（1）或只看群聊（2）；0 表示不限。左栏的「全部 / 单聊 / 群聊」胶囊
    int               m_ctypeOnly;
    int               m_page;          //页号（从 1 起）
    int               m_pageSize;      //每页条数（大于 0）

    HistoryLibraryFilter()
        : m_kind(kHistoryLibraryFiles)
        , m_direction(kHistoryLibraryDirAll)
        , m_source(kHistoryLibrarySourceAll)
        , m_category(kHistoryFileCatAll)
        , m_range(kHistoryTimeAll)
        , m_customMinMs(0)
        , m_customMaxMs(0)
        , m_wildcard(false)
        , m_relation(kHistoryLibraryRelAll)
        , m_selfUserId(0)
        , m_ctype(1)
        , m_cid(0)
        , m_ctypeOnly(0)
        , m_page(1)
        , m_pageSize(20)
    {
    }
};

//群文件信息缓存表（t_group_file_cache）的一行：服务端 cmd_get_group_files 列表项的本地副本，
//再加上本机下载后的落地路径。字段含义见 net::GroupFileEntry（net/GroupFileProtocolClient.h）
struct GroupFileCacheItem
{
    int64_t     m_groupid;       //所在群 ID
    std::string m_entryid;       //群文件条目 ID（字符串编码的 int64，全局唯一）
    std::string m_fileid;        //文件本体 ID（字符串编码的 int64，下载用）
    std::string m_name;          //原始文件名（UTF-8，含扩展名）
    std::string m_ext;           //扩展名，不含点（UTF-8，可空）
    int64_t     m_size;          //文件总字节数
    std::string m_md5;           //文件内容 MD5（小写十六进制）
    int         m_filetype;      //服务端的文件类别，取值同 HistoryFileCategory 的 1～6
    int64_t     m_uploaderid;    //上传者 userid
    std::string m_uploaderName;  //上传者显示名（UTF-8，服务端解析好的）
    int64_t     m_uploadTime;    //登记时间（Unix 毫秒）
    std::string m_localPath;     //本机下载后的落地完整路径（UTF-8）；从没下载过为空串

    GroupFileCacheItem()
        : m_groupid(0)
        , m_size(0)
        , m_filetype(0)
        , m_uploaderid(0)
        , m_uploadTime(0)
    {
    }
};

//分页查询结果第 0 列「来源」的取值（见 BuildLibraryPageSql）。与文件页的来源筛选 HistoryLibrarySource 不是一回事
enum HistoryLibraryRowSource
{
    kHistoryLibrarySrcChat      = 0,   //聊天里的一条消息（文件、文件夹、图片、截图、链接等）
    kHistoryLibrarySrcGroupFile = 1,   //群文件缓存表里的一个群文件（只在文件页出现）
    kHistoryLibrarySrcCardImage = 2,   //帖子卡片或补充卡片里的一张图片（只在图片页出现，docs/帖子群方案.md 14.5）：
                                       //一张卡片有几张图片就有几行，entryid 列是图片在卡片里的序号
};

//文件 / 图片页的一行：来自 t_msg 的一条消息，或来自 t_group_file_cache 的一个群文件，或帖子卡片里的一张图片
struct HistoryLibraryItem
{
    bool        m_isGroupFile;   //true 表示群文件（来自缓存表），false 表示聊天里的一条消息
    int         m_ctype;         //所在会话类型：1 单聊 2 群聊（群文件恒为 2）
    int64_t     m_cid;           //所在会话 ID（单聊为对端 userid，群聊为群 ID）
    int64_t     m_msgid;         //聊天消息的服务器 msgid；群文件为 0
    std::string m_entryid;       //群文件条目 ID；聊天消息为空串
    int         m_msgtype;       //消息类型：文件 3、文件夹 18、图片 2、截图 9；群文件按文件记为 3
    int64_t     m_fromid;        //发送人 userid；群文件为上传者
    std::string m_uploaderName;  //群文件的上传者显示名（UTF-8）；聊天消息为空串，由界面按 m_fromid 解析
    std::string m_name;          //文件名或文件夹名（UTF-8）；图片为空串
    int64_t     m_size;          //字节数；文件夹为总大小；图片取内容里的大小，没有时为 0
    std::string m_fileid;        //服务端文件 ID（下载用）；文件夹为空串
    std::string m_md5;           //内容 MD5；文件夹为空串
    int64_t     m_timestamp;     //消息时间或群文件登记时间（Unix 毫秒）
    std::string m_content;       //聊天消息的归一化内容 JSON（BuildContentJson 的格式）；群文件为空串
    std::string m_localPath;     //群文件登记的本机落地路径（UTF-8）；聊天消息为空串（按内容另行查找）
    int         m_filetype;      //群文件的服务端类别；聊天消息为 0
    bool        m_direct;        //聊天文件是否为在线直传：服务器上没有副本，无法从服务器下载
    //与我有关页里这一行属于哪一类：kHistoryLibraryRelMention / Reply / Mine；其它页恒为 kHistoryLibraryRelAll（0）。
    //一条消息同时 @我又回复我时记为回复我
    int         m_relation;
    //帖子卡片或补充卡片里的第几张图片（0 起，docs/帖子群方案.md 14.5）；不是卡片图片时为 -1。为卡片图片时 m_fileid、
    //m_md5、m_size 取自这张图片，m_content 是整张卡片（看图时取全部图片左右翻看）
    int         m_cardImageIndex;

    HistoryLibraryItem()
        : m_isGroupFile(false)
        , m_ctype(1)
        , m_cid(0)
        , m_msgid(0)
        , m_msgtype(0)
        , m_fromid(0)
        , m_size(0)
        , m_timestamp(0)
        , m_filetype(0)
        , m_direct(false)
        , m_relation(kHistoryLibraryRelAll)
        , m_cardImageIndex(-1)
    {
    }
};

//按会话统计的一项：左栏每一行右侧的「N 个」
struct HistoryLibraryConvCount
{
    int     m_ctype;   //会话类型：1 单聊 2 群聊
    int64_t m_cid;     //会话 ID
    int     m_count;   //当前筛选条件下这个会话里的条数

    HistoryLibraryConvCount()
        : m_ctype(1)
        , m_cid(0)
        , m_count(0)
    {
    }
};

/**
 *  取文件名的扩展名：最后一个 '.' 之后的部分，ASCII 字母转成小写。没有 '.'、'.' 在最前面（如 ".gitignore"）
 *  或在最后面时返回空串。
 *  @param nameUtf8 文件名（UTF-8）
 *  @return 小写扩展名，不含点
 */
std::string FileExtFromName(const std::string& nameUtf8);

/**
 *  按扩展名给文件归类，规则与服务端 GroupFileProtocol.cpp 的扩展名表一致。
 *  @param nameUtf8 文件名（UTF-8，含扩展名）
 *  @return HistoryFileCategory 的 1～6 之一；扩展名不在任何一类里（含没有扩展名）时为 kHistoryFileCatOther
 */
int FileCategoryFromName(const std::string& nameUtf8);

/**
 *  这次查询要不要查聊天消息（t_msg）：文件页来源不是「只看群文件」；其它页总要查。
 *  @param filter 查询条件
 *  @return 要查时返回 true
 */
bool LibraryIncludesChat(const HistoryLibraryFilter& filter);

/**
 *  这次查询要不要查群文件缓存表：文件页、来源不是「只看聊天文件」、类型不是「文件夹」，并且没有限定到
 *  单聊会话（只看某个单聊、或左栏只看单聊）。
 *  @param filter 查询条件
 *  @return 要查时返回 true
 */
bool LibraryIncludesGroupFiles(const HistoryLibraryFilter& filter);

/**
 *  关键字筛选是否生效：图片页不生效；文件页要求按搜索语法解析后有词或排除词（文件名只认这两样）；链接页与
 *  与我有关页要求解析后有任一条件（SearchQueryHasCondition）。勾选通配符时只输了 '*' 的词不算。
 *  @param filter 查询条件
 *  @return 生效时返回 true
 */
bool LibraryHasKeyword(const HistoryLibraryFilter& filter);

/**
 *  拼统计总条数的语句（一行一列）。聊天消息与群文件两部分各自统计后相加。关键字筛选生效时每部分各带一段
 *  搜索语法条件（BuildSearchSyntaxClause），其中的 LIKE / NOT LIKE 占位由调用方按部分依次绑定：outLikeBinds
 *  个部分，每部分绑定一遍 SearchSyntaxLikeBinds(m_keyword, m_wildcard) 的全部匹配串。
 *  @param filter       查询条件（页号、每页条数不参与）
 *  @param nowMs        当前时间（Unix 毫秒），供「近 N 天」换算
 *  @param outLikeBinds [出参] 带关键字条件的部分个数（0～2）
 *  @return 完整 SQL；两部分都不用查时返回空串，调用方应直接把总数当作 0
 */
std::string BuildLibraryCountSql(const HistoryLibraryFilter& filter, int64_t nowMs, int& outLikeBinds);

/**
 *  拼分页查询的语句：两部分 UNION ALL 后按时间倒序（同一时刻聊天消息在前，再按 msgid、条目 ID 倒序，
 *  保证每次排出来的顺序一样），取第 m_page 页。结果列依次为：0 来源（0 聊天消息、1 群文件），1 ctype，
 *  2 cid，3 msgid，4 entryid，5 msgtype，6 fromid，7 上传者名，8 名称，9 大小，10 fileid，11 md5，
 *  12 时间，13 内容 JSON，14 本机路径，15 群文件类别，16 关系（与我有关页为 HistoryLibraryRelation 的
 *  1～3，其它页为 0）。占位规则同 BuildLibraryCountSql。
 *  @param filter       查询条件
 *  @param nowMs        当前时间（Unix 毫秒）
 *  @param outLikeBinds [出参] 带关键字条件的部分个数（0～2）
 *  @return 完整 SQL；两部分都不用查时返回空串，调用方应直接返回空结果
 */
std::string BuildLibraryPageSql(const HistoryLibraryFilter& filter, int64_t nowMs, int& outLikeBinds);

/**
 *  拼按会话统计的语句：每个会话在当前筛选条件下有几条，供左栏每一行右侧显示。不受 m_ctype / m_cid /
 *  m_ctypeOnly 限制（左栏列出的是全部会话）。结果列：0 ctype，1 cid，2 条数，按 ctype、cid 升序。
 *  占位规则同 BuildLibraryCountSql。
 *  @param filter       查询条件
 *  @param nowMs        当前时间（Unix 毫秒）
 *  @param outLikeBinds [出参] 带关键字条件的部分个数（0～2）
 *  @return 完整 SQL；两部分都不用查时返回空串
 */
std::string BuildLibraryConvCountSql(const HistoryLibraryFilter& filter, int64_t nowMs, int& outLikeBinds);

/**
 *  用聊天消息的内容 JSON 补齐一行的文件信息：文件消息取 file 子对象的 name / size / fileid / md5 / direct，
 *  文件夹记录取 folder 子对象的 foldername / totalsize，图片取 image 子对象的 size / fileid / md5。
 *  名称已有（来自 search_text）时不覆盖。内容为空或解析失败时什么都不改。群文件行不处理。
 *  @param item [入出参] 一行
 */
void FillLibraryItemFromContent(HistoryLibraryItem& item);

/**
 *  把聊天消息那一行转成 HistoryRecord，供按记录找本机文件（ResolveRecordLocalPath）、转发等现成函数使用。
 *  @param item 一行（应为聊天消息）
 *  @return 记录；msgid、会话、发送人、类型、内容、名称（search_text）、时间取自 item，其余为默认值
 */
HistoryRecord RecordFromLibraryItem(const HistoryLibraryItem& item);

} // namespace balloonhistory
