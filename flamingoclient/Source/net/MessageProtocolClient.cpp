/**
 *  v2 单聊消息报文编解码（客户端侧）实现，MessageProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-01
 */
#include "MessageProtocolClient.h"
#include "SystemTipText.h"   // FormatSystemTip：系统提示按当前界面语言生成文字
#include "Protocol.h"                   // conversation_single / message_type_text
//文件夹传输记录条（msgtype=18）的 content 校验与归一只有这一份实现，本文件转调它，
//不在这里另抄一遍字段名。
#include "FolderProtocolClient.h"       // net::FolderRecordContent / parseRecordContent
#include <stdlib.h>                     // strtoll
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "../Common/I18n/I18n.h"   // TR8 / TR_NOOP：UTF-8 界面文字的多语言查找

namespace net {

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0。
//v2 约定 64 位 ID（msgid / cid / from / to）以字符串编码进 JSON，以避免
//JSON 数值精度丢失，故解析时需经本函数还原成整型。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//把 JSON 文本解析成对象 Json::Value；非法 JSON 或顶层非对象返回 false。
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 0xRRGGBB 转成 6 位大写十六进制字符串（不带 #）。手写避免依赖 snprintf。
static std::string rgbToHex6(uint32_t rgb)
{
    static const char* const kHex = "0123456789ABCDEF";
    rgb &= 0xFFFFFFu;
    std::string s(6, '0');
    for (int i = 5; i >= 0; --i)
    {
        s[i] = kHex[rgb & 0xF];
        rgb >>= 4;
    }
    return s;
}

//把字体设置写进 content.font 子对象。m_valid 为 false 时什么都不写（不带字体，
//收方按默认外观渲染）。颜色以 "RRGGBB" 六位十六进制字符串编码。
static void writeFontToContent(Json::Value& content, const TextFont& font)
{
    if (!font.m_valid)
    {
        return;
    }
    Json::Value f;
    f["size"]      = font.m_size;
    f["bold"]      = font.m_bold;
    f["italic"]    = font.m_italic;
    f["underline"] = font.m_underline;
    f["strike"]    = font.m_strike;
    f["color"]     = rgbToHex6(font.m_colorRgb);
    content["font"] = f;
}

//从 content.font 子对象读出字体设置。无该对象时不改动 font（保持 m_valid=false）。
//字号夹到 [8,72] 防御异常值。
static void readFontFromContent(const Json::Value& content, TextFont& font)
{
    const Json::Value& f = content["font"];
    if (!f.isObject())
    {
        return;
    }
    font.m_valid     = true;
    font.m_size      = f["size"].isInt() ? f["size"].asInt() : 14;
    font.m_bold      = f["bold"].isBool() ? f["bold"].asBool() : false;
    font.m_italic    = f["italic"].isBool() ? f["italic"].asBool() : false;
    font.m_underline = f["underline"].isBool() ? f["underline"].asBool() : false;
    font.m_strike    = f["strike"].isBool() ? f["strike"].asBool() : false;
    font.m_colorRgb  = 0x1A1A1A;
    if (f["color"].isString())
    {
        font.m_colorRgb = (uint32_t)strtoul(f["color"].asString().c_str(), NULL, 16) & 0xFFFFFFu;
    }
    if (font.m_size < 8)
    {
        font.m_size = 8;
    }
    if (font.m_size > 72)
    {
        font.m_size = 72;
    }
}

//把引用回复写进 content 的 quote 子对象。q.m_valid 为假时<u>整个 quote 字段都不写</u>
//——老版本客户端不认识它，不写就是一条普通消息，天然向后兼容（与 at 同一策略）。
//msgid / from / fileid 一律字符串编码，避免大整数在 JSON 里丢精度。
static void writeQuoteToContent(Json::Value& content, const ReplyQuote& q)
{
    if (!q.m_valid || q.m_msgid == 0)
    {
        return;
    }
    Json::Value quote;
    quote["msgid"]   = std::to_string(q.m_msgid);
    quote["from"]    = std::to_string(q.m_from);
    quote["name"]    = q.m_name;
    quote["msgtype"] = q.m_msgtype;
    quote["digest"]  = q.m_digest;
    //图片引用才带这几项：收方据 fileid + md5 命中本地缓存或发起下载，
    //宽高用来把缩略图按引用块高度等比缩放。
    if (!q.m_fileid.empty())
    {
        quote["fileid"] = q.m_fileid;
        quote["md5"]    = q.m_md5;
        quote["w"]      = q.m_width;
        quote["h"]      = q.m_height;
    }
    content["quote"] = quote;
}

//从 content 读出引用回复。没有 quote 字段（老版本消息 / 普通消息）时 q 保持
//m_valid=false，调用方按无引用渲染。字段缺失一律给安全默认值，绝不因为对端少写了
//一个字段就让整条消息渲染不出来。
static void readQuoteFromContent(const Json::Value& content, ReplyQuote& q)
{
    const Json::Value& v = content["quote"];
    if (!v.isObject())
    {
        return;
    }
    const int64_t msgid = jsonToInt64(v["msgid"]);
    if (msgid == 0)
    {
        return;   //没有可定位的原消息，这条引用没有意义，按无引用处理
    }
    q.m_valid   = true;
    q.m_msgid   = msgid;
    q.m_from    = jsonToInt64(v["from"]);
    q.m_name    = v["name"].isString() ? v["name"].asString() : std::string();
    q.m_msgtype = v["msgtype"].isInt() ? v["msgtype"].asInt() : message_type_text;
    q.m_digest  = v["digest"].isString() ? v["digest"].asString() : std::string();

    //图片引用的附加项。fileid 容错字符串或数字编码，与图片消息侧同口径。
    const Json::Value& fid = v["fileid"];
    if (fid.isString())
        q.m_fileid = fid.asString();
    else if (fid.isIntegral())
        q.m_fileid = std::to_string(fid.asInt64());

    q.m_md5    = v["md5"].isString() ? v["md5"].asString() : std::string();
    q.m_width  = v["w"].isNumeric() ? v["w"].asInt() : 0;
    q.m_height = v["h"].isNumeric() ? v["h"].asInt() : 0;
}

//把群聊 @提醒 写进 content 的 at 子对象。ats 空且 atAll 为假时<u>整个 at 字段都不写</u>
//——老版本客户端不认识 at，不写就是一条普通文本消息，天然向后兼容。
//uid 以字符串编码（与 cid / msgid 同口径，避免大整数精度丢失）；off / len 是正文
//的 UTF-8 字节偏移与长度。格式见 docs/群聊@提醒方案.md §3。
static void writeMentionsToContent(Json::Value& content,
                                   const std::vector<balloonchatwnd::MentionSpan>& ats,
                                   bool atAll)
{
    if (ats.empty() && !atAll)
        return;

    Json::Value at;
    at["all"] = atAll ? 1 : 0;

    Json::Value users(Json::arrayValue);
    for (size_t i = 0; i < ats.size(); ++i)
    {
        const balloonchatwnd::MentionSpan& s = ats[i];
        //长度非正的片段没有意义，直接不写，免得对端解析出一段空高亮。
        if (s.m_len <= 0 || s.m_off < 0)
            continue;

        Json::Value one;
        one["uid"] = std::to_string(s.m_uid);
        one["off"] = s.m_off;
        one["len"] = s.m_len;
        users.append(one);
    }
    at["users"] = users;

    content["at"] = at;
}

//从 content 的 at 子对象解析出 @提醒。缺该字段（老版本消息）或字段类型异常时
//安全降级为"本条不含 @"，不报错、不影响这条消息的其余部分正常渲染。
static void readMentionsFromContent(const Json::Value& content,
                                    bool& atAll,
                                    std::vector<balloonchatwnd::MentionSpan>& ats)
{
    atAll = false;
    ats.clear();

    const Json::Value& at = content["at"];
    if (!at.isObject())
        return;

    //all 容错整型与布尔两种写法
    if (at["all"].isIntegral())
        atAll = (at["all"].asInt() != 0);
    else if (at["all"].isBool())
        atAll = at["all"].asBool();

    const Json::Value& users = at["users"];
    if (!users.isArray())
        return;

    for (Json::ArrayIndex i = 0; i < users.size(); ++i)
    {
        const Json::Value& one = users[i];
        if (!one.isObject())
            continue;
        if (!one["off"].isIntegral() || !one["len"].isIntegral())
            continue;

        balloonchatwnd::MentionSpan s;
        s.m_uid = jsonToInt64(one["uid"]);
        s.m_off = one["off"].asInt();
        s.m_len = one["len"].asInt();
        //越界 / 非正长度的片段丢弃 —— 渲染侧还会再校验一次，这里先挡一道。
        if (s.m_off < 0 || s.m_len <= 0)
            continue;

        ats.push_back(s);
    }
}

//构造发送请求的公共骨架：填 clientmsgid / ctype / cid(字符串编码) / msgtype /
//content，再序列化为紧凑 JSON。单聊群聊只是 ctype 与 cid 含义不同（单聊 cid=对端
//userid，群聊 cid=群id），各类消息只是 msgtype 与 content 不同。
static std::string buildSendRequest(int ctype, int64_t cid, const std::string& clientMsgId,
                                    int msgType, const Json::Value& content)
{
    Json::Value root;
    root["clientmsgid"] = clientMsgId;
    //cid 以字符串编码，避免大整数精度丢失
    root["ctype"]       = ctype;
    root["cid"]         = std::to_string(cid);
    root["msgtype"]     = msgType;
    root["content"]     = content;
    return writeCompact(root);
}

const char* const kTextFormatMarkdownValue = "markdown";
const char* const kTextFormatCodeValue = "code";
const char* const kCodeLangKey = "lang";
const char* const kCodeNotesKey = "notes";

//批注每一项的键名
static const char* const kCodeNoteFromKey = "from";
static const char* const kCodeNoteToKey = "to";
static const char* const kCodeNoteTextKey = "text";
static const char* const kCodeNoteUserIdKey = "userid";   //作者，缺省为消息发送者（第 11.4 节）
static const char* const kCodeNoteNewKey = "new";         //这一轮新增或修改的批注，取值 1

//content.format 的键名
static const char* const kTextFormatKey = "format";

//组装文字消息的 content：正文写进 text；设过字体写 font；带 @ 写 at（ats 为空且不是
//@所有人时不写）；带引用写 quote；Markdown 与代码片段写 format（纯文字不写，与旧消息逐字节相同），
//代码片段另写 lang（语言标识为空时不写，接收方按纯文本显示）与 notes（没有批注时不写）。
//三种文本发送请求与 textContentBytes 共用这一段，保证发送前计量的体积与真正发出去的 content 逐字节相同。
static Json::Value buildTextContent(const std::string& text, const TextFont& font,
                                    const ReplyQuote& quote,
                                    const std::vector<balloonchatwnd::MentionSpan>& ats,
                                    bool atAll, int textFormat, const std::string& codeLang,
                                    const std::vector<CodeNoteData>& codeNotes)
{
    Json::Value content;
    content["text"] = text;
    writeFontToContent(content, font);
    writeMentionsToContent(content, ats, atAll);
    writeQuoteToContent(content, quote);
    if (textFormat == text_format_markdown)
    {
        content[kTextFormatKey] = kTextFormatMarkdownValue;
    }
    else if (textFormat == text_format_code)
    {
        content[kTextFormatKey] = kTextFormatCodeValue;
        if (!codeLang.empty())
            content[kCodeLangKey] = codeLang;
        MessageProtocolClient::writeCodeNotesToContent(content, codeNotes);
    }
    return content;
}

void MessageProtocolClient::writeCodeNotesToContent(Json::Value& content, const std::vector<CodeNoteData>& notes)
{
    if (notes.empty())
        return;
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        Json::Value item;
        item[kCodeNoteFromKey] = notes[i].m_from;
        item[kCodeNoteToKey] = notes[i].m_to;
        item[kCodeNoteTextKey] = notes[i].m_text;
        //64 位 id 按协议约定写成字符串
        if (notes[i].m_userId != 0)
            item[kCodeNoteUserIdKey] = std::to_string(static_cast<long long>(notes[i].m_userId));
        if (notes[i].m_isNew)
            item[kCodeNoteNewKey] = 1;
        arr.append(item);
    }
    content[kCodeNotesKey] = arr;
}

