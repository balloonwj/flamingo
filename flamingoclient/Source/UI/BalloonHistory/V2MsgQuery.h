/**
 *  v2 历史消息本地库——查询纯逻辑（不依赖 SQLite），V2MsgQuery.h
 *
 *  本文件承载“历史消息记录窗口”的可单测纯逻辑：URL 识别、关键字搜索文本构造、
 *  内容 JSON 归一、时间区间换算、类型筛选到 SQL 片段、COUNT/分页 SQL 拼装，以及
 *  把网络层 net::MessageNotify 转成本地入库记录 HistoryRecord。这些逻辑与 SQLite
 *  读写（见 V2MsgStore）严格隔离，便于脱离数据库直接做单元测试，对应用例见
 *  flamingoclient/Source/Tests/V2MsgQueryTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <string>
#include <vector>
#include <stdint.h>

//前置声明：本头只按 const 引用使用 net::MessageNotify，不需其完整定义，避免把
//jsoncpp 等实现细节带进本头。完整定义见 net/MessageProtocolClient.h。
namespace net { struct MessageNotify; }

namespace balloonhistory {

//类型筛选胶囊——对应设计稿顶部一排可点选标签（全部/文字/图片/文件/语音/链接）。
enum HistoryTypeFilter
{
    kHistoryTypeAll   = 0,   //全部：不按内容类型过滤
    kHistoryTypeText  = 1,   //文字：纯文本且不含 URL（msgtype=文本 且 is_link=0）
    kHistoryTypeImage = 2,   //图片：普通图片 + 截图（msgtype IN(图片,截图)），以及带图片、未删除的帖子卡片与补充卡片
    kHistoryTypeFile  = 3,   //文件：msgtype=文件
    kHistoryTypeVoice = 4,   //语音：msgtype=语音
    kHistoryTypeLink  = 5,   //链接：正文含 URL 的文本（is_link=1）
    kHistoryTypePinned = 6,  //Pin：本会话中被钉住的消息（pinned=1），<u>不分内容类型</u>。
                             //胶囊是单选的，故选了它就看不成"只看被钉的图片"——这是本轮
                             //刻意的取舍，见 docs/消息Pin接入方案.md §7.3
};

//时间范围下拉——对应界面右上角的时间筛选。
//枚举值<u>刻意与下拉项下标一一对应</u>（0 起、按界面上从上到下的顺序排），
//TimeRangeFromComboIndex / ComboIndexFromRange 因此只是一层带兜底的取值检查。
//例外是最后的 kHistoryTimeCustom：它不在下拉框里，由「选日期」弹层的「只看这段时间」设置。
//该值只在进程内使用，不落盘、不上网，故日后调整顺序无需考虑兼容。
enum HistoryTimeRange
{
    kHistoryTimeAll     = 0,   //全部时间：不按时间过滤
    kHistoryTime7Days   = 1,   //近7天：[now-7天, now]
    kHistoryTime30Days  = 2,   //近30天：[now-30天, now]
    kHistoryTime3Months = 3,   //近3个月：[now-90天, now]（按固定天数折算，见 TimeRangeToBounds）
    kHistoryTime6Months = 4,   //近半年：[now-180天, now]（同上，按固定天数折算）
    kHistoryTime1Year   = 5,   //近一年：[now-365天, now]（同上，不区分闰年）
    kHistoryTimeCustom  = 6,   //自定义：用 m_customMinMs / m_customMaxMs 指定起止
};

//一次历史消息查询的过滤条件，供 BuildCountSql / BuildPageSql 拼装 SQL。
struct HistoryFilter
{
    int               m_ctype;        //会话类型：1单聊 2群聊
    int64_t           m_cid;          //会话ID（我方视角）：单聊=对端userid，群聊=群id
    HistoryTypeFilter m_type;         //类型筛选胶囊
    HistoryTimeRange  m_range;        //时间范围
    int64_t           m_customMinMs;  //自定义时间起（含），Unix毫秒；仅 m_range==Custom 有效
    int64_t           m_customMaxMs;  //自定义时间止（含），Unix毫秒；仅 m_range==Custom 有效
    std::string       m_keyword;      //关键字（UTF-8）；空串=不按关键字过滤
    //关键字是否按通配符解释（搜索框旁「通配符」复选框，docs/聊天记录管理器方案.md §11.4）：
    //true 时 '*' 匹配任意个字符、'?' 匹配一个字符，规则见 HistoryWildcard.h；false 时二者按普通字符。
    //关键字为空时没有意义
    bool              m_wildcard;
    //发送人筛选：只查这个 userid 发的消息；0 表示不限。Pin 分类同样受它限制
    int64_t           m_fromid;
    //关键字里 from:名字 解析出的 userid（搜索语法，docs/聊天记录管理器方案.md §12.3）：本地库只有 userid，名字由窗口
    //按显示名匹配后填进来。关键字里有 from: 时只查这些人发的；为空表示一个都没匹配上，这次搜索没有结果。
    //关键字里没有 from: 时不使用
    std::vector<int64_t> m_syntaxFromIds;
    int               m_page;         //页号（从1起）
    int               m_pageSize;     //每页条数（>0）

    //显示下界 msgid：只查 msgid 不小于它的消息；0 表示不限制。由消息记录窗按该会话的
    //覆盖区间算出（见 HistoryCoverage.h 的 CoverageMinVisibleMsgid），断层以下的旧行
    //不显示。Pin 分类不受它限制 —— Pin 列表是服务端单独给的全集。
    int64_t           m_minMsgid;

    HistoryFilter()
        : m_ctype(1)
        , m_cid(0)
        , m_type(kHistoryTypeAll)
        , m_range(kHistoryTimeAll)
        , m_customMinMs(0)
        , m_customMaxMs(0)
        , m_wildcard(false)
        , m_fromid(0)
        , m_page(1)
        , m_pageSize(20)
        , m_minMsgid(0)
    {
    }
};

//本地库里一条消息记录。既用于入库（Upsert），也用于查询返回。
struct HistoryRecord
{
    int64_t     m_msgid;        //服务器消息ID（全局自增、单调递增）
    std::string m_clientmsgid;  //客户端消息ID（UUID）
    int         m_ctype;        //会话类型：1单聊 2群聊
    int64_t     m_cid;          //会话ID（我方视角）：单聊=对端userid，群聊=群id
    int64_t     m_fromid;       //发送方userid（渲染时据此取昵称/头像）
    int         m_msgtype;      //内容类型，取值见 net::message_type
    std::string m_content;      //归一化内容JSON（文本含 text/font；图片/文件含元信息）
    std::string m_searchText;   //关键字 LIKE 目标：正文 + 文件名
    bool        m_isLink;       //正文是否含 URL（入库时预算，供“链接”筛选）
    int64_t     m_timestamp;    //消息时间（Unix毫秒）
    bool        m_recalled;     //是否已撤回（撤回的不计数、不出现在查询结果里）
    bool        m_pinned;       //是否被Pin（会话内共享的状态，由服务端推送与对账维护）
    int64_t     m_pinTime;      //Pin时间（Unix毫秒）；未Pin时为 0
    std::string m_raw;          //服务端发来的整条消息的原始JSON（net::MessageNotify::m_rawJson）；
                                //打开聊天窗时据此还原消息先行显示。老数据与查询时未取此列的结果为空串
    bool        m_played;       //语音消息是否已听（只在写入时使用：新行按它写 played 列，已有的行不改）；
                                //默认 true，对方发来的语音由 RecordFromNotify 的四参数版本置为 false

    HistoryRecord()
        : m_msgid(0)
        , m_ctype(1)
        , m_cid(0)
        , m_fromid(0)
        , m_msgtype(1)
        , m_isLink(false)
        , m_timestamp(0)
        , m_recalled(false)
        , m_pinned(false)
        , m_pinTime(0)
        , m_played(true)
    {
    }
};

//判断一段文本是否含 URL（http:// 或 https://，大小写不敏感）。用于入库时预算
//HistoryRecord::m_isLink，进而支撑“链接”类型筛选。
//@param text  待判定文本（UTF-8）
//@return 含 http(s):// 返回 true，否则 false
bool IsLink(const std::string& text);

//把发送正文里的内联表情标记 /f["<faceId>"] 净化成固定占位“[表情]”，并把转义斜杠
//“//”还原成“/”。口径与 balloonchat::BodyToDigest 一致（会话列表 / 通知用同款），使
//历史窗一行预览与搜索文本里不暴露 /f["id"] 原始标记。纯 UTF-8 字节扫描，不依赖 ATL。
//@param body  发送正文（UTF-8，可能含 /f["id"] 与转义 //）
//@return 净化后的可读文本（UTF-8）
std::string DigestFaceTokens(const std::string& body);

//由一条网络消息构造关键字搜索文本（写入 HistoryRecord::m_searchText）。文本/链接
//取正文（经 DigestFaceTokens 净化表情）；文件取文件名；合并转发的聊天记录取标题加
//各条子消息的搜索文本（嵌套的记录逐层展开）；图片等无可搜文本返回空串。
//供 search_text LIKE 检索。
//@param notify  网络层解析出的消息
//@return 搜索文本（UTF-8）
std::string BuildSearchText(const net::MessageNotify& notify);

//由一条网络消息构造本地归一化内容JSON（写入 HistoryRecord::m_content）。文本写
//{text,font?}；图片写 {image:{fileid,md5,w,h,size}}；文件写 {file:{fileid,name,
//size,md5}}；合并转发的聊天记录写 {merged:<content 原文>}；其它类型写空对象 {}。
//输出紧凑无缩进。
//@param notify  网络层解析出的消息
//@return 内容JSON文本（UTF-8）
std::string BuildContentJson(const net::MessageNotify& notify);

//把网络消息转成本地入库记录。会话归属由调用方传入（实时收到用 notify 自身的
//ctype/cid；同步/回填用本批会话的 ctype/cid），其余字段从 notify 取。
//@param notify  网络层解析出的消息
//@param ctype   会话类型（1单聊 2群聊）
//@param cid     会话ID（我方视角）
//@return 入库记录
HistoryRecord RecordFromNotify(const net::MessageNotify& notify, int ctype, int64_t cid);

/**
 *  同上，并按本人 userid 定下语音消息的「已听」初值（docs/语音消息方案.md V7）：对方发来的语音记为未听，
 *  本人发的与其它类型记为已听。主面板写本地库时用这个版本。
 *  @param notify     消息
 *  @param ctype      会话类型
 *  @param cid        会话ID（我方视角）
 *  @param selfUserId 本人 userid
 *  @return 记录
 */
