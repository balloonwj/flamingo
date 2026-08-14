/**
 *  v2 通讯录报文（cmd_get_contacts）的编解码：把部门树、部门成员、联系人
 *  资料池、服务号、星标好友、群组列表组装成应答 JSON。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_contactsprotocol.cpp）。会话层 ChatSession::onGetContacts
 *  负责从 DB / 各 Manager 取数填充 ContactsData，再调用本模块生成 JSON。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "FavoriteProtocol.h"   //FavoriteItem（收藏项 = ctype + cid）

//部门成员归属记录：描述某用户在某部门内的身份，不含用户资料本身
struct DeptMemberRef
{
    int64_t     userid;       //成员用户ID
    std::string position;     //该成员在本部门的职位/职务
    int         isprimary;    //是否主部门：1 主部门，0 兼属部门
    int         memberorder;  //成员在本部门内的排序权重，值小者靠前
};

//部门信息：部门树的一个节点
struct DepartmentInfo
{
    int64_t     deptid;        //部门ID
    std::string name;          //部门名称
    int64_t     parentdeptid;  //父部门ID，0 表示顶级部门
    int         deptorder;     //同级部门排序权重，值小者靠前
    int64_t     leaderuserid;  //部门负责人用户ID，0 表示未指定
    std::vector<DeptMemberRef> members;  //本部门的直属成员归属记录
};

//联系人资料：去重的用户资料池中的一项（含部门成员与服务号）
struct ContactInfo
{
    int64_t     userid;       //用户ID
    std::string username;     //账号
    std::string nickname;     //昵称
    int         usertype;     //用户类型：0 普通用户 1 服务号
    int         facetype;     //头像类型
    std::string customface;   //自定义头像文件标识
    int         gender;       //性别
    int         birthday;     //生日，形如 19891208
    std::string signature;    //个性签名
    std::string address;      //所在地
    std::string phonenumber;  //电话
    std::string mail;         //邮箱
    int         presence;     //在线状态，取值见 enum presence_type
    int         clienttype;   //在线设备类型，取值见 enum platform_type
    //本人给该联系人起的备注名（cmd_set_friend_remark 设的那个），空串表示未设置。
    //备注名是单向的——只影响"本人看该联系人"的显示，故它随每个登录用户各自的
    //通讯录下发，而不是联系人自身的资料。非好友（通讯录里的同事不一定是好友）
    //没有好友关系行，取不到备注，一律给空串。
    std::string remark;
    //本人给该联系人贴的标签（cmd_set_contact_tags 设的那些），按用户当初提交的顺序排列，
    //没贴过则为空。标签与备注名一样是单向的——只影响"本人看该联系人"的显示，故它随
    //每个登录用户各自的通讯录下发，而不是联系人自身的资料。
    std::vector<std::string> tags;
};

//群组信息：仅群列表项，不含群成员明细
struct GroupInfo
{
    int64_t     groupid;      //群ID
    std::string name;         //群名称
    int         facetype;     //群头像类型
    std::string customface;   //群自定义头像文件标识
    int         membercount;  //群成员人数
    int64_t     ownerid;      //群主用户ID
};

//cmd_get_contacts 应答所需的全部数据
struct ContactsData
{
    int64_t     enterpriseid;    //登录用户所属企业ID
    std::string enterprisename;  //企业名称，作为通讯录树根显示
    int64_t     synctime;        //服务器当前时间，Unix 毫秒时间戳（UTC）
    std::vector<DepartmentInfo> departments;     //企业部门（扁平数组，靠 parentdeptid 还原成树）
    std::vector<ContactInfo>    contacts;        //去重的联系人资料池
    std::vector<FavoriteItem>   favorites;       //「我的收藏」列表，含联系人与群组两类，按收藏时间倒序
    std::vector<int64_t>        starred;         //「特别关注」的联系人userid列表，按标记时间倒序。只装人（群没有上下线，标记它没意义），故不带 ctype；客户端据它决定这些人上下线时弹不弹右下角提示
    std::vector<int64_t>        serviceaccounts; //服务号的用户ID列表
    std::vector<GroupInfo>      groups;          //登录用户所属的群组列表
};

//通讯录报文编解码工具类；全部为静态方法，不可实例化
class ContactsProtocol
{
public:
    /**
     *  构造 cmd_get_contacts 成功应答 JSON（code=0）。
     *  64 位 ID（userid/deptid/groupid 等）一律以字符串编码进 JSON。
     *  @param data 通讯录全量数据
     *  @return 紧凑格式的应答 JSON 文本
     */
    static std::string buildContactsResponse(const ContactsData& data);

private:
    ContactsProtocol() = delete;
    ~ContactsProtocol() = delete;
    ContactsProtocol(const ContactsProtocol&) = delete;
    ContactsProtocol& operator=(const ContactsProtocol&) = delete;
};
