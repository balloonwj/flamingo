/**
 *  加群链接的报文编解码（客户端侧）：cmd_create_group_link(1340) / cmd_get_group_links(1341) /
 *  cmd_revoke_group_link(1342) / cmd_get_group_link_info(1343) / cmd_join_group_by_link(1344)，
 *  以及链接文本 flamingo://joingroup?code=xxx 的识别与解析。
 *
 *  与服务端 chatserversrc/GroupLinkProtocol 互为镜像。纯逻辑，只依赖 jsoncpp，不触及网络与 UI，
 *  单测见 Source/Tests/GroupLinkTests.cpp。方案见 docs/邀请入群方案.md §12。
 *
 *  典型用法：
 *      std::string code;
 *      if (net::GroupLinkProtocolClient::parseJoinUrl(L"flamingo://joingroup?code=abc...", code))
 *          CIUSocket::GetInstance().RequestGroupLinkInfo(code);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//链接有效期的三个档位（天）。0 表示永久有效
static const int kGroupLinkDaysOne = 1;
static const int kGroupLinkDaysSeven = 7;
static const int kGroupLinkDaysForever = 0;

//随机码长度，与服务端 kGroupLinkCodeLength 一致
static const size_t kGroupLinkCodeLen = 12;

//一条加群链接（1340 应答的 link、1341 应答 links 数组的一项）
struct GroupLinkItem
{
    int64_t     m_id;           //链接ID
    std::string m_code;         //随机码
    std::string m_url;          //完整链接文本 flamingo://joingroup?code=...
    int64_t     m_groupid;      //所属群ID
    int64_t     m_creator;      //生成者 userid
    std::string m_creatorname;  //生成者昵称（UTF-8）
    int64_t     m_createtime;   //生成时间，Unix 毫秒
    int64_t     m_expiretime;   //到期时间，Unix 毫秒；0 表示永久有效

    GroupLinkItem()
        : m_id(0), m_groupid(0), m_creator(0), m_createtime(0), m_expiretime(0)
    {
    }
};

//生成链接（cmd_create_group_link=1340）的应答
struct CreateGroupLinkResult
{
    int           m_code;     //结果码，0 成功；6 群禁止加入、302 不是群成员
    std::string   m_msg;      //结果描述
    bool          m_hasLink;  //成功时为 true，m_link 有效
    GroupLinkItem m_link;     //新生成的链接

    CreateGroupLinkResult()
        : m_code(-1), m_hasLink(false)
    {
    }
};

//列出链接（cmd_get_group_links=1341）的应答
struct GroupLinksResult
{
    int                        m_code;     //结果码，0 成功
    std::string                m_msg;      //结果描述
    int64_t                    m_groupid;  //群ID
    std::vector<GroupLinkItem> m_links;    //仍可用的链接，按生成时间倒序

    GroupLinksResult()
        : m_code(-1), m_groupid(0)
    {
    }
};

//按随机码查群（cmd_get_group_link_info=1343）的应答
struct GroupLinkInfoResult
{
    int         m_code;         //结果码，0 成功；316 链接无效、317 不是同一企业
    std::string m_msg;          //结果描述
    int64_t     m_groupid;      //群ID
    std::string m_name;         //群名（UTF-8）
    int         m_facetype;     //群头像类型
    std::string m_customface;   //群头像标识
    int         m_membercount;  //成员数
    int         m_joinmode;     //加入方式：0 自由加入 1 需审批 2 禁止加入
    bool        m_member;       //本人是否已在群里

    GroupLinkInfoResult()
        : m_code(-1), m_groupid(0), m_facetype(0), m_membercount(0), m_joinmode(0), m_member(false)
    {
    }
};

//凭链接加群（cmd_join_group_by_link=1344）的应答
struct JoinGroupByLinkResult
{
    int         m_code;     //结果码，0 成功；316 / 317 链接不可用，6 群禁止加入
    std::string m_msg;      //结果描述
    int64_t     m_groupid;  //群ID；链接无效时为 0
    bool        m_joined;   //true 已在群里（含本来就在），false 已提交加群申请、等待审批

    JoinGroupByLinkResult()
        : m_code(-1), m_groupid(0), m_joined(false)
    {
    }
};

//加群链接报文的编解码，全部为静态函数
class GroupLinkProtocolClient
{
public:
    /**
     *  构造生成链接的请求（1340）：{ "groupid":"...", "validdays": N }。
     *  @param days 有效期天数：kGroupLinkDaysOne / kGroupLinkDaysSeven / kGroupLinkDaysForever
     */
    static std::string buildCreateRequest(int64_t groupid, int days);

    /**
     *  构造作废链接的请求（1342）：{ groupid, id }。
     */
    static std::string buildRevokeRequest(int64_t groupid, int64_t id);

    /**
     *  构造按随机码查询或加群的请求（1343 / 1344）：{ "code":"..." }。
     */
    static std::string buildCodeRequest(const std::string& code);

    /**
     *  解析生成链接的应答（1340）。成功时要求 link 子对象带正的 id 与非空的 code。
     *  @return JSON 合法且带 code 返回 true
     */
    static bool parseCreateResponse(const std::string& jsonbody, CreateGroupLinkResult& out);

    /**
     *  解析列出链接的应答（1341）。id 不为正的条目跳过。
     *  @return JSON 合法且带 code 返回 true
     */
    static bool parseListResponse(const std::string& jsonbody, GroupLinksResult& out);

    /**
     *  解析按随机码查群的应答（1343）。应答里没有随机码，发起方须自己记住请求时的随机码。
     *  @return JSON 合法且带 code 返回 true；成功应答还要求 groupid 为正
     */
    static bool parseInfoResponse(const std::string& jsonbody, GroupLinkInfoResult& out);

    /**
     *  解析凭链接加群的应答（1344）。
     *  @return JSON 合法且带 code 返回 true
     */
    static bool parseJoinResponse(const std::string& jsonbody, JoinGroupByLinkResult& out);

    /**
     *  从链接文本里取出随机码。接受 flamingo://joingroup?code=xxx 与 flamingo://joingroup/?code=xxx，
     *  协议名与主机名不区分大小写，code 之后的其它参数（&...）与首尾空白、引号忽略；随机码转成小写。
     *  @param url  链接文本（可来自命令行，带引号也可）
     *  @param code [出参] 随机码；只含字母与数字，长度为 kGroupLinkCodeLen
     *  @return 是加群链接且随机码合法返回 true
     */
    static bool parseJoinUrl(const std::wstring& url, std::string& code);

    /**
     *  文本是否以加群链接的前缀开头（不区分大小写），用于在命令行参数里认出它。
     */
    static bool isJoinUrl(const std::wstring& text);

private:
    GroupLinkProtocolClient();
};

} // namespace net