HistoryRecord RecordFromNotify(const net::MessageNotify& notify, int ctype, int64_t cid, int64_t selfUserId);
//用入库记录里的原始JSON还原出网络消息，结果与当初网络应答解析出的完全相同，
//只有四样以本地库为准：撤回状态（实时收到的撤回只更新了库里的 recalled 列，原始JSON不变）；
//接龙卡片的内容（收到 cmd_relay_changed_notify 后只更新了 content 列，见 RelayContentFromRecord）；
//屏幕共享卡片的内容（收到 cmd_screenshare_info_notify 后只更新了 content 列，见 ScreenShareContentFromRecord）；
//入群邀请卡片的内容（收到 cmd_group_invite_changed_notify 后只更新了 content 列，见 GroupInviteContentFromRecord）。
//打开聊天窗时先用本地数据显示即靠它（见 CV2MsgStore::QueryLatest）。
//@param rec  入库记录，须带原始JSON（m_raw）
//@param out  还原出的网络消息
//@return 还原成功返回 true；记录没有原始JSON（老数据）或原始JSON解析失败时返回 false
bool NotifyFromRecord(const HistoryRecord& rec, net::MessageNotify& out);
//从一行入库记录里取出接龙卡片的最新内容（紧凑 JSON，即 net::MessageNotify::m_relayContent
//的格式）。收到 cmd_relay_changed_notify 后只更新 content 列（2026-10-02 与用户确认，与撤回同一
//做法，原始JSON保持服务端发来的原样），故先取 content 列的 relay 子对象；没有（content 列写入于
//本功能之前的旧行）或为空对象时，退回原始JSON里的 content。
//@param rec        入库记录，msgtype 应为 message_type_relay
//@param relayJson  [出参] 卡片内容；只在返回 true 时写入
//@return 取到返回 true；两处都取不到时返回 false
bool RelayContentFromRecord(const HistoryRecord& rec, std::string& relayJson);
//从一行入库记录里取出屏幕共享卡片的最新内容（紧凑 JSON，即 net::MessageNotify::m_screenShareContent 的格式）。
//发起人在共享中改了标题与描述后（第 15 期，cmd_screenshare_info_notify），与接龙卡片同一做法只更新 content 列，
//故先取 content 列的 screenshare 子对象；没有或为空对象时，退回原始JSON里的 content。
//@param rec          入库记录，msgtype 应为 message_type_screenshare
//@param contentJson  [出参] 卡片内容；只在返回 true 时写入
//@return 取到返回 true；两处都取不到时返回 false
bool ScreenShareContentFromRecord(const HistoryRecord& rec, std::string& contentJson);
//从一行入库记录里取出群通话卡片的最新内容（紧凑 JSON，即 net::MessageNotify::m_groupCallContent 的格式）。
//通话结束时服务端改写了卡片（加上 endtime / duration），本地与屏幕共享卡片同一做法只更新 content 列，
//故先取 content 列的 groupcall 子对象；没有或为空对象时，退回原始JSON里的 content。
//@param rec          入库记录，msgtype 应为 message_type_rtc
//@param contentJson  [出参] 卡片内容；只在返回 true 时写入
//@return 取到返回 true；两处都取不到时返回 false
bool GroupCallContentFromRecord(const HistoryRecord& rec, std::string& contentJson);
//从一行入库记录里取出入群邀请卡片的最新内容（紧凑 JSON，即 net::MessageNotify::m_groupInviteContent 的格式）。
//收到卡片状态变化推送（cmd_group_invite_changed_notify）后与接龙卡片同一做法只更新 content 列，
//故先取 content 列的 groupinvite 子对象；没有或为空对象时，退回原始JSON里的 content。
//@param rec          入库记录，msgtype 应为 message_type_group_invite
//@param contentJson  [出参] 卡片内容；只在返回 true 时写入
//@return 取到返回 true；两处都取不到时返回 false
bool GroupInviteContentFromRecord(const HistoryRecord& rec, std::string& contentJson);
//从一行入库记录里取出群公告卡片的最新内容（紧凑 JSON，即 net::MessageNotify::m_announcementContent 的格式）。
//公告被删除后服务端改写了卡片（去掉正文、带 deleted），本地收到 1326 推送时与入群邀请卡片同一做法只更新 content 列，
//故先取 content 列的 announcement 子对象；没有或为空对象时，退回原始JSON里的 content。
//@param rec          入库记录，msgtype 应为 message_type_announcement
//@param contentJson  [出参] 卡片内容；只在返回 true 时写入
//@return 取到返回 true；两处都取不到时返回 false
bool AnnouncementContentFromRecord(const HistoryRecord& rec, std::string& contentJson);

