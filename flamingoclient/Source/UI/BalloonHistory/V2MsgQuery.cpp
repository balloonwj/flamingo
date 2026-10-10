/**
 *  v2 历史消息本地库——查询纯逻辑实现，V2MsgQuery.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "V2MsgQuery.h"
#include "../../net/MessageProtocolClient.h"   // net::MessageNotify / net::TextFont
#include "../../net/Protocol.h"                // ::message_type_* / conversation_*
//文件夹传输记录条（msgtype=18）的 content 字段名与校验只有这一份出处，落库时转调它。
#include "../../net/FolderProtocolClient.h"    // net::FolderRecordContent / buildRecordContent
#include "../../net/CardProtocolClient.h"      // net::RelayCardContent / parseRelayCardContent
#include <cctype>
#include <algorithm>   //SortLocalConversationBriefs：std::stable_sort
#include "../../third-party/jsoncpp-1.9.0/json.h"
#include "../../Common/I18n/I18n.h"   // TR8：UTF-8 界面文字的多语言查找
#include "HistoryWildcard.h"          // WildcardHasContent / MakeSearchLikePattern
#include "HistorySearchSyntax.h"      // ParseHistorySearch：关键字按搜索语法拆成多个词与限定条件

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

//把 JSON 文本解析为对象；文本为空、解析失败或不是对象时返回 false，out 不保证内容。
static bool ParseJsonObject(const std::string& text, Json::Value& out)
{
    if (text.empty())
        return false;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errs);
    delete reader;
    return ok && out.isObject();
}

//卡片 content 里「标题 + 空格 + 另一段文字」形式的搜索文本：另一段为空时只有标题。
//群公告卡片的另一段是正文 body（已删除的卡片没有 body），群备忘卡片的另一段是备注 note
static std::string CardTitleAndText(const std::string& contentJson, const char* textKey)
{
    Json::Value card;
    if (!ParseJsonObject(contentJson, card))
        return std::string();
    const std::string title = card["title"].isString() ? card["title"].asString() : std::string();
    const std::string text = card[textKey].isString() ? card[textKey].asString() : std::string();
    if (text.empty())
        return title;
    if (title.empty())
        return text;
    return title + " " + text;
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
                out += TR8("[表情]");
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
    //合并转发的聊天记录：标题加上每条子消息的搜索文本，嵌套的记录逐层展开。用户回头
    //找的往往是记录里某句话，只存标题就搜不到。各段之间以空格分隔，LIKE 匹配不受影响
    if (notify.m_msgtype == ::message_type_merged)
    {
        net::MergedRecord record;
        if (!net::MessageProtocolClient::parseMergedContent(notify.m_mergedContent, record))
        {
            return std::string();
        }
        std::string text = record.m_title;
        for (size_t i = 0; i < record.m_messages.size(); ++i)
        {
            const std::string part = BuildSearchText(record.m_messages[i]);
            if (part.empty())
            {
                continue;
            }
            if (!text.empty())
            {
                text += " ";
            }
            text += part;
        }
        return text;
    }
    //系统提示（入群、退群、转让群主、接龙的参与 / 结束等）：取提示文字，消息记录窗里能搜到（2026-10-03 起）
    if (notify.m_msgtype == ::message_type_system)
    {
        return notify.m_text;
    }
    //群接龙卡片：标题与说明。接龙条目不进搜索文本（2026-10-02 用户确认）：名单随推送不断变化，
    //标题与说明发出后不会再变，入库时生成一次即可
    if (notify.m_msgtype == ::message_type_relay)
    {
        net::RelayCardContent card;
        if (!net::CardProtocolClient::parseRelayCardContent(notify.m_relayContent, card))
        {
            return std::string();
        }
        if (card.m_note.empty())
        {
            return card.m_title;
        }
        return card.m_title + " " + card.m_note;
    }
    //文件共享卡片：共享名。用户回头找的是「那个叫 xxx 的共享」，显示路径是本机路径，不拿来搜
    if (notify.m_msgtype == ::message_type_fileshare)
    {
        net::ShareCardContent card;
        if (!net::MessageProtocolClient::parseShareCardContent(notify.m_shareContent, card))
        {
            return std::string();
        }
        return card.m_name;
    }
    //群公告卡片：标题与正文，与服务端 MessageSearchText 的口径一致。公告被删除后正文已去掉，只剩标题
    if (notify.m_msgtype == ::message_type_announcement)
    {
        return CardTitleAndText(notify.m_announcementContent, "body");
    }
    //群备忘卡片：标题与备注
    if (notify.m_msgtype == ::message_type_memo)
    {
        return CardTitleAndText(notify.m_memoCardContent, "note");
    }
    //云端录像卡片：录像标题，与服务端 MessageSearchText 的口径一致
    if (notify.m_msgtype == ::message_type_cloudrecord)
    {
        Json::Value card;
        if (!ParseJsonObject(notify.m_cloudRecordContent, card) || !card["title"].isString())
        {
            return std::string();
        }
        return card["title"].asString();
    }
    //帖子卡片：标题与正文摘要；帖子动态卡片：帖子标题与留言、回复或补充的内容摘要（docs/帖子群方案.md 6.2）
    if (notify.m_msgtype == ::message_type_thread_post || notify.m_msgtype == ::message_type_thread_reply)
    {
        Json::Value card;
        if (!ParseJsonObject(notify.m_threadCardContent, card))
        {
            return std::string();
        }
        const char* const titleKey = (notify.m_msgtype == ::message_type_thread_post) ? "title" : "posttitle";
        std::string text = card[titleKey].isString() ? card[titleKey].asString() : std::string();
        if (card["digest"].isString() && !card["digest"].asString().empty())
        {
            if (!text.empty())
                text += " ";
            text += card["digest"].asString();
        }
        return text;
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
        //Markdown 消息与协议同名同值地写 format，消息记录窗等读 content 列的地方据此认出它（docs/Markdown消息方案.md 第 6.2 节）
        if (notify.m_textFormat == net::text_format_markdown)
        {
            root["format"] = net::kTextFormatMarkdownValue;
        }
        //代码片段同样按协议的写法存 format 与 lang，消息记录窗据此画代码卡片（docs/代码片段方案.md §2.3）
        else if (notify.m_textFormat == net::text_format_code)
        {
            root["format"] = net::kTextFormatCodeValue;
            if (!notify.m_codeLang.empty())
                root[net::kCodeLangKey] = notify.m_codeLang;
            //批注与发送请求同一种写法（docs/代码片段方案.md 第 10.5 节）
            net::MessageProtocolClient::writeCodeNotesToContent(root, notify.m_codeNotes);
        }
    }
    else if (notify.m_msgtype == ::message_type_system)
    {
        //系统提示：提示文字写进 text，消息记录窗与导出据它显示正文（2026-10-03 起）。此前这一类
        //落库时 content 是空对象，那些旧行在会话重新同步时会被这里覆盖
        root["text"] = notify.m_text;
        //结构化提示的类型与参数（多语言第 5 期）一并存下，消息记录窗按查看时的界面语言重新生成文字；
        //接龙提示所指的卡片消息 id 也存下，与服务端 content 的字段同名
        if (!notify.m_tipKind.empty())
        {
            Json::Value args(Json::objectValue);
            Json::Value parsed;
            Json::CharReaderBuilder b;
            JSONCPP_STRING errs;
            Json::CharReader* reader = b.newCharReader();
            const bool parsedOk = reader->parse(notify.m_tipArgs.c_str(),
                                                notify.m_tipArgs.c_str() + notify.m_tipArgs.size(), &parsed, &errs);
            delete reader;
            if (parsedOk && parsed.isObject())
            {
                args = parsed;
            }
            root["tipkind"] = notify.m_tipKind;
            root["tipargs"] = args;
        }
        if (notify.m_cardMsgId != 0)
            root["cardmsgid"] = std::to_string(notify.m_cardMsgId);
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
    else if (notify.m_msgtype == ::message_type_voice)
    {
        //语音：与线上 content 同名的字段放进 voice 子对象；没有波形时不写 wave
        Json::Value voice(Json::objectValue);
        voice["fileid"] = notify.m_voiceFileId;
        voice["md5"] = notify.m_voiceMd5;
        voice["duration"] = notify.m_voiceDuration;
        voice["size"] = Int64ToText(notify.m_voiceSize);
        if (!notify.m_voiceWave.empty())
            voice["wave"] = notify.m_voiceWave;
        root["voice"] = voice;
    }
    else if (notify.m_msgtype == ::message_type_file)
    {
        Json::Value file(Json::objectValue);
        file["fileid"] = notify.m_fileFileId;
        file["name"] = notify.m_fileName;
        file["size"] = Int64ToText(notify.m_fileSize);
        file["md5"] = notify.m_fileMd5;
        //在线直传留下的文件消息（2026-10-05 起写入）：服务器上没有副本，消息记录窗转发时要单独处理；普通文件不写
        if (notify.m_fileDirect)
            file["direct"] = 1;
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
    else if (notify.m_msgtype == ::message_type_merged)
    {
        //合并转发的聊天记录：content 原文整体放进 merged 子对象，打开查看窗时原样取出
        //交给 parseMergedContent。原文解析不了时留一个空的 merged 子对象，历史行退化成
        //只显示"[聊天记录]"，不至于崩
        Json::Value merged(Json::objectValue);
        if (!notify.m_mergedContent.empty())
        {
            Json::CharReaderBuilder b;
            JSONCPP_STRING errs;
            Json::CharReader* reader = b.newCharReader();
            Json::Value parsed;
            const bool ok = reader->parse(notify.m_mergedContent.c_str(),
                                          notify.m_mergedContent.c_str() + notify.m_mergedContent.size(),
                                          &parsed, &errs);
            delete reader;
            if (ok && parsed.isObject())
            {
                merged = parsed;
            }
        }
        root["merged"] = merged;
    }
    else if (notify.m_msgtype == ::message_type_relay)
    {
        //群接龙卡片：卡片内容（含名单与状态）整体放进 relay 子对象。收到变更推送后只更新这一列
        //（CV2MsgStore::UpdateContent），还原时以它为准（见 RelayContentFromRecord）。内容解析不了
        //时留一个空的 relay 子对象，历史行退化成只显示类型，不至于崩
        Json::Value relay(Json::objectValue);
        if (!notify.m_relayContent.empty())
        {
            Json::CharReaderBuilder b;
            JSONCPP_STRING errs;
            Json::CharReader* reader = b.newCharReader();
            Json::Value parsed;
            const bool ok = reader->parse(notify.m_relayContent.c_str(),
                                          notify.m_relayContent.c_str() + notify.m_relayContent.size(),
                                          &parsed, &errs);
            delete reader;
            if (ok && parsed.isObject())
            {
                relay = parsed;
            }
        }
        root["relay"] = relay;
    }
    else if (notify.m_msgtype == ::message_type_fileshare)
    {
        //文件共享卡片：卡片内容整体放进 share 子对象，消息记录窗据它显示「[共享] 名称」。聊天窗还原时用的是原始 JSON
        //（m_raw），不读这一列。内容解析不了时留一个空的 share 子对象，历史行退化成只显示类型
        Json::Value share(Json::objectValue);
        if (!notify.m_shareContent.empty())
        {
            Json::CharReaderBuilder b;
            JSONCPP_STRING errs;
            Json::CharReader* reader = b.newCharReader();
            Json::Value parsed;
            const bool ok = reader->parse(notify.m_shareContent.c_str(),
                                          notify.m_shareContent.c_str() + notify.m_shareContent.size(),
                                          &parsed, &errs);
            delete reader;
            if (ok && parsed.isObject())
            {
                share = parsed;
            }
        }
        root["share"] = share;
    }
    else if (notify.m_msgtype == ::message_type_screenshare)
    {
        //屏幕共享卡片：卡片内容整体放进 screenshare 子对象。聊天窗还原时用的是原始 JSON（m_raw），不读这一列；
        //消息记录窗只显示「[屏幕共享]」。内容解析不了时留一个空的 screenshare 子对象
        Json::Value screenshare(Json::objectValue);
        if (!notify.m_screenShareContent.empty())
        {
            Json::CharReaderBuilder b;
            JSONCPP_STRING errs;
            Json::CharReader* reader = b.newCharReader();
            Json::Value parsed;
            const bool ok = reader->parse(notify.m_screenShareContent.c_str(),
                                          notify.m_screenShareContent.c_str() + notify.m_screenShareContent.size(),
                                          &parsed, &errs);
            delete reader;
            if (ok && parsed.isObject())
            {
                screenshare = parsed;
            }
        }
        root["screenshare"] = screenshare;
    }
    else if (notify.m_msgtype == ::message_type_rtc)
    {
        //群通话卡片：卡片内容整体放进 groupcall 子对象。聊天窗还原时优先取这一列（通话结束后服务端改写的内容只写在这里），
        //消息记录窗按它的媒体类型写「[群语音通话]」或「[群视频通话]」。内容解析不了时留一个空的 groupcall 子对象
        Json::Value groupcall(Json::objectValue);
        if (!notify.m_groupCallContent.empty())
        {
            Json::CharReaderBuilder b;
            JSONCPP_STRING errs;
            Json::CharReader* reader = b.newCharReader();
            Json::Value parsed;
            const bool ok = reader->parse(notify.m_groupCallContent.c_str(),
                                          notify.m_groupCallContent.c_str() + notify.m_groupCallContent.size(),
                                          &parsed, &errs);
            delete reader;
            if (ok && parsed.isObject())
            {
                groupcall = parsed;
            }
        }
        root["groupcall"] = groupcall;
    }
    else if (notify.m_msgtype == ::message_type_group_invite)
    {
        //入群邀请卡片：卡片内容整体放进 groupinvite 子对象。收到状态变化推送（1328）后只更新这一列，
        //还原时以它为准（见 GroupInviteContentFromRecord）。内容解析不了时留一个空的 groupinvite 子对象
        Json::Value invite(Json::objectValue);
        if (!notify.m_groupInviteContent.empty())
        {
            Json::CharReaderBuilder b;
            JSONCPP_STRING errs;
            Json::CharReader* reader = b.newCharReader();
            Json::Value parsed;
            const bool ok = reader->parse(notify.m_groupInviteContent.c_str(),
                                          notify.m_groupInviteContent.c_str() + notify.m_groupInviteContent.size(),
                                          &parsed, &errs);
            delete reader;
            if (ok && parsed.isObject())
            {
                invite = parsed;
            }
        }
        root["groupinvite"] = invite;
    }
    else if (notify.m_msgtype == ::message_type_announcement)
    {
        //群公告卡片：卡片内容整体放进 announcement 子对象。公告被删除时（1326 推送带 cardmsgid 与改写后的卡片）
        //只更新这一列，还原时以它为准（见 AnnouncementContentFromRecord）。解析不了时留一个空的子对象
        Json::Value card(Json::objectValue);
        Json::Value parsed;
        if (ParseJsonObject(notify.m_announcementContent, parsed))
        {
            card = parsed;
        }
        root["announcement"] = card;
    }
    else if (notify.m_msgtype == ::message_type_memo)
    {
        //群备忘卡片：卡片内容整体放进 memocard 子对象，消息记录窗的预览从这里取标题。卡片发出后不再变化，
        //没有对应的「只改 content 列」的推送。解析不了时留一个空的子对象
        Json::Value card(Json::objectValue);
        Json::Value parsed;
        if (ParseJsonObject(notify.m_memoCardContent, parsed))
        {
            card = parsed;
        }
        root["memocard"] = card;
    }
    else if (notify.m_msgtype == ::message_type_cloudrecord)
    {
        //云端录像卡片：卡片内容整体放进 cloudrecord 子对象，消息记录窗的预览从这里取标题。卡片发出后不再变化。
        //解析不了时留一个空的子对象
        Json::Value card(Json::objectValue);
        Json::Value parsed;
        if (ParseJsonObject(notify.m_cloudRecordContent, parsed))
        {
            card = parsed;
        }
        root["cloudrecord"] = card;
    }
    else if (notify.m_msgtype == ::message_type_thread_post || notify.m_msgtype == ::message_type_thread_reply)
    {
        //帖子卡片、帖子动态卡片：卡片内容整体放进 threadcard 子对象，消息记录窗的预览从这里生成。删帖后服务端改写的
        //已删除形态只随会话同步与消息同步下发，没有「只改 content 列」的推送。解析不了时留一个空的子对象
        Json::Value card(Json::objectValue);
        Json::Value parsed;
        if (ParseJsonObject(notify.m_threadCardContent, parsed))
        {
            card = parsed;
        }
        root["threadcard"] = card;
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
    rec.m_raw = notify.m_rawJson;
    return rec;
}

HistoryRecord RecordFromNotify(const net::MessageNotify& notify, int ctype, int64_t cid, int64_t selfUserId)
{
    HistoryRecord rec = RecordFromNotify(notify, ctype, cid);
    //对方发来的语音记为未听（气泡带红点）；本人发的、其它类型一律记为已听
    rec.m_played = !(notify.m_msgtype == ::message_type_voice && notify.m_from != selfUserId);
    return rec;
}

bool NotifyFromRecord(const HistoryRecord& rec, net::MessageNotify& out)
{
    if (rec.m_raw.empty())
        return false;
    if (!net::MessageProtocolClient::parseMessageNotify(rec.m_raw, out))
        return false;

    //撤回状态以本地库为准：实时收到撤回通知时只调了 MarkRecalled 更新 recalled 列，
    //原始JSON仍是撤回之前的样子
    if (rec.m_recalled)
        out.m_recalled = true;

    //接龙卡片的内容同理以本地库为准：收到变更推送时只更新了 content 列
    if (out.m_msgtype == ::message_type_relay)
    {
        std::string relayJson;
        if (RelayContentFromRecord(rec, relayJson))
            out.m_relayContent = relayJson;
    }

    //屏幕共享卡片的内容同理（第 15 期）：发起人改了标题与描述后只更新了 content 列
    if (out.m_msgtype == ::message_type_screenshare)
    {
        std::string contentJson;
        if (ScreenShareContentFromRecord(rec, contentJson))
            out.m_screenShareContent = contentJson;
    }

    //群通话卡片同理：通话结束、服务端改写卡片后只更新了 content 列
    if (out.m_msgtype == ::message_type_rtc)
    {
        std::string contentJson;
        if (GroupCallContentFromRecord(rec, contentJson))
            out.m_groupCallContent = contentJson;
    }

    //入群邀请卡片同理：收到状态变化推送（1328）后只更新了 content 列
    if (out.m_msgtype == ::message_type_group_invite)
    {
        std::string contentJson;
        if (GroupInviteContentFromRecord(rec, contentJson))
            out.m_groupInviteContent = contentJson;
    }

    //群公告卡片同理：公告被删除、收到 1326 推送后只更新了 content 列
    if (out.m_msgtype == ::message_type_announcement)
    {
        std::string contentJson;
        if (AnnouncementContentFromRecord(rec, contentJson))
            out.m_announcementContent = contentJson;
    }
    return true;
}

bool AnnouncementContentFromRecord(const HistoryRecord& rec, std::string& contentJson)
{
    //先取 content 列的 announcement 子对象：公告被删除后，已删除形态只写在这里
    Json::Value column;
    if (ParseJsonObject(rec.m_content, column) && column["announcement"].isObject()
        && !column["announcement"].empty())
    {
        contentJson = WriteCompact(column["announcement"]);
        return true;
    }

    //content 列没有或为空对象：退回原始JSON里服务端发来的 content
    if (rec.m_raw.empty())
        return false;
    net::MessageNotify fromRaw;
    if (!net::MessageProtocolClient::parseMessageNotify(rec.m_raw, fromRaw) || fromRaw.m_announcementContent.empty())
        return false;
    contentJson = fromRaw.m_announcementContent;
    return true;
}

bool RelayContentFromRecord(const HistoryRecord& rec, std::string& relayJson)
{
    //先取 content 列的 relay 子对象：收到变更推送后最新的名单与状态只写在这里
    Json::Value column;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(rec.m_content.c_str(), rec.m_content.c_str() + rec.m_content.size(),
                                  &column, &errs);
    delete reader;
    if (ok && column.isObject() && column["relay"].isObject() && !column["relay"].empty())
    {
        relayJson = WriteCompact(column["relay"]);
        return true;
    }

    //content 列没有（本功能之前写入的旧行）或为空对象：退回原始JSON里服务端发来的 content
    if (rec.m_raw.empty())
        return false;
    net::MessageNotify fromRaw;
    if (!net::MessageProtocolClient::parseMessageNotify(rec.m_raw, fromRaw) || fromRaw.m_relayContent.empty())
        return false;
    relayJson = fromRaw.m_relayContent;
    return true;
}

bool ScreenShareContentFromRecord(const HistoryRecord& rec, std::string& contentJson)
{
    //先取 content 列的 screenshare 子对象：发起人改了标题与描述后，最新内容只写在这里
    Json::Value column;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(rec.m_content.c_str(), rec.m_content.c_str() + rec.m_content.size(),
                                  &column, &errs);
    delete reader;
    if (ok && column.isObject() && column["screenshare"].isObject() && !column["screenshare"].empty())
    {
        contentJson = WriteCompact(column["screenshare"]);
        return true;
    }

    //content 列没有或为空对象：退回原始JSON里服务端发来的 content
    if (rec.m_raw.empty())
        return false;
    net::MessageNotify fromRaw;
    if (!net::MessageProtocolClient::parseMessageNotify(rec.m_raw, fromRaw) || fromRaw.m_screenShareContent.empty())
        return false;
    contentJson = fromRaw.m_screenShareContent;
    return true;
}

bool GroupCallContentFromRecord(const HistoryRecord& rec, std::string& contentJson)
{
    //先取 content 列的 groupcall 子对象：通话结束、服务端改写卡片后，最新内容只写在这里
    Json::Value column;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(rec.m_content.c_str(), rec.m_content.c_str() + rec.m_content.size(),
                                  &column, &errs);
    delete reader;
    if (ok && column.isObject() && column["groupcall"].isObject() && !column["groupcall"].empty())
    {
        contentJson = WriteCompact(column["groupcall"]);
        return true;
    }

    //content 列没有或为空对象：退回原始JSON里服务端发来的 content
    if (rec.m_raw.empty())
        return false;
    net::MessageNotify fromRaw;
    if (!net::MessageProtocolClient::parseMessageNotify(rec.m_raw, fromRaw) || fromRaw.m_groupCallContent.empty())
        return false;
    contentJson = fromRaw.m_groupCallContent;
    return true;
}

bool GroupInviteContentFromRecord(const HistoryRecord& rec, std::string& contentJson)
{
    //先取 content 列的 groupinvite 子对象：收到状态变化推送后，最新状态只写在这里
    Json::Value column;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(rec.m_content.c_str(), rec.m_content.c_str() + rec.m_content.size(),
                                  &column, &errs);
    delete reader;
    if (ok && column.isObject() && column["groupinvite"].isObject() && !column["groupinvite"].empty())
    {
        contentJson = WriteCompact(column["groupinvite"]);
        return true;
    }

    //content 列没有或为空对象：退回原始JSON里服务端发来的 content
    if (rec.m_raw.empty())
        return false;
    net::MessageNotify fromRaw;
    if (!net::MessageProtocolClient::parseMessageNotify(rec.m_raw, fromRaw) || fromRaw.m_groupInviteContent.empty())
        return false;
    contentJson = fromRaw.m_groupInviteContent;
    return true;
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
        //图片：普通图片 + 截图（截图本质为图片子类），以及带图片、不是已删除形态的帖子卡片与补充卡片
        //（docs/帖子群方案.md 14.5，图片在 content 的 threadcard.images[]）。content 不是合法 JSON 时 json_* 函数
        //会让整条查询报错，故先用 CASE 判 json_valid，只在合法时才取字段
        return std::string("(msgtype IN (") + std::to_string((int)::message_type_image)
             + "," + std::to_string((int)::message_type_screenshot) + ")"
             + " OR (msgtype IN (" + std::to_string((int)::message_type_thread_post)
             + "," + std::to_string((int)::message_type_thread_reply) + ")"
             + " AND CASE WHEN json_valid(content) THEN IFNULL(json_array_length(content,'$.threadcard.images'),0)>0"
             + " AND IFNULL(json_extract(content,'$.threadcard.deleted'),0)=0 ELSE 0 END))";
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

    //显示下界：只查覆盖区间内的消息，断层以下的旧行不显示（见 HistoryCoverage.h）。
    //Pin 分类不加 —— Pin 列表是服务端单独给的全集，与本地历史是否连续无关
    if (filter.m_minMsgid > 0 && filter.m_type != kHistoryTypePinned)
    {
        where += " AND msgid>=";
        where += Int64ToText(filter.m_minMsgid);
    }

    //发送人筛选。fromid 是整数，直接拼进语句即可，不必像关键字那样占位绑定
    if (filter.m_fromid > 0)
    {
        where += " AND fromid=";
        where += Int64ToText(filter.m_fromid);
    }

    if (HasKeywordFilter(filter))
    {
        //按搜索语法拼：每个词以 ? 占位，由 CV2MsgStore 依次绑定 SearchSyntaxLikeBinds 的结果，避免引号转义与注入。
        //ESCAPE 让匹配串里转义过的 \ % _ 按普通字符匹配（原先没有转义，输入的 % _ 被当成通配符）
        where += BuildSearchSyntaxClause(filter.m_keyword, filter.m_wildcard, filter.m_syntaxFromIds,
                                         "search_text", false);
    }
    return where;
}

bool HasKeywordFilter(const HistoryFilter& filter)
{
    if (filter.m_keyword.empty())
    {
        return false;
    }
    //按搜索语法解析；勾选通配符时只输了 '*' 的词已被丢弃，什么条件都不剩就等于没有关键字
    return SearchQueryHasCondition(ParseHistorySearch(filter.m_keyword, filter.m_wildcard));
}

std::string BuildSearchSyntaxClause(const std::string& keyword, bool wildcard, const std::vector<int64_t>& fromIds,
                                    const char* textColumn, bool termsOnly)
{
    const HistorySearchQuery q = ParseHistorySearch(keyword, wildcard);
    std::string clause;
    for (size_t i = 0; i < q.m_terms.size(); ++i)
    {
        clause += " AND ";
        clause += textColumn;
        clause += " LIKE ? ESCAPE '\\'";
    }
    for (size_t i = 0; i < q.m_excludes.size(); ++i)
    {
        clause += " AND ";
        clause += textColumn;
        clause += " NOT LIKE ? ESCAPE '\\'";
    }
    if (termsOnly)
    {
        return clause;
    }
    //写错的限定值：这次搜索不应有结果，不悄悄忽略
    if (q.m_badModifier)
    {
        clause += " AND 0";
    }
    if (q.m_type != kSearchTypeNone)
    {
        const std::string typeFrag = TypeFilterToSqlFragment((HistoryTypeFilter)q.m_type);
        if (!typeFrag.empty())
        {
            clause += " AND (" + typeFrag + ")";
        }
    }
    if (q.m_afterMs != 0)
    {
        clause += " AND timestamp>=" + Int64ToText(q.m_afterMs);
    }
    if (q.m_beforeMs != 0)
    {
        clause += " AND timestamp<=" + Int64ToText(q.m_beforeMs);
    }
    //from:名字：只查解析出的这些人；一个都没匹配上时没有结果
    if (!q.m_fromNames.empty())
    {
        if (fromIds.empty())
        {
            clause += " AND 0";
        }
        else
        {
            clause += " AND fromid IN (";
            for (size_t i = 0; i < fromIds.size(); ++i)
            {
                if (i > 0)
                {
                    clause += ",";
                }
                clause += Int64ToText(fromIds[i]);
            }
            clause += ")";
        }
    }
    return clause;
}

std::vector<std::string> SearchSyntaxLikeBinds(const std::string& keyword, bool wildcard)
{
    const HistorySearchQuery q = ParseHistorySearch(keyword, wildcard);
    std::vector<std::string> binds;
    for (size_t i = 0; i < q.m_terms.size(); ++i)
    {
        binds.push_back(MakeSearchLikePattern(q.m_terms[i], wildcard));
    }
    for (size_t i = 0; i < q.m_excludes.size(); ++i)
    {
        binds.push_back(MakeSearchLikePattern(q.m_excludes[i], wildcard));
    }
    return binds;
}

std::string BuildCountSql(const HistoryFilter& filter, int64_t nowMs)
{
    std::string sql = "SELECT COUNT(*) FROM ";
    sql += kTableName;
    sql += " WHERE ";
    sql += BuildWhereClause(filter, nowMs);
    return sql;
}

std::string BuildCountBeforeSql(const HistoryFilter& filter, int64_t nowMs, int64_t timestamp, int64_t msgid)
{
    std::string sql = BuildCountSql(filter, nowMs);
    //时间与 msgid 都是整数，直接拼进语句；关键字的 ? 占位仍是语句里唯一的占位
    sql += " AND (timestamp>";
    sql += Int64ToText(timestamp);
    sql += " OR (timestamp=";
    sql += Int64ToText(timestamp);
    sql += " AND msgid>";
    sql += Int64ToText(msgid);
    sql += "))";
    return sql;
}

//时间轴的分组统计：把毫秒时间换成本地时间后按给定格式取年月或年月日，再转成整数键。
//SQLite 的 'localtime' 与界面上 _localtime64_s 用的是同一套 C 运行库时区换算
static std::string LocalPeriodKeyExpr(const char* format)
{
    std::string expr = "CAST(strftime('";
    expr += format;
    expr += "', timestamp/1000, 'unixepoch', 'localtime') AS INTEGER)";
    return expr;
}

std::string BuildMonthCountSql(const HistoryFilter& filter, int64_t nowMs)
{
    std::string sql = "SELECT ";
    sql += LocalPeriodKeyExpr("%Y%m");
    sql += " AS k, COUNT(*) FROM ";
    sql += kTableName;
    sql += " WHERE ";
    sql += BuildWhereClause(filter, nowMs);
    sql += " GROUP BY k ORDER BY k DESC";
    return sql;
}

std::string BuildMonthRangeCountSql(const HistoryFilter& filter, int64_t nowMs,
                                    const std::vector<HistoryMonthRange>& months)
{
    if (months.empty())
    {
        return std::string();
    }
    //VALUES 子查询的三列依次是 column1（年月键）、column2（起）、column3（止），与 t_msg 的列名不重名，
    //WHERE 里不带表名的 ctype、cid、timestamp 都指 t_msg
    std::string sql = "SELECT m.column1 AS k, COUNT(*) FROM (VALUES ";
    const size_t n = (months.size() < kMonthRangeSqlMaxMonths) ? months.size() : kMonthRangeSqlMaxMonths;
    for (size_t i = 0; i < n; ++i)
    {
        if (i > 0)
        {
            sql += ",";
        }
        sql += "(";
        sql += Int64ToText(months[i].m_key);
        sql += ",";
        sql += Int64ToText(months[i].m_startMs);
        sql += ",";
        sql += Int64ToText(months[i].m_endMs);
        sql += ")";
    }
    //CROSS JOIN 让 SQLite 按书写顺序以月份为外层循环，每个月走一次索引的区间扫描
    sql += ") AS m CROSS JOIN ";
    sql += kTableName;
    sql += " WHERE ";
    sql += kTableName;
    sql += ".timestamp>=m.column2 AND ";
    sql += kTableName;
    sql += ".timestamp<=m.column3 AND ";
    sql += BuildWhereClause(filter, nowMs);
    sql += " GROUP BY k ORDER BY k DESC";
    return sql;
}

std::string BuildEdgeTimestampSql(const HistoryFilter& filter, int64_t nowMs, bool newest)
{
    std::string sql = "SELECT timestamp FROM ";
    sql += kTableName;
    sql += " WHERE ";
    sql += BuildWhereClause(filter, nowMs);
    sql += newest ? " ORDER BY timestamp DESC LIMIT 1" : " ORDER BY timestamp ASC LIMIT 1";
    return sql;
}

std::string BuildDayCountSql(const HistoryFilter& filter, int64_t nowMs, int64_t fromMs, int64_t toMs)
{
    std::string sql = "SELECT ";
    sql += LocalPeriodKeyExpr("%Y%m%d");
    sql += " AS k, COUNT(*) FROM ";
    sql += kTableName;
    sql += " WHERE ";
    sql += BuildWhereClause(filter, nowMs);
    //时间区间是整数，直接拼进语句；关键字的 ? 占位仍是语句里唯一的占位
    sql += " AND timestamp>=";
    sql += Int64ToText(fromMs);
    sql += " AND timestamp<=";
    sql += Int64ToText(toMs);
    sql += " GROUP BY k ORDER BY k DESC";
    return sql;
}

std::string BuildPositionsSql(const HistoryFilter& filter, int64_t nowMs, int limit)
{
    std::string sql = "SELECT timestamp, msgid FROM ";
    sql += kTableName;
    sql += " WHERE ";
    sql += BuildWhereClause(filter, nowMs);
    sql += " ORDER BY timestamp DESC, msgid DESC LIMIT ";
    sql += Int64ToText((limit > 0) ? limit : 1);
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

//==================== 主面板全局搜索 ====================

//全局搜索的两条语句都把 t_msg（别名 m）与覆盖区间表 t_cover（别名 c）左连接，两表都有 ctype、cid
//两列，所以取整行时要带表别名。列顺序与 kColumns 相同，末尾多一列 raw，与 CV2MsgStore 读整行记录
//的函数（ReadRecordRow）一一对应。
static const char* const kSearchRecordColumns =
    "m.msgid,m.clientmsgid,m.ctype,m.cid,m.fromid,m.msgtype,m.content,m.search_text,m.is_link,"
    "m.timestamp,m.recalled,m.pinned,m.pin_time,m.raw";

//全局搜索两条语句共用的 FROM 与 WHERE（不含类型条件）：未撤回、search_text 命中关键字，且按覆盖
//区间可见。可见的口径与 CoverageMinVisibleMsgid 相同：该会话没有区间、已拉到服务端最早一条、或
//msgid 不小于区间下界。关键字以 ? 占位，ESCAPE 子句让 MakeLikePattern 转义过的 \ % _ 按普通字符匹配。
static const char* const kSearchFromWhere =
    " FROM t_msg m LEFT JOIN t_cover c ON c.ctype=m.ctype AND c.cid=m.cid"
    " WHERE m.recalled=0 AND m.search_text LIKE ? ESCAPE '\\'"
    " AND (c.ctype IS NULL OR c.reached_oldest<>0 OR m.msgid>=c.lo_msgid)";

//同 kSearchFromWhere，但先用全文索引找出候选行再判 LIKE（理由见 BuildSearchConversationsSql
//的说明）。两个占位：第一个 MATCH ?，第二个 LIKE ?。
static const char* const kSearchFromWhereFts =
    " FROM t_msg m LEFT JOIN t_cover c ON c.ctype=m.ctype AND c.cid=m.cid"
    " WHERE m.rowid IN (SELECT rowid FROM t_msg_fts WHERE t_msg_fts MATCH ?)"
    " AND m.recalled=0 AND m.search_text LIKE ? ESCAPE '\\'"
    " AND (c.ctype IS NULL OR c.reached_oldest<>0 OR m.msgid>=c.lo_msgid)";

//归「文件」标签的三类消息，写成 SQL 的 IN 列表内容。取值须与 IsFileTabMsgtype 一致
static std::string FileTabMsgtypeList()
{
    std::string list = Int64ToText(::message_type_file);
    list += ",";
    list += Int64ToText(::message_type_folder);
    list += ",";
    list += Int64ToText(::message_type_fileshare);
    return list;
}

std::string MakeLikePattern(const std::string& keyword)
{
    std::string pattern = "%";
    for (size_t i = 0; i < keyword.size(); ++i)
    {
        const char ch = keyword[i];
        //UTF-8 多字节字符的每个字节都不小于 0x80，不会与这三个 ASCII 字符混淆，逐字节处理即可
        if (ch == '\\' || ch == '%' || ch == '_')
        {
            pattern.push_back('\\');
        }
        pattern.push_back(ch);
    }
    pattern += "%";
    return pattern;
}

bool CanUseTrigramSearch(const std::string& keywordUtf8)
{
    //数 Unicode 字符：UTF-8 里每个字符恰有一个首字节，续字节都形如 10xxxxxx
    const unsigned char kContinuationMask = 0xC0;
    const unsigned char kContinuationBits = 0x80;
    int chars = 0;
    for (size_t i = 0; i < keywordUtf8.size(); ++i)
    {
        const unsigned char ch = (unsigned char)keywordUtf8[i];
        if ((ch & kContinuationMask) != kContinuationBits)
        {
            ++chars;
        }
    }
    return chars >= kTrigramMinChars;
}

std::string MakeFtsPhrase(const std::string& keywordUtf8)
{
    std::string phrase = "\"";
    for (size_t i = 0; i < keywordUtf8.size(); ++i)
    {
        if (keywordUtf8[i] == '"')
        {
            phrase.push_back('"');
        }
        phrase.push_back(keywordUtf8[i]);
    }
    phrase.push_back('"');
    return phrase;
}

bool IsFileTabMsgtype(int msgtype)
{
    return msgtype == ::message_type_file
        || msgtype == ::message_type_folder
        || msgtype == ::message_type_fileshare;
}

std::string BuildSearchConversationsSql(int limit, bool useFullText)
{
    //SQLite 的聚合查询里只有一个 MAX() 时，同一行里的非聚合列（这里的 m.msgid、m.search_text）取自
    //MAX 所在的那一行（SQLite 文档「Bare columns in an aggregate query」），据此直接拿到每个会话
    //最近一条命中消息的 msgid 与文字，不必再写一层子查询
    std::string sql = "SELECT m.ctype,m.cid,COUNT(*),MAX(m.timestamp),m.msgid,m.search_text";
    sql += useFullText ? kSearchFromWhereFts : kSearchFromWhere;
    sql += " AND m.msgtype NOT IN (";
    sql += FileTabMsgtypeList();
    sql += ") GROUP BY m.ctype,m.cid ORDER BY MAX(m.timestamp) DESC LIMIT ";
    sql += Int64ToText((limit > 0) ? limit : 1);
    return sql;
}

std::string BuildSearchFileMessagesSql(int limit, bool useFullText)
{
    std::string sql = "SELECT ";
    sql += kSearchRecordColumns;
    sql += useFullText ? kSearchFromWhereFts : kSearchFromWhere;
    sql += " AND m.msgtype IN (";
    sql += FileTabMsgtypeList();
    sql += ") ORDER BY m.timestamp DESC, m.msgid DESC LIMIT ";
    sql += Int64ToText((limit > 0) ? limit : 1);
    return sql;
}

//==================== 聊天记录管理器 ====================

//SortLocalConversationBriefs 的比较函数：a 是否应排在 b 前面（时间晚的在前，同毫秒 msgid 大的在前）
static bool LocalBriefNewerFirst(const LocalConversationBrief& a, const LocalConversationBrief& b)
{
    if (a.m_lastTimestamp != b.m_lastTimestamp)
    {
        return a.m_lastTimestamp > b.m_lastTimestamp;
    }
    return a.m_lastMsgid > b.m_lastMsgid;
}

void SortLocalConversationBriefs(std::vector<LocalConversationBrief>& briefs)
{
    std::stable_sort(briefs.begin(), briefs.end(), LocalBriefNewerFirst);
}

std::string BuildManagerSearchSql(int limit, const std::vector<HistoryConversationKey>& scope,
                                  bool useFullText)
{
    //聚合里只有一个 MAX()，同一行的 m.msgid、m.search_text 取自 MAX 所在的那一行（理由见
    //BuildSearchConversationsSql）
    std::string sql = "SELECT m.ctype,m.cid,COUNT(*),MAX(m.timestamp),m.msgid,m.search_text";
    sql += useFullText ? kSearchFromWhereFts : kSearchFromWhere;
    if (!scope.empty())
    {
        sql += " AND (m.ctype,m.cid) IN (VALUES ";
        for (size_t i = 0; i < scope.size(); ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }
            sql += "(";
            sql += Int64ToText(scope[i].m_ctype);
            sql += ",";
            sql += Int64ToText(scope[i].m_cid);
            sql += ")";
        }
        sql += ")";
    }
    sql += " GROUP BY m.ctype,m.cid ORDER BY MAX(m.timestamp) DESC LIMIT ";
    sql += Int64ToText((limit > 0) ? limit : 1);
    return sql;
}

bool BuildSearchSyntaxFtsQuery(const std::string& keyword, bool wildcard, std::string& outQuery)
{
    outQuery.clear();
    const HistorySearchQuery q = ParseHistorySearch(keyword, wildcard);
    for (size_t i = 0; i < q.m_terms.size(); ++i)
    {
        //每个正向词各自切出能交给全文索引的部分；一个都切不出的词只靠 LIKE 复核
        std::string part;
        if (wildcard)
        {
            if (!BuildWildcardFtsQuery(q.m_terms[i], part))
            {
                continue;
            }
        }
        else
        {
            if (!CanUseTrigramSearch(q.m_terms[i]))
            {
                continue;
            }
            part = MakeFtsPhrase(q.m_terms[i]);
        }
        if (!outQuery.empty())
        {
            outQuery += " AND ";
        }
        outQuery += part;
    }
    return !outQuery.empty();
}

std::string BuildManagerSearchSql(int limit, const std::vector<HistoryConversationKey>& scope,
                                  const std::string& keyword, bool wildcard, const std::vector<int64_t>& fromIds,
                                  bool useFullText)
{
    //聚合里只有一个 MAX()，同一行的 m.msgid、m.search_text 取自 MAX 所在的那一行（理由见
    //BuildSearchConversationsSql）
    std::string sql = "SELECT m.ctype,m.cid,COUNT(*),MAX(m.timestamp),m.msgid,m.search_text"
                      " FROM t_msg m LEFT JOIN t_cover c ON c.ctype=m.ctype AND c.cid=m.cid WHERE ";
    if (useFullText)
    {
        sql += "m.rowid IN (SELECT rowid FROM t_msg_fts WHERE t_msg_fts MATCH ?) AND ";
    }
    sql += "m.recalled=0 AND (c.ctype IS NULL OR c.reached_oldest<>0 OR m.msgid>=c.lo_msgid)";
    //t_cover 没有 timestamp、fromid、msgtype 等列，语法条件里不带表名的列名都指 t_msg
    sql += BuildSearchSyntaxClause(keyword, wildcard, fromIds, "m.search_text", false);
    if (!scope.empty())
    {
        sql += " AND (m.ctype,m.cid) IN (VALUES ";
        for (size_t i = 0; i < scope.size(); ++i)
        {
            if (i > 0)
            {
                sql += ",";
            }
            sql += "(";
            sql += Int64ToText(scope[i].m_ctype);
            sql += ",";
            sql += Int64ToText(scope[i].m_cid);
            sql += ")";
        }
        sql += ")";
    }
    sql += " GROUP BY m.ctype,m.cid ORDER BY MAX(m.timestamp) DESC LIMIT ";
    sql += Int64ToText((limit > 0) ? limit : 1);
    return sql;
}

} // namespace balloonhistory
