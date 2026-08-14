/**
 *  v2 通讯录报文编解码（客户端侧）：解析 cmd_get_contacts(1201) 应答 JSON。
 *
 *  本模块与服务端 chatserversrc/ContactsProtocol 互为镜像——服务端构造应答，
 *  客户端解析应答。本模块是纯逻辑，只依赖 jsoncpp，不触及网络与 UI，便于
 *  单元测试，见 flamingoclient/Source/Tests/ContactsProtocolTests.cpp。
 *
 *  本轮仅取"联系人资料池"（contacts 数组）用于让联系人面板真实化、支撑单聊
 *  发起；部门树、群组、星标、服务号分区等暂不解析（留作后续轮次）。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//联系人资料池中的一项（部门成员/服务号去重后的用户资料）。
struct ContactItem
{
    int64_t     m_userid;      //用户ID
    std::string m_username;    //账号
    std::string m_nickname;    //昵称（UTF-8）
    int         m_usertype;    //用户类型：0 普通用户 1 服务号
    int         m_facetype;    //头像类型（见 BalloonMain/AvatarSpec.h AvatarKind：0 自动/1 字形/2 自定义图）
    std::string m_customface;  //头像标识（UTF-8）：字形头像为 "glyph|colorIdx"，自定义图为 fileid；空表示自动
    std::string m_signature;   //个性签名（UTF-8）
    std::string m_address;     //所在地区（UTF-8），搜索副行"账号 · 地区"与地区匹配用
    int         m_presence;    //在线状态，取值见 enum presence_type
    //本端登录用户给该联系人起的备注名（UTF-8），空串表示未设置。经
    //cmd_set_friend_remark(1209) 设置，随本应答读回。备注名是单向的——同一个
    //联系人在不同人的通讯录里备注不同，故它属于"谁在看"而非联系人自身的资料；
    //通讯录里的同事不一定是好友，非好友一律为空串。显示联系人名时按"有备注用
    //备注、否则用昵称"取名，统一走 BalloonMain/ContactDisplayName.h。
    std::string m_remark;

    //本端登录用户给该联系人贴的标签（UTF-8），未贴过则为空数组。经
    //cmd_set_contact_tags(1217) 整份设置，随本应答读回。与备注名同为单向数据——
    //只影响"本人看该联系人"的显示，对方看不到也不受影响，故同一个联系人在不同人的
    //通讯录里标签可以完全不同。顺序即服务端存的显示顺序，界面照此顺序排胶囊。
    std::vector<std::string> m_tags;

    ContactItem()
        : m_userid(0)
        , m_usertype(0)
        , m_facetype(0)
        , m_presence(0)
    {
    }
};

//部门成员归属记录：某用户在某部门内的身份（资料本身在 ContactItem 池里）。
struct DeptMemberItem
{
    int64_t     m_userid;       //成员用户ID
    std::string m_position;     //该成员在本部门的职务（UTF-8），如"设计组组长"
    int         m_isprimary;    //是否主部门：1 主部门，0 兼属部门
    int         m_memberorder;  //成员在本部门内的排序权重，值小者靠前

    DeptMemberItem()
        : m_userid(0)
        , m_isprimary(0)
        , m_memberorder(0)
    {
    }
};

//部门：组织架构树的一个节点（扁平存储，靠 m_parentdeptid 还原成树）。
struct DepartmentItem
{
    int64_t     m_deptid;        //部门ID
    std::string m_name;          //部门名称（UTF-8）
    int64_t     m_parentdeptid;  //父部门ID，0 表示顶级部门
    int         m_deptorder;     //同级部门排序权重，值小者靠前
    int64_t     m_leaderuserid;  //部门负责人用户ID，0 表示未指定
    std::vector<DeptMemberItem> m_members;  //本部门的直属成员

    DepartmentItem()
        : m_deptid(0)
        , m_parentdeptid(0)
        , m_deptorder(0)
        , m_leaderuserid(0)
    {
    }
};

//一条「我的收藏」记录：收藏对象既可以是联系人也可以是群，两者用 (ctype, cid) 区分。
//与服务端 FavoriteProtocol.h 的 FavoriteItem 逐字段对应。
struct FavoriteItem
{
    int     m_ctype;  //收藏对象类型，取值见 Protocol.h 的 conversation_type：1 联系人（含服务号）、2 群组
    int64_t m_cid;    //收藏对象ID：联系人为对端userid，群组为群id

    FavoriteItem()
        : m_ctype(0)
        , m_cid(0)
    {
    }
};

//cmd_get_contacts 应答解析结果（企业名 + 部门树 + 联系人资料池 + 我的收藏）。
struct ContactsResult
{
    int                         m_code;            //结果码，0 成功
    std::string                 m_msg;             //结果描述
    int64_t                     m_enterpriseid;    //登录用户所属企业ID
    std::string                 m_enterprisename;  //企业名称（作通讯录树根显示）
    std::vector<DepartmentItem> m_departments;     //企业部门（扁平数组，按 m_parentdeptid 还原成树）
    std::vector<ContactItem>    m_contacts;        //去重的联系人资料池
    std::vector<FavoriteItem>   m_favorites;       //「我的收藏」，联系人与群混排，按收藏时间倒序（最近收藏的在前）
    std::vector<int64_t>        m_starred;         //「特别关注」的联系人 userid，按标记时间倒序（最近标记的在前）。只装人、不带 ctype —— 群没有上下线，标记它没有意义。客户端据它决定这些人上下线时弹不弹右下角提示

    ContactsResult()
        : m_code(-1)        //-1：尚未解析出有效结果码
        , m_enterpriseid(0)
    {
    }
};

//cmd_presence_changed_notify(1109) 推送解析结果：某联系人在线状态发生变更。
//服务端 ProfileProtocol::buildPresenceChangedNotify 产出 {userid(字符串), presence, clienttype}。
struct PresenceChangedNotify
{
    int64_t m_userid;      //状态发生变更的联系人用户ID
    int     m_presence;    //新的在线状态，取值见 enum presence_type
    int     m_clienttype;  //该联系人变更状态时所用客户端类型（见 platform_type；本端暂只用于日志）

    PresenceChangedNotify()
        : m_userid(0)
        , m_presence(0)
        , m_clienttype(0)
    {
    }
};

//cmd_user_info_changed_notify(1108) 推送解析结果：某联系人资料（头像 / 昵称等）发生变更。
//服务端 ProfileProtocol::buildUserInfoChangedNotify 产出 {userid(字符串), profile:{...}}，
//本端关心其中的头像字段（facetype/customface）与昵称，用于实时刷新联系人 / 会话头像。
struct UserInfoChangedNotify
{
    int64_t     m_userid;      //资料发生变更的联系人用户ID
    int         m_facetype;    //新头像类型（见 BalloonMain/AvatarSpec.h AvatarKind）
    std::string m_customface;  //新头像标识（UTF-8）
    std::string m_nickname;    //新昵称（UTF-8；可空，调用方据需更新显示名）
    std::string m_signature;   //新个性签名（UTF-8；可空，调用方据需更新好友资料窗 / 单聊窗签名）

    UserInfoChangedNotify()
        : m_userid(0)
        , m_facetype(0)
    {
    }
};

//组织架构变更的类型，与服务端 dept::org_change_type 一一对应（见
//flamingoserver/chatserversrc/DepartmentMsg.h）。本端目前两种都只是"重新拉一次
//通讯录"，区分它主要是为了打日志时能看出变的是部门还是人。
enum org_change_type
{
    org_change_department = 1,  //部门结构变更：管理台新建 / 改名 / 删除 / 移动了部门
    org_change_member     = 2   //成员归属变更：有人被加进部门（含新用户注册）、改岗、移出或换部门
};

//cmd_org_structure_notify(1404) 推送解析结果：本企业的组织架构发生了变更。
//服务端 dept::BuildOrgStructureNotify 产出 {changetype, deptid}，两个都是数字。
//本端收到后重新拉一次通讯录（cmd_get_contacts），因此这两个字段只用于日志与
//将来做增量刷新时的判断依据，当前不参与刷新逻辑。
struct OrgStructureChangedNotify
{
    int     m_changetype;  //变更类型，取值见 enum org_change_type
    int64_t m_deptid;      //受影响的部门ID；0 表示服务端未指明具体部门（如员工被整个删除），
                           //此时仍应整体刷新通讯录

    OrgStructureChangedNotify()
        : m_changetype(0)
        , m_deptid(0)
    {
    }
};

//通讯录报文编解码工具类；全部为静态方法，不可实例化。
class ContactsProtocolClient
{
public:
    /**
     *  解析 cmd_get_contacts 应答 JSON 包体。
     *  JSON 合法且含整型 code 即返回 true；code==0 时 m_enterprise* 与
     *  m_contacts 有效。JSON 非法或缺 code 返回 false。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseContactsResponse(const std::string& jsonbody, ContactsResult& out);

    /**
     *  解析 cmd_presence_changed_notify(1109) 推送 JSON 包体。
     *  userid 在 v2 协议里是字符串（与登录应答一致），本函数转回 int64；presence/clienttype
     *  为整型。要求 userid 与 presence 两字段齐备且类型正确才算有效。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；返回 true 时字段有效
     *  @return JSON 合法且含 userid(字符串可转整数)+presence(整型) 返回 true，否则 false
     */
    static bool parsePresenceChangedNotify(const std::string& jsonbody, PresenceChangedNotify& out);

    /**
     *  解析 cmd_user_info_changed_notify(1108) 推送 JSON 包体 {userid, profile:{...}}。
     *  取 userid（字符串编码 64 位）与 profile 内的 facetype / customface / nickname。
     *  要求 userid 有效（>0）才算成功；profile 缺字段时对应项取默认（facetype=0、空串）。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；返回 true 时字段有效
     *  @return JSON 合法且 userid>0 返回 true，否则 false
     */
    static bool parseUserInfoChangedNotify(const std::string& jsonbody, UserInfoChangedNotify& out);

    /**
     *  解析 cmd_org_structure_notify(1404) 推送 JSON 包体 {changetype, deptid}。
     *  changetype 必须是整型且为 org_change_type 里的取值，否则视为无效推送——
     *  收到不认识的变更类型时宁可不刷，也好过为一条读不懂的报文触发一次全量拉取。
     *  deptid 允许缺失或为 0：服务端删员工那条路径就传 0（该员工可能横跨多个部门，
     *  没有单一的"受影响部门"），此时仍算有效推送，由调用方整体刷新通讯录。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；返回 true 时字段有效
     *  @return JSON 合法且 changetype 为已知取值返回 true，否则 false
     */
    static bool parseOrgStructureChangedNotify(const std::string& jsonbody, OrgStructureChangedNotify& out);

private:
    ContactsProtocolClient() = delete;
    ~ContactsProtocolClient() = delete;
    ContactsProtocolClient(const ContactsProtocolClient&) = delete;
    ContactsProtocolClient& operator=(const ContactsProtocolClient&) = delete;
};

} // namespace net