//把时间范围换算成 [minMs, maxMs] 闭区间边界。全部时间返回 [0, INT64_MAX]；各个
//“近 N”档以 nowMs 为右端、左端回退对应天数（3个月/半年/一年分别按 90/180/365 天
//折算，不做日历月份加减）；自定义原样返回给定起止。
//@param range        时间范围
//@param nowMs        当前时间（Unix毫秒），由调用方传入以便测试可控
//@param customMinMs  自定义起（仅 range==Custom 用）
//@param customMaxMs  自定义止（仅 range==Custom 用）
//@param outMinMs     [出参] 区间左端（含）
//@param outMaxMs     [出参] 区间右端（含）
void TimeRangeToBounds(HistoryTimeRange range, int64_t nowMs,
                       int64_t customMinMs, int64_t customMaxMs,
                       int64_t& outMinMs, int64_t& outMaxMs);

//把类型筛选胶囊换算成 SQL 的 WHERE 片段（不含前导 AND；kHistoryTypeAll 返回空串）。
//例：文字→"msgtype=1 AND is_link=0"；图片→"msgtype IN (2,9)"；链接→"is_link=1"；
//Pin→"pinned=1"。
//@param type  类型筛选胶囊
//@return SQL 片段；kHistoryTypeAll 返回空串
std::string TypeFilterToSqlFragment(HistoryTypeFilter type);

