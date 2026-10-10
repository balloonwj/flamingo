/**
 *  v2 通讯录报文编解码（客户端侧）实现，ContactsProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-01
 */
#include "ContactsProtocolClient.h"
#include "Protocol.h"                   // conversation_single / conversation_group（收藏项 ctype 取值）
#include <stdlib.h>                     // strtoll
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0。
//v2 约定 64 位 ID（userid/enterpriseid 等）以字符串编码进 JSON，避免精度丢失。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

bool ContactsProtocolClient::parseContactsResponse(const std::string& jsonbody, ContactsResult& out)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //code 为必有字段，缺失或非整型视为非法应答
    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //失败应答到此为止，企业信息与联系人不再解析
    if (out.m_code != 0)
        return true;

    //企业信息：通讯录树根
    const Json::Value& ent = root["enterprise"];
    if (ent.isObject())
    {
        out.m_enterpriseid   = jsonToInt64(ent["enterpriseid"]);
        out.m_enterprisename = ent["name"].isString() ? ent["name"].asString() : std::string();
    }

    //部门：扁平数组，每个部门含直属成员归属记录
    out.m_departments.clear();
    const Json::Value& departments = root["departments"];
    if (departments.isArray())
    {
        for (Json::ArrayIndex i = 0; i < departments.size(); ++i)
        {
            const Json::Value& d = departments[i];
            if (!d.isObject())
                continue;

            DepartmentItem dept;
            dept.m_deptid       = jsonToInt64(d["deptid"]);
            dept.m_name         = d["name"].isString() ? d["name"].asString() : std::string();
            dept.m_parentdeptid = jsonToInt64(d["parentdeptid"]);
            dept.m_deptorder    = d["deptorder"].isInt() ? d["deptorder"].asInt() : 0;
            dept.m_leaderuserid = jsonToInt64(d["leaderuserid"]);
            //部门ID 非法的条目跳过
            if (dept.m_deptid <= 0)
                continue;

            const Json::Value& members = d["members"];
            if (members.isArray())
            {
                for (Json::ArrayIndex k = 0; k < members.size(); ++k)
                {
                    const Json::Value& m = members[k];
                    if (!m.isObject())
                        continue;
                    DeptMemberItem mem;
                    mem.m_userid      = jsonToInt64(m["userid"]);
                    mem.m_position    = m["position"].isString() ? m["position"].asString() : std::string();
                    mem.m_isprimary   = m["isprimary"].isInt()   ? m["isprimary"].asInt()   : 0;
                    mem.m_memberorder = m["memberorder"].isInt() ? m["memberorder"].asInt() : 0;
                    if (mem.m_userid <= 0)
                        continue;
                    dept.m_members.push_back(mem);
                }
            }
            out.m_departments.push_back(dept);
        }
    }

    //联系人资料池
    out.m_contacts.clear();
    const Json::Value& contacts = root["contacts"];
    if (contacts.isArray())
    {
        for (Json::ArrayIndex i = 0; i < contacts.size(); ++i)
        {
            const Json::Value& c = contacts[i];
            if (!c.isObject())
                continue;

            ContactItem item;
            item.m_userid    = jsonToInt64(c["userid"]);
            item.m_username  = c["username"].isString()  ? c["username"].asString()  : std::string();
            item.m_nickname  = c["nickname"].isString()  ? c["nickname"].asString()  : std::string();
            item.m_usertype  = c["usertype"].isInt()     ? c["usertype"].asInt()     : 0;
            item.m_facetype  = c["facetype"].isInt()     ? c["facetype"].asInt()     : 0;
            item.m_customface = c["customface"].isString() ? c["customface"].asString() : std::string();
            item.m_signature = c["signature"].isString() ? c["signature"].asString() : std::string();
            item.m_address   = c["address"].isString()   ? c["address"].asString()   : std::string();
            item.m_gender      = c["gender"].isInt()         ? c["gender"].asInt()         : 0;
            item.m_birthday    = c["birthday"].isInt()       ? c["birthday"].asInt()       : 0;
            item.m_phonenumber = c["phonenumber"].isString() ? c["phonenumber"].asString() : std::string();
            item.m_mail        = c["mail"].isString()        ? c["mail"].asString()        : std::string();
            item.m_landline    = c["landline"].isString()    ? c["landline"].asString()    : std::string();
            item.m_post        = c["post"].isString()        ? c["post"].asString()        : std::string();
            item.m_presence  = c["presence"].isInt()     ? c["presence"].asInt()     : 0;
            //备注名：字段整体缺失是合法情形（旧服务端不带它），此时按"未设备注"
            //处理，显示时自然回落到昵称。
            item.m_remark    = c["remark"].isString()    ? c["remark"].asString()    : std::string();
            //标签：与备注名同理，字段整体缺失是合法情形（旧服务端不带它），按"未贴过
            //标签"处理。数组里混进非字符串元素时只跳过那一个元素，不整条作废——一条
            //脏数据不该让整个联系人从通讯录里消失。
            if (c["tags"].isArray())
            {
                const Json::Value& jtags = c["tags"];
                for (Json::ArrayIndex t = 0; t < jtags.size(); ++t)
                {
                    if (jtags[t].isString())
                        item.m_tags.push_back(jtags[t].asString());
                }
            }
            //userid 非法（缺失/为 0）的条目跳过，避免产生无法发起会话的死项
            if (item.m_userid <= 0)
                continue;
            out.m_contacts.push_back(item);
        }
    }

    //「我的收藏」：每项是 {ctype, cid} 对象，服务端已按收藏时间倒序排好，本端保持原序。
    //字段整体缺失是合法情形（老服务端不带这个字段），此时收藏为空、界面隐藏收藏分区。
    const Json::Value& favorites = root["favorites"];
    if (favorites.isArray())
    {
        for (Json::ArrayIndex i = 0; i < favorites.size(); ++i)
        {
            const Json::Value& f = favorites[i];
            if (!f.isObject())
                continue;

            FavoriteItem item;
            item.m_ctype = f["ctype"].isInt() ? f["ctype"].asInt() : 0;
            item.m_cid   = jsonToInt64(f["cid"]);

            //只认联系人与群两种。非法项跳过而不是整条应答判失败 —— 收藏里混进一条
            //脏数据，不该连整个通讯录都收不到。
            if (item.m_ctype != conversation_single && item.m_ctype != conversation_group)
                continue;
            if (item.m_cid <= 0)
                continue;

            out.m_favorites.push_back(item);
        }
    }

    return true;
}

