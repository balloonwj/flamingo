/**
 *  v2 群互动卡片报文编解码实现，CardProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "CardProtocol.h"
#include "../jsoncpp1.9.0/json.h"
#include "Protocol.h"   //卡片消息类型常量 message_type_poll / _relay / _todo，错误码 error_code_*

//UTF-8 续字节的判定：高两位为 10 的字节是多字节字符的后续字节，不单独构成一个字符。
//取值与 MessageProtocol.cpp 里的同名常量相同（那两个常量是该文件私有的）。
constexpr unsigned char UTF8_CONTINUATION_MASK = 0xC0;
constexpr unsigned char UTF8_CONTINUATION_BITS = 0x80;

//全角空格 U+3000 的 UTF-8 编码及其字节数。中文输入法下容易在接龙文本首尾带上它，
//与半角空格一样在规范化时去掉
static const char* const FULLWIDTH_SPACE_UTF8 = "\xE3\x80\x80";
constexpr size_t FULLWIDTH_SPACE_UTF8_LEN = 3;

//判断单个字节是否为可在首尾去掉的半角空白：空格、制表符、回车、换行
static bool isAsciiBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

//text 从 pos 开始若是一个首尾可去掉的空白字符，返回它的字节数，否则返回 0
static size_t blankLengthAt(const std::string& text, size_t pos)
{
    if (isAsciiBlank(text[pos]))
        return 1;
    if (text.compare(pos, FULLWIDTH_SPACE_UTF8_LEN, FULLWIDTH_SPACE_UTF8) == 0)
        return FULLWIDTH_SPACE_UTF8_LEN;
    return 0;
}

//text 在区间 [begin, end) 内、紧挨 end 之前若是一个首尾可去掉的空白字符，返回它的字节数，
//否则返回 0。调用方保证 begin < end
static size_t blankLengthBefore(const std::string& text, size_t begin, size_t end)
{
    if (isAsciiBlank(text[end - 1]))
        return 1;
    if (end - begin >= FULLWIDTH_SPACE_UTF8_LEN
        && text.compare(end - FULLWIDTH_SPACE_UTF8_LEN, FULLWIDTH_SPACE_UTF8_LEN,
                        FULLWIDTH_SPACE_UTF8) == 0)
        return FULLWIDTH_SPACE_UTF8_LEN;
    return 0;
}

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 JSON 里的字符串型 64 位 ID 取出；字段缺失或非字符串返回 0
static int64_t getStringId(const Json::Value& root, const char* key)
{
    if (!root[key].isString())
        return 0;
    return static_cast<int64_t>(atoll(root[key].asString().c_str()));
}

//把一段原始JSON文本解析回 Json::Value；空串或解析失败返回 null 值
static Json::Value rawToValue(const std::string& raw)
{
    if (raw.empty())
        return Json::Value(Json::nullValue);

    Json::Value v;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(raw.c_str(), raw.c_str() + raw.size(), &v, &errs);
    delete reader;
    if (!ok || !errs.empty())
        return Json::Value(Json::nullValue);
    return v;
}

//聚合投票互动行：votes[选项下标] = 票数，totalcount = 去重投票人数
static void aggregatePoll(const std::vector<CardInteractionRecord>& rows,
                          Json::Value& votes, int& totalcount)
{
    votes = Json::Value(Json::objectValue);
    //每行即一名投票人（UNIQUE(msgid,userid) 保证）
    totalcount = static_cast<int>(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        //row.data 形如 [0,2]：本投票人所投的选项下标
        Json::Value chosen = rawToValue(rows[i].data);
        if (!chosen.isArray())
            continue;
        for (Json::ArrayIndex j = 0; j < chosen.size(); ++j)
        {
            if (!chosen[j].isInt())
                continue;
            std::string key = std::to_string(chosen[j].asInt());
            int prev = votes[key].isInt() ? votes[key].asInt() : 0;
            votes[key] = prev + 1;
        }
    }
}

//聚合接龙互动行：items = [{userid,text}]，按互动时间（行顺序）排列
static Json::Value aggregateRelay(const std::vector<CardInteractionRecord>& rows)
{
    Json::Value items(Json::arrayValue);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        Json::Value jo;
        jo["userid"] = std::to_string(rows[i].userid);
        jo["text"]   = rows[i].data;
        items.append(jo);
    }
    return items;
}

//聚合待办互动行：doneusers = [已勾选完成的userid]
static Json::Value aggregateTodo(const std::vector<CardInteractionRecord>& rows)
{
    Json::Value users(Json::arrayValue);
    for (size_t i = 0; i < rows.size(); ++i)
        users.append(std::to_string(rows[i].userid));
    return users;
}

bool CardProtocol::parseVotePollRequest(const std::string& json, VotePollRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    //options 必填且非空：所投选项下标列表
    const Json::Value& arr = root["options"];
    if (!arr.isArray() || arr.size() == 0)
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i].isInt())
            req.options.push_back(arr[i].asInt());
    }
    if (req.options.empty())
        return false;
    return true;
}

bool CardProtocol::parseJoinRelayRequest(const std::string& json, JoinRelayRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    //text 必填且非空
    if (!root["text"].isString())
        return false;
    req.text = root["text"].asString();
    if (req.text.empty())
        return false;
    return true;
}

int CardProtocol::checkCardTarget(int msgtype, bool recalled, int expectedMsgtype)
{
    //撤回先于类型判断：卡片撤回后对客户端来说已不存在，类型是否相符已无意义
    if (recalled)
        return error_code_message_not_exist;
    if (msgtype != expectedMsgtype)
        return error_code_bad_request;
    return error_code_ok;
}

//去掉 raw 首尾的空白（半角空格、制表符、回车、换行、全角空格 U+3000），返回剩下的部分；
//全是空白时返回空串。接龙文本、卡片标题与说明共用这一套规则
static std::string trimRelayBlank(const std::string& raw)
{
    //去掉开头的空白
    size_t begin = 0;
    while (begin < raw.size())
    {
        const size_t blank = blankLengthAt(raw, begin);
        if (blank == 0)
            break;
        begin += blank;
    }

    //去掉结尾的空白
    size_t end = raw.size();
    while (end > begin)
    {
        const size_t blank = blankLengthBefore(raw, begin, end);
        if (blank == 0)
            break;
        end -= blank;
    }
    return raw.substr(begin, end - begin);
}

//判断文本中间是否含回车或换行。接龙文本与卡片标题都只允许一行
static bool containsLineBreak(const std::string& text)
{
    return text.find_first_of("\r\n") != std::string::npos;
}

int CardProtocol::normalizeRelayText(const std::string& raw, std::string& out)
{
    const std::string trimmed = trimRelayBlank(raw);

    //只有空白字符
    if (trimmed.empty())
        return error_code_bad_request;

    //每条接龙只有一行：中间的回车、换行一律拒绝，不替换成空格（2026-10-02 与用户确认）
    if (containsLineBreak(trimmed))
        return error_code_bad_request;

    if (utf8CharCount(trimmed) > RELAY_TEXT_MAX_CHARS)
        return error_code_msg_too_large;

    out = trimmed;
    return error_code_ok;
}

int CardProtocol::normalizeRelayContent(const std::string& content, int64_t nowMs, std::string& out)
{
    Json::Value root;
    if (!parseObject(content, root))
        return error_code_bad_request;

    //标题：必填的单行文本（标题中间的换行一律拒绝，2026-10-02 与用户确认）
    if (!root["title"].isString())
        return error_code_bad_request;
    const std::string title = trimRelayBlank(root["title"].asString());
    if (title.empty() || containsLineBreak(title))
        return error_code_bad_request;
    if (utf8CharCount(title) > RELAY_TITLE_MAX_CHARS)
        return error_code_msg_too_large;

    //说明：选填的多行文本，缺省或 null 视为空串；中间的换行保留
    std::string note;
    const Json::Value& noteValue = root["note"];
    if (!noteValue.isNull())
    {
        if (!noteValue.isString())
            return error_code_bad_request;
        note = trimRelayBlank(noteValue.asString());
        if (utf8CharCount(note) > RELAY_NOTE_MAX_CHARS)
            return error_code_msg_too_large;
    }

    //截止时间：Unix 毫秒整数。只接受 JSON 整数：字符串与小数一律拒绝；超出 int64 范围的无符号
    //整数也拒绝，否则 asInt64 会抛异常
    int64_t deadline = 0;
    const Json::Value& deadlineValue = root["deadline"];
    if (!deadlineValue.isNull())
    {
        const Json::ValueType type = deadlineValue.type();
        if ((type != Json::intValue && type != Json::uintValue) || !deadlineValue.isInt64())
            return error_code_bad_request;
        deadline = static_cast<int64_t>(deadlineValue.asInt64());
        if (deadline < 0)
            return error_code_bad_request;
    }
    //2026-10-03 起必须设置截止时间（缺省、null、0 都算没有设置），且不能早于当前时间 + 5 分钟：
    //太近的截止时间发出去，群里的人还来不及看到就截止了
    if (deadline == 0 || deadline < nowMs + RELAY_DEADLINE_MIN_LEAD_MS)
        return error_code_relay_deadline_invalid;

    //只写回这三个字段，客户端自带的其它字段（items、state 等）由此全部丢弃
    Json::Value normalized(Json::objectValue);
    normalized["title"]    = title;
    normalized["note"]     = note;
    normalized["deadline"] = static_cast<Json::Int64>(deadline);
    out = writeCompact(normalized);
    return error_code_ok;
}

bool CardProtocol::canForwardSeparately(int msgtype)
{
    return msgtype != message_type_relay;
}

bool CardProtocol::parseRelayTargetRequest(const std::string& json, RelayTargetRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;
    return true;
}

std::string CardProtocol::buildRelayOpResponse(int code, const std::string& msg,
                                               int ctype, int64_t cid, int64_t msgid)
{
    Json::Value root;
    root["code"]  = code;
    root["msg"]   = msg;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    return writeCompact(root);
}

int64_t CardProtocol::cardConversationIdFor(int ctype, int64_t from, int64_t to, int64_t viewerId)
{
    //群卡片：会话ID就是群id，对所有成员相同
    if (ctype == conversation_group)
        return to;
    //单聊卡片：会话ID是"对方"的userid
    return (viewerId == from) ? to : from;
}

RelayStatus CardProtocol::computeRelayStatus(const CardStateRecord& stateRow, int64_t deadline, int64_t nowMs)
{
    RelayStatus status;

    //手动结束优先：即使截止时间也已经过了，仍按"已提前结束"显示，并带上是谁结束的
    if (stateRow.closed)
    {
        status.state     = relay_state_closed;
        status.closedby  = stateRow.closer;
        status.closetime = stateRow.closetime;
        return status;
    }

    //截止时间已到：等于截止时间即算截止
    if (deadline > 0 && nowMs >= deadline)
    {
        status.state     = relay_state_expired;
        status.closetime = deadline;
        return status;
    }

    status.state = relay_state_open;
    return status;
}

int64_t CardProtocol::getRelayDeadline(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root))
        return 0;

    const Json::Value& v = root["deadline"];
    const Json::ValueType type = v.type();
    if ((type != Json::intValue && type != Json::uintValue) || !v.isInt64())
        return 0;
    const int64_t deadline = static_cast<int64_t>(v.asInt64());
    return (deadline > 0) ? deadline : 0;
}

bool CardProtocol::canCloseRelay(bool isGroupCard, int64_t creatorId, int64_t operatorId, int operatorRole)
{
    //发起人总可以结束自己发起的接龙
    if (operatorId == creatorId)
        return true;
    //单聊卡片没有群角色，只有发起人可以
    if (!isGroupCard)
        return false;
    return operatorRole == group_role_owner || operatorRole == group_role_admin;
}

bool CardProtocol::shouldAugmentCard(int msgtype, bool recalled)
{
    //已撤回的卡片：客户端只显示撤回提示，不需要互动状态，也不该把名单再发出去
    if (recalled)
        return false;
    return msgtype == message_type_poll || msgtype == message_type_relay || msgtype == message_type_todo;
}

int CardProtocol::classifyRelayJoin(const std::vector<CardInteractionRecord>& rows, int64_t userid,
                                    const std::string& text)
{
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].userid != userid)
            continue;
        //内容完全相同视为重复提交（如双击），2026-10-02 与用户确认不写库、不推送、不发提示
        return (rows[i].data == text) ? relay_join_unchanged : relay_join_modify;
    }
    return relay_join_new;
}

std::string CardProtocol::buildRelayTip(int kind, const std::string& nickname, const std::string& title,
                                        size_t count)
{
    //操作者：昵称加书名号；昵称为空时写成「有成员」，不留一对空的书名号
    const std::string who = nickname.empty() ? std::string(u8"有成员")
                                             : (std::string(u8"「") + nickname + u8"」");
    //接龙：带标题时写成 接龙「标题」，标题为空（只有旧数据会出现）时只写 接龙
    const std::string card = title.empty() ? std::string(u8"接龙")
                                           : (std::string(u8"接龙「") + title + u8"」");

    switch (kind)
    {
    //首次参与
    case relay_tip_join:
        return who + u8"参与了" + card;

    //修改本人那条
    case relay_tip_modify:
        return who + u8"修改了在" + card + u8"中的内容";

    //撤回本人那条
    case relay_tip_withdraw:
        return who + u8"撤回了在" + card + u8"中的内容";

    //提前结束：带上结束时的参与人数
    case relay_tip_close:
        return who + u8"结束了" + card + u8"，共 " + std::to_string(count) + u8" 人参与";

    //未知种类：调用方传错了，给出不含具体动作的兜底文字，不让提示变成空行
    default:
        return who + u8"更新了" + card;
    }
}

std::string CardProtocol::getRelayTitle(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root))
        return std::string();
    if (!root["title"].isString())
        return std::string();
    return root["title"].asString();
}

const char* CardProtocol::relayTipKindName(int kind)
{
    switch (kind)
    {
    //首次参与
    case relay_tip_join:
        return kSystemTipRelayJoin;

    //修改本人那条
    case relay_tip_modify:
        return kSystemTipRelayModify;

    //撤回本人那条
    case relay_tip_withdraw:
        return kSystemTipRelayWithdraw;

    //提前结束
    case relay_tip_close:
        return kSystemTipRelayClose;

    //未知种类：与 buildRelayTip 的「更新了接龙」兜底对应
    default:
        return kSystemTipRelayUpdate;
    }
}

std::string CardProtocol::buildRelayTipContent(int kind, const std::string& nickname, const std::string& title,
                                               size_t count, int64_t cardMsgId)
{
    Json::Value args(Json::objectValue);
    if (!nickname.empty())
        args["nickname"] = nickname;
    if (!title.empty())
        args["title"] = title;
    if (kind == relay_tip_close)
        args["count"] = static_cast<Json::UInt64>(count);

    Json::Value root;
    root["text"]      = buildRelayTip(kind, nickname, title, count);
    root["cardmsgid"] = std::to_string(cardMsgId);
    root["tipkind"]   = relayTipKindName(kind);
    root["tipargs"]   = args;
    return writeCompact(root);
}

std::string CardProtocol::buildRelayTipClientMsgId(int kind, int64_t cardMsgId, int64_t userid, int64_t nowMs)
{
    return "sys-relay-" + std::to_string(kind) + "-" + std::to_string(cardMsgId) + "-"
         + std::to_string(userid) + "-" + std::to_string(nowMs);
}

int CardProtocol::checkRelayJoinCapacity(bool alreadyJoined, size_t currentCount)
{
    //已在名单里的人是修改，不占新名额
    if (alreadyJoined)
        return error_code_ok;
    if (currentCount >= RELAY_MAX_PARTICIPANTS)
        return error_code_card_full;
    return error_code_ok;
}

size_t CardProtocol::utf8CharCount(const std::string& text)
{
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if ((b & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_BITS)
            ++chars;
    }
    return chars;
}

bool CardProtocol::parseUpdateTodoRequest(const std::string& json, UpdateTodoRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    //done 必填：1 勾选完成 0 取消勾选
    if (!root["done"].isInt())
        return false;
    req.done = root["done"].asInt();
    return true;
}

std::string CardProtocol::buildVoteData(const std::vector<int>& options)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < options.size(); ++i)
        arr.append(options[i]);
    return writeCompact(arr);
}

bool CardProtocol::getPollMeta(const std::string& content, int& optioncount,
                               int& multi, int64_t& expiretime)
{
    optioncount = 0;
    multi       = 0;
    expiretime  = 0;

    Json::Value root;
    if (!parseObject(content, root))
        return false;

    //options 数组个数即选项数
    if (!root["options"].isArray())
        return false;
    optioncount = static_cast<int>(root["options"].size());

    multi      = (root["multi"].isBool() && root["multi"].asBool()) ? 1 : 0;
    expiretime = root["expiretime"].isIntegral()
               ? static_cast<int64_t>(root["expiretime"].asInt64()) : 0;
    return true;
}

bool CardProtocol::isTodoAssignee(const std::string& content, int64_t userid)
{
    Json::Value root;
    if (!parseObject(content, root))
        return false;

    const Json::Value& arr = root["assignees"];
    if (!arr.isArray())
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i].isString() &&
            static_cast<int64_t>(atoll(arr[i].asString().c_str())) == userid)
        {
            return true;
        }
    }
    return false;
}

std::string CardProtocol::buildPollChangedNotify(int64_t msgid,
                                                 const std::vector<CardInteractionRecord>& rows)
{
    Json::Value votes;
    int totalcount = 0;
    aggregatePoll(rows, votes, totalcount);

    Json::Value root;
    root["msgid"]      = std::to_string(msgid);
    root["votes"]      = votes;
    root["totalcount"] = totalcount;
    return writeCompact(root);
}

//把接龙状态的三个字段写进 JSON 对象：推送与下发卡片内容共用，保证两处字段名与编码一致
static void writeRelayStatus(Json::Value& root, const RelayStatus& status)
{
    root["state"]     = status.state;
    root["closedby"]  = std::to_string(status.closedby);
    root["closetime"] = static_cast<Json::Int64>(status.closetime);
}

std::string CardProtocol::buildRelayChangedNotify(int ctype, int64_t cid, int64_t msgid,
                                                  const std::vector<CardInteractionRecord>& rows,
                                                  const RelayStatus& status)
{
    Json::Value root;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    root["items"] = aggregateRelay(rows);
    writeRelayStatus(root, status);
    return writeCompact(root);
}

std::string CardProtocol::augmentRelayContent(const std::string& content,
                                              const std::vector<CardInteractionRecord>& rows,
                                              const RelayStatus& status)
{
    Json::Value root;
    //content 不是 JSON 对象时原样返回，不做合并
    if (!parseObject(content, root))
        return content;

    //名单以 t_card_interaction 为准，覆盖 content 里可能残留的旧 items
    root["items"] = aggregateRelay(rows);
    writeRelayStatus(root, status);
    return writeCompact(root);
}

std::string CardProtocol::buildTodoChangedNotify(int64_t msgid,
                                                 const std::vector<CardInteractionRecord>& rows)
{
    Json::Value root;
    root["msgid"]     = std::to_string(msgid);
    root["doneusers"] = aggregateTodo(rows);
    return writeCompact(root);
}

std::string CardProtocol::augmentCardContent(int msgtype, const std::string& content,
                                             const std::vector<CardInteractionRecord>& rows)
{
    Json::Value root;
    //content 非法（非对象）则原样返回，不做合并
    if (!parseObject(content, root))
        return content;

    if (msgtype == message_type_poll)
    {
        Json::Value votes;
        int totalcount = 0;
        aggregatePoll(rows, votes, totalcount);
        root["votes"]      = votes;
        root["totalcount"] = totalcount;
    }
    else if (msgtype == message_type_relay)
    {
        root["items"] = aggregateRelay(rows);
    }
    else if (msgtype == message_type_todo)
    {
        root["doneusers"] = aggregateTodo(rows);
    }
    else
    {
        //非卡片消息：原样返回
        return content;
    }
    return writeCompact(root);
}

std::string CardProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}