//关键字筛选是否生效：按搜索语法（HistorySearchSyntax.h）解析关键字后有任何条件（正向词、排除词、from、type、
//日期或写错的限定值）。勾选了通配符时去掉 '*' 之后为空的词不算（只输了 '*' 的按没有关键字处理）。生效时
//BuildCountSql 等语句里才有关键字的占位，调用方据此决定绑不绑定，空状态文字、「查看上下文」等也据此判断是否在
//按关键字筛选。
//@param filter  过滤条件
//@return 关键字筛选生效时返回 true
bool HasKeywordFilter(const HistoryFilter& filter);

/**
 *  按搜索语法（docs/聊天记录管理器方案.md §12.3）拼关键字的条件，以 " AND " 开头，各项依次为：每个正向词一个
 *  "<文字列> LIKE ? ESCAPE '\'"，每个排除词一个 "<文字列> NOT LIKE ? ESCAPE '\'"，然后（termsOnly 为 false 时）
 *  type、after / before 与 from 的条件。占位的绑定顺序与 SearchSyntaxLikeBinds 给出的顺序相同。
 *  有写错的限定值、或写了 from: 而 fromIds 为空时条件里有 " AND 0"（没有结果）。
 *  @param keyword     搜索框里的输入（UTF-8）
 *  @param wildcard    是否勾选了通配符
 *  @param fromIds     from: 名字解析出的 userid（见 HistoryFilter::m_syntaxFromIds）
 *  @param textColumn  匹配文字的列，如 "search_text"、"m.search_text"、"g.name"
 *  @param termsOnly   true 时只拼正向词与排除词，限定词一概不用（文件页按文件名搜索）
 *  @return 条件文字；没有任何条件时为空串
 */
std::string BuildSearchSyntaxClause(const std::string& keyword, bool wildcard, const std::vector<int64_t>& fromIds,
                                    const char* textColumn, bool termsOnly);

