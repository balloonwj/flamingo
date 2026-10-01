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
    kHistoryTypeImage = 2,   //图片：普通图片 + 截图（msgtype IN(图片,截图)）
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
    //发送人筛选：只查这个 userid 发的消息；0 表示不限。Pin 分类同样受它限制
    int64_t           m_fromid;
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
//用入库记录里的原始JSON还原出网络消息，结果与当初网络应答解析出的完全相同，
//只有撤回状态以本地库为准（实时收到的撤回只更新了库里的 recalled 列，原始JSON不变）。
//打开聊天窗时先用本地数据显示即靠它（见 CV2MsgStore::QueryLatest）。
//@param rec  入库记录，须带原始JSON（m_raw）
//@param out  还原出的网络消息
//@return 还原成功返回 true；记录没有原始JSON（老数据）或原始JSON解析失败时返回 false
bool NotifyFromRecord(const HistoryRecord& rec, net::MessageNotify& out);

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

//拼装统计总数的 SQL（SELECT COUNT(*) ...）。关键字以 ? 占位（由调用方绑定
//"%关键字%"），数值类条件内联；WHERE 恒含 recalled=0。
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

} // namespace balloonhistory
