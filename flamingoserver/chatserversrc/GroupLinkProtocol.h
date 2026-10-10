/**
 *  加群链接的记录结构、纯逻辑与报文编解码，GroupLinkProtocol.h（docs/邀请入群方案.md §12）。
 *
 *  链接形如 flamingo://joingroup?code=XXXXXXXXXXXX，code 是 12 位随机码（小写字母与数字，去掉容易看错的 i l o 0 1）。
 *  有效期 1 天、7 天或永久；只对生成者所在企业的同事有效；点了之后按群的加群方式直接入群或转为加群申请。
 *
 *  本模块只依赖 jsoncpp，不碰网络与数据库，见 tests/test_grouplink.cpp。存取在 GroupLinkManager，会话层在
 *  ChatSession（1340~1344）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

//随机码长度与字符表。字符表去掉了 i l o 0 1，人工抄写时不会认错
static const size_t kGroupLinkCodeLength = 12;
static const char* const kGroupLinkCodeAlphabet = "abcdefghjkmnpqrstuvwxyz23456789";

//加群链接的前缀；客户端识别与拼接都用这一份
static const char* const kGroupLinkUrlPrefix = "flamingo://joingroup?code=";

//有效期可选的天数：1 天、7 天、永久（0）
static const int kGroupLinkDaysOne = 1;
static const int kGroupLinkDaysSeven = 7;
static const int kGroupLinkDaysForever = 0;

//一天的毫秒数
static const int64_t kGroupLinkMillisPerDay = 24LL * 60 * 60 * 1000;

//一条加群链接，对应 t_group_link 的一行
struct GroupLinkRecord
{
    int64_t     id;           //链接ID
    std::string code;         //随机码
    int64_t     groupid;      //群ID
    int64_t     creator;      //生成者userid
    int64_t     enterpriseid; //生成者所属企业ID
    int64_t     createtime;   //生成时间，Unix毫秒
    int64_t     expiretime;   //到期时间，Unix毫秒；0 表示永久有效
    int         revoked;      //是否已作废：0 否 1 是
    std::string creatorname;  //生成者昵称（展示用，会话层补）

    GroupLinkRecord()
        : id(0), groupid(0), creator(0), enterpriseid(0), createtime(0), expiretime(0), revoked(0)
    {
    }
};

//点链接后查到的群的样子（cmd_get_group_link_info 应答）
struct GroupLinkInfo
{
    int64_t     groupid;     //群ID
    std::string name;        //群名
    int         facetype;    //群头像类型
    std::string customface;  //群自定义头像标识
    int         membercount; //群成员数
    int         joinmode;    //加群方式，取值见 group_join_mode
    bool        member;      //请求者是否已是群成员

    GroupLinkInfo()
        : groupid(0), facetype(0), membercount(0), joinmode(0), member(false)
    {
    }
};

//加群链接的纯逻辑与报文编解码工具类；全部为静态方法，不可实例化
class GroupLinkProtocol
{
public:
    /**
     *  用一串随机字节生成随机码：每个字节对字符表长度取模挑一个字符，取前 kGroupLinkCodeLength 个。
     *  @param bytes 随机字节，至少 kGroupLinkCodeLength 个；不够时返回空串
     */
    static std::string makeCodeFromBytes(const std::vector<unsigned char>& bytes);

    /**
     *  生成一个新的随机码（系统随机源）。
     */
    static std::string makeRandomCode();

    /**
     *  随机码是否合法：长度为 kGroupLinkCodeLength，且只含字符表里的字符。
     */
    static bool isValidCode(const std::string& code);

    /**
     *  有效期天数是否为可选的三种之一（1、7、0）。
     */
    static bool isValidDays(int days);

    /**
     *  按有效期算到期时间：永久为 0，否则为 nowMs 加上相应天数。
     */
    static int64_t computeExpire(int64_t nowMs, int days);

    /**
     *  链接此刻是否可用：没有作废，并且永久有效或还没到期。
     */
    static bool isUsable(const GroupLinkRecord& rec, int64_t nowMs);

    /**
     *  拼出完整链接：kGroupLinkUrlPrefix + code。
     */
    static std::string buildUrl(const std::string& code);

    /**
     *  解析 cmd_create_group_link 请求 { groupid:"群ID", validdays }。validdays 缺省按 7。
     *  @return groupid 为正、validdays 是可选的三种之一时返回 true
     */
    static bool parseCreateRequest(const std::string& json, int64_t& groupid, int& days);

    /**
     *  解析 cmd_revoke_group_link 请求 { groupid:"群ID", id:"链接ID" }。
     *  @return 两个 ID 都为正时返回 true
     */
    static bool parseRevokeRequest(const std::string& json, int64_t& groupid, int64_t& id);

    /**
     *  解析带随机码的请求 { code }（cmd_get_group_link_info、cmd_join_group_by_link）。随机码转成小写再校验。
     *  @return 随机码合法时返回 true
     */
    static bool parseCodeRequest(const std::string& json, std::string& code);

    /**
     *  构造 cmd_create_group_link 应答：{ code, msg, link:{...} }；失败时 rec 传 NULL，不带 link。
     */
    static std::string buildCreateResponse(int code, const std::string& msg, const GroupLinkRecord* rec);

    /**
     *  构造 cmd_get_group_links 应答：{ code, msg, groupid, links:[...] }。
     */
    static std::string buildListResponse(int code, const std::string& msg, int64_t groupid,
                                         const std::vector<GroupLinkRecord>& links);

    /**
     *  构造 cmd_get_group_link_info 应答：{ code, msg, groupid, name, facetype, customface, membercount, joinmode, member }；
     *  失败时 info 传 NULL，只带 code 与 msg。
     */
    static std::string buildInfoResponse(int code, const std::string& msg, const GroupLinkInfo* info);

    /**
     *  构造 cmd_join_group_by_link 应答：{ code, msg, groupid, joined }。
     */
    static std::string buildJoinResponse(int code, const std::string& msg, int64_t groupid, bool joined);

private:
    GroupLinkProtocol() = delete;
    ~GroupLinkProtocol() = delete;
    GroupLinkProtocol(const GroupLinkProtocol&) = delete;
    GroupLinkProtocol& operator=(const GroupLinkProtocol&) = delete;
};