/**
 *  BuildSearchSyntaxClause 拼出的 LIKE / NOT LIKE 占位依次要绑定的匹配串：先各正向词、后各排除词，每个都经
 *  MakeSearchLikePattern 转换。
 *  @param keyword   搜索框里的输入（UTF-8）
 *  @param wildcard  是否勾选了通配符
 *  @return 匹配串，个数等于条件里占位的个数
 */
std::vector<std::string> SearchSyntaxLikeBinds(const std::string& keyword, bool wildcard);

//拼装统计总数的 SQL（SELECT COUNT(*) ...）。关键字筛选生效时（HasKeywordFilter）语句里有一个
//"search_text LIKE ? ESCAPE '\'" 占位，由调用方绑定 MakeSearchLikePattern(m_keyword, m_wildcard)
//的结果；数值类条件内联；WHERE 恒含 recalled=0。
//@param filter  过滤条件
//@param nowMs   当前时间（Unix毫秒）
//@return 完整 SQL 文本
std::string BuildCountSql(const HistoryFilter& filter, int64_t nowMs);

//拼装分页查询的 SQL（SELECT 各列 ... ORDER BY timestamp DESC, msgid DESC LIMIT
//pageSize OFFSET (page-1)*pageSize）。关键字以 ? 占位；WHERE 恒含 recalled=0。
//@param filter  过滤条件（含页号/页大小）
//@param nowMs   当前时间（Unix毫秒）
//@return 完整 SQL 文本
std::string BuildPageSql(const HistoryFilter& filter, int64_t nowMs);

//拼"在当前排序下排在某条消息前面的有几条"的统计 SQL。排序与 BuildPageSql 相同（时间倒序，
//同毫秒按 msgid 倒序），所以排在前面的是：时间更晚的，或时间相同但 msgid 更大的。
//关键字占位与 BuildCountSql 相同，由 CV2MsgStore 绑定。
//@param filter     过滤条件（页号、每页条数不参与）
//@param nowMs      当前时间（Unix毫秒），供"近N天"换算
//@param timestamp  那条消息的时间（Unix毫秒）
//@param msgid      那条消息的 msgid；传 INT64_MAX 时等于"时间晚于 timestamp 的有几条"
std::string BuildCountBeforeSql(const HistoryFilter& filter, int64_t nowMs, int64_t timestamp, int64_t msgid);

//==================== 时间轴模式（docs/聊天记录管理器方案.md §11.7） ====================

//按本地日期分段统计的一段：某个月或某一天，以及这一段里的条数
struct HistoryPeriodCount
{
    //分段键：按月统计时是 年*100+月（如 202610），按天统计时是 年*10000+月*100+日（如 20261001）
    int64_t m_key;
    int64_t m_count;   //这一段里符合条件的条数

    HistoryPeriodCount()
        : m_key(0)
        , m_count(0)
    {
    }

    HistoryPeriodCount(int64_t key, int64_t count)
        : m_key(key)
        , m_count(count)
    {
    }
};

//一条消息在列表排序里的位置：时间与 msgid（排序同 BuildPageSql：时间倒序，同毫秒按 msgid 倒序）
struct HistoryPosition
{
    int64_t m_timestamp;   //消息时间（Unix 毫秒）
    int64_t m_msgid;       //服务器消息ID

    HistoryPosition()
        : m_timestamp(0)
        , m_msgid(0)
    {
    }

    HistoryPosition(int64_t timestamp, int64_t msgid)
        : m_timestamp(timestamp)
        , m_msgid(msgid)
    {
    }
};

//拼「按本地时间的年月分组统计」的 SQL：WHERE 与 BuildCountSql 相同（关键字筛选生效时同样有一个
//LIKE ? 占位），按 strftime(..., 'localtime') 换算成本地年月后分组。
//结果列：0 年月键（年*100+月），1 条数；按年月倒序。
//对每一行都做一次本地时间换算，10 万条的会话要两三百毫秒，本地库的 CountByLocalMonth 已改用
//BuildMonthRangeCountSql；本函数留作单测里核对两种写法结果一致的参照。
//@param filter  过滤条件（页号、每页条数不参与）
//@param nowMs   当前时间（Unix毫秒），供"近N天"换算
//@return 完整 SQL 文本
std::string BuildMonthCountSql(const HistoryFilter& filter, int64_t nowMs);

//一个月在本地时间里的起止，供按区间统计各月条数（BuildMonthRangeCountSql）
struct HistoryMonthRange
{
    int64_t m_key;       //年月键：年*100+月（如 202610）
    int64_t m_startMs;   //当月 1 日 0 点（Unix 毫秒）
    int64_t m_endMs;     //下月 1 日 0 点前 1 毫秒

    HistoryMonthRange()
        : m_key(0)
        , m_startMs(0)
        , m_endMs(0)
    {
    }