bool ContactsProtocolClient::parsePresenceChangedNotify(const std::string& jsonbody, PresenceChangedNotify& out)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //presence 必为整型；userid 为字符串编码的 64 位 ID（jsonToInt64 容错整型）。
    if (!root["presence"].isInt())
        return false;

    out.m_userid     = jsonToInt64(root["userid"]);
    out.m_presence   = root["presence"].asInt();
    out.m_clienttype = root["clienttype"].isInt() ? root["clienttype"].asInt() : 0;

    //userid 非法（缺失 / 为 0）的推送无法定位联系人，视为无效。
    if (out.m_userid <= 0)
        return false;

    return true;
}

bool ContactsProtocolClient::parseUserInfoChangedNotify(const std::string& jsonbody, UserInfoChangedNotify& out)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    out.m_userid = jsonToInt64(root["userid"]);
    if (out.m_userid <= 0)
        return false;   //无法定位联系人

    //profile 子对象里取全部资料字段（缺字段取默认；服务端 makeProfileJson 产出）。
    const Json::Value& p = root["profile"];
    out.m_hasProfile = p.isObject();
    if (out.m_hasProfile)
    {
        out.m_facetype    = p["facetype"].isInt()       ? p["facetype"].asInt()       : 0;
        out.m_customface  = p["customface"].isString()  ? p["customface"].asString()  : std::string();
        out.m_nickname    = p["nickname"].isString()    ? p["nickname"].asString()    : std::string();
        out.m_signature   = p["signature"].isString()   ? p["signature"].asString()   : std::string();
        out.m_gender      = p["gender"].isInt()         ? p["gender"].asInt()         : 0;
        out.m_birthday    = p["birthday"].isInt()       ? p["birthday"].asInt()       : 0;
        out.m_address     = p["address"].isString()     ? p["address"].asString()     : std::string();
        out.m_phonenumber = p["phonenumber"].isString() ? p["phonenumber"].asString() : std::string();
        out.m_mail        = p["mail"].isString()        ? p["mail"].asString()        : std::string();
        out.m_landline    = p["landline"].isString()    ? p["landline"].asString()    : std::string();
        out.m_post        = p["post"].isString()        ? p["post"].asString()        : std::string();
    }
    return true;
}

void ContactsProtocolClient::applyUserInfoChanged(const UserInfoChangedNotify& notify, ContactItem& contact)
{
    if (!notify.m_hasProfile)
        return;

    contact.m_facetype   = notify.m_facetype;
    contact.m_customface = notify.m_customface;
    //昵称为空时保留原值：显示名不能变成空白
    if (!notify.m_nickname.empty())
        contact.m_nickname = notify.m_nickname;
    contact.m_signature   = notify.m_signature;
    contact.m_gender      = notify.m_gender;
    contact.m_birthday    = notify.m_birthday;
    contact.m_address     = notify.m_address;
    contact.m_phonenumber = notify.m_phonenumber;
    contact.m_mail        = notify.m_mail;
    contact.m_landline    = notify.m_landline;
    contact.m_post        = notify.m_post;
}

bool ContactsProtocolClient::parseOrgStructureChangedNotify(const std::string& jsonbody,
                                                            OrgStructureChangedNotify& out)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //changetype 是这条推送里唯一的必有字段，缺了或者类型不对就没法判断它到底
    //在说什么，直接当无效丢弃。
    if (!root["changetype"].isInt())
        return false;

    int changetype = root["changetype"].asInt();
    if (changetype != org_change_department && changetype != org_change_member)
        return false;   //将来服务端若加了新的变更类型，老客户端在这里安全地忽略掉

    out.m_changetype = changetype;
    //deptid 服务端写的是数字，这里仍走 jsonToInt64：部门ID从 0x60000000 起递增，
    //早晚要越过 32 位，用 int64 读才不会截断。缺字段时得 0，按"未指明部门"处理。
    out.m_deptid = jsonToInt64(root["deptid"]);
    return true;
}

} // namespace net