std::vector<CodeNoteData> MessageProtocolClient::readCodeNotesFromContent(const Json::Value& content)
{
    std::vector<CodeNoteData> notes;
    if (!content.isObject() || !content.isMember(kCodeNotesKey) || !content[kCodeNotesKey].isArray())
        return notes;
    const Json::Value& arr = content[kCodeNotesKey];
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject() || !item[kCodeNoteFromKey].isInt() || !item[kCodeNoteToKey].isInt()
            || !item[kCodeNoteTextKey].isString())
            continue;
        CodeNoteData note;
        note.m_from = item[kCodeNoteFromKey].asInt();
        note.m_to = item[kCodeNoteToKey].asInt();
        note.m_text = item[kCodeNoteTextKey].asString();
        if (note.m_from < 1 || note.m_to < note.m_from || note.m_text.empty())
            continue;
        //作者：带了却不是正整数时整条跳过，不按缺省归到消息发送者名下
        if (item.isMember(kCodeNoteUserIdKey))
        {
            note.m_userId = jsonToInt64(item[kCodeNoteUserIdKey]);
            if (note.m_userId <= 0)
                continue;
        }
        const Json::Value& isNew = item[kCodeNoteNewKey];
        note.m_isNew = (isNew.isIntegral() && isNew.asInt64() == 1) || (isNew.isBool() && isNew.asBool());
        notes.push_back(note);
    }
    return notes;
}

//读 content.format：只有文本消息才认，"markdown" 为 Markdown、"code" 为代码片段，其余（缺省、不认识的取值）都按纯文字
static int readTextFormatFromContent(const Json::Value& content, int msgtype)
{
    if (msgtype != message_type_text || !content.isMember(kTextFormatKey) || !content[kTextFormatKey].isString())
        return text_format_plain;
    const std::string format = content[kTextFormatKey].asString();
    if (format == kTextFormatMarkdownValue)
        return text_format_markdown;
    if (format == kTextFormatCodeValue)
        return text_format_code;
    return text_format_plain;
}

//读代码片段的 content.lang：只有代码片段才读，缺省或不是字符串时为空（显示时按纯文本）
static std::string readCodeLangFromContent(const Json::Value& content, int textFormat)
{
    if (textFormat != text_format_code || !content.isMember(kCodeLangKey) || !content[kCodeLangKey].isString())
        return std::string();
    return content[kCodeLangKey].asString();
}

std::string MessageProtocolClient::buildSendTextRequest(int64_t peerUserId,
                                                        const std::string& clientMsgId,
                                                        const std::string& text,
                                                        const TextFont& font,
                                                        const ReplyQuote& quote,
                                                        int textFormat,
                                                        const std::string& codeLang,
                                                        const std::vector<CodeNoteData>& codeNotes)
{
    //文本内容承载在 content 对象的 text 字段里；单聊 ctype=conversation_single。
    //发送方若设过字体，再写入 content.font（对端与历史据此渲染相同效果）。单聊没有 @。
    const Json::Value content = buildTextContent(text, font, quote,
                                                 std::vector<balloonchatwnd::MentionSpan>(), false, textFormat,
                                                 codeLang, codeNotes);
    return buildSendRequest(conversation_single, peerUserId, clientMsgId, message_type_text, content);
}

const char* const kAutoReplyTextPrefix = TR_NOOP(u8"[自动回复] ");

//自动回复消息的 content：正文带前缀，另带 autoreply 标记。发送请求与本端回显共用，保证两份逐字相同
static Json::Value buildAutoReplyContent(const std::string& replyText)
{
    Json::Value content;
    content["text"] = TR8(kAutoReplyTextPrefix) + replyText;
    content["autoreply"] = true;
    return content;
}

std::string MessageProtocolClient::buildSendAutoReplyRequest(int64_t peerUserId,
                                                             const std::string& clientMsgId,
                                                             const std::string& replyText)
{
    return buildSendRequest(conversation_single, peerUserId, clientMsgId, message_type_text,
                            buildAutoReplyContent(replyText));
}

std::string MessageProtocolClient::buildSendGroupTextRequest(int64_t groupId,
                                                             const std::string& clientMsgId,
                                                             const std::string& text,
                                                             const TextFont& font,
                                                             const ReplyQuote& quote,
                                                             int textFormat,
                                                             const std::string& codeLang,
                                                             const std::vector<CodeNoteData>& codeNotes)
{
    //群聊文本：ctype=conversation_group、cid=群id，其余与单聊文本一致。
    const Json::Value content = buildTextContent(text, font, quote,
                                                 std::vector<balloonchatwnd::MentionSpan>(), false, textFormat,
                                                 codeLang, codeNotes);
    return buildSendRequest(conversation_group, groupId, clientMsgId, message_type_text, content);
}

std::string MessageProtocolClient::buildSendGroupTextRequest(
    int64_t groupId,
    const std::string& clientMsgId,
    const std::string& text,
    const std::vector<balloonchatwnd::MentionSpan>& ats,
    bool atAll,
    const TextFont& font,
    const ReplyQuote& quote,
    int textFormat,
    const std::string& codeLang,
    const std::vector<CodeNoteData>& codeNotes)
{
    const Json::Value content = buildTextContent(text, font, quote, ats, atAll, textFormat, codeLang, codeNotes);
    return buildSendRequest(conversation_group, groupId, clientMsgId, message_type_text, content);
}

size_t MessageProtocolClient::textContentBytes(const std::string& text, const TextFont& font,
                                               const ReplyQuote& quote,
                                               const std::vector<balloonchatwnd::MentionSpan>& ats,
                                               bool atAll,
                                               int textFormat,
                                               const std::string& codeLang,
                                               const std::vector<CodeNoteData>& codeNotes)
{
    //与服务端同一口径：content 对象的紧凑 JSON 字节数（服务端解析请求后按同样的写法重新序列化）
    return writeCompact(buildTextContent(text, font, quote, ats, atAll, textFormat, codeLang, codeNotes)).size();
}

std::string MessageProtocolClient::buildSendImageRequest(int ctype, int64_t cid,
                                                         const std::string& clientMsgId,
                                                         const std::string& fileid,
                                                         const std::string& md5,
                                                         int width, int height, int64_t size)
{
    //图片消息 content：fileid（字符串编码避免大整数精度丢失）+ md5 + 宽高 + 大小。
    //本轮不带 thumbfileid（缩略图），收方下载原图后缩放显示。
    Json::Value content;
    content["fileid"] = fileid;
    content["md5"]    = md5;
    content["width"]  = width;
    content["height"] = height;
    content["size"]   = static_cast<Json::Int64>(size);
    return buildSendRequest(ctype, cid, clientMsgId, message_type_image, content);
}