    HistoryMonthRange(int64_t key, int64_t startMs, int64_t endMs)
        : m_key(key)
        , m_startMs(startMs)
        , m_endMs(endMs)
    {
    }
};

//一条按区间统计的 SQL 里最多放几个月：SQLite 的 VALUES 多行写法受 SQLITE_MAX_COMPOUND_SELECT（默认 500）限制，
//月份更多时调用方分批拼
const size_t kMonthRangeSqlMaxMonths = 400;

//拼「按给定的各月区间统计条数」的 SQL：(VALUES (键, 起, 止), ...) 与 t_msg 做 CROSS JOIN，强制以月份为外层循环，
//每个月沿 (ctype, cid, timestamp) 索引取一段区间计数，不逐行换算本地时间。WHERE 与 BuildCountSql
//相同，关键字筛选生效时同样有一个 LIKE ? 占位。没有消息的月份不出现在结果里。
//结果列：0 年月键，1 条数；按年月倒序。
//@param filter  过滤条件（页号、每页条数不参与）
//@param nowMs   当前时间（Unix毫秒）
//@param months  各月区间，不能为空，最多 kMonthRangeSqlMaxMonths 个（多出的部分不拼进语句）
//@return 完整 SQL 文本；months 为空时返回空串
std::string BuildMonthRangeCountSql(const HistoryFilter& filter, int64_t nowMs,
                                    const std::vector<HistoryMonthRange>& months);

//拼「符合条件的最早（或最晚）一条消息的时间」的 SQL：按时间排序取一条，WHERE 与 BuildCountSql 相同，关键字
//占位同样。只有会话、时间等条件时沿 (ctype, cid, timestamp) 索引取到第一条就结束。
//结果列：0 timestamp；没有符合条件的消息时没有结果行。
//@param filter  过滤条件
//@param nowMs   当前时间（Unix毫秒）
//@param newest  true 取最晚一条，false 取最早一条
//@return 完整 SQL 文本
std::string BuildEdgeTimestampSql(const HistoryFilter& filter, int64_t nowMs, bool newest);

//拼「某个时间区间里按本地日期分组统计」的 SQL：WHERE 与 BuildCountSql 相同，另加
//timestamp>=fromMs AND timestamp<=toMs。时间轴列表每天那个大节点上的「当天条数」用它查。
//结果列：0 日期键（年*10000+月*100+日），1 条数；按日期倒序。
//@param filter  过滤条件
//@param nowMs   当前时间（Unix毫秒）
//@param fromMs  区间起（含，Unix毫秒）
//@param toMs    区间止（含，Unix毫秒）
//@return 完整 SQL 文本
std::string BuildDayCountSql(const HistoryFilter& filter, int64_t nowMs, int64_t fromMs, int64_t toMs);

//拼「取符合条件的各条消息的位置」的 SQL：只取 timestamp 与 msgid 两列，排序同 BuildPageSql，最多
//limit 条。时间轴模式下按关键字找出全部命中位置、供「上一处 / 下一处」跳转用。关键字占位同 BuildCountSql。
//@param filter  过滤条件（页号、每页条数不参与）
//@param nowMs   当前时间（Unix毫秒）
//@param limit   最多几条；不大于 0 时按 1 处理
//@return 完整 SQL 文本
std::string BuildPositionsSql(const HistoryFilter& filter, int64_t nowMs, int limit);

//==================== 主面板全局搜索（搜索下拉的「消息」「文件」标签） ====================

//「消息」标签的一行：某个会话里命中关键字的聊天记录汇总。
struct ConversationHit
{
    int         m_ctype;            //会话类型：1单聊 2群聊
    int64_t     m_cid;              //会话ID（我方视角）：单聊=对端userid，群聊=群id
    int         m_hitCount;         //该会话里命中的条数
    int64_t     m_latestMsgid;      //最近一条命中消息的 msgid
    int64_t     m_latestTimestamp;  //最近一条命中消息的时间（Unix毫秒）
    std::string m_latestText;       //最近一条命中消息的搜索文本（UTF-8）；只命中一条时界面直接显示它

    ConversationHit()
        : m_ctype(1)
        , m_cid(0)
        , m_hitCount(0)
        , m_latestMsgid(0)
        , m_latestTimestamp(0)
    {
    }
};

//把用户输入的关键字转成 LIKE 的匹配串：前后各加一个 %，并在 \ % _ 三个字符前面加反斜杠，
//配合语句里的 ESCAPE '\' 让它们按普通字符匹配（否则搜「50%」会把「500」也搜出来）。
//@param keyword  用户输入的关键字（UTF-8），不做大小写转换（SQLite LIKE 对 ASCII 不区分大小写）
//@return 可直接绑定到 LIKE ? 的匹配串
std::string MakeLikePattern(const std::string& keyword);

