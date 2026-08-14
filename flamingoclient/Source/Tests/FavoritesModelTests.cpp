/**
 *  FavoritesModelTests.cpp
 *
 *  「我的收藏」内存模型（FavoritesModel）与其协议编解码（FavoriteProtocolClient）的测试：
 *    · 收藏集合的判定、增删、乐观更新回滚，以及 (ctype,cid) 同 id 不同类型互不干扰。
 *    · 右键菜单文案随收藏状态取反。
 *    · BuildRows 把 (ctype,cid) 还原成可显示的行：联系人带在线状态点、群不带；
 *      查不到资料的收藏项被跳过（这条是"好友刚被删"时不画死行的兜底）。
 *    · cmd_set_favorite 请求构造与应答解析。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖消息循环。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cwchar>          // wcscmp —— 比对中文菜单文案

#include "../BalloonMain/FavoritesModel.h"
#include "../net/FavoriteProtocolClient.h"
#include "../net/Protocol.h"   // conversation_single / conversation_group / error_code

using namespace balloonmain;

namespace {

//造一条联系人资料，供 BuildRows 用例复用。
net::ContactItem MakeContact(int64_t userid, const char* nickname,
                             const char* signature, int presence)
{
    net::ContactItem c;
    c.m_userid    = userid;
    c.m_username  = "u" + std::to_string(userid);
    c.m_nickname  = nickname;
    c.m_signature = signature;
    c.m_presence  = presence;
    return c;
}

//造一条群列表项。
net::UserGroupItem MakeGroup(int64_t groupid, const char* name, int memberCount)
{
    net::UserGroupItem g;
    g.m_groupid     = groupid;
    g.m_name        = name;
    g.m_membercount = memberCount;
    return g;
}

//造一条收藏项。
net::FavoriteItem MakeFav(int ctype, int64_t cid)
{
    net::FavoriteItem f;
    f.m_ctype = ctype;
    f.m_cid   = cid;
    return f;
}

} // anonymous namespace

// =====================================================================
// FAVM1 ~ FAVM4 —— 收藏集合的判定与增删
// =====================================================================

TEST_CASE("FAVM1 IsFavorite 区分已收藏 / 未收藏 / 同 id 不同类型", "[favorites]")
{
    std::vector<net::FavoriteItem> items;
    items.push_back(MakeFav(conversation_single, 5081));
    items.push_back(MakeFav(conversation_group, 268435456LL));

    FavoritesModel model;
    model.Reset(items);

    CHECK(model.IsFavorite(conversation_single, 5081));
    CHECK(model.IsFavorite(conversation_group, 268435456LL));
    CHECK_FALSE(model.IsFavorite(conversation_single, 9999));

    //关键用例：联系人 5081 与群 5081 是两条互不相干的记录，只比 cid 会串。
    CHECK_FALSE(model.IsFavorite(conversation_group, 5081));
    CHECK_FALSE(model.IsFavorite(conversation_single, 268435456LL));

    //非法入参恒为未收藏，不能崩也不能误报
    CHECK_FALSE(model.IsFavorite(conversation_service, 5081));   // ctype=3 不是收藏对象
    CHECK_FALSE(model.IsFavorite(0, 5081));
    CHECK_FALSE(model.IsFavorite(conversation_single, 0));
    CHECK_FALSE(model.IsFavorite(conversation_single, -1));
}

TEST_CASE("FAVM2 SetFavorite 增删生效，且不产生重复项", "[favorites]")
{
    FavoritesModel model;
    CHECK(model.IsEmpty());
    CHECK(model.Count() == 0);

    model.SetFavorite(conversation_single, 5081, true);
    CHECK(model.IsFavorite(conversation_single, 5081));
    CHECK(model.Count() == 1);
    CHECK_FALSE(model.IsEmpty());

    //重复加入不产生第二条
    model.SetFavorite(conversation_single, 5081, true);
    CHECK(model.Count() == 1);

    model.SetFavorite(conversation_single, 5081, false);
    CHECK_FALSE(model.IsFavorite(conversation_single, 5081));
    CHECK(model.Count() == 0);

    //取消一条本来就不存在的收藏，不该崩、也不该把条数弄成负数
    model.SetFavorite(conversation_single, 5081, false);
    CHECK(model.Count() == 0);

    //非法入参不改变任何状态
    model.SetFavorite(conversation_service, 5081, true);
    model.SetFavorite(conversation_single, 0, true);
    CHECK(model.Count() == 0);
}

TEST_CASE("FAVM3 SetFavorite 返回改动前的状态，据此可原样回滚", "[favorites]")
{
    //乐观更新的用法：先改本地、记住返回值，服务端应答失败时把返回值再传回来。
    FavoritesModel model;

    const bool prevAdd = model.SetFavorite(conversation_single, 5081, true);
    CHECK_FALSE(prevAdd);                 //改之前是"未收藏"
    CHECK(model.IsFavorite(conversation_single, 5081));

    //回滚
    model.SetFavorite(conversation_single, 5081, prevAdd);
    CHECK_FALSE(model.IsFavorite(conversation_single, 5081));

    //反方向同理：先收藏好，再取消，再回滚
    model.SetFavorite(conversation_group, 700001, true);
    const bool prevRemove = model.SetFavorite(conversation_group, 700001, false);
    CHECK(prevRemove);                    //改之前是"已收藏"
    CHECK_FALSE(model.IsFavorite(conversation_group, 700001));

    model.SetFavorite(conversation_group, 700001, prevRemove);
    CHECK(model.IsFavorite(conversation_group, 700001));
}

TEST_CASE("FAVM4 Reset 整体替换，并丢弃非法项与重复项", "[favorites]")
{
    FavoritesModel model;
    model.SetFavorite(conversation_single, 111, true);

    std::vector<net::FavoriteItem> items;
    items.push_back(MakeFav(conversation_single, 5081));
    items.push_back(MakeFav(conversation_service, 10000));   //ctype 非法，丢弃
    items.push_back(MakeFav(conversation_group, 0));         //cid 非法，丢弃
    items.push_back(MakeFav(conversation_single, 5081));     //重复，丢弃
    items.push_back(MakeFav(conversation_group, 700001));

    model.Reset(items);

    //Reset 是整体替换，之前手工加的 111 不该留下
    CHECK_FALSE(model.IsFavorite(conversation_single, 111));
    CHECK(model.Count() == 2);
    CHECK(model.IsFavorite(conversation_single, 5081));
    CHECK(model.IsFavorite(conversation_group, 700001));

    //空列表 Reset 后应为空，界面据此隐藏收藏分区根节点
    model.Reset(std::vector<net::FavoriteItem>());
    CHECK(model.IsEmpty());
}

TEST_CASE("FAVM4b HasGroupFavorite 认出集合里有没有群", "[favorites]")
{
    // 这条对应一个真实缺陷：群列表(cmd_get_user_groups)比通讯录(cmd_get_contacts)
    // 晚到十几毫秒，收藏分区在通讯录到达时就建好了，那会儿群列表还是空的、群收藏
    // 会被 BuildRows 当成"查不到资料"跳过。主面板据本方法决定群列表到达后要不要
    // 把联系人树补建一次。
    FavoritesModel model;
    CHECK_FALSE(model.HasGroupFavorite());

    model.SetFavorite(conversation_single, 5081, true);
    CHECK_FALSE(model.HasGroupFavorite());   //只有联系人，不必补建

    model.SetFavorite(conversation_group, 700001, true);
    CHECK(model.HasGroupFavorite());

    model.SetFavorite(conversation_group, 700001, false);
    CHECK_FALSE(model.HasGroupFavorite());   //群收藏被取消后又不必补建了
}

TEST_CASE("FAVM4c ExportItems 导出现状，可原样喂回 Reset", "[favorites]")
{
    // 重建联系人树要复用缓存的通讯录应答，而那份应答里的 favorites 是服务端当初
    // 下发的旧值。重建前先用 ExportItems 把模型现状写回缓存，重入时的 Reset 才不会
    // 把刚做的乐观更新覆盖掉 —— 这条钉住"导出→再灌入"这一圈不丢东西、不改顺序。
    FavoritesModel model;
    model.SetFavorite(conversation_single, 5081, true);
    model.SetFavorite(conversation_group, 700001, true);   //后加的排在最前

    std::vector<net::FavoriteItem> exported;
    model.ExportItems(exported);
    REQUIRE(exported.size() == 2);
    CHECK(exported[0].m_ctype == conversation_group);      //顺序即界面顺序
    CHECK(exported[0].m_cid == 700001);
    CHECK(exported[1].m_ctype == conversation_single);
    CHECK(exported[1].m_cid == 5081);

    //导出的内容重新灌回去，状态与顺序都不变（幂等）
    FavoritesModel other;
    other.Reset(exported);
    CHECK(other.Count() == 2);
    CHECK(other.IsFavorite(conversation_group, 700001));
    CHECK(other.IsFavorite(conversation_single, 5081));

    std::vector<net::FavoriteItem> again;
    other.ExportItems(again);
    REQUIRE(again.size() == 2);
    CHECK(again[0].m_cid == 700001);
    CHECK(again[1].m_cid == 5081);

    //空模型导出空集合，且会先清掉出参里的旧内容
    FavoritesModel empty;
    empty.ExportItems(exported);
    CHECK(exported.empty());
}

// =====================================================================
// FAVM5 —— 右键菜单文案
// =====================================================================

TEST_CASE("FAVM5 菜单文案随收藏状态取反", "[favorites]")
{
    CHECK(::wcscmp(FavoritesModel::GetMenuLabel(true), L"取消收藏") == 0);
    CHECK(::wcscmp(FavoritesModel::GetMenuLabel(false), L"添加到收藏") == 0);
}

// =====================================================================
// FAVM6 ~ FAVM8 —— BuildRows
// =====================================================================

TEST_CASE("FAVM6 联系人行带在线状态点，群行不带", "[favorites]")
{
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(5081, "苏沐辰", "用数据验证每个判断", presence_online));

    std::vector<net::UserGroupItem> groups;
    groups.push_back(MakeGroup(700001, "产品部周会", 12));

    std::vector<net::FavoriteItem> items;
    items.push_back(MakeFav(conversation_single, 5081));
    items.push_back(MakeFav(conversation_group, 700001));

    FavoritesModel model;
    model.Reset(items);

    std::vector<FavoriteRow> rows;
    model.BuildRows(contacts, groups, rows);

    REQUIRE(rows.size() == 2);

    //顺序与收藏顺序一致（服务端已按时间倒序排好，本端不再排序）
    CHECK(rows[0].m_ctype == conversation_single);
    CHECK(rows[0].m_cid == 5081);
    CHECK(rows[0].m_displayName == _T("苏沐辰"));
    CHECK(rows[0].m_subLabel == _T("用数据验证每个判断"));
    CHECK(rows[0].m_showStatusDot);
    CHECK(rows[0].m_presence == presence_online);

    CHECK(rows[1].m_ctype == conversation_group);
    CHECK(rows[1].m_cid == 700001);
    CHECK(rows[1].m_displayName == _T("产品部周会"));
    //群第二行显示成员数
    CHECK(rows[1].m_subLabel == _T("12 人"));
    //群没有"在线"这个概念，不画状态点
    CHECK_FALSE(rows[1].m_showStatusDot);
}

TEST_CASE("FAVM7 查不到资料的收藏项被跳过", "[favorites]")
{
    //这条是"好友刚被删 / 群刚被解散，本地缓存还没更新"时的兜底：宁可少显示一行，
    //也不要画出一条点不开的死行。
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(5081, "苏沐辰", "", presence_online));

    std::vector<net::UserGroupItem> groups;
    groups.push_back(MakeGroup(700001, "产品部周会", 12));

    std::vector<net::FavoriteItem> items;
    items.push_back(MakeFav(conversation_single, 5081));
    items.push_back(MakeFav(conversation_single, 9999));      //资料池里没有
    items.push_back(MakeFav(conversation_group, 888888));     //群列表里没有
    items.push_back(MakeFav(conversation_group, 700001));

    FavoritesModel model;
    model.Reset(items);
    CHECK(model.Count() == 4);   //模型里仍是 4 条，只是显示不出来

    std::vector<FavoriteRow> rows;
    model.BuildRows(contacts, groups, rows);

    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_cid == 5081);
    CHECK(rows[1].m_cid == 700001);
}

TEST_CASE("FAVM8 BuildRows 的边界：空收藏、空资料、缺昵称、成员数未知", "[favorites]")
{
    std::vector<net::ContactItem> contacts;
    std::vector<net::UserGroupItem> groups;
    std::vector<FavoriteRow> rows;

    //空收藏集合 → 空行集，界面据此隐藏根节点
    FavoritesModel empty;
    empty.BuildRows(contacts, groups, rows);
    CHECK(rows.empty());

    //昵称为空时退回账号显示，不能画出一行没名字的空白
    contacts.push_back(MakeContact(5081, "", "", presence_offline));
    //成员数未知（0）时不显示第二行，不显示骗人的 "0 人"
    groups.push_back(MakeGroup(700001, "无人群", 0));

    std::vector<net::FavoriteItem> items;
    items.push_back(MakeFav(conversation_single, 5081));
    items.push_back(MakeFav(conversation_group, 700001));

    FavoritesModel model;
    model.Reset(items);
    model.BuildRows(contacts, groups, rows);

    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_displayName == _T("u5081"));   //退回账号
    CHECK(rows[0].m_subLabel.IsEmpty());           //签名为空则第二行为空
    CHECK(rows[1].m_subLabel.IsEmpty());           //成员数未知则不显示 "0 人"
}

TEST_CASE("FAVM8b 收藏行显示名带备注，与通讯录树口径一致", "[favorites]")
{
    //改动前收藏区只取昵称、完全不看备注，于是同一个人在收藏区显示真名、在部门树里
    //显示备注，两处对不上号。现在两处都走 ResolveContactDisplayName。
    std::vector<net::ContactItem> contacts;
    contacts.push_back(MakeContact(5081, "苏沐辰", "", presence_online));
    contacts[0].m_remark = "数据组";
    contacts.push_back(MakeContact(5082, "谢知行", "", presence_online));

    std::vector<net::UserGroupItem> groups;

    std::vector<net::FavoriteItem> items;
    items.push_back(MakeFav(conversation_single, 5081));
    items.push_back(MakeFav(conversation_single, 5082));

    FavoritesModel model;
    model.Reset(items);

    std::vector<FavoriteRow> rows;
    model.BuildRows(contacts, groups, rows);

    REQUIRE(rows.size() == 2);
    CHECK(rows[0].m_displayName == _T("苏沐辰（数据组）"));   //有备注：真名（备注）
    CHECK(rows[1].m_displayName == _T("谢知行"));             //无备注：只有真名
}

// =====================================================================
// FAVM9 ~ FAVM11 —— cmd_set_favorite 的请求构造与应答解析
// =====================================================================

TEST_CASE("FAVM9 请求构造：cid 走字符串，favorite 走整数 0/1", "[favorites][protocol]")
{
    const std::string reqAdd =
        net::FavoriteProtocolClient::buildSetFavoriteRequest(conversation_single, 5081, true);

    //cid 必须是字符串（大整数走 JSON number 会丢精度），favorite 必须是整数 ——
    //服务端 parseSetFavoriteRequest 读的是 isInt()，写成 true/false 会被判非法。
    CHECK(reqAdd.find("\"cid\":\"5081\"") != std::string::npos);
    CHECK(reqAdd.find("\"favorite\":1") != std::string::npos);
    CHECK(reqAdd.find("\"ctype\":1") != std::string::npos);
    CHECK(reqAdd.find("true") == std::string::npos);

    const std::string reqDel =
        net::FavoriteProtocolClient::buildSetFavoriteRequest(conversation_group, 268435456LL, false);
    CHECK(reqDel.find("\"cid\":\"268435456\"") != std::string::npos);
    CHECK(reqDel.find("\"favorite\":0") != std::string::npos);
    CHECK(reqDel.find("\"ctype\":2") != std::string::npos);
}

TEST_CASE("FAVM10 应答解析：成功与失败都回带 ctype / cid / favorite", "[favorites][protocol]")
{
    net::SetFavoriteResult res;

    REQUIRE(net::FavoriteProtocolClient::parseSetFavoriteResponse(
        "{\"code\":0,\"msg\":\"ok\",\"ctype\":2,\"cid\":\"268435456\",\"favorite\":1}", res));
    CHECK(res.m_code == error_code_ok);
    CHECK(res.m_msg == "ok");
    CHECK(res.m_ctype == conversation_group);
    CHECK(res.m_cid == 268435456LL);
    CHECK(res.m_favorite == 1);

    //失败应答同样要能对上号，否则乐观更新没法正确回滚
    REQUIRE(net::FavoriteProtocolClient::parseSetFavoriteResponse(
        "{\"code\":209,\"msg\":\"limit\",\"ctype\":1,\"cid\":\"5081\",\"favorite\":1}", res));
    CHECK(res.m_code == error_code_favorite_limit);
    CHECK(res.m_ctype == conversation_single);
    CHECK(res.m_cid == 5081);
}

TEST_CASE("FAVM11 应答解析的容错", "[favorites][protocol]")
{
    net::SetFavoriteResult res;

    //JSON 非法 / 不是对象 / 缺 code：一律判失败，调用方丢弃
    CHECK_FALSE(net::FavoriteProtocolClient::parseSetFavoriteResponse("", res));
    CHECK_FALSE(net::FavoriteProtocolClient::parseSetFavoriteResponse("not json", res));
    CHECK_FALSE(net::FavoriteProtocolClient::parseSetFavoriteResponse("[1,2]", res));
    CHECK_FALSE(net::FavoriteProtocolClient::parseSetFavoriteResponse(
        "{\"msg\":\"ok\"}", res));

    //只有 code 也算可用 —— 至少知道成没成，回带三项缺失时取 0
    REQUIRE(net::FavoriteProtocolClient::parseSetFavoriteResponse("{\"code\":0}", res));
    CHECK(res.m_code == error_code_ok);
    CHECK(res.m_ctype == 0);
    CHECK(res.m_cid == 0);
}