std::string MessageProtocolClient::buildSendFileRequest(int ctype, int64_t cid,
                                                        const std::string& clientMsgId,
                                                        const std::string& fileid,
                                                        const std::string& filename,
                                                        int64_t filesize,
                                                        const std::string& md5)
{
    //文件消息 content：fileid（字符串编码避免大整数精度丢失）+ filename（原始文件名，
    //含扩展名，UTF-8）+ filesize（字节数）+ md5。收方据 fileid 走下载链路取原文件，
    //按 filename 命名落地、按 md5 校验完整性。与图片消息区别在 msgtype 与字段集。
    Json::Value content;
    content["fileid"]   = fileid;
    content["filename"] = filename;
    content["filesize"] = static_cast<Json::Int64>(filesize);
    content["md5"]      = md5;
    return buildSendRequest(ctype, cid, clientMsgId, message_type_file, content);
}

std::string MessageProtocolClient::buildSendVoiceRequest(int ctype, int64_t cid,
                                                         const std::string& clientMsgId,
                                                         const std::string& fileid,
                                                         const std::string& md5,
                                                         int durationSeconds, int64_t size,
                                                         const std::string& wave)
{
    //语音消息 content：fileid（字符串编码）+ md5 + duration（整秒）+ size（字节数）+ wave（可选，
    //40 个音量值的 Base64）。服务端按 docs/语音消息方案.md 第 5 节校验，不合格回 code=3
    Json::Value content;
    content["fileid"]   = fileid;
    content["md5"]      = md5;
    content["duration"] = durationSeconds;
    content["size"]     = static_cast<Json::Int64>(size);
    if (!wave.empty())
        content["wave"] = wave;
    return buildSendRequest(ctype, cid, clientMsgId, message_type_voice, content);
}

std::string MessageProtocolClient::buildSendCardRequest(int ctype, int64_t cid,
                                                        const std::string& clientMsgId,
                                                        int msgtype,
                                                        const std::string& contentJson)
{
    if (cid <= 0 || clientMsgId.empty())
        return std::string();

    //卡片 content 已由各卡片的协议模块校验并拼好，这里只解析回 Json::Value 再套上外层信封，
    //使它与其它消息一样作为 JSON 对象嵌入（而不是被当成一个字符串字段）
    Json::Value content;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(contentJson.c_str(), contentJson.c_str() + contentJson.size(),
                                  &content, &errs);
    delete reader;
    if (!ok || !errs.empty() || !content.isObject())
        return std::string();
    return buildSendRequest(ctype, cid, clientMsgId, msgtype, content);
}

std::string MessageProtocolClient::buildSendShakeRequest(int64_t peerUserId,
                                                         const std::string& clientMsgId)
{
    //抖动无正文，content 为空对象 {}。Json::objectValue 确保序列化成 {} 而非 null。
    Json::Value content(Json::objectValue);
    return buildSendRequest(conversation_single, peerUserId, clientMsgId, message_type_shake, content);
}

//实现说明见 MessageProtocolClient.h 的声明处。/f 标签是纯 ASCII，UTF-8 多字节字符
//均 >0x7F，按字节扫描 '/' 安全。
bool MessageProtocolClient::tryNormalizeInlineFaceText(const std::string& text, std::string& out)
{
    bool hasFace = false;
    std::string result;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n)
    {
        const char c = text[i];
        if (c == '/')
        {
            //"//" → 一个字面 '/'
            if (i + 1 < n && text[i + 1] == '/')
            {
                result += '/';
                i += 2;
                continue;
            }
            //尝试 /f["<数字>"]
            if (i + 4 < n && text[i + 1] == 'f' && text[i + 2] == '[' && text[i + 3] == '"')
            {
                size_t j = i + 4;
                bool hasDigit = false;
                while (j < n && text[j] >= '0' && text[j] <= '9')
                {
                    ++j;
                    hasDigit = true;
                }
                if (hasDigit && j + 1 < n && text[j] == '"' && text[j + 1] == ']')
                {
                    result += TR8(u8"[表情]");
                    hasFace = true;
                    i = j + 2;
                    continue;
                }
            }
            //孤立 '/'（残缺标签）：当普通字符。
            result += c;
            ++i;
            continue;
        }
        result += c;
        ++i;
    }

    if (hasFace)
    {
        out = result;
    }
    return hasFace;
}

std::string MessageProtocolClient::digestForMsgType(int msgtype, const std::string& text)
{
    //文本与系统消息：预览直接取正文，空则用通用占位（中文字面量用 u8"" 保证
    //以 UTF-8 落入会话预览，与服务端 buildMessageDigest 对齐）。
    if (msgtype == message_type_text || msgtype == message_type_system)
    {
        if (!text.empty())
        {
            //含内联表情的文本：标签替换成 [表情]、转义斜杠还原，不把 /f["id"]
            //原始标记暴露到会话预览。
            std::string normalized;
            if (tryNormalizeInlineFaceText(text, normalized))
            {
                return normalized;
            }
            return text;
        }
        return TR8(u8"[消息]");
    }

    //其余类型返回占位文案。本轮单聊只新增表情/抖动，其余类型预留同款占位，
    //待对应功能接入时各自渲染。
    switch (msgtype)
    {
    case message_type_image:
        return TR8(u8"[图片]");
    case message_type_file:
        return TR8(u8"[文件]");
    case message_type_voice:
        return TR8(u8"[语音]");
    case message_type_emoji:
        return TR8(u8"[表情]");
    case message_type_shake:
        return TR8(u8"[抖一抖]");
    //位置、名片、投票、待办：与服务端 buildMessageDigest 的措辞一致（多语言第 5 期补齐，此前落到「[消息]」）
    case message_type_location:
        return TR8(u8"[位置]");
    case message_type_card:
        return TR8(u8"[名片]");
    case message_type_poll:
        return TR8(u8"[投票]");
    case message_type_todo:
        return TR8(u8"[待办]");
    case message_type_folder:
        //文件夹传输记录：这里只给类型占位。会话列表上真正显示的是"[文件夹] 项目资料"
        //那种带名字的写法，由 balloonmain::BuildFolderInviteDigest 在主面板那一层拼出来
        //（本函数拿不到文件夹名，它在 content 里而不在正文里）。两处的方括号措辞一致。
        return TR8(u8"[文件夹]");
    case message_type_merged:
        //合并转发的聊天记录，与服务端 buildMessageDigest 的措辞一致
        return TR8(u8"[聊天记录]");
    case message_type_relay:
        //群接龙卡片：这里只给类型占位，带标题的写法见 digestForNotify（标题在 content 里而不在正文里）
        return TR8(u8"[接龙]");
    case message_type_fileshare:
        //文件共享卡片：这里只给类型占位，带共享名的写法见 digestForNotify，与服务端 buildMessageDigest 一致
        return TR8(u8"[共享]");
    case message_type_screenshare:
        //屏幕共享卡片：与服务端 buildMessageDigest 一致，不带发起人名（会话列表本身已显示发送者）
        return TR8(u8"[屏幕共享]");
    case message_type_rtc:
        //通话记录：这里只给类型占位，群通话卡片按媒体类型的写法见 digestForNotify，与服务端 buildMessageDigest 一致
        return TR8(u8"[通话]");
    case message_type_group_invite:
        //入群邀请卡片：这里只给类型占位，带群名的写法见 digestForNotify，与服务端 buildMessageDigest 一致
        return TR8(u8"[入群邀请]");
    case message_type_announcement:
        //群公告卡片：这里只给类型占位，带标题的写法见 digestForNotify，与服务端 buildMessageDigest 一致
        return TR8(u8"[群公告]");
    case message_type_memo:
        //群备忘卡片：这里只给类型占位，带标题的写法见 digestForNotify，与服务端 buildMessageDigest 一致
        return TR8(u8"[群备忘]");
    case message_type_cloudrecord:
        //云端录像卡片：这里只给类型占位，带标题的写法见 digestForNotify，与服务端 buildMessageDigest 一致
        return TR8(u8"[云端录像]");
    case message_type_thread_post:
        //帖子卡片：这里只给类型占位，带标题的写法见 threadCardDigest
        return TR8(u8"[帖子]");
    case message_type_thread_reply:
        //帖子动态卡片（留言、回复、补充）：这里只给类型占位，带内容的写法见 threadCardDigest
        return TR8(u8"[帖子动态]");
    default:
        return TR8(u8"[消息]");
    }
}

std::string MessageProtocolClient::threadCardDigest(int msgtype, int kind, const std::string& arg, bool deleted)
{
    //帖子卡片：「发帖：标题」；已删除时写「帖子已删除」
    if (msgtype == message_type_thread_post)
    {
        if (deleted)
            return TR8(u8"帖子已删除");
        return arg.empty() ? TRC8("帖子动态摘要", u8"发帖") : TRF8(u8"发帖：{0}", arg);
    }
    switch (kind)
    {
    //有人在帖子下留言；留言被删除后（或所在的帖子被删除后）写「留言已删除」
    case thread_reply_comment:
        if (deleted)
            return TR8(u8"留言已删除");
        return arg.empty() ? TRC8("帖子动态摘要", u8"留言") : TRF8(u8"留言：{0}", arg);

    //有人回复了一条留言；被删除后写「回复已删除」
    case thread_reply_reply:
        if (deleted)
            return TR8(u8"回复已删除");
        return arg.empty() ? TRC8("帖子动态摘要", u8"回复") : TRF8(u8"回复：{0}", arg);

    //楼主追加了补充；被删除后写「补充已删除」
    case thread_reply_addendum:
        if (deleted)
            return TR8(u8"补充已删除");
        return arg.empty() ? TR8(u8"补充了帖子") : TRF8(u8"补充了帖子：{0}", arg);

    //不认识的 kind（新服务端加了新的动作）：只写类型
    default:
        return digestForMsgType(msgtype, std::string());
    }
}

bool MessageProtocolClient::threadCardMentions(const MessageNotify& notify, int64_t userId)
{
    if (notify.m_msgtype != message_type_thread_post && notify.m_msgtype != message_type_thread_reply)
        return false;
    //@ 了所有人：每个成员都算被 @ 到
    if (notify.m_threadMentionAll)
        return true;
    for (size_t i = 0; i < notify.m_threadMentions.size(); ++i)
    {
        if (notify.m_threadMentions[i] == userId)
            return true;
    }
    return false;
}

