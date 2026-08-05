/**
 *  v2 通讯录报文编解码单元测试（Suite G），test_contactsprotocol.cpp
 *  覆盖：部门树结构、联系人池字段、群组、服务号/星标、企业信息、空数据。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/ContactsProtocol.h"
#include "../chatserversrc/Protocol.h"   //conversation_single / conversation_group（收藏项的 ctype 取值）
#include "../jsoncpp1.9.0/json.h"

//把 JSON 文本解析成 Json::Value，供断言检查应答内容
static bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//构造一份典型的通讯录数据，供多个用例复用
static ContactsData makeSampleData()
{
    ContactsData data;
    data.enterpriseid = 5001;
    data.enterprisename = "火烈鸟科技 · 上海";
    data.synctime = 1716268800000LL;

    DepartmentInfo dept1;
    dept1.deptid = 1001;
    dept1.name = "产品部";
    dept1.parentdeptid = 0;
    dept1.deptorder = 0;
    dept1.leaderuserid = 100007;
    DeptMemberRef m1;
    m1.userid = 100007;
    m1.position = "产品总监";
    m1.isprimary = 1;
    m1.memberorder = 0;
    dept1.members.push_back(m1);

    DepartmentInfo dept2;
    dept2.deptid = 1002;
    dept2.name = "设计组";
    dept2.parentdeptid = 1001;
    dept2.deptorder = 0;
    dept2.leaderuserid = 100002;

    data.departments.push_back(dept1);
    data.departments.push_back(dept2);

    ContactInfo c1;
    c1.userid = 100007;
    c1.username = "10010001";
    c1.nickname = "谢知行";
    c1.usertype = 0;
    c1.facetype = 0;
    c1.customface = "";
    c1.gender = 1;
    c1.birthday = 19900101;
    c1.signature = "好的产品是迭代出来的";
    c1.address = "上海";
    c1.phonenumber = "";
    c1.mail = "";
    c1.presence = 1;
    c1.clienttype = 1;
    data.contacts.push_back(c1);

    data.serviceaccounts.push_back(10000);
    data.serviceaccounts.push_back(10001);

    GroupInfo g1;
    g1.groupid = 268435457;
    g1.name = "设计组 · 周会";
    g1.facetype = 0;
    g1.customface = "";
    g1.membercount = 12;
    g1.ownerid = 100007;
    data.groups.push_back(g1);

    return data;
}

TEST_CASE("G1 部门树：扁平数组 + parentdeptid + 成员归属")
{
    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(makeSampleData()), root));
    CHECK(root["code"].asInt() == 0);

    const Json::Value& depts = root["departments"];
    REQUIRE(depts.isArray());
    REQUIRE(depts.size() == 2);
    //顶级部门 parentdeptid 为 "0"
    CHECK(depts[0]["deptid"].asString() == "1001");
    CHECK(depts[0]["name"].asString() == "产品部");
    CHECK(depts[0]["parentdeptid"].asString() == "0");
    //子部门挂在 1001 之下
    CHECK(depts[1]["parentdeptid"].asString() == "1001");
    //部门成员归属信息
    REQUIRE(depts[0]["members"].size() == 1);
    CHECK(depts[0]["members"][0]["userid"].asString() == "100007");
    CHECK(depts[0]["members"][0]["position"].asString() == "产品总监");
    CHECK(depts[0]["members"][0]["isprimary"].asInt() == 1);
    //只挂子部门不挂人的部门，members 为空数组
    CHECK(depts[1]["members"].isArray());
    CHECK(depts[1]["members"].size() == 0);
}

TEST_CASE("G2 联系人池：字段齐全且 64 位 id 为字符串")
{
    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(makeSampleData()), root));

    const Json::Value& contacts = root["contacts"];
    REQUIRE(contacts.isArray());
    REQUIRE(contacts.size() == 1);
    const Json::Value& c = contacts[0];
    CHECK(c["userid"].asString() == "100007");        //64 位 id 编码为字符串
    CHECK(c["nickname"].asString() == "谢知行");
    CHECK(c["usertype"].asInt() == 0);
    CHECK(c["gender"].asInt() == 1);
    CHECK(c["birthday"].asInt() == 19900101);
    CHECK(c["presence"].asInt() == 1);
    CHECK(c["clienttype"].asInt() == 1);
}

TEST_CASE("G3 群组列表")
{
    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(makeSampleData()), root));

    const Json::Value& groups = root["groups"];
    REQUIRE(groups.isArray());
    REQUIRE(groups.size() == 1);
    CHECK(groups[0]["groupid"].asString() == "268435457");
    CHECK(groups[0]["name"].asString() == "设计组 · 周会");
    CHECK(groups[0]["membercount"].asInt() == 12);
    CHECK(groups[0]["ownerid"].asString() == "100007");
}

TEST_CASE("G4 服务号 id 列表与未设置收藏时的 favorites 空数组")
{
    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(makeSampleData()), root));

    const Json::Value& svc = root["serviceaccounts"];
    REQUIRE(svc.isArray());
    REQUIRE(svc.size() == 2);
    CHECK(svc[0].asString() == "10000");
    CHECK(svc[1].asString() == "10001");
    //样例数据没设收藏，favorites 应是合法的空数组（字段须存在，客户端据此隐藏收藏分区）
    REQUIRE(root["favorites"].isArray());
    CHECK(root["favorites"].size() == 0);
}

TEST_CASE("G4b favorites 里联系人与群混排，各项为 {ctype, cid} 对象")
{
    //这是「我的收藏」的核心编码用例：收藏对象既可以是联系人也可以是群，
    //只给裸 ID 分不出是哪一种，故每项必须是带 ctype 的对象。
    ContactsData data = makeSampleData();
    data.favorites.push_back(FavoriteItem(conversation_single, 100007));
    data.favorites.push_back(FavoriteItem(conversation_group, 268435456LL));
    data.favorites.push_back(FavoriteItem(conversation_single, 10000));   //服务号也可收藏

    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(data), root));

    const Json::Value& favs = root["favorites"];
    REQUIRE(favs.isArray());
    REQUIRE(favs.size() == 3);

    CHECK(favs[0]["ctype"].asInt() == conversation_single);
    //64 位 ID 一律以字符串编码，不能退化成数字
    REQUIRE(favs[0]["cid"].isString());
    CHECK(favs[0]["cid"].asString() == "100007");

    CHECK(favs[1]["ctype"].asInt() == conversation_group);
    CHECK(favs[1]["cid"].asString() == "268435456");

    CHECK(favs[2]["ctype"].asInt() == conversation_single);
    CHECK(favs[2]["cid"].asString() == "10000");
}

TEST_CASE("G4c favorites 的顺序与传入顺序一致")
{
    //服务端已按收藏时间倒序取好，编码环节不得再排序 —— 否则"最近收藏的排最前"
    //这个约定会在这里被悄悄打乱。
    ContactsData data = makeSampleData();
    data.favorites.push_back(FavoriteItem(conversation_group, 300));
    data.favorites.push_back(FavoriteItem(conversation_single, 100));
    data.favorites.push_back(FavoriteItem(conversation_single, 200));

    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(data), root));

    const Json::Value& favs = root["favorites"];
    REQUIRE(favs.size() == 3);
    CHECK(favs[0]["cid"].asString() == "300");
    CHECK(favs[1]["cid"].asString() == "100");
    CHECK(favs[2]["cid"].asString() == "200");
}

TEST_CASE("G5 企业信息与同步时间")
{
    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(makeSampleData()), root));

    CHECK(root["enterprise"]["enterpriseid"].asString() == "5001");
    CHECK(root["enterprise"]["name"].asString() == "火烈鸟科技 · 上海");
    CHECK(root["synctime"].asInt64() == 1716268800000LL);
}

TEST_CASE("G7 联系人备注名：设了就原样带出")
{
    //备注名是单向的（只影响"本人看该联系人"），故它随每个登录用户各自的通讯录
    //下发，而不是联系人自身的资料。改备注走 cmd_set_friend_remark(1209)，读回来
    //就靠本字段——不带的话客户端重登后备注就丢了。
    ContactsData data = makeSampleData();
    data.contacts[0].remark = "产品组同事";

    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(data), root));

    const Json::Value& contacts = root["contacts"];
    REQUIRE(contacts.size() == 1);
    REQUIRE(contacts[0]["remark"].isString());
    CHECK(contacts[0]["remark"].asString() == "产品组同事");
}

TEST_CASE("G7b 未设备注：remark 字段仍在且为空串")
{
    //刻意不做"空则省略字段"的优化：客户端只需一条解析路径，且"清除了备注"
    //与"从未设过备注"在显示上本就是同一种效果，不必靠字段有无来区分。
    //makeSampleData 里的联系人没设备注。
    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(makeSampleData()), root));

    const Json::Value& contacts = root["contacts"];
    REQUIRE(contacts.size() == 1);
    REQUIRE(contacts[0].isMember("remark"));
    REQUIRE(contacts[0]["remark"].isString());
    CHECK(contacts[0]["remark"].asString().empty());
}

TEST_CASE("G7c 备注名含中文与需转义字符时 JSON 仍合法")
{
    //备注名是用户自由输入的，双引号、反斜杠、换行都可能出现。这里验证转义
    //由 jsoncpp 正确完成——只要能原样解析回来，就说明没有把报文拼坏。
    ContactsData data = makeSampleData();
    data.contacts[0].remark = "他说\"随便\"\\ 换行\n结束 · 中文";

    const std::string text = ContactsProtocol::buildContactsResponse(data);
    Json::Value root;
    REQUIRE(parseJson(text, root));

    const Json::Value& contacts = root["contacts"];
    REQUIRE(contacts.size() == 1);
    CHECK(contacts[0]["remark"].asString() == "他说\"随便\"\\ 换行\n结束 · 中文");
}

TEST_CASE("G6 空数据：各区块均为合法空数组")
{
    ContactsData empty;
    empty.enterpriseid = 0;
    empty.synctime = 0;

    Json::Value root;
    REQUIRE(parseJson(ContactsProtocol::buildContactsResponse(empty), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["departments"].isArray());
    CHECK(root["departments"].size() == 0);
    CHECK(root["contacts"].isArray());
    CHECK(root["contacts"].size() == 0);
    CHECK(root["favorites"].isArray());
    CHECK(root["serviceaccounts"].isArray());
    CHECK(root["groups"].isArray());
    CHECK(root["groups"].size() == 0);
}
