/**
 *  v2 群互动卡片报文编解码（客户端侧），CardProtocolClient.h
 *
 *  目前只覆盖群接龙：拼装发接龙卡片用的 content（cmd_send_message=1503、msgtype=11，外层
 *  信封由 MessageProtocolClient::buildSendCardRequest 套上），拼装参与接龙（1603）、撤回本人
 *  那条（1607）、提前结束（1608）三条请求，解析这三条请求的应答与接龙变更推送（1604）。
 *  投票、待办两种卡片接入时再在本模块里补上。
 *
 *  本模块与服务端 chatserversrc/CardProtocol 互为镜像——服务端解析请求、构造应答与推送，
 *  客户端构造请求、解析应答与推送，两侧的字段名与编码口径必须逐字对应：64 位 ID 以十进制
 *  字符串编码进 JSON，避免按 IEEE754 双精度解析时丢精度；时间以 JSON 64 位整数（Unix 毫秒）
 *  编码。发送前的校验（去首尾空白、拒绝中间换行、按 UTF-8 码点计长度）与服务端规则相同，
 *  用于在界面上提前提示，最终以服务端的判定为准。
 *
 *  本模块为纯逻辑，只依赖 jsoncpp 与标准库，不触及 socket、界面与日志，便于单元测试，
 *  见 flamingoclient/Source/Tests/CardProtocolClientTests.cpp（Suite CPC）。
 *
 *  典型用法（发起一张接龙）：
 *      std::string content;
 *      if (net::CardProtocolClient::buildRelayCardContent(title, note, deadline, content) == 0)
 *          CIUSocket::GetInstance().SendRelayCard(conversation_group, groupId, clientMsgId, content);
 *
 *  协议契约见 docs/协议设计-v2.md §10.22，整体方案见 docs/群接龙方案.md。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//接龙卡片标题的长度上限，单位为字符（UTF-8 码点）。与服务端 CardProtocol.h 的
//RELAY_TITLE_MAX_CHARS 取值相同（docs/群接龙方案.md §4.8），超长整条拒绝、不截断
const size_t kRelayTitleMaxChars = 50;

//接龙卡片说明的长度上限，单位为字符（UTF-8 码点）。与服务端 RELAY_NOTE_MAX_CHARS 相同
const size_t kRelayNoteMaxChars = 200;

//一条接龙文本的长度上限，单位为字符（UTF-8 码点）。与服务端 RELAY_TEXT_MAX_CHARS 相同
const size_t kRelayTextMaxChars = 100;

//一张接龙最多接受多少人参与。与服务端 RELAY_MAX_PARTICIPANTS 相同；满员后服务端对新人回
//error_code_card_full（509），客户端据此在卡片与详情窗上提示已满
const size_t kRelayMaxParticipants = 200;

//发起接龙时截止时间至少要比当前时间晚多少，单位毫秒。与服务端 CardProtocol.h 的
//RELAY_DEADLINE_MIN_LEAD_MS 相同（2026-10-03 起必须设置截止时间）；服务端按它的时钟再查一次，
//不满足时回 error_code_relay_deadline_invalid（510）
const int64_t kRelayDeadlineMinLeadMs = 5LL * 60LL * 1000LL;

//接龙名单里的一条
struct RelayItem
{
    int64_t     m_userid;   //参与者 userid
    std::string m_text;     //该参与者的接龙文本（UTF-8，单行，最多 kRelayTextMaxChars 个字符）

    RelayItem()
        : m_userid(0)
    {
    }
};

//接龙变更推送（cmd_relay_changed_notify=1604）的解析结果。参与、修改、撤回本人那条、提前
//结束之后，服务端向卡片所在会话的全体成员推送，操作者本人也会收到；推送里带的是完整名单
struct RelayChangedNotify
{
    int                    m_ctype;      //卡片所在会话的类型：见 enum conversation_type，单聊=1、群聊=2
    int64_t                m_cid;        //收件方视角的会话ID：群聊=群 id，单聊=对端 userid
    int64_t                m_msgid;      //接龙卡片所在消息的服务器 msgid（恒 >0）
    std::vector<RelayItem> m_items;      //完整名单，按名次排列；格式不对的条目已被跳过
    int                    m_state;      //接龙状态，取值见 enum relay_state；推送缺该字段时按进行中（0）
    int64_t                m_closedBy;   //提前结束的操作者 userid；未被提前结束时为 0
    int64_t                m_closeTime;  //结束时间，Unix 毫秒：提前结束时为操作时间，已截止时为截止时间，进行中为 0

    RelayChangedNotify()
        : m_ctype(0)
        , m_cid(0)
        , m_msgid(0)
        , m_state(0)
        , m_closedBy(0)
        , m_closeTime(0)
    {
    }
};

//一张接龙卡片的完整内容：卡片 content 的解析结果，也是本地库与气泡里保存的那一份。
//同步、Pin 列表、搜索下发的卡片带全部七个字段（服务端合并了名单与状态）；实时推送的新卡片
//只有 title / note / deadline，此时名单为空、状态为进行中。收到 cmd_relay_changed_notify 后
//由 balloonchatwnd::ApplyRelayNotify 用推送替换名单与状态
struct RelayCardContent
{
    std::string            m_title;      //标题（UTF-8，单行）；只有旧数据才可能为空
    std::string            m_note;       //说明（UTF-8，可多行），可以为空
    int64_t                m_deadline;   //截止时间，Unix 毫秒；0 表示不限
    std::vector<RelayItem> m_items;      //名单，按名次排列；格式不对的条目已被跳过
    int                    m_state;      //服务端给出的接龙状态，取值见 enum relay_state；缺省为进行中（0）
    int64_t                m_closedBy;   //提前结束的操作者 userid；未被提前结束时为 0
    int64_t                m_closeTime;  //结束时间，Unix 毫秒；提前结束为操作时间，已截止为截止时间，进行中为 0

    RelayCardContent()
        : m_deadline(0)
        , m_state(0)
        , m_closedBy(0)
        , m_closeTime(0)
    {
    }
};

//参与接龙（1603）、撤回本人那条（1607）、提前结束（1608）三条请求共用的应答解析结果。
//请求解析成功时服务端无论成败都原样带回请求里的 ctype / cid / msgid，客户端据 msgid 把结果
//送到对应卡片的详情窗；请求格式错误时服务端只回 { code, msg }，此时三个定位字段为 0
struct RelayOpResponse
{
    int         m_code;   //错误码，0 表示成功，取值见 enum error_code
    std::string m_msg;    //服务端给出的英文原因，只用于日志，不直接显示给用户
    int         m_ctype;  //请求里的会话类型；应答不带时为 0
    int64_t     m_cid;    //请求里的会话ID；应答不带时为 0
    int64_t     m_msgid;  //请求里的卡片消息 msgid；应答不带时为 0

    RelayOpResponse()
        : m_code(0)
        , m_ctype(0)
        , m_cid(0)
        , m_msgid(0)
    {
    }
};

//群互动卡片报文编解码工具类；全部为静态方法，不可实例化
class CardProtocolClient
{
public:
    /**
     *  校验并拼装发接龙卡片用的 content：{ title, note, deadline }。规则与服务端
     *  CardProtocol::normalizeRelayContent 相同：标题去首尾空白后不能为空、中间不能含换行、
     *  最多 kRelayTitleMaxChars 个字符；说明去首尾空白、可多行、最多 kRelayNoteMaxChars 个字符；
     *  截止时间不能为负。截止时间是否晚于当前时间由服务端按它的时钟判断，客户端不检查。
     *  @param title    标题（UTF-8）
     *  @param note     说明（UTF-8），可以为空
     *  @param deadline 截止时间，Unix 毫秒；0 表示不限
     *  @param out      [出参] 紧凑 JSON 文本；只在返回 0 时写入
     *  @return 0 表示合法；error_code_bad_request 表示标题为空或含换行、截止时间为负；
     *          error_code_msg_too_large 表示标题或说明超长
     */
    static int buildRelayCardContent(const std::string& title, const std::string& note,
                                     int64_t deadline, std::string& out);

    /**
     *  规范化一条接龙文本，规则与服务端 CardProtocol::normalizeRelayText 相同：去首尾空白
     *  （半角空格、制表符、回车、换行、全角空格 U+3000），去完后不能为空、中间不能含换行、
     *  最多 kRelayTextMaxChars 个字符。详情窗提交前调用，用于提前提示。
     *  @param raw 用户输入的文本（UTF-8）
     *  @param out [出参] 去掉首尾空白后的文本；只在返回 0 时写入
     *  @return 0 表示合法；error_code_bad_request 表示为空或含换行；error_code_msg_too_large 表示超长
     */
    static int normalizeRelayText(const std::string& raw, std::string& out);

    /**
     *  统计 UTF-8 文本的字符数（码点数），口径与服务端 CardProtocol::utf8CharCount 相同。
     *  @param text UTF-8 文本，可以为空
     *  @return 字符数；空串返回 0
     */
    static size_t utf8CharCount(const std::string& text);

    /**
     *  构造参与接龙的请求 JSON 包体（cmd_join_relay=1603，紧凑 UTF-8）。
     *  @param ctype 卡片所在会话的类型（见 enum conversation_type）
     *  @param cid   会话ID（群聊=群 id，单聊=对端 userid）；须 >0
     *  @param msgid 接龙卡片所在消息的服务器 msgid；须 >0
     *  @param text  接龙文本（UTF-8，建议先经 normalizeRelayText）；不能为空
     *  @return 紧凑 JSON 文本；cid、msgid 非正或 text 为空时返回<u>空串</u>，调用方据此放弃发送
     */
    static std::string buildJoinRelayRequest(int ctype, int64_t cid, int64_t msgid, const std::string& text);

    /**
     *  构造只指明一张卡片的请求 JSON 包体 { ctype, cid, msgid }，撤回本人那条
     *  （cmd_withdraw_relay=1607）与提前结束（cmd_close_relay=1608）共用。
     *  @param ctype 卡片所在会话的类型
     *  @param cid   会话ID；须 >0
     *  @param msgid 接龙卡片所在消息的服务器 msgid；须 >0
     *  @return 紧凑 JSON 文本；cid 或 msgid 非正时返回<u>空串</u>
     */
    static std::string buildRelayTargetRequest(int ctype, int64_t cid, int64_t msgid);

    /**
     *  解析参与接龙、撤回本人那条、提前结束三条请求的应答 JSON 包体。
     *  只要 JSON 合法且含整型 code 即返回 true；ctype / cid / msgid 缺失时取 0。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；返回 true 时各字段有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseRelayOpResponse(const std::string& jsonbody, RelayOpResponse& out);

    /**
     *  解析接龙变更推送 JSON 包体（cmd_relay_changed_notify=1604）。
     *  名单里格式不对的个别条目（userid 缺失或非正、text 不是字符串）跳过，其余照常返回
     *  （2026-10-02 与用户确认）；state / closedby / closetime 缺失时按进行中、0、0 处理。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；返回 true 时各字段有效
     *  @return 解析成功返回 true；JSON 非法、cid 或 msgid 缺失或非正、items 不是数组时返回 false
     */
    static bool parseRelayChangedNotify(const std::string& jsonbody, RelayChangedNotify& out);

    /**
     *  解析接龙卡片的 content（msgtype=11 消息的 content 字段）。名单里格式不对的个别条目
     *  跳过；items / state / closedby / closetime / deadline / note 缺失时取缺省值
     *  （实时推送的新卡片只带 title / note / deadline）。
     *  @param contentJson 卡片 content 的 JSON 文本
     *  @param out         [出参] 解析结果；返回 true 时各字段有效
     *  @return content 是 JSON 对象返回 true；否则返回 false
     */
    static bool parseRelayCardContent(const std::string& contentJson, RelayCardContent& out);

    /**
     *  把一张接龙卡片的内容写回 content JSON，字段与服务端下发的合并结果同构
     *  （title / note / deadline / items / state / closedby / closetime，64 位 ID 按字符串编码）。
     *  收到推送合并之后用它更新本地库与气泡里保存的 content，使下次从本地库还原时拿到最新名单。
     *  @param card 卡片内容
     *  @return 紧凑 JSON 文本
     */
    static std::string serializeRelayCardContent(const RelayCardContent& card);

private:
    CardProtocolClient();
    ~CardProtocolClient();
    CardProtocolClient(const CardProtocolClient&);
    CardProtocolClient& operator=(const CardProtocolClient&);
};

} // namespace net