bool MessageProtocolClient::parseThreadCardContent(int msgtype, const std::string& content, std::string& arg, int& kind,
                                                   int64_t& postId, bool& deleted)
{
    arg.clear();
    kind = 0;
    postId = 0;
    deleted = false;
    Json::Value card;
    if (!parseObject(content, card))
        return false;
    if (card["postid"].isString())
        postId = static_cast<int64_t>(strtoll(card["postid"].asString().c_str(), NULL, 10));
    else if (card["postid"].isIntegral())
        postId = card["postid"].asInt64();
    //帖子卡片与动态卡片都可能被改写为已删除形态（docs/帖子群方案.md 14.6），此时不带原文
    deleted = card["deleted"].isIntegral() && card["deleted"].asInt() != 0;
    if (msgtype == message_type_thread_post)
    {
        if (deleted)
            return true;
        if (card["title"].isString() && !card["title"].asString().empty())
            arg = card["title"].asString();
        else if (card["digest"].isString())
            arg = card["digest"].asString();
        return true;
    }
    kind = card["kind"].isIntegral() ? card["kind"].asInt() : 0;
    if (deleted)
        return true;
    if (card["digest"].isString() && !card["digest"].asString().empty())
        arg = card["digest"].asString();
    else if (card["posttitle"].isString())
        arg = card["posttitle"].asString();
    return true;
}

std::string MessageProtocolClient::groupCallDigest(const std::string& contentJson)
{
    //通话卡片 content 的 kind 与媒体类型取值：群通话卡片 groupcall（docs/协议设计-v2.md 21.5）、一对一通话卡片 call
    //（7.8 节「一对一通话卡片」），视频都为 2
    const char* const kGroupCallKind = "groupcall";
    const char* const kCallKind = "call";
    const int kCardVideo = 2;
    Json::Value card;
    if (!parseObject(contentJson, card) || !card["kind"].isString())
        return TR8(u8"[通话]");
    const bool video = card["media"].isInt() && card["media"].asInt() == kCardVideo;
    if (card["kind"].asString() == kGroupCallKind)
        return video ? TR8(u8"[群视频通话]") : TR8(u8"[群语音通话]");
    if (card["kind"].asString() == kCallKind)
        return video ? TR8(u8"[视频通话]") : TR8(u8"[语音通话]");
    return TR8(u8"[通话]");
}

std::string MessageProtocolClient::digestForNotify(const MessageNotify& notify)
{
    //帖子卡片、帖子动态卡片：「发帖：标题」「回复：内容」等，与会话同步时在本地生成的写法一致
    if (notify.m_msgtype == message_type_thread_post || notify.m_msgtype == message_type_thread_reply)
    {
        std::string arg;
        int kind = 0;
        int64_t postId = 0;
        bool deleted = false;
        parseThreadCardContent(notify.m_msgtype, notify.m_threadCardContent, arg, kind, postId, deleted);
        return threadCardDigest(notify.m_msgtype, kind, arg, deleted);
    }
    //文件共享卡片：「[共享] 共享名」，与服务端 buildMessageDigest 一致；content 不可用时只写「[共享]」
    if (notify.m_msgtype == message_type_fileshare)
    {
        ShareCardContent card;
        if (parseShareCardContent(notify.m_shareContent, card) && !card.m_name.empty())
            return digestForMsgType(notify.m_msgtype, notify.m_text) + " " + card.m_name;
        return digestForMsgType(notify.m_msgtype, notify.m_text);
    }
    //群通话卡片：按媒体类型写「[群语音通话]」「[群视频通话]」，与服务端 buildMessageDigest 一致；content 不可用时只写类型
    if (notify.m_msgtype == message_type_rtc)
        return groupCallDigest(notify.m_groupCallContent);
    //入群邀请卡片：「[入群邀请] 群名」，与服务端 buildMessageDigest 一致；content 不可用或没有群名时只写类型
    if (notify.m_msgtype == message_type_group_invite)
    {
        Json::Value card;
        if (parseObject(notify.m_groupInviteContent, card) && card["groupname"].isString()
            && !card["groupname"].asString().empty())
        {
            return digestForMsgType(notify.m_msgtype, notify.m_text) + " " + card["groupname"].asString();
        }
        return digestForMsgType(notify.m_msgtype, notify.m_text);
    }
    //群公告卡片、群备忘卡片、云端录像卡片：「[群公告] 标题」「[群备忘] 标题」「[云端录像] 标题」，与服务端
    //buildMessageDigest 一致；content 不可用或没有标题时只写类型
    if (notify.m_msgtype == message_type_announcement || notify.m_msgtype == message_type_memo
        || notify.m_msgtype == message_type_cloudrecord)
    {
        const std::string& cardJson = (notify.m_msgtype == message_type_announcement) ? notify.m_announcementContent
                                      : (notify.m_msgtype == message_type_memo)       ? notify.m_memoCardContent
                                                                                      : notify.m_cloudRecordContent;
        Json::Value card;
        if (parseObject(cardJson, card) && card["title"].isString() && !card["title"].asString().empty())
            return digestForMsgType(notify.m_msgtype, notify.m_text) + " " + card["title"].asString();
        return digestForMsgType(notify.m_msgtype, notify.m_text);
    }
    if (notify.m_msgtype != message_type_relay)
    {
        return digestForMsgType(notify.m_msgtype, notify.m_text);
    }
    //接龙卡片：「[接龙] 标题」，与服务端 buildMessageDigest 一致；content 不可用或标题为空时只写「[接龙]」
    std::string digest = digestForMsgType(notify.m_msgtype, notify.m_text);
    Json::Value root;
    if (parseObject(notify.m_relayContent, root) && root["title"].isString())
    {
        const std::string title = root["title"].asString();
        if (!title.empty())
        {
            digest += " ";
            digest += title;
        }
    }
    return digest;
}

std::string MessageProtocolClient::buildFileTransferAckRequest(int64_t peerUserId, int64_t msgid, int state)
{
    //三者任一非法这条上报都没有意义：服务端会拒收，本端也无从对回
    if (peerUserId <= 0 || msgid <= 0)
        return std::string();
    if (state != file_ack_receiving && state != file_ack_received && state != file_ack_rejected)
        return std::string();

    Json::Value root;
    root["ctype"] = conversation_single;   //当前只有单聊文件消息
    //cid 取对端（文件发送方）userid；大整数一律字符串编码，避免 JSON 精度丢失
    root["cid"]   = std::to_string(peerUserId);
    root["msgid"] = std::to_string(msgid);
    root["state"] = state;
    return writeCompact(root);
}

bool MessageProtocolClient::parseFileTransferAckNotify(const std::string& jsonbody,
                                                       FileTransferAckNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;
    out.m_cid   = jsonToInt64(root["cid"]);
    out.m_msgid = jsonToInt64(root["msgid"]);
    if (out.m_msgid <= 0)
        return false;

    //状态直接决定气泡显示什么文字，越界值会显示成莫名其妙的状态，故在此挡掉
    out.m_state = root["state"].isInt() ? root["state"].asInt() : 0;
    if (out.m_state != file_ack_receiving
        && out.m_state != file_ack_received
        && out.m_state != file_ack_rejected)
    {
        return false;
    }
    return true;
}

std::string MessageProtocolClient::buildTypingNotifyRequest(int64_t peerUserId, int state)
{
    //对端非法这条提示无处可送；state 越界服务端会原样转给对端，反而让对方界面
    //出现无法解释的状态，故一并在此挡掉
    if (peerUserId <= 0)
        return std::string();
    if (state != typing_state_typing && state != typing_state_stopped)
        return std::string();

    Json::Value root;
    root["ctype"] = conversation_single;   //服务端只转发单聊的输入提示
    //cid 取对端userid（"发给谁"）；大整数一律字符串编码，避免 JSON 精度丢失
    root["cid"]   = std::to_string(peerUserId);
    root["state"] = state;
    return writeCompact(root);
}

bool MessageProtocolClient::parseTypingNotify(const std::string& jsonbody, TypingNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    out.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;
    //服务端只转发单聊，收到别的会话类型说明报文有问题，丢弃即可
    if (out.m_ctype != conversation_single)
        return false;

    //推送里的 cid 是"正在输入的那个人"，界面据它找聊天窗，为空则无从路由
    out.m_cid = jsonToInt64(root["cid"]);
    if (out.m_cid <= 0)
        return false;

    out.m_state = root["state"].isInt() ? root["state"].asInt() : typing_state_stopped;
    if (out.m_state != typing_state_typing && out.m_state != typing_state_stopped)
        return false;

    return true;
}

bool MessageProtocolClient::parseSendResponse(const std::string& jsonbody, SendMessageResponse& resp)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //code 为必有字段，缺失或非整型视为非法应答
    if (!root["code"].isInt())
        return false;

    resp.m_code        = root["code"].asInt();
    resp.m_msg         = root["msg"].isString() ? root["msg"].asString() : std::string();
    resp.m_clientmsgid = root["clientmsgid"].isString() ? root["clientmsgid"].asString() : std::string();

    //失败应答到此为止，msgid / timestamp 不再解析
    if (resp.m_code != 0)
        return true;

    //成功应答：msgid 以字符串编码，timestamp 为整型毫秒
    resp.m_msgid     = jsonToInt64(root["msgid"]);
    resp.m_timestamp = root["timestamp"].isNumeric() ? root["timestamp"].asInt64() : 0;
    return true;
}

