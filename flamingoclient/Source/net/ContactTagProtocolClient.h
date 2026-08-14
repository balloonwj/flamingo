/**
 *  ContactTagProtocolClient.h
 *
 *  v2「联系人标签」报文（cmd_set_contact_tags=1217）的客户端侧编解码。
 *
 *  标签是本端登录用户给某个联系人贴的短词（形如"设计""外包"），在联系人资料对话框
 *  里成排显示。与备注名一样是<u>单向</u>的：只影响本人这一侧的显示，对方看不到也不
 *  受影响，同一个联系人在不同人的通讯录里标签可以完全不同。
 *
 *  请求带的是某个联系人在本人视角下的<u>完整</u>标签列表，服务端整份覆盖，空数组表示
 *  清空 —— 增、改、删共用这一条命令，客户端不必自己算差异，也就没有"两个增量请求
 *  乱序到达导致状态错乱"这一类问题。
 *
 *  应答回带 userid 与服务端<u>归一后</u>的最终列表（失败时回带服务端当前实际存着的
 *  列表）。客户端据它把界面刷成与服务端一致的样子：服务端会去掉首尾空白、丢掉空串、
 *  不区分大小写去重，本端乐观更新算出来的那一份未必与它逐字相同，直接采信回带的
 *  列表比自己猜服务端怎么处理的可靠。
 *
 *  读取标签不另设命令：标签随 cmd_get_contacts 应答的每个 contacts[] 元素一起下发
 *  （见 ContactsProtocolClient.h 的 ContactItem::m_tags）。
 *
 *  本模块是纯逻辑 —— 只依赖 jsoncpp，不触及 socket 与界面，便于被 FlamingoUnit
 *  测试工程直接断言（见 Tests/ContactTagProtocolClientTests.cpp）。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//cmd_set_contact_tags 应答解析结果。
struct SetContactTagsResult
{
    int         m_code;     //结果码，0 成功；其余取值见 Protocol.h 的 enum error_code
                            //（条数超上限为 error_code_tag_limit=211；单条超长与报文
                            //  格式错一律为 error_code_bad_request=3）
    std::string m_msg;      //结果描述（UTF-8），可为空串
    int64_t     m_userid;   //本次操作的联系人 userid，服务端原样回带

    //服务端侧该联系人当前的标签列表（UTF-8）。成功时是本次提交归一后的最终结果，
    //失败时是服务端当前实际存着的那一份 —— 两种情形下都可以直接拿它覆盖本地缓存，
    //调用方不必区分是"对齐"还是"回滚"。
    std::vector<std::string> m_tags;

    SetContactTagsResult()
        : m_code(-1)        //-1：尚未解析出有效结果码
        , m_userid(0)
    {
    }
};

//「联系人标签」报文编解码工具类；全部为静态方法，不可实例化。
class ContactTagProtocolClient
{
public:
    /**
     *  构造"设置联系人标签"请求 JSON 包体（cmd_set_contact_tags=1217）。
     *
     *    userid： 被贴标签的联系人 userid（须 > 0，且不能是自己 —— 服务端会拒绝
     *             给自己贴标签，口径与特别关注一致）。
     *    tags：   该联系人在本人视角下的<u>完整</u>标签列表（UTF-8）。传空数组表示
     *             清空该联系人的全部标签，这是合法请求。
     *
     *  @return 紧凑格式的请求 JSON 文本，形如
     *          {"userid":"5081","tags":["设计","外包"]}。
     *          userid 以字符串编码，避免大整数在 JSON 里丢精度（与本协议其余
     *          64 位 ID 同一口径）。
     *
     *  本方法<u>不校验</u>入参合法性，也<u>不做归一</u>（不 trim、不去重、不判长度）：
     *  校验是服务端的事，归一由服务端做完再随应答回带。传了非法值照样生成 JSON，
     *  服务端会以对应错误码拒绝。
     */
    static std::string buildSetContactTagsRequest(int64_t userid,
                                                  const std::vector<std::string>& tags);

    /**
     *  解析 cmd_set_contact_tags 应答包体。
     *
     *    jsonbody： 应答包体（UTF-8 JSON 文本）。
     *    out：      [出参] 解析结果，仅返回 true 时有效。调用前无需清空，
     *               本方法内部会先把 out 复位。
     *
     *  @return true 解析成功（<u>不代表操作成功</u> —— 操作是否成功看 out.m_code）；
     *          false 表示 JSON 非法、顶层不是对象、或缺少 code 字段。
     *
     *  userid 缺失时置 0：老服务端可能不回带它。调用方拿到 0 就无从判断这条应答
     *  对应哪一次请求，此时宁可不回滚，也不要退错人的标签。
     *  tags 缺失时为空数组，调用方须自行判断该不该拿它去覆盖本地缓存。
     */
    static bool parseSetContactTagsResponse(const std::string& jsonbody,
                                            SetContactTagsResult& out);

private:
    ContactTagProtocolClient();
    ~ContactTagProtocolClient();
    ContactTagProtocolClient(const ContactTagProtocolClient&);
    ContactTagProtocolClient& operator=(const ContactTagProtocolClient&);
};

} // namespace net
