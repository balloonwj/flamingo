/**
 *  v2 通讯录报文编解码实现，ContactsProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "ContactsProtocol.h"
#include "../jsoncpp1.9.0/json.h"

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

std::string ContactsProtocol::buildContactsResponse(const ContactsData& data)
{
    Json::Value root;
    root["code"]       = 0;
    root["msg"]        = "ok";
    root["synctime"]   = static_cast<Json::Int64>(data.synctime);

    //企业信息：作为通讯录树根显示
    Json::Value enterprise;
    enterprise["enterpriseid"] = std::to_string(data.enterpriseid);
    enterprise["name"]         = data.enterprisename;
    root["enterprise"] = enterprise;

    //部门：扁平数组，客户端按 parentdeptid 还原成树
    Json::Value departments(Json::arrayValue);
    for (const DepartmentInfo& d : data.departments)
    {
        Json::Value jd;
        jd["deptid"]       = std::to_string(d.deptid);
        jd["name"]         = d.name;
        jd["parentdeptid"] = std::to_string(d.parentdeptid);
        jd["deptorder"]    = d.deptorder;
        jd["leaderuserid"] = std::to_string(d.leaderuserid);

        Json::Value members(Json::arrayValue);
        for (const DeptMemberRef& m : d.members)
        {
            Json::Value jm;
            jm["userid"]      = std::to_string(m.userid);
            jm["position"]    = m.position;
            jm["isprimary"]   = m.isprimary;
            jm["memberorder"] = m.memberorder;
            members.append(jm);
        }
        jd["members"] = members;
        departments.append(jd);
    }
    root["departments"] = departments;

    //联系人资料池：部门成员与服务号去重后的完整资料
    Json::Value contacts(Json::arrayValue);
    for (const ContactInfo& c : data.contacts)
    {
        Json::Value jc;
        jc["userid"]      = std::to_string(c.userid);
        jc["username"]    = c.username;
        jc["nickname"]    = c.nickname;
        jc["usertype"]    = c.usertype;
        jc["facetype"]    = c.facetype;
        jc["customface"]  = c.customface;
        jc["gender"]      = c.gender;
        jc["birthday"]    = c.birthday;
        jc["signature"]   = c.signature;
        jc["address"]     = c.address;
        jc["phonenumber"] = c.phonenumber;
        jc["mail"]        = c.mail;
        jc["presence"]    = c.presence;
        jc["clienttype"]  = c.clienttype;
        //备注名一律写出（未设置时为空串），不做"空则省略字段"的优化：客户端
        //只需一条 get("remark","") 的解析路径，且"清除备注"与"从未设置"在
        //显示上本就该是同一种效果，不必用字段有无来区分。
        jc["remark"]      = c.remark;
        contacts.append(jc);
    }
    root["contacts"] = contacts;

    //「我的收藏」列表，按收藏时间倒序。每项是 {ctype, cid} 而不是裸的用户ID ——
    //收藏对象既可以是联系人（ctype=1）也可以是群（ctype=2），只给 ID 分不出是哪种。
    //客户端据本数组在「联系人」Tab 树顶渲染「我的收藏」分区。
    Json::Value favorites(Json::arrayValue);
    for (const FavoriteItem& fav : data.favorites)
    {
        Json::Value jf;
        jf["ctype"] = fav.ctype;
        jf["cid"]   = std::to_string(fav.cid);
        favorites.append(jf);
    }
    root["favorites"] = favorites;

    //「特别关注」的联系人列表，按标记时间倒序。与 favorites 不同，这里是<u>扁平的
    //ID 数组</u>而不是对象数组 —— 特别关注只能是人，不存在"是人还是群"的歧义，
    //多带一个 ctype 反而会让人误以为群也能标。
    //客户端据本数组判断某人上下线时要不要弹右下角提示（服务端不参与该判断）。
    Json::Value starred(Json::arrayValue);
    for (int64_t uid : data.starred)
    {
        starred.append(std::to_string(uid));
    }
    root["starred"] = starred;

    //服务号的用户ID列表，客户端据此渲染"服务号"分区
    Json::Value serviceaccounts(Json::arrayValue);
    for (int64_t id : data.serviceaccounts)
        serviceaccounts.append(std::to_string(id));
    root["serviceaccounts"] = serviceaccounts;

    //群组列表
    Json::Value groups(Json::arrayValue);
    for (const GroupInfo& g : data.groups)
    {
        Json::Value jg;
        jg["groupid"]     = std::to_string(g.groupid);
        jg["name"]        = g.name;
        jg["facetype"]    = g.facetype;
        jg["customface"]  = g.customface;
        jg["membercount"] = g.membercount;
        jg["ownerid"]     = std::to_string(g.ownerid);
        groups.append(jg);
    }
    root["groups"] = groups;

    return writeCompact(root);
}