//从一个消息体 JSON 对象（cmd_message_notify 推送 / cmd_sync_messages 里的一条）
//解析出 MessageNotify。msgid/from 缺失（<=0）视为非法。供推送与历史拉取共用。
static bool fillMessageFromObject(const Json::Value& root, MessageNotify& notify)
{
    notify.m_msgid   = jsonToInt64(root["msgid"]);
    notify.m_from    = jsonToInt64(root["from"]);
    notify.m_msgtype = root["msgtype"].isInt() ? root["msgtype"].asInt() : 0;
    if (notify.m_msgid <= 0)
        return false;
    //发送者须为正；例外是服务端以系统名义写入的两类消息，from 统一为 0（服务端 kSystemMessageSenderId）：
    //一是系统提示（发布公告、入群欢迎语、退出群聊、转让群主等）；二是入群必读卡片（公告卡片且 content 带
    //mustread，只发给新成员本人，docs/群公告接入方案.md §14）。若一律要求 from > 0，这两类消息在推送与拉历史时
    //都会被当成脏数据丢掉（2026-10-07 实测必读卡片即因此没有显示）。普通公告卡片以发布人的名义发出，from 为 0 时仍判为脏数据
    const Json::Value& rawContent = root["content"];
    const bool mustReadCard = (notify.m_msgtype == message_type_announcement && rawContent.isObject()
                               && rawContent["mustread"].isBool() && rawContent["mustread"].asBool());
    const bool systemFromServer = (notify.m_from == 0
                                   && (notify.m_msgtype == message_type_system || mustReadCard));
    if (notify.m_from <= 0 && !systemFromServer)
        return false;

    notify.m_clientmsgid = root["clientmsgid"].isString() ? root["clientmsgid"].asString() : std::string();
    notify.m_ctype       = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;
    notify.m_cid         = jsonToInt64(root["cid"]);
    notify.m_to          = jsonToInt64(root["to"]);
    notify.m_timestamp   = root["timestamp"].isNumeric() ? root["timestamp"].asInt64() : 0;
    notify.m_recalled    = root["recalled"].isBool() ? root["recalled"].asBool() : false;
    //撤回者：只有已撤回且服务端记录了撤回者时才有这个字段，其余情形取 0
    notify.m_recalledBy  = jsonToInt64(root["recalledby"]);
    //文件收取状态：服务端只对"拉取者自己发出的文件消息"回填，其余情形无此字段（保持 0）。
    //发送方离线期间对方的接收 / 拒绝，就是靠拉历史时的这个字段补看到的。
    notify.m_fileAckState = root["fileackstate"].isInt() ? root["fileackstate"].asInt() : 0;
    //发送者昵称：只有合并记录里的子消息带，其余消息没有此字段（保持空串）
    notify.m_fromName = root["fromname"].isString() ? root["fromname"].asString() : std::string();

    //按 msgtype 从 content 取出对应字段。content 可能非对象（脏数据），统一先判类型。
    const Json::Value& content = root["content"];
    if (content.isObject())
    {
        if (notify.m_msgtype == message_type_image)
        {
            //图片消息：fileid / md5 / 宽高 / 大小（fileid 容错字符串或数字编码）
            const Json::Value& fid = content["fileid"];
            if (fid.isString())
                notify.m_imageFileId = fid.asString();
            else if (fid.isIntegral())
                notify.m_imageFileId = std::to_string(fid.asInt64());

            notify.m_imageMd5    = content["md5"].isString() ? content["md5"].asString() : std::string();
            notify.m_imageWidth  = content["width"].isInt() ? content["width"].asInt() : 0;
            notify.m_imageHeight = content["height"].isInt() ? content["height"].asInt() : 0;
            notify.m_imageSize   = content["size"].isNumeric() ? content["size"].asInt64() : 0;
        }
        else if (notify.m_msgtype == message_type_file)
        {
            //文件消息：fileid / filename / filesize / md5（fileid 容错字符串或数字编码，
            //与图片侧同口径）。收方据 fileid 下载、按 filename 命名落地、按 md5 校验。
            const Json::Value& fid = content["fileid"];
            if (fid.isString())
                notify.m_fileFileId = fid.asString();
            else if (fid.isIntegral())
                notify.m_fileFileId = std::to_string(fid.asInt64());

            notify.m_fileName = content["filename"].isString() ? content["filename"].asString() : std::string();
            notify.m_fileSize = content["filesize"].isNumeric() ? content["filesize"].asInt64() : 0;
            notify.m_fileMd5  = content["md5"].isString() ? content["md5"].asString() : std::string();

            //单聊单文件 P2P 直传留下的文件消息（content 带 direct=1）：校验与归一集中在
            //FolderProtocolClient::parseDirectFileContent，与服务端的产出逐字对应。普通离线文件消息解析不出来，字段保持默认
            DirectFileContent direct;
            if (FolderProtocolClient::parseDirectFileContent(writeCompact(content), direct))
            {
                notify.m_fileDirect          = true;
                notify.m_fileDirectSessionId = direct.m_sessionId;
                notify.m_fileDirectResult    = direct.m_result;
                notify.m_fileLinkPath        = direct.m_linkPath;
                notify.m_fileMd5             = direct.m_md5;
            }
        }
        else if (notify.m_msgtype == message_type_voice)
        {
            //语音消息：fileid / md5 / duration / size / wave（fileid 容错字符串或数字编码，与图片、文件同口径；
            //wave 可选，老消息或发送方没写时为空串，画气泡时换成均匀波形）
            const Json::Value& fid = content["fileid"];
            if (fid.isString())
                notify.m_voiceFileId = fid.asString();
            else if (fid.isIntegral())
                notify.m_voiceFileId = std::to_string(fid.asInt64());

            notify.m_voiceMd5      = content["md5"].isString() ? content["md5"].asString() : std::string();
            notify.m_voiceDuration = content["duration"].isInt() ? content["duration"].asInt() : 0;
            notify.m_voiceSize     = content["size"].isNumeric() ? content["size"].asInt64() : 0;
            notify.m_voiceWave     = content["wave"].isString() ? content["wave"].asString() : std::string();
        }
        else if (notify.m_msgtype == message_type_folder)
        {
            //文件夹传输记录条：字段校验与归一集中在 FolderProtocolClient::parseRecordContent，
            //本处不另抄一份 —— 那边与服务端 FolderTransferProtocol 是逐字对应的镜像。
            //之所以先把 content 序列化回文本再交给它：该函数的入参是 JSON 文本（它要能被
            //单元测试直接喂字符串），而这里手上是已经解析好的 Json::Value。多一次序列化在
            //这条路径上无所谓——记录条一次传输才产生一条，不是热路径。
            FolderRecordContent record;
            if (FolderProtocolClient::parseRecordContent(writeCompact(content), record))
            {
                notify.m_folderSessionId  = record.m_sessionId;
                notify.m_folderName       = record.m_folderName;
                notify.m_folderTotalDirs  = record.m_totalDirs;
                notify.m_folderTotalFiles = record.m_totalFiles;
                notify.m_folderTotalSize  = record.m_totalSize;
                notify.m_folderResult     = record.m_result;
                notify.m_folderErrCode    = record.m_errCode;
                notify.m_folderLinkPath   = record.m_linkPath;
            }
            //解析不出来时各字段保持默认（m_folderResult 为 0），调用方据此按"内容不可用"
            //降级渲染；整条消息仍然有效，不因 content 脏了就把它从聊天记录里抹掉
        }
        else if (notify.m_msgtype == message_type_merged)
        {
            //合并转发的聊天记录：content 原文整体保留，用到时再经 parseMergedContent 展开
            notify.m_mergedContent = writeCompact(content);
        }
        else if (notify.m_msgtype == message_type_relay)
        {
            //群接龙卡片：content 原文整体保留，用到时再经 CardProtocolClient::parseRelayCardContent 解析
            notify.m_relayContent = writeCompact(content);
        }
        else if (notify.m_msgtype == message_type_fileshare)
        {
            //文件共享卡片：content 原文整体保留，用到时再经 parseShareCardContent 解析
            notify.m_shareContent = writeCompact(content);
        }
        else if (notify.m_msgtype == message_type_screenshare)
        {
            //屏幕共享卡片：content 原文整体保留，用到时再经 parseScreenShareCardContent 解析
            notify.m_screenShareContent = writeCompact(content);
        }
        else if (notify.m_msgtype == message_type_group_invite)
        {
            //入群邀请卡片：content 原文整体保留，用到时再经 balloonchatwnd::ApplyGroupInviteCard 解析
            notify.m_groupInviteContent = writeCompact(content);
        }
        else if (notify.m_msgtype == message_type_announcement)
        {
            //群公告卡片：content 原文整体保留，用到时再经 balloonchatwnd::ApplyAnnouncementCard 解析。
            //content 里的 text 只给老客户端兜底，这里不取，否则会走进下面的文本分支
            notify.m_announcementContent = writeCompact(content);
        }
        else if (notify.m_msgtype == message_type_memo)
        {
            //群备忘卡片：content 原文整体保留，用到时再经 balloonchatwnd::ApplyMemoCard 解析
            notify.m_memoCardContent = writeCompact(content);
        }
        else if (notify.m_msgtype == message_type_cloudrecord)
        {
            //云端录像卡片：content 原文整体保留，用到时再经 balloonchatwnd::ApplyCloudRecordCard 解析
            notify.m_cloudRecordContent = writeCompact(content);
        }
        else if (notify.m_msgtype == message_type_thread_post || notify.m_msgtype == message_type_thread_reply)
        {
            //帖子卡片、帖子动态卡片：content 原文整体保留，用到时再经 parseThreadCardContent 解析；
            //被 @ 的人另读进 m_threadMentions，主面板据此判断这张卡片有没有 @ 到本人
            notify.m_threadCardContent = writeCompact(content);
            const Json::Value& mentions = content["mentions"];
            if (mentions.isArray())
            {
                for (Json::ArrayIndex i = 0; i < mentions.size(); ++i)
                {
                    const int64_t uid = jsonToInt64(mentions[i]);
                    if (uid > 0)
                        notify.m_threadMentions.push_back(uid);
                }
            }
            //@ 了所有人（docs/帖子群方案.md §15）：服务端只在为真时写这个字段
            notify.m_threadMentionAll = content["mentionall"].isBool() && content["mentionall"].asBool();
        }
        else if (notify.m_msgtype == message_type_rtc)
        {
            //通话记录（目前只有群通话卡片）：content 原文整体保留，用到时再经 GroupCallProtocolClient::parseCard 解析
            notify.m_groupCallContent = writeCompact(content);
        }
        else if (content["text"].isString())
        {
            //文本（及系统等取正文的类型）：正文在 content.text
            notify.m_text = content["text"].asString();
            //可选字体设置：发送方设过字体时 content.font 存在，读进 m_font 供气泡按
            //相同效果渲染；无该字段则 m_font.m_valid 保持 false，按默认外观渲染。
            readFontFromContent(content, notify.m_font);
            //可选 @提醒：群聊里发送方 @ 过人时 content.at 存在。无该字段（老版本消息）
            //则 m_atAll 保持 false、m_ats 为空，按纯文本渲染。
            readMentionsFromContent(content, notify.m_atAll, notify.m_ats);
            //可选 引用回复：发送方引用过某条消息时 content.quote 存在。无该字段
            //（老版本消息 / 普通消息）则 m_quote.m_valid 保持 false，按无引用渲染。
            readQuoteFromContent(content, notify.m_quote);
            //可选 文本格式：Markdown 消息带 content.format = "markdown"，代码片段带 "code"；缺省或不认识的取值按纯文字
            notify.m_textFormat = readTextFormatFromContent(content, notify.m_msgtype);
            //代码片段的语言标识：content.lang；不是代码片段或没有这个字段时为空
            notify.m_codeLang = readCodeLangFromContent(content, notify.m_textFormat);
            //代码片段的批注：content.notes（docs/代码片段方案.md 第 10.5 节）
            if (notify.m_textFormat == text_format_code)
                notify.m_codeNotes = MessageProtocolClient::readCodeNotesFromContent(content);
            if (notify.m_msgtype == message_type_system)
            {
                //接龙相关的群系统提示另带 cardmsgid（卡片所在消息 id，字符串编码），提示行据此可点击
                notify.m_cardMsgId = jsonToInt64(content["cardmsgid"]);
                //结构化提示（多语言第 5 期）：认得类型时正文换成当前界面语言的文字；认不出的类型仍用服务端的 text
                if (content["tipkind"].isString() && content["tipargs"].isObject())
                {
                    notify.m_tipKind = content["tipkind"].asString();
                    notify.m_tipArgs = writeCompact(content["tipargs"]);
                    std::string localized;
                    if (FormatSystemTip(notify.m_tipKind, content["tipargs"], localized))
                        notify.m_text = localized;
                }
            }
            //可选 自动回复标记：发送方开着「离开 / 忙碌时自动回复」时由客户端自动发出的文本带这个字段。
            //只认文本消息、只认布尔 true，其余一律按普通消息处理
            notify.m_autoReply = (notify.m_msgtype == message_type_text)
                && content["autoreply"].isBool() && content["autoreply"].asBool();
            //可选 机器人标记：只认文本消息、只认对象；单聊代答的正文带「[机器人] 」或「[自动回复] 」前缀，供老客户端阅读，
            //这里原样保留，由显示层按 m_botKind 去掉前缀并加上标记
            const Json::Value& botMark = content["bot"];
            if (notify.m_msgtype == message_type_text && botMark.isObject())
            {
                notify.m_botId = jsonToInt64(botMark["id"]);
                notify.m_botName = botMark["name"].isString() ? botMark["name"].asString() : std::string();
                notify.m_botKind = botMark["kind"].isString() ? botMark["kind"].asString() : std::string();
            }
        }
    }

    //保存整条消息的原始 JSON，供本地消息库存储、打开聊天窗时先用本地数据还原。
    //writeCompact 按键名排序输出，同一条消息无论解析几次得到的文本都相同
    notify.m_rawJson = writeCompact(root);

    return true;
}