//全文索引（t_msg_fts，trigram 分词）能处理的关键字最少几个字（Unicode 字符）。trigram 把文字
//切成连续三个字的片段建索引，少于三个字的关键字切不出片段，只能整表 LIKE 扫描。
const int kTrigramMinChars = 3;

//关键字能否走全文索引：按 UTF-8 数出的 Unicode 字符数不少于 kTrigramMinChars。
//@param keywordUtf8  关键字（UTF-8，调用方已去掉首尾空白）
//@return 能走全文索引返回 true
bool CanUseTrigramSearch(const std::string& keywordUtf8);

//把关键字转成全文索引 MATCH 用的短语：整体放进一对双引号，内部的双引号写成两个。双引号里的
//其它字符（* ^ : 括号、AND / OR 等）一律按普通文字处理，用户输入什么就找什么。
//@param keywordUtf8  关键字（UTF-8）
//@return 可直接绑定到 MATCH ? 的短语
std::string MakeFtsPhrase(const std::string& keywordUtf8);

//判断某种消息类型是否归搜索下拉的「文件」标签：文件、文件夹传输记录、文件共享卡片三类。
//这三类不计入「消息」标签的汇总，避免同一条记录在两个标签里各出现一次。
//@param msgtype  消息类型，取值见 net/Protocol.h 的 message_type
//@return 归「文件」标签返回 true
bool IsFileTabMsgtype(int msgtype);

//拼「消息」标签的汇总查询：按会话分组统计 search_text 命中关键字的消息，排除已撤回的与「文件」
//标签那三类；每个会话按消息记录窗相同的覆盖区间规则只计可见的行（区间有效且没拉到最早时，
//msgid 小于区间下界的旧行不计，见 HistoryCoverage.h 的 CoverageMinVisibleMsgid）。结果按最近
//一条命中的时间倒序，最多 limit 个会话。语句里有一个 LIKE ? 占位，绑定 MakeLikePattern 的结果。
//结果列：0 ctype，1 cid，2 命中条数，3 最近命中时间，4 最近命中的 msgid，5 最近命中的 search_text。
//
//useFullText 为 true 时先用全文索引找出候选行（rowid IN (SELECT rowid FROM t_msg_fts WHERE
//t_msg_fts MATCH ?)），再在候选行上照常判 LIKE。全文索引不区分大小写的范围比 LIKE 宽（LIKE 只
//忽略 ASCII 字母的大小写），候选行一定包含 LIKE 的全部命中，复核之后结果与只用 LIKE 完全相同，
//只是不必扫描整张表。这时语句里有两个占位：第一个 MATCH ?，绑定 MakeFtsPhrase 的结果；第二个
//LIKE ?。调用方须确认全文索引已经建好（存量数据回填完毕）且 CanUseTrigramSearch 为 true。
//@param limit        最多返回几个会话；不大于 0 时按 1 处理
//@param useFullText  是否先用全文索引找候选行
//@return 完整 SQL 文本
std::string BuildSearchConversationsSql(int limit, bool useFullText = false);

//拼「文件」标签本地部分的查询：文件、文件夹传输记录、文件共享卡片三类中 search_text 命中关键字的
//消息，排除已撤回的，覆盖区间规则同上，按时间倒序（同毫秒按 msgid 倒序），最多 limit 条。语句里有
//一个 LIKE ? 占位。结果列与 CV2MsgStore 读整行记录的顺序一致（kColumns 加上 raw 列）。
//useFullText 的含义与占位顺序同 BuildSearchConversationsSql。
//@param limit        最多返回几条；不大于 0 时按 1 处理
//@param useFullText  是否先用全文索引找候选行
//@return 完整 SQL 文本
std::string BuildSearchFileMessagesSql(int limit, bool useFullText = false);

//==================== 聊天记录管理器（docs/聊天记录管理器方案.md） ====================

//本地库里一个会话的汇总：该会话最后一条可见消息的概况。聊天记录管理器的左栏把它与主面板的
//会话缓存合并（见 HistoryConversationList.h）；只在本地有的会话（例如在主面板「从列表删除」
//过的）靠它列出来，摘要也由这里带出的内容生成。
struct LocalConversationBrief
{
    int         m_ctype;          //会话类型：1单聊 2群聊
    int64_t     m_cid;            //会话ID（我方视角）：单聊=对端userid，群聊=群id
    int64_t     m_lastMsgid;      //最后一条可见消息的服务器 msgid
    int64_t     m_lastTimestamp;  //它的时间（Unix毫秒）
    int64_t     m_lastFromid;     //它的发送方 userid；系统提示为 0
    int         m_lastMsgtype;    //它的内容类型，取值见 net::message_type
    std::string m_lastContent;    //它的归一化内容JSON（UTF-8），供 PreviewFromContent 生成摘要

