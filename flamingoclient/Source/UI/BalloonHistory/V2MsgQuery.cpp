/**
 *  v2 历史消息本地库——查询纯逻辑实现，V2MsgQuery.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "V2MsgQuery.h"
#include "../../net/MessageProtocolClient.h"   // net::MessageNotify / net::TextFont
#include "../../net/Protocol.h"                // ::message_type_* / conversation_*
//文件夹传输记录条（msgtype=18）的 content 字段名与校验只有这一份出处，落库时转调它。
#include "../../net/FolderProtocolClient.h"    // net::FolderRecordContent / buildRecordContent
#include <cctype>
//见 MessageProtocolClient.cpp 同处说明：vendored 的 json.h 含非 UTF-8 字节，作为
//UTF-8 编译单元时 MSVC 报 C4828；该告警仅涉及注释，就地屏蔽不污染编译输出。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

namespace balloonhistory {

//本地库表名与列清单。列顺序须与 CV2MsgStore::QueryPage 读取列的顺序严格一致。
static const char* const kTableName = "t_msg";
static const char* const kColumns =
    "msgid,clientmsgid,ctype,cid,fromid,msgtype,content,search_text,is_link,timestamp,recalled,"
    "pinned,pin_time";

//一天的毫秒数，用于“近N天”时间区间换算。
static const int64_t kMsPerDay = 24LL * 60LL * 60LL * 1000LL;
//各个“近 N”档位各自回溯的天数（避免直接在表达式里写魔数）。
//“3个月/半年/一年”一律按<u>固定天数</u>折算，不做日历意义上的月份加减：这里筛的是
//聊天记录，用户要的是“大致最近这么久”，90/180/365 天足够；真要精确到某年某月某日，
//界面上另有“自定义”那一档。这样也与既有的“近 30 天”口径一致（30 天 ≠ 一个自然月）。
static const int64_t kRecentDays7      = 7;
static const int64_t kRecentDays30     = 30;
static const int64_t kRecentDays3Month = 90;    //3 个月按 90 天算
static const int64_t kRecentDays6Month = 180;   //半年按 180 天算
static const int64_t kRecentDays1Year  = 365;   //一年按 365 天算，不区分闰年

//把 Json::Value 序列化为紧凑（无缩进、无换行）JSON 文本。
static std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 int64 转成十进制字符串（64 位 ID / size 以字符串编码进 JSON，避免精度丢失）。
static std::string Int64ToText(int64_t v)
{
    return std::to_string((long long)v);
}

bool IsLink(const std::string& text)
{
    //转小写后子串匹配 http:// 或 https://，实现大小写不敏感的轻量识别。
    std::string low;
    low.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        low.push_back((char)::tolower((unsigned char)text[i]));
    }
    if (low.find("http://") != std::string::npos)
    {
        return true;
    }
    if (low.find("https://") != std::string::npos)
    {
        return true;
    }
    return false;
}

std::string DigestFaceTokens(const std::string& body)
{
    std::string out;
    out.reserve(body.size());
    const size_t n = body.size();
    size_t i = 0;
    while (i < n)
    {
        //转义斜杠“//”→ 字面 '/'。
        if (body[i] == '/' && i + 1 < n && body[i + 1] == '/')
        {
            out.push_back('/');
            i += 2;
            continue;
        }
        //系统表情 /f["<digits>"] → “[表情]”。
        if (body[i] == '/' && i + 3 < n
            && body[i + 1] == 'f' && body[i + 2] == '[' && body[i + 3] == '"')
        {
            size_t j = i + 4;
            const size_t digitsStart = j;
            while (j < n && body[j] >= '0' && body[j] <= '9')
            {
                ++j;
            }
            if (j > digitsStart && j + 1 < n && body[j] == '"' && body[j + 1] == ']')
            {
                out += "[表情]";
                i = j + 2;
                continue;
            }
        }
        //其余字符（含中文多字节，其字节不含 '/'）原样保留；残缺 /x... 当普通文本。
        out.push_back(body[i]);
        ++i;
    }
    return out;
}

std::string BuildSearchText(const net::MessageNotify& notify)
{
    //文本/链接（链接本质是含URL的文本消息）取正文（净化内联表情）；文件取文件名；
    //其它类型无可搜文本。
    if (notify.m_msgtype == ::message_type_text)
    {
        return DigestFaceTokens(notify.m_text);
    }
    if (notify.m_msgtype == ::message_type_file)
    {
        return notify.m_fileName;
    }
    //文件夹传输记录：取文件夹名。这条记录里唯一有检索价值的就是它 —— 用户回头找的
    //是"我给谁发过那个叫 xxx 的文件夹"，而规模统计与结局都不是会被拿来搜的词。
    if (notify.m_msgtype == ::message_type_folder)
    {
        return notify.m_folderName;
    }
    return std::string();
}

std::string BuildContentJson(const net::MessageNotify& notify)
{
    Json::Value root(Json::objectValue);
    if (notify.m_msgtype == ::message_type_text)
    {
        root["text"] = notify.m_text;
        //仅发送方设过字体时才落 font 子对象，与协议“无字体则不写”保持一致。
        if (notify.m_font.m_valid)
        {
            Json::Value font(Json::objectValue);
            font["size"] = notify.m_font.m_size;
            font["bold"] = notify.m_font.m_bold;
            font["italic"] = notify.m_font.m_italic;
            font["underline"] = notify.m_font.m_underline;
            font["strike"] = notify.m_font.m_strike;
            font["color"] = (Json::Int)notify.m_font.m_colorRgb;
            root["font"] = font;
        }
    }
    else if (notify.m_msgtype == ::message_type_image
             || notify.m_msgtype == ::message_type_screenshot)
    {
        Json::Value image(Json::objectValue);
        image["fileid"] = notify.m_imageFileId;
        image["md5"] = notify.m_imageMd5;
        image["w"] = notify.m_imageWidth;
        image["h"] = notify.m_imageHeight;
        image["size"] = Int64ToText(notify.m_imageSize);
        root["image"] = image;
    }
    else if (notify.m_msgtype == ::message_type_file)
    {
        Json::Value file(Json::objectValue);
        file["fileid"] = notify.m_fileFileId;
        file["name"] = notify.m_fileName;
        file["size"] = Int64ToText(notify.m_fileSize);
        file["md5"] = notify.m_fileMd5;
        root["file"] = file;
    }
    else if (notify.m_msgtype == ::message_type_folder)
    {
        //文件夹传输记录：整条 content 原样落进 folder 子对象。字段名与线上那份逐字相同
        //（sessionid / foldername / totaldirs / totalfiles / totalsize / result / errcode），
        //构造与校验都转调 net::FolderProtocolClient::buildRecordContent，本文件不另写一份 ——
        //历史行要按 sessionid 去查本地落点登记，键名一旦两处写岔就会静默查不到。
        //<u>没有</u>文件清单与 MD5，故这条记录同样不支持从历史重新下载。
        net::FolderRecordContent record;
        record.m_sessionId  = notify.m_folderSessionId;
        record.m_folderName = notify.m_folderName;
        record.m_totalDirs  = notify.m_folderTotalDirs;
        record.m_totalFiles = notify.m_folderTotalFiles;
        record.m_totalSize  = notify.m_folderTotalSize;
        record.m_result     = notify.m_folderResult;
        record.m_errCode    = notify.m_folderErrCode;

        const std::string folderJson = net::FolderProtocolClient::buildRecordContent(record);
        //构造失败（会话 id 为空、结局非终态等）说明这条推送的 content 本就没解析出来，
        //此时留一个空的 folder 子对象即可：历史行会退化成只显示"[文件夹]"，不至于崩。
        Json::Value folder(Json::objectValue);
        if (!folderJson.empty())
        {
            Json::CharReaderBuilder b;
            JSONCPP_STRING errs;
            Json::CharReader* reader = b.newCharReader();
            reader->parse(folderJson.c_str(), folderJson.c_str() + folderJson.size(),
                          &folder, &errs);
            delete reader;
        }
        root["folder"] = folder;
    }
    //其它类型（语音/抖动/系统等）本期不带结构化内容，content 留空对象 {}。
    return WriteCompact(root);
}

HistoryRecord RecordFromNotify(const net::MessageNotify& notify, int ctype, int64_t cid)
{
    HistoryRecord rec;
    rec.m_msgid = notify.m_msgid;
    rec.m_clientmsgid = notify.m_clientmsgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = notify.m_from;
    rec.m_msgtype = notify.m_msgtype;
    rec.m_content = BuildContentJson(notify);
    rec.m_searchText = BuildSearchText(notify);
    //“链接”仅对文本消息有意义：正文含 URL 才算。
    rec.m_isLink = (notify.m_msgtype == ::message_type_text) && IsLink(notify.m_text);
    rec.m_timestamp = notify.m_timestamp;
    rec.m_recalled = notify.m_recalled;
    return rec;
}

void TimeRangeToBounds(HistoryTimeRange range, int64_t nowMs,
                       int64_t customMinMs, int64_t customMaxMs,
                       int64_t& outMinMs, int64_t& outMaxMs)
{
    switch (range)
    {
    case kHistoryTime7Days:
        outMinMs = nowMs - kRecentDays7 * kMsPerDay;
        outMaxMs = nowMs;
        break;
    case kHistoryTime30Days:
        outMinMs = nowMs - kRecentDays30 * kMsPerDay;
        outMaxMs = nowMs;
        break;
    case kHistoryTime3Months:
        outMinMs = nowMs - kRecentDays3Month * kMsPerDay;
        outMaxMs = nowMs;
        break;
    case kHistoryTime6Months:
        outMinMs = nowMs - kRecentDays6Month * kMsPerDay;
        outMaxMs = nowMs;
        break;
    case kHistoryTime1Year:
        outMinMs = nowMs - kRecentDays1Year * kMsPerDay;
        outMaxMs = nowMs;
        break;
    case kHistoryTimeCustom:
        outMinMs = customMinMs;
        outMaxMs = customMaxMs;
        break;
    case kHistoryTimeAll:
    default:
        //全部时间：用 [0, INT64_MAX] 表示无界（调用方可据此省略时间条件）。
        outMinMs = 0;
        outMaxMs = INT64_MAX;
        break;
    }
}

std::string TypeFilterToSqlFragment(HistoryTypeFilter type)
{
    //msgtype 数值取自 net::message_type，避免在 SQL 字符串里写魔数。
    switch (type)
    {
    case kHistoryTypeText:
        //文字：纯文本且不含 URL（链接单列一类）。
        return std::string("msgtype=") + std::to_string((int)::message_type_text)
             + " AND is_link=0";
    case kHistoryTypeImage:
        //图片：普通图片 + 截图（截图本质为图片子类）。
        return std::string("msgtype IN (") + std::to_string((int)::message_type_image)
             + "," + std::to_string((int)::message_type_screenshot) + ")";
    case kHistoryTypeFile:
        return std::string("msgtype=") + std::to_string((int)::message_type_file);
    case kHistoryTypeVoice:
        return std::string("msgtype=") + std::to_string((int)::message_type_voice);
    case kHistoryTypeLink:
        //链接：正文含 URL 的消息（入库时已预算 is_link）。
        return std::string("is_link=1");
    case kHistoryTypePinned:
        //Pin：被钉住的消息，不分内容类型。pinned 由服务端推送与开窗对账维护，
        //不是入库时算出来的，这一点与上面几类都不同。
        return std::string("pinned=1");
    case kHistoryTypeAll:
    default:
        return std::string();
    }
}

//拼装 WHERE 子句（不含 "WHERE" 关键字本身）。关键字以 ? 占位、由 CV2MsgStore 绑定。
static std::string BuildWhereClause(const HistoryFilter& filter, int64_t nowMs)
{
    std::string where;
    where += "ctype=";
    where += Int64ToText(filter.m_ctype);
    where += " AND cid=";
    where += Int64ToText(filter.m_cid);
    where += " AND recalled=0";

    std::string typeFrag = TypeFilterToSqlFragment(filter.m_type);
    if (!typeFrag.empty())
    {
        where += " AND ";
        where += typeFrag;
    }

    if (filter.m_range != kHistoryTimeAll)
    {
        int64_t minMs = 0;
        int64_t maxMs = 0;
        TimeRangeToBounds(filter.m_range, nowMs, filter.m_customMinMs, filter.m_customMaxMs,
                          minMs, maxMs);
        where += " AND timestamp>=";
        where += Int64ToText(minMs);
        where += " AND timestamp<=";
        where += Int64ToText(maxMs);
    }

    if (!filter.m_keyword.empty())
    {
        //关键字以 ? 占位，由 CV2MsgStore 绑定 "%关键字%"，避免引号转义与注入。
        where += " AND search_text LIKE ?";
    }
    return where;
}

std::string BuildCountSql(const HistoryFilter& filter, int64_t nowMs)
{
    std::string sql = "SELECT COUNT(*) FROM ";
    sql += kTableName;
    sql += " WHERE ";
    sql += BuildWhereClause(filter, nowMs);
    return sql;
}

std::string BuildPageSql(const HistoryFilter& filter, int64_t nowMs)
{
    int page = (filter.m_page >= 1) ? filter.m_page : 1;
    int pageSize = (filter.m_pageSize > 0) ? filter.m_pageSize : 1;
    int64_t offset = (int64_t)(page - 1) * (int64_t)pageSize;

    std::string sql = "SELECT ";
    sql += kColumns;
    sql += " FROM ";
    sql += kTableName;
    sql += " WHERE ";
    sql += BuildWhereClause(filter, nowMs);
    //时间倒序（最新在前）；同毫秒再按 msgid 倒序，保证稳定排序。
    sql += " ORDER BY timestamp DESC, msgid DESC LIMIT ";
    sql += Int64ToText(pageSize);
    sql += " OFFSET ";
    sql += Int64ToText(offset);
    return sql;
}

} // namespace balloonhistory