bool MessageProtocolClient::parseMessageNotify(const std::string& jsonbody, MessageNotify& notify)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    return fillMessageFromObject(root, notify);
}

bool MessageProtocolClient::buildAutoReplyEcho(int64_t selfId, int64_t peerUserId,
                                               const std::string& clientMsgId, int64_t msgid,
                                               int64_t timestamp, const std::string& replyText,
                                               MessageNotify& out)
{
    if (selfId <= 0 || peerUserId <= 0 || msgid <= 0)
        return false;

    //字段与服务端 messageBodyValue 一致：id 以字符串编码；单聊没有 @，mentions 为空数组
    Json::Value root;
    root["msgid"]       = std::to_string(msgid);
    root["clientmsgid"] = clientMsgId;
    root["ctype"]       = conversation_single;
    root["cid"]         = std::to_string(peerUserId);
    root["from"]        = std::to_string(selfId);
    root["to"]          = std::to_string(peerUserId);
    root["msgtype"]     = message_type_text;
    root["content"]     = buildAutoReplyContent(replyText);
    root["mentions"]    = Json::Value(Json::arrayValue);
    root["mentionall"]  = false;
    root["timestamp"]   = static_cast<Json::Int64>(timestamp);
    root["recalled"]    = false;

    out = MessageNotify();
    return fillMessageFromObject(root, out);
}

std::string MessageProtocolClient::buildSyncMessagesRequest(int ctype, int64_t cid,
                                                            int64_t beforeMsgId, int limit)
{
    Json::Value root;
    root["ctype"]       = ctype;
    //cid / beforemsgid 以字符串编码，避免大整数精度丢失
    root["cid"]         = std::to_string(cid);
    root["beforemsgid"] = std::to_string(beforeMsgId);
    root["limit"]       = limit;
    return writeCompact(root);
}

std::string MessageProtocolClient::buildSyncRewrittenRequest(int64_t since, int limit)
{
    Json::Value root;
    //since 以字符串编码，与服务端 getStringId 的契约一致
    root["since"] = std::to_string(since);
    root["limit"] = limit;
    return writeCompact(root);
}

std::string MessageProtocolClient::buildMarkReadRequest(int ctype, int64_t cid, int64_t readmsgid)
{
    Json::Value root;
    root["ctype"]     = ctype;
    //cid / readmsgid 以字符串编码，避免大整数精度丢失（与服务端 getStringId 契约一致）
    root["cid"]       = std::to_string(cid);
    root["readmsgid"] = std::to_string(readmsgid);
    return writeCompact(root);
}

std::string MessageProtocolClient::buildRecallMessageRequest(int ctype, int64_t cid, int64_t msgid)
{
    //服务端 parseRecallMessageRequest 对 cid / msgid <=0 直接判非法请求；这里提前挡住，
    //返回空串让调用方放弃发送，省一次必然失败的往返。
    if (cid <= 0 || msgid <= 0)
    {
        return std::string();
    }

    Json::Value root;
    root["ctype"] = ctype;
    //cid / msgid 以字符串编码，避免大整数精度丢失（与服务端 getStringId 契约一致）
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    return writeCompact(root);
}

bool MessageProtocolClient::parseRecallResponse(const std::string& jsonbody,
                                                RecallMessageResponse& resp)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //与其它应答同口径：没有整型 code 就认为报文不可用，调用方不得据此判成功。
    if (!root["code"].isIntegral())
        return false;

    resp.m_code = root["code"].asInt();
    resp.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    return true;
}

bool MessageProtocolClient::parseMessageRecalledNotify(const std::string& jsonbody,
                                                       MessageRecalledNotify& notify)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //ctype 缺省按单聊处理，与服务端 parseRecallMessageRequest 的容错口径一致。
    notify.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;

    //cid / msgid 是定位"哪个会话的哪条消息"的关键，缺失或非正则整条推送不可用。
    notify.m_cid = jsonToInt64(root["cid"]);
    if (notify.m_cid <= 0)
        return false;

    notify.m_msgid = jsonToInt64(root["msgid"]);
    if (notify.m_msgid <= 0)
        return false;

    //operator 用于区分"你撤回了"与"某某撤回了"；缺失时取 0，调用方按"他人撤回"退化处理。
    notify.m_operator = jsonToInt64(root["operator"]);
    return true;
}

std::string MessageProtocolClient::buildDeleteMessageRequest(int64_t msgid)
{
    //服务端对 msgid <=0 直接判非法请求；这里提前挡住，省一次必然失败的往返
    if (msgid <= 0)
    {
        return std::string();
    }

    Json::Value root;
    //以字符串编码，避免大整数精度丢失（与服务端 getStringId 契约一致）
    root["msgid"] = std::to_string(msgid);
    return writeCompact(root);
}

bool MessageProtocolClient::parseDeleteMessageResponse(const std::string& jsonbody,
                                                       DeleteMessageResponse& resp)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //与其它应答同口径：没有整型 code 就认为报文不可用，调用方不得据此判成功
    if (!root["code"].isIntegral())
        return false;

    resp.m_code = root["code"].asInt();
    resp.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    return true;
}