    LocalConversationBrief()
        : m_ctype(1)
        , m_cid(0)
        , m_lastMsgid(0)
        , m_lastTimestamp(0)
        , m_lastFromid(0)
        , m_lastMsgtype(1)
    {
    }
};

//把会话汇总按最后一条消息的时间倒序排列；时间相同时按最后一条的 msgid 倒序，保证每次排出来
//的顺序都一样（同毫秒的两个会话不会在两次刷新之间互换位置）。
//@param briefs  [入出参] 待排序的会话汇总
void SortLocalConversationBriefs(std::vector<LocalConversationBrief>& briefs);

//一个会话的标识。聊天记录管理器用它表示「限定在这些会话里搜索」的范围。
struct HistoryConversationKey
{
    int     m_ctype;   //会话类型：1单聊 2群聊
    int64_t m_cid;     //会话ID（我方视角）

    HistoryConversationKey()
        : m_ctype(1)
        , m_cid(0)
    {
    }

    HistoryConversationKey(int ctype, int64_t cid)
        : m_ctype(ctype)
        , m_cid(cid)
    {
    }
};

//拼聊天记录管理器左栏的跨会话搜索：按会话分组统计 search_text 命中关键字的消息，排除已撤回的，
//每个会话按覆盖区间只计可见的行（与 BuildSearchConversationsSql 同一口径）。
//
//与 BuildSearchConversationsSql 的区别有两处：
//  · <u>不排除</u>文件、文件夹传输记录、文件共享卡片三类。主面板搜索下拉把它们单独归进「文件」
//    标签，管理器只有一份结果，这三类的名字也要能搜到；这样统计出的条数也与点进该会话后右侧
//    按同一关键字筛出的条数一致（右侧的关键字筛选同样不分类型）。
//  · 可以限定在若干会话里。scope 为空表示不限；不为空时只统计这些会话，ctype、cid 都是整数，
//    直接写进语句（以 (m.ctype,m.cid) IN (VALUES ...) 的形式），不占位绑定。
//
//语句里只有一个 LIKE ? 占位，绑定 MakeLikePattern 的结果。结果列与 BuildSearchConversationsSql
//相同：0 ctype，1 cid，2 命中条数，3 最近命中时间，4 最近命中的 msgid，5 最近命中的 search_text。
//useFullText 的含义与占位顺序同 BuildSearchConversationsSql。
//@param limit        最多返回几个会话；不大于 0 时按 1 处理
//@param scope        限定的会话；为空表示所有会话
//@param useFullText  是否先用全文索引找候选行
//@return 完整 SQL 文本
std::string BuildManagerSearchSql(int limit, const std::vector<HistoryConversationKey>& scope,
                                  bool useFullText = false);

/**
 *  搜索语法版的全文索引查询串：取各正向词里能交给全文索引的部分（不勾通配符时是不少于 3 个字的整个词，勾选时是
 *  BuildWildcardFtsQuery 切出的段），用 AND 连接。排除词与限定词不参与。
 *  @param keyword   搜索框里的输入（UTF-8）
 *  @param wildcard  是否勾选了通配符
 *  @param outQuery  [出参] 查询串
 *  @return 至少有一个正向词能交给全文索引时返回 true
 */
bool BuildSearchSyntaxFtsQuery(const std::string& keyword, bool wildcard, std::string& outQuery);

/**
 *  搜索语法版的左栏跨会话搜索语句（docs/聊天记录管理器方案.md §12.3）：与上一个函数同样按会话汇总、按覆盖区间只计
 *  可见的行，关键字条件改由 BuildSearchSyntaxClause 拼出（多个词、排除词、from、type、日期）。
 *  占位顺序：useFullText 为 true 时第一个是 MATCH ?（绑定 BuildSearchSyntaxFtsQuery 的结果），之后依次是
 *  SearchSyntaxLikeBinds 给出的各匹配串。
 *  @param limit        最多返回几个会话；不大于 0 时按 1 处理
 *  @param scope        限定的会话；为空表示所有会话
 *  @param keyword      搜索框里的输入（UTF-8）
 *  @param wildcard     是否勾选了通配符
 *  @param fromIds      from: 名字解析出的 userid
 *  @param useFullText  是否先用全文索引找候选行
 *  @return 完整 SQL 文本
 */
std::string BuildManagerSearchSql(int limit, const std::vector<HistoryConversationKey>& scope,
                                  const std::string& keyword, bool wildcard, const std::vector<int64_t>& fromIds,
                                  bool useFullText);

} // namespace balloonhistory
