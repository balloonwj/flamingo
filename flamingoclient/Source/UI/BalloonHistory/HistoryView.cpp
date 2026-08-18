/**
 *  历史消息记录窗口——记录到行视图的转换纯逻辑实现，HistoryView.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "HistoryView.h"
#include <stdio.h>
#include <stdlib.h>
#include "../../net/Protocol.h"   // message_type_*（全局命名空间）
//见 MessageProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../../third-party/jsoncpp-1.9.0/json.h"
#pragma warning(pop)

namespace balloonhistory {

//把 JSON 文本解析成对象 Json::Value；非法 JSON 或顶层非对象返回 false。
static bool ParseObject(const std::string& json, Json::Value& root)
{
    if (json.empty())
    {
        return false;
    }
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

std::string PreviewFromContent(int msgtype, const std::string& contentJson)
{
    Json::Value root;
    bool ok = ParseObject(contentJson, root);
    switch (msgtype)
    {
    case ::message_type_text:
        //文本 / 链接：取正文并净化内联表情（/f["id"]→“[表情]”、//→/）；缺 text 返回空串。
        if (ok && root.isMember("text") && root["text"].isString())
        {
            return DigestFaceTokens(root["text"].asString());
        }
        return std::string();
    case ::message_type_image:
    case ::message_type_screenshot:
        return "[图片]";
    case ::message_type_file:
        //文件：带上文件名便于一眼辨认。
        if (ok && root.isMember("file") && root["file"].isObject())
        {
            const Json::Value& file = root["file"];
            if (file.isMember("name") && file["name"].isString() && !file["name"].asString().empty())
            {
                return std::string("[文件] ") + file["name"].asString();
            }
        }
        return "[文件]";
    case ::message_type_folder:
        //文件夹传输记录：带上文件夹名，体例与文件那条一致（"[文件] 名字"）。
        //只写名字、不写规模统计与结局 —— 列表一行的横向空间有限，规模与结局由
        //窗口侧另行拼在名字后面（见 CBalloonHistoryWindow 的行组装）。
        if (ok && root.isMember("folder") && root["folder"].isObject())
        {
            const Json::Value& folder = root["folder"];
            if (folder.isMember("foldername") && folder["foldername"].isString()
                && !folder["foldername"].asString().empty())
            {
                return std::string("[文件夹] ") + folder["foldername"].asString();
            }
        }
        return "[文件夹]";
    case ::message_type_voice:
        return "[语音]";
    case ::message_type_emoji:
        //动画表情：与会话列表 digestForMsgType 口径对齐，显示“[表情]”。
        return "[表情]";
    case ::message_type_shake:
        //窗口抖动：与会话列表 digestForMsgType 口径对齐，显示“[抖一抖]”，
        //不再落到通用“[消息]”兜底（此前缺本分支导致抖动消息显示成“[消息]”）。
        return "[抖一抖]";
    default:
        //其余尚未在历史窗单独渲染的类型（位置 / 名片 / 投票 / 接龙 / 待办 /
        //合并转发 / 通话记录 / 系统等）暂用通用占位，与会话列表兜底口径一致。
        return "[消息]";
    }
}

std::string FullTextFromContent(int msgtype, const std::string& contentJson)
{
    //只有文本 / 链接这一类的口径与 PreviewFromContent 不同：这里要把 /f["<id>"] 原始
    //标记留着，交给上层拆段后把表情画成图。其余类型逐字沿用 PreviewFromContent，故
    //直接转调 —— 不复制那段 switch，否则两份实现迟早会漂移。
    if (msgtype != ::message_type_text)
    {
        return PreviewFromContent(msgtype, contentJson);
    }

    Json::Value root;
    if (!ParseObject(contentJson, root))
    {
        //JSON 非法 / 顶层非对象：与 PreviewFromContent 一样退化为空串。
        return std::string();
    }

    if (root.isMember("text") && root["text"].isString())
    {
        return root["text"].asString();
    }
    return std::string();
}

HistoryContentBrief ParseContentBrief(int msgtype, const std::string& contentJson)
{
    HistoryContentBrief brief;

    Json::Value root;
    if (!ParseObject(contentJson, root))
    {
        return brief;
    }

    if (msgtype == ::message_type_image || msgtype == ::message_type_screenshot)
    {
        if (root.isMember("image") && root["image"].isObject())
        {
            const Json::Value& image = root["image"];
            if (image.isMember("md5") && image["md5"].isString())
            {
                brief.m_imageMd5 = image["md5"].asString();
            }
            //fileid：本地缓存里没有原图时，拿它发起下载。键名与落库时的
            //BuildContentJson（V2MsgQuery.cpp 的图片分支）一致。
            if (image.isMember("fileid") && image["fileid"].isString())
            {
                brief.m_imageFileId = image["fileid"].asString();
            }
            //size 与文件那边同样是字符串编码的 64 位整数（避免 JSON 数字丢精度）。
            if (image.isMember("size"))
            {
                if (image["size"].isString())
                {
                    brief.m_imageSize = ::_strtoi64(image["size"].asString().c_str(), NULL, 10);
                }
                else if (image["size"].isIntegral())
                {
                    brief.m_imageSize = (int64_t)image["size"].asInt64();
                }
            }
        }
        return brief;
    }

    if (msgtype == ::message_type_file)
    {
        if (root.isMember("file") && root["file"].isObject())
        {
            const Json::Value& file = root["file"];
            if (file.isMember("name") && file["name"].isString())
            {
                brief.m_fileName = file["name"].asString();
            }
            //size 落库时是字符串（64 位整数一律以字符串编码，避免丢精度）。
            if (file.isMember("size"))
            {
                if (file["size"].isString())
                {
                    brief.m_fileSize = ::_strtoi64(file["size"].asString().c_str(), NULL, 10);
                }
                else if (file["size"].isIntegral())
                {
                    brief.m_fileSize = (int64_t)file["size"].asInt64();
                }
            }
            //fileid：下载时要原样传给服务端的 cmd_request_download。键名与落库时的
            //BuildContentJson（V2MsgQuery.cpp）一致，写错这里下载会静默失败。
            if (file.isMember("fileid") && file["fileid"].isString())
            {
                brief.m_fileId = file["fileid"].asString();
            }
            //md5：下载完成后校验完整性，也用来推导本地缓存路径判断"本地有没有"。
            if (file.isMember("md5") && file["md5"].isString())
            {
                brief.m_fileMd5 = file["md5"].asString();
            }
        }
        return brief;
    }

    if (msgtype == ::message_type_folder)
    {
        if (root.isMember("folder") && root["folder"].isObject())
        {
            const Json::Value& folder = root["folder"];
            //会话 id：拿它去 CFolderLocalStore 查本机的根目录落点，决定这一行要不要给
            //「打开文件夹」。键名与落库时的 BuildContentJson（V2MsgQuery.cpp 的文件夹
            //分支，那边又转调 net::FolderProtocolClient::buildRecordContent）一致。
            if (folder.isMember("sessionid") && folder["sessionid"].isString())
            {
                brief.m_folderSessionId = folder["sessionid"].asString();
            }
            if (folder.isMember("foldername") && folder["foldername"].isString())
            {
                brief.m_folderName = folder["foldername"].asString();
            }
            //规模统计与结局都是 JSON 整数（不是文件那边的字符串编码），因为它们由
            //buildRecordContent 以 Json::Int64 写入；仍然容错地接受字符串形式。
            if (folder.isMember("totalfiles"))
            {
                if (folder["totalfiles"].isString())
                {
                    brief.m_folderTotalFiles =
                        ::_strtoi64(folder["totalfiles"].asString().c_str(), NULL, 10);
                }
                else if (folder["totalfiles"].isIntegral())
                {
                    brief.m_folderTotalFiles = (int64_t)folder["totalfiles"].asInt64();
                }
            }
            if (folder.isMember("totalsize"))
            {
                if (folder["totalsize"].isString())
                {
                    brief.m_folderTotalSize =
                        ::_strtoi64(folder["totalsize"].asString().c_str(), NULL, 10);
                }
                else if (folder["totalsize"].isIntegral())
                {
                    brief.m_folderTotalSize = (int64_t)folder["totalsize"].asInt64();
                }
            }
            if (folder.isMember("result") && folder["result"].isInt())
            {
                brief.m_folderResult = folder["result"].asInt();
            }
        }
    }
    return brief;
}

std::string FolderResultText(int result)
{
    switch (result)
    {
    //接收端在气泡上点了「拒绝」，一个字节都没有传
    case ::folder_state_rejected:
        return "已拒绝";

    //发送端在对方应答前撤回了邀请
    case ::folder_state_canceled:
        return "已撤回";

    //发起邀请时对方无在线连接，或响铃期间对方退出登录
    case ::folder_state_peer_offline:
        return "对方不在线";

    //邀请在服务端配置的时限内无人应答，服务端自动结束了会话
    case ::folder_state_timeout:
        return "邀请已超时";

    //传输过程中任一端出错、通道断开，或用户中途取消（协议侧都按失败 + 错误码表达）
    case ::folder_state_failed:
        return "传输失败";

    //全部文件传完并通过校验
    case ::folder_state_completed:
        return "已完成";

    //非终态（响铃中 / 传输中）与未知取值：记录条按协议只会带终态，走到这里说明
    //content 是脏的，此时宁可不显示结局，也不要显示一个编不出名字的状态
    default:
        return std::string();
    }
}

//往一行文字后面追加一段，段与段之间以 " · " 分隔；待追加的段为空时什么都不做。
//集中在这里判空，各调用点就不必各自小心"别留下一个孤零零的分隔点"。
static void AppendRowSegment(std::string& line, const std::string& segment)
{
    if (segment.empty())
    {
        return;
    }
    if (!line.empty())
    {
        line += " · ";
    }
    line += segment;
}

std::string BuildFolderRowText(const HistoryContentBrief& brief)
{
    std::string line;
    //类型前缀与文件夹名连在一起，中间不加分隔点 —— 与会话列表摘要的
    //"[文件夹] 项目资料" 逐字一致
    if (brief.m_folderName.empty())
    {
        line = "[文件夹]";
    }
    else
    {
        line = "[文件夹] " + brief.m_folderName;
    }

    //文件数为 0 是合法的（发送了一个空目录），此时不写这一段，免得出现"0 个文件"
    if (brief.m_folderTotalFiles > 0)
    {
        AppendRowSegment(line, std::to_string((long long)brief.m_folderTotalFiles) + " 个文件");
    }
    //FormatFileSize 对 0 与负数返回空串，AppendRowSegment 因而自动略过这一段
    AppendRowSegment(line, FormatFileSize(brief.m_folderTotalSize));
    AppendRowSegment(line, FolderResultText(brief.m_folderResult));
    return line;
}

std::string FileExtensionOf(const std::string& fileName)
{
    const std::string::size_type dot = fileName.find_last_of('.');
    //点在开头的（如 ".gitignore"）不算扩展名，那是没有主干的隐藏文件名。
    if (dot == std::string::npos || dot == 0 || dot + 1 >= fileName.size())
    {
        return std::string();
    }

    std::string ext = fileName.substr(dot + 1);
    for (size_t i = 0; i < ext.size(); ++i)
    {
        if (ext[i] >= 'A' && ext[i] <= 'Z')
        {
            ext[i] = (char)(ext[i] - 'A' + 'a');
        }
    }
    return ext;
}

std::string FormatFileSize(int64_t bytes)
{
    if (bytes <= 0)
    {
        return std::string();
    }

    //各档的分界与单位。不足 1 KB 的直接按字节写，省得出现 "0.0 KB" 这种没信息量的值。
    const int64_t kKB = 1024;
    const int64_t kMB = kKB * 1024;
    const int64_t kGB = kMB * 1024;

    char buf[32] = { 0 };
    if (bytes < kKB)
    {
        ::sprintf_s(buf, sizeof(buf), "%d B", (int)bytes);
    }
    else if (bytes < kMB)
    {
        ::sprintf_s(buf, sizeof(buf), "%.1f KB", (double)bytes / (double)kKB);
    }
    else if (bytes < kGB)
    {
        ::sprintf_s(buf, sizeof(buf), "%.1f MB", (double)bytes / (double)kMB);
    }
    else
    {
        ::sprintf_s(buf, sizeof(buf), "%.2f GB", (double)bytes / (double)kGB);
    }
    return std::string(buf);
}

HistoryTimeRange TimeRangeFromComboIndex(int comboIndex)
{
    //枚举值与下拉项下标是一一对应的（见 HistoryTimeRange 的注释），故这里只做
    //一次范围检查，越界（下拉没选中时 GetCurSel 会给 -1）一律退回“全部时间”。
    if (comboIndex >= (int)kHistoryTimeAll && comboIndex <= (int)kHistoryTimeCustom)
    {
        return (HistoryTimeRange)comboIndex;
    }
    return kHistoryTimeAll;
}

//往分页器模型里追加一个页码格。
static void AppendPageCell(std::vector<HistoryPagerCell>& out, int page, int currentPage)
{
    HistoryPagerCell cell;
    cell.m_page      = page;
    cell.m_isCurrent = (page == currentPage);
    out.push_back(cell);
}

//往分页器模型里追加一个省略号格。
static void AppendEllipsisCell(std::vector<HistoryPagerCell>& out)
{
    HistoryPagerCell cell;
    cell.m_isEllipsis = true;
    out.push_back(cell);
}

std::vector<HistoryPagerCell> BuildPagerModel(int currentPage, int pageCount)
{
    std::vector<HistoryPagerCell> cells;

    if (pageCount < 1)
    {
        pageCount = 1;
    }
    if (currentPage < 1)
    {
        currentPage = 1;
    }
    if (currentPage > pageCount)
    {
        currentPage = pageCount;
    }

    //页数不多时全部平铺，不必省略 —— 上限恰好取 kHistoryPagerMaxCells，这样"平铺"
    //与"省略"两条路产出的格子数都不会超过它。
    if (pageCount <= kHistoryPagerMaxCells)
    {
        for (int p = 1; p <= pageCount; ++p)
        {
            AppendPageCell(cells, p, currentPage);
        }
        return cells;
    }

    //窗口 = 当前页前后各一页，夹在 [2, pageCount-1] 之内（首末页单独恒显示）。
    int windowFirst = currentPage - 1;
    int windowLast  = currentPage + 1;
    if (windowFirst < 2)
    {
        windowFirst = 2;
    }
    if (windowLast > pageCount - 1)
    {
        windowLast = pageCount - 1;
    }

    AppendPageCell(cells, 1, currentPage);

    //与首页不相邻才需要省略号；恰好相邻（windowFirst == 2）时直接接上，
    //否则会出现"1 … 2"这种把一个页号藏进省略号的荒唐结果。
    if (windowFirst > 2)
    {
        AppendEllipsisCell(cells);
    }

    for (int p = windowFirst; p <= windowLast; ++p)
    {
        AppendPageCell(cells, p, currentPage);
    }

    //与末页不相邻才需要省略号，理由同上。
    if (windowLast < pageCount - 1)
    {
        AppendEllipsisCell(cells);
    }

    AppendPageCell(cells, pageCount, currentPage);
    return cells;
}

//把一个字符转成小写，仅对 ASCII 生效。中文本无大小写之分，原样返回即可 ——
//不用 towlower 是为了绕开它对 locale 的依赖：同一份代码在不同 locale 下给出不同
//结果，测试就没法稳定断言了。
static wchar_t LowerAscii(wchar_t c)
{
    if (c >= L'A' && c <= L'Z')
    {
        return (wchar_t)(c - L'A' + L'a');
    }
    return c;
}

std::vector<HighlightRange> FindHighlightRanges(const std::wstring& text,
                                                const std::wstring& keyword)
{
    std::vector<HighlightRange> ranges;
    if (text.empty() || keyword.empty() || keyword.size() > text.size())
    {
        return ranges;
    }

    const size_t klen = keyword.size();
    size_t i = 0;
    while (i + klen <= text.size())
    {
        size_t j = 0;
        while (j < klen && LowerAscii(text[i + j]) == LowerAscii(keyword[j]))
        {
            ++j;
        }

        if (j == klen)
        {
            HighlightRange r;
            r.m_begin = (int)i;
            r.m_len   = (int)klen;
            ranges.push_back(r);
            //命中不重叠：从这一处末尾继续找，"aaa" 里搜 "aa" 只报一处。
            i += klen;
            continue;
        }
        ++i;
    }
    return ranges;
}

//把 (年, 月, 日) 换成"距离 1970-01-01 的天数"，供两天之间求差。
//取自 Howard Hinnant 的 civil-from-days 算法的逆函数，纯整数运算、不碰时区，
//跨月跨年都对 —— 直接比 yyyymmdd 的大小是算不出"差一天"的（月初与上月末尾差 1 天，
//但数值上差了几十）。
static int64_t DaysFromCivil(int64_t y, int64_t m, int64_t d)
{
    y -= (m <= 2) ? 1 : 0;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;                                   // [0, 399]
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
    return era * 146097 + doe - 719468;
}

std::string FormatDateGroupLabel(int64_t dayKey, int64_t todayKey)
{
    if (dayKey <= 0)
    {
        return std::string();
    }

    const int64_t y = dayKey / 10000;
    const int64_t m = (dayKey / 100) % 100;
    const int64_t d = dayKey % 100;

    if (todayKey > 0)
    {
        const int64_t ty = todayKey / 10000;
        const int64_t tm = (todayKey / 100) % 100;
        const int64_t td = todayKey % 100;
        const int64_t diff = DaysFromCivil(ty, tm, td) - DaysFromCivil(y, m, d);
        if (diff == 0)
        {
            return "今天";
        }
        if (diff == 1)
        {
            return "昨天";
        }
    }

    char buf[32] = { 0 };
    ::sprintf_s(buf, sizeof(buf), "%04d-%02d-%02d", (int)y, (int)m, (int)d);
    return std::string(buf);
}

std::vector<bool> ComputeCompactFlags(const std::vector<int64_t>& senderIds,
                                      const std::vector<int64_t>& dayKeys)
{
    std::vector<bool> flags(senderIds.size(), false);

    //两个数组本该等长；长度对不上时只在共同前缀里判折叠，多出来的行一律不折叠 ——
    //宁可多画几个头像，也不能因为越界读到垃圾值而把不相干的两行连成一片。
    size_t usable = senderIds.size();
    if (dayKeys.size() < usable)
    {
        usable = dayKeys.size();
    }

    //首行恒不折叠，故自第二行起判。
    for (size_t i = 1; i < usable; ++i)
    {
        if (senderIds[i] == senderIds[i - 1] && dayKeys[i] == dayKeys[i - 1])
        {
            flags[i] = true;
        }
    }
    return flags;
}

//类型胶囊对应的中文名，供空状态文案拼句子用。kHistoryTypeAll 与 Pin 不走这里
//（前者压根不该进这个分支，后者有自己的整句文案）。
static std::string TypeFilterDisplayName(HistoryTypeFilter type)
{
    switch (type)
    {
    case kHistoryTypeText:
        return "文字";
    case kHistoryTypeImage:
        return "图片";
    case kHistoryTypeFile:
        return "文件";
    case kHistoryTypeVoice:
        return "语音";
    case kHistoryTypeLink:
        return "链接";
    default:
        return std::string();
    }
}

HistoryEmptyText BuildEmptyStateText(const HistoryFilter& filter)
{
    HistoryEmptyText text;
    //时间筛选是否收窄过。它不单独成句，而是作为"所选时间范围内"这个限定语加在
    //类型那句前面 —— 否则选了"近7天 + 图片"却搜空时，会说成"这个会话还没有图片
    //消息"，而实际上更早的图片可能有一堆，等于误导。
    const bool ranged = (filter.m_range != kHistoryTimeAll);

    //关键字优先：用户刚敲完字，最想知道的是"这个词没搜到"，而不是别的。
    if (!filter.m_keyword.empty())
    {
        text.m_title    = "没有找到「" + filter.m_keyword + "」相关的消息";
        text.m_subtitle = "换个词，或者把类型与时间的筛选放宽试试";
        return text;
    }

    if (filter.m_type == kHistoryTypePinned)
    {
        text.m_title    = ranged ? "所选时间范围内没有被钉住的消息"
                                 : "还没有钉住任何消息";
        text.m_subtitle = "在聊天气泡上右键就能把重要消息钉在这里";
        return text;
    }

    const std::string typeName = TypeFilterDisplayName(filter.m_type);
    if (!typeName.empty())
    {
        text.m_title = ranged ? ("所选时间范围内没有" + typeName + "消息")
                              : ("这个会话还没有" + typeName + "消息");
        if (ranged)
        {
            text.m_subtitle = "把时间范围放宽试试";
        }
        return text;
    }

    if (ranged)
    {
        text.m_title    = "所选时间范围内没有消息";
        text.m_subtitle = "把时间范围放宽试试";
        return text;
    }

    text.m_title = "这个会话还没有消息";
    return text;
}

} // namespace balloonhistory