std::string MessageProtocolClient::buildDeleteMessagesByTimeRequest(int64_t begintime, int64_t endtime)
{
    //服务端对负数与"结束早于开始"判非法请求；这里提前挡住
    if (begintime < 0 || endtime < begintime)
    {
        return std::string();
    }

    Json::Value root;
    //时间与消息体的 timestamp 同一口径按数字传（毫秒值远小于 2^53，不会丢精度），
    //服务端只接受数字，不接受字符串
    root["begintime"] = static_cast<Json::Int64>(begintime);
    root["endtime"]   = static_cast<Json::Int64>(endtime);
    return writeCompact(root);
}

bool MessageProtocolClient::parseDeleteMessagesByTimeResponse(const std::string& jsonbody,
                                                              DeleteMessagesByTimeResponse& resp)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isIntegral())
        return false;

    resp.m_code = root["code"].asInt();
    resp.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //失败应答只有 code / msg，起止时间保持为 0；成功应答带回服务端截断后的实际区间
    resp.m_begintime = root["begintime"].isIntegral() ? root["begintime"].asInt64() : 0;
    resp.m_endtime   = root["endtime"].isIntegral() ? root["endtime"].asInt64() : 0;
    return true;
}

std::string MessageProtocolClient::buildClearConversationRequest(int ctype, int64_t cid)
{
    //服务端只接受单聊与群聊、会话ID为正；这里提前挡住，省一次必然失败的往返
    if ((ctype != conversation_single && ctype != conversation_group) || cid <= 0)
    {
        return std::string();
    }
    Json::Value root;
    root["ctype"] = ctype;
    //cid 以字符串编码，避免大整数精度丢失（与服务端 getStringId 契约一致）
    root["cid"] = std::to_string(cid);
    return writeCompact(root);
}

bool MessageProtocolClient::parseClearConversationResponse(const std::string& jsonbody,
                                                           ClearConversationResponse& resp)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //与其它应答同口径：没有整型 code 就认为报文不可用，调用方不得据此判成功
    if (!root["code"].isIntegral())
        return false;

    resp.m_code = root["code"].asInt();
    resp.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();
    //失败应答只有 code / msg，会话与锚点保持为 0
    resp.m_ctype        = root["ctype"].isInt() ? root["ctype"].asInt() : 0;
    resp.m_cid          = jsonToInt64(root["cid"]);
    resp.m_clearedMsgid = jsonToInt64(root["clearedmsgid"]);
    return true;
}

bool MessageProtocolClient::parseMessagesDeletedNotify(const std::string& jsonbody,
                                                       MessagesDeletedNotify& notify)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    notify.m_msgids.clear();
    notify.m_isClear = false;
    notify.m_clearedMsgid = 0;

    //带 clearedmsgid 的是清空形式（来自 cmd_clear_conversation）：会话与锚点都要合法，
    //清错会话无法挽回，宁可不删
    if (root.isMember("clearedmsgid"))
    {
        if (!root["ctype"].isInt())
            return false;
        const int ctype = root["ctype"].asInt();
        if (ctype != conversation_single && ctype != conversation_group)
            return false;
        const int64_t cid = jsonToInt64(root["cid"]);
        if (cid <= 0)
            return false;
        //锚点可以为 0（清空的是一个没有任何消息的会话），不能为负；jsonToInt64 解析失败时返回 0，
        //故先排除字符串以 '-' 开头的负值
        const Json::Value& anchorValue = root["clearedmsgid"];
        if (anchorValue.isString() && !anchorValue.asString().empty() && anchorValue.asString()[0] == '-')
            return false;
        if (anchorValue.isIntegral() && anchorValue.asInt64() < 0)
            return false;

        notify.m_isClear = true;
        notify.m_isRange = false;
        notify.m_ctype = ctype;
        notify.m_cid = cid;
        notify.m_clearedMsgid = jsonToInt64(anchorValue);
        return true;
    }

    //带 msgids 的是单条形式（来自 cmd_delete_message）
    if (root.isMember("msgids"))
    {
        notify.m_isRange = false;
        notify.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;
        notify.m_cid = jsonToInt64(root["cid"]);
        if (notify.m_cid <= 0)
            return false;

        const Json::Value& arr = root["msgids"];
        if (!arr.isArray() || arr.size() == 0)
            return false;
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            //任何一个 msgid 不合法都判整条推送不可用：删错一条消息无法挽回，宁可不删
            const int64_t msgid = jsonToInt64(arr[i]);
            if (msgid <= 0)
                return false;
            notify.m_msgids.push_back(msgid);
        }
        return true;
    }

    //否则按区间形式解析（来自 cmd_delete_messages_by_time），两个时间都必须是整数
    if (!root["begintime"].isIntegral() || !root["endtime"].isIntegral())
        return false;

    notify.m_isRange = true;
    notify.m_begintime = root["begintime"].asInt64();
    notify.m_endtime = root["endtime"].asInt64();
    if (notify.m_begintime < 0 || notify.m_endtime < notify.m_begintime)
        return false;
    return true;
}

bool MessageProtocolClient::parseMessageReadNotify(const std::string& jsonbody,
                                                   MessageReadNotify& notify)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //ctype 缺省按单聊处理，与撤回推送的容错口径一致；当前服务端只对单聊推送本条。
    notify.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;

    //cid 定位「哪个会话被读了」，readmsgid 说明「读到了哪条」，缺一不可。两者都以字符串
    //编码，jsonToInt64 按字符串解析，超过 2^53 也不丢精度。
    notify.m_cid = jsonToInt64(root["cid"]);
    if (notify.m_cid <= 0)
        return false;

    notify.m_readmsgid = jsonToInt64(root["readmsgid"]);
    if (notify.m_readmsgid <= 0)
        return false;

    //readcount 只是附带信息（单聊恒为 1），缺失时按 1 处理，不影响按位点标记已读。
    notify.m_readcount = root["readcount"].isInt() ? root["readcount"].asInt() : 1;
    return true;
}

std::string MessageProtocolClient::buildPinMessageRequest(int ctype, int64_t cid,
                                                          int64_t msgid, bool pin)
{
    //服务端 parsePinMessageRequest 对 cid / msgid <=0 直接判非法请求；这里提前挡住，
    //返回空串让调用方放弃发送，省一次必然失败的往返。
    if (cid <= 0 || msgid <= 0)
    {
        return std::string();
    }

    Json::Value root;
    root["ctype"] = ctype;
    //cid / msgid 以字符串编码，避免大整数精度丢失（与服务端 getStringId 契约一致）
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    root["pin"]   = pin ? 1 : 0;
    return writeCompact(root);
}

bool MessageProtocolClient::parsePinResponse(const std::string& jsonbody,
                                             PinMessageResponse& resp)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //与其它应答同口径：没有整型 code 就认为报文不可用，调用方不得据此判成功。
    if (!root["code"].isIntegral())
        return false;

    resp.m_code = root["code"].asInt();
    resp.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //服务端把这四项原样回带，供多条请求在途时对上号。老服务端不带也不算失败 ——
    //此时取默认值，调用方只能给一句笼统的失败提示，但不至于把整条应答丢掉。
    resp.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;
    resp.m_cid   = jsonToInt64(root["cid"]);
    resp.m_msgid = jsonToInt64(root["msgid"]);
    resp.m_pin   = root["pin"].isInt() ? root["pin"].asInt() : 0;
    return true;
}

bool MessageProtocolClient::parseCallCardNotify(const std::string& jsonbody, CallCardNotify& notify)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    //msgid / cid 定位「哪个会话的哪条消息」，content 是改写后的内容，三者缺一则整条推送不可用
    notify.m_msgid = jsonToInt64(root["msgid"]);
    notify.m_cid = jsonToInt64(root["cid"]);
    if (notify.m_msgid <= 0 || notify.m_cid <= 0 || !root["content"].isObject())
        return false;
    notify.m_sessionId = root["sessionid"].isString() ? root["sessionid"].asString() : std::string();
    notify.m_content = writeCompact(root["content"]);
    return true;
}

bool MessageProtocolClient::parseMessagePinnedNotify(const std::string& jsonbody,
                                                     MessagePinnedNotify& notify)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //ctype 缺省按单聊处理，与撤回推送的容错口径一致。
    notify.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;

    //cid / msgid 是定位"哪个会话的哪条消息"的关键，缺失或非正则整条推送不可用。
    notify.m_cid = jsonToInt64(root["cid"]);
    if (notify.m_cid <= 0)
        return false;

    notify.m_msgid = jsonToInt64(root["msgid"]);
    if (notify.m_msgid <= 0)
        return false;

    //pin 是这条推送的核心语义（钉住还是取消），缺失或越界则整条不可用 —— 缺省成
    //"取消"会把别人刚钉上的消息在本端悄悄抹掉，比丢弃这条推送糟得多。
    if (!root["pin"].isInt())
        return false;

    notify.m_pin = root["pin"].asInt();
    if (notify.m_pin != 0 && notify.m_pin != 1)
        return false;

    //operator / pintime 是附加信息，缺失时取 0，不影响本条推送可用。
    notify.m_operator = jsonToInt64(root["operator"]);
    notify.m_pinTime  = jsonToInt64(root["pintime"]);
    return true;
}

std::string MessageProtocolClient::buildGetPinnedMessagesRequest(int ctype, int64_t cid)
{
    if (cid <= 0)
    {
        return std::string();
    }

    Json::Value root;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    return writeCompact(root);
}

bool MessageProtocolClient::parseGetPinnedMessagesResponse(const std::string& jsonbody,
                                                           PinnedMessagesResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isIntegral())
        return false;

    out.m_code = root["code"].asInt();
    if (out.m_code != 0)
        return true;

    out.m_ctype = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;
    out.m_cid   = jsonToInt64(root["cid"]);
    out.m_total = jsonToInt64(root["total"]);

    out.m_items.clear();
    const Json::Value& arr = root["msgs"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& jm = arr[i];
            if (!jm.isObject())
                continue;

            PinnedMessageItem item;
            //解析失败（如缺 msgid/from 的脏数据）的条目跳过，不影响其余的补齐，
            //口径与 parseSyncMessagesResponse 一致。
            if (!fillMessageFromObject(jm, item.m_msg))
                continue;

            const Json::Value& jp = jm["pin"];
            if (jp.isObject())
            {
                item.m_operator = jsonToInt64(jp["operator"]);
                item.m_pinTime  = jsonToInt64(jp["pintime"]);
            }
            out.m_items.push_back(item);
        }
    }

    //老服务端不带 total 时按"返回多少就是多少"兜底，使调用方走全量对账那条路 ——
    //总比按 0 处理（会被当成截断、下界算不出来）稳妥。
    if (out.m_total < (int64_t)out.m_items.size())
    {
        out.m_total = (int64_t)out.m_items.size();
    }
    return true;
}

std::string MessageProtocolClient::buildForwardMessageRequest(const std::string& clientmsgid,
                                                              int toctype, int64_t tocid, bool merge,
                                                              const std::string& title,
                                                              const std::vector<int64_t>& msgids)
{
    //服务端对这几种情况一律判非法请求；提前挡住，省一次必然失败的往返
    if (clientmsgid.empty() || tocid <= 0 || msgids.empty() || msgids.size() > kForwardMaxMessages)
    {
        return std::string();
    }

    Json::Value root;
    root["clientmsgid"] = clientmsgid;
    root["toctype"]     = toctype;
    //tocid / msgid 以字符串编码，避免大整数精度丢失（与服务端 getStringId 契约一致）
    root["tocid"]       = std::to_string(tocid);
    root["merge"]       = merge;
    //标题只对合并转发有意义；为空时不写，由服务端取缺省值
    if (merge && !title.empty())
    {
        root["title"] = title;
    }

    Json::Value ids(Json::arrayValue);
    for (size_t i = 0; i < msgids.size(); ++i)
    {
        ids.append(std::to_string(msgids[i]));
    }
    root["msgids"] = ids;
    return writeCompact(root);
}

bool MessageProtocolClient::parseForwardMessageResponse(const std::string& jsonbody,
                                                        ForwardMessageResponse& resp)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //与其它应答同口径：没有整型 code 就认为报文不可用，调用方不得据此判成功
    if (!root["code"].isIntegral())
        return false;

    resp.m_code        = root["code"].asInt();
    resp.m_msg         = root["msg"].isString() ? root["msg"].asString() : std::string();
    resp.m_clientmsgid = root["clientmsgid"].isString() ? root["clientmsgid"].asString() : std::string();

    resp.m_msgids.clear();
    const Json::Value& ids = root["msgids"];
    if (ids.isArray())
    {
        for (Json::ArrayIndex i = 0; i < ids.size(); ++i)
        {
            const int64_t id = jsonToInt64(ids[i]);
            if (id > 0)
                resp.m_msgids.push_back(id);
        }
    }

    resp.m_messages.clear();
    const Json::Value& bodies = root["messages"];
    if (bodies.isArray())
    {
        for (Json::ArrayIndex i = 0; i < bodies.size(); ++i)
        {
            const Json::Value& jm = bodies[i];
            if (!jm.isObject())
                continue;

            //解析失败（如缺 msgid/from 的脏数据）的条目跳过，不影响其余
            MessageNotify m;
            if (!fillMessageFromObject(jm, m))
                continue;
            resp.m_messages.push_back(m);
        }
    }
    return true;
}

bool MessageProtocolClient::parseShareCardContent(const std::string& contentJson, ShareCardContent& out)
{
    out = ShareCardContent();
    Json::Value root;
    if (!parseObject(contentJson, root))
        return false;
    //64 位 ID 写成十进制字符串；也接受整数写法
    out.m_shareId = jsonToInt64(root["shareid"]);
    out.m_ownerId = jsonToInt64(root["ownerid"]);
    if (out.m_shareId <= 0 || out.m_ownerId <= 0 || !root["kind"].isInt() || !root["name"].isString())
        return false;
    out.m_kind = root["kind"].asInt();
    if (out.m_kind != share_kind_disk && out.m_kind != share_kind_folder && out.m_kind != share_kind_file)
        return false;
    out.m_name = root["name"].asString();
    out.m_displayPath = root["displaypath"].isString() ? root["displaypath"].asString() : std::string();
    //服务端按看的人合并的状态与权限：可能都不带（本地库里的旧内容、实时推送给分享者本人的那一份）
    if (root["sharestate"].isInt())
    {
        out.m_hasState = true;
        out.m_state = root["sharestate"].asInt();
        out.m_reason = root["sharereason"].isInt() ? root["sharereason"].asInt() : 0;
    }
    if (root["myperms"].isInt())
    {
        out.m_hasPerms = true;
        out.m_myPerms = root["myperms"].asInt();
    }
    return true;
}

bool MessageProtocolClient::parseScreenShareCardContent(const std::string& contentJson, ScreenShareCardContent& out)
{
    out = ScreenShareCardContent();
    Json::Value root;
    if (!parseObject(contentJson, root))
        return false;
    if (!root["roomid"].isString() || root["roomid"].asString().empty())
        return false;
    //64 位 ID 与时间写成十进制字符串；也接受整数写法
    out.m_hostId = jsonToInt64(root["host"]);
    if (out.m_hostId <= 0)
        return false;
    out.m_roomId = root["roomid"].asString();
    out.m_hostName = root["hostname"].isString() ? root["hostname"].asString() : std::string();
    out.m_startTime = jsonToInt64(root["starttime"]);
    //locked 为整数 0 / 1（第 12 期）；第 12 期之前的卡片没有这一项
    out.m_locked = root["locked"].isIntegral() && root["locked"].asInt() != 0;
    //forced 为整数 0 / 1（第 13 期）；第 13 期之前的卡片没有这一项
    out.m_forced = root["forced"].isIntegral() && root["forced"].asInt() != 0;
    //共享标题与描述（第 15 期）；第 15 期之前的卡片没有这两项
    out.m_title = root["title"].isString() ? root["title"].asString() : std::string();
    out.m_desc = root["desc"].isString() ? root["desc"].asString() : std::string();
    return true;
}

bool MessageProtocolClient::parseMergedContent(const std::string& contentJson, MergedRecord& out)
{
    out.m_title.clear();
    out.m_messages.clear();

    Json::Value root;
    if (!parseObject(contentJson, root))
        return false;

    const Json::Value& arr = root["messages"];
    if (!arr.isArray())
        return false;

    out.m_title = root["title"].isString() ? root["title"].asString() : std::string();
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& jm = arr[i];
        if (!jm.isObject())
            continue;

        MessageNotify m;
        if (!fillMessageFromObject(jm, m))
            continue;
        out.m_messages.push_back(m);
    }
    return true;
}

bool MessageProtocolClient::canRecallMessage(int64_t msgid, int64_t msgTimestampMs, int64_t nowMs)
{
    //服务器尚未回执、msgid 未回填：服务端还不认识这条消息，发过去必被判 message_not_exist。
    if (msgid <= 0)
    {
        return false;
    }

    //时间未知就无法与时限比较，保守判为不可撤回。
    if (msgTimestampMs <= 0)
    {
        return false;
    }

    //时钟回拨导致 now 早于消息时间时 elapsed 为负，不算超时。
    int64_t elapsed = nowMs - msgTimestampMs;
    if (elapsed > kRecallTimeLimitMs)
    {
        return false;
    }

    return true;
}

bool MessageProtocolClient::parseSyncRewrittenResponse(const std::string& jsonbody, SyncRewrittenResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    if (out.m_code != 0)
        return true;

    out.m_rev    = jsonToInt64(root["rev"]);
    out.m_more   = root["more"].isBool() ? root["more"].asBool() : false;
    out.m_currev = jsonToInt64(root["currev"]);

    out.m_items.clear();
    const Json::Value& arr = root["items"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& jm = arr[i];
            if (!jm.isObject())
                continue;
            RewrittenMessageItem item;
            //与 cmd_sync_messages 同口径解析；缺 msgid / from 的脏数据跳过，不影响其余条目
            if (!fillMessageFromObject(jm, item.m_msg))
                continue;
            item.m_rev = jsonToInt64(jm["rev"]);
            out.m_items.push_back(item);
        }
    }
    return true;
}

bool MessageProtocolClient::parseSyncMessagesResponse(const std::string& jsonbody, SyncMessagesResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    if (out.m_code != 0)
        return true;

    out.m_ctype   = root["ctype"].isInt() ? root["ctype"].asInt() : conversation_single;
    out.m_cid     = jsonToInt64(root["cid"]);
    out.m_hasmore = root["hasmore"].isBool() ? root["hasmore"].asBool() : false;
    //单聊时服务端带上对方的已读位点（字符串编码）；群聊不带，解析为 0。
    out.m_peerReadMsgid = jsonToInt64(root["peerreadmsgid"]);

    //翻历史模式的请求锚点（2026-09-30 起服务端原样回带，字符串编码）。旧服务端不带、或本页
    //来自补齐模式（回带的是 aftermsgid）时标记为没有锚点，调用方不据此计算本地覆盖区间
    out.m_hasAnchor = root.isMember("beforemsgid");
    out.m_beforeMsgid = out.m_hasAnchor ? jsonToInt64(root["beforemsgid"]) : 0;

    out.m_messages.clear();
    const Json::Value& arr = root["messages"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& jm = arr[i];
            if (!jm.isObject())
                continue;
            MessageNotify m;
            //解析失败（如缺 msgid/from 的脏数据）的条目跳过，不影响其余历史。
            if (fillMessageFromObject(jm, m))
                out.m_messages.push_back(m);
        }
    }
    return true;
}

} // namespace net
