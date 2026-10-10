/**
 *  文件共享信令编解码与状态计算单元测试（Suite FSP），test_fileshareprotocol.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 5 步，2026-10-02 与用户确认的用例）：
 *    - FSP1 发起共享请求的解析：合法的单聊 / 群聊请求，以及各类非法输入；
 *    - FSP2 其余请求的解析：取消、调整成员、拉取列表、在线提供报告、打开、结束访问、申请传输、
 *           结束传输、审计上报；
 *    - FSP3 应答与推送的构造：字段名与方案逐字一致，64 位 ID 以十进制字符串写出；
 *    - FSP4 共享卡片正文的构造与解析，以及按「看的人」合并状态；
 *    - FSP5 computeShareState 的组合表；
 *    - FSP6 与客户端共用的线上格式样例：请求能解析，构造出的应答与样例逐字相同；
 *    - FSP7 2222 的 p2psessionid（P2P 方案第 6 期）：只在传输子会话且非空时写出；
 *    - FSP8 卡片正文里分享者的真实路径（2026-10-04 起）：入库不带；合并时只给分享者本人补上，成员与
 *           旧卡片一律去掉；removeCardDisplayPath 只去掉路径；
 *    - FSP9 共享列表里的真实路径：「我的共享」带，「共享给我的」不带；
 *    - FSP10 共享名称敏感词检查用到的取值：拦截来源 15、错误码 822（与客户端、管理后台的约定一致）。
 *  balloonwj@qq.com   2026-10-02
 */
#include <string>
#include <vector>

#include "doctest.h"
#include "../chatserversrc/FileShareProtocol.h"
#include "../chatserversrc/Protocol.h"
#include "../chatserversrc/SensitiveWordDefs.h"   //FSP10：共享名称的拦截来源
#include "../jsoncpp1.9.0/json.h"
#include "fileshare_wire_samples.h"   //FSP6：与客户端 ShareProtocolCodec 共用的线上格式样例

namespace {

//把 JSON 文本解析成对象，失败时让用例失败
Json::Value parseJson(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    REQUIRE(ok);
    return root;
}

//一个合法的群聊发起共享请求，各用例在它的基础上改一个字段来造非法输入
const char* const kValidGroupCreate =
    "{\"clientshareid\":\"uuid-1\",\"ctype\":2,\"cid\":\"268435457\",\"kind\":2,"
    "\"name\":\"Projects\",\"displaypath\":\"D:\\\\Projects\",\"includehidden\":1,"
    "\"members\":[{\"userid\":\"10002\",\"perms\":1},{\"userid\":\"10003\",\"perms\":31}]}";

} // namespace

TEST_CASE("FSP1 发起共享请求的解析")
{
    SUBCASE("合法的群聊请求")
    {
        ShareCreateRequest req;
        REQUIRE(FileShareProtocol::parseCreateRequest(kValidGroupCreate, req));
        CHECK(req.m_clientShareId == "uuid-1");
        CHECK(req.m_ctype == conversation_group);
        CHECK(req.m_cid == 268435457LL);
        CHECK(req.m_kind == share_kind_folder);
        CHECK(req.m_name == "Projects");
        CHECK(req.m_displayPath == "D:\\Projects");
        CHECK(req.m_includeHidden);
        REQUIRE(req.m_members.size() == 2);
        CHECK(req.m_members[0].m_userId == 10002);
        CHECK(req.m_members[0].m_perms == share_perm_upload);
        CHECK(req.m_members[1].m_perms == kSharePermAll);
    }

    SUBCASE("合法的单聊请求：成员恰好是对端一人；ID 写成整数也接受；includehidden 缺省为假")
    {
        ShareCreateRequest req;
        REQUIRE(FileShareProtocol::parseCreateRequest(
            "{\"clientshareid\":\"u\",\"ctype\":1,\"cid\":10002,\"kind\":3,\"name\":\"a.docx\","
            "\"displaypath\":\"D:\\\\a.docx\",\"members\":[{\"userid\":10002,\"perms\":0}]}", req));
        CHECK(req.m_ctype == conversation_single);
        CHECK(req.m_kind == share_kind_file);
        CHECK_FALSE(req.m_includeHidden);
        REQUIRE(req.m_members.size() == 1);
        CHECK(req.m_members[0].m_userId == 10002);
    }

    SUBCASE("单文件共享带了写权限：不算错误，解析结果里权限清零")
    {
        ShareCreateRequest req;
        REQUIRE(FileShareProtocol::parseCreateRequest(
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":3,\"name\":\"n\",\"displaypath\":\"p\","
            "\"members\":[{\"userid\":\"2\",\"perms\":1},{\"userid\":\"3\",\"perms\":31}]}", req));
        REQUIRE(req.m_members.size() == 2);
        CHECK(req.m_members[0].m_perms == 0);
        CHECK(req.m_members[1].m_perms == 0);
    }

    SUBCASE("非法输入一律失败")
    {
        const char* bad[] =
        {
            "",
            "[]",
            //缺 clientshareid
            "{\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":0}]}",
            //clientshareid 超过 64 字节
            "{\"clientshareid\":\"0123456789012345678901234567890123456789012345678901234567890123x\",\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":0}]}",
            //ctype 越界
            "{\"clientshareid\":\"u\",\"ctype\":3,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":0}]}",
            //cid 不为正
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"0\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":0}]}",
            //kind 越界
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":4,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":0}]}",
            //name 为空
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":0}]}",
            //displaypath 缺失
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"members\":[{\"userid\":\"2\",\"perms\":0}]}",
            //includehidden 不是 0 / 1
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"includehidden\":2,\"members\":[{\"userid\":\"2\",\"perms\":0}]}",
            //members 为空
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[]}",
            //成员重复
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":0},{\"userid\":\"2\",\"perms\":1}]}",
            //perms 超出 0~31
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":32}]}",
            "{\"clientshareid\":\"u\",\"ctype\":2,\"cid\":\"1\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"2\",\"perms\":-1}]}",
            //单聊的成员不是对端
            "{\"clientshareid\":\"u\",\"ctype\":1,\"cid\":\"10002\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"10003\",\"perms\":0}]}",
            //单聊的成员多于一人
            "{\"clientshareid\":\"u\",\"ctype\":1,\"cid\":\"10002\",\"kind\":2,\"name\":\"n\",\"displaypath\":\"p\",\"members\":[{\"userid\":\"10002\",\"perms\":0},{\"userid\":\"10003\",\"perms\":0}]}",
        };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        {
            ShareCreateRequest req;
            INFO("case #" << i << ": " << bad[i]);
            CHECK_FALSE(FileShareProtocol::parseCreateRequest(bad[i], req));
        }
    }
}

TEST_CASE("FSP2 其余请求的解析")
{
    SUBCASE("取消 / 打开：只带 shareid")
    {
        int64_t shareId = 0;
        REQUIRE(FileShareProtocol::parseShareIdRequest("{\"shareid\":\"1024\"}", shareId));
        CHECK(shareId == 1024);
        REQUIRE(FileShareProtocol::parseShareIdRequest("{\"shareid\":7}", shareId));
        CHECK(shareId == 7);
        CHECK_FALSE(FileShareProtocol::parseShareIdRequest("{\"shareid\":\"0\"}", shareId));
        CHECK_FALSE(FileShareProtocol::parseShareIdRequest("{}", shareId));
    }

    SUBCASE("调整成员")
    {
        ShareUpdateMembersRequest req;
        REQUIRE(FileShareProtocol::parseUpdateMembersRequest(
            "{\"shareid\":\"5\",\"add\":[{\"userid\":\"11\",\"perms\":2}],\"remove\":[\"12\"],"
            "\"update\":[{\"userid\":\"13\",\"perms\":8}]}", req));
        CHECK(req.m_shareId == 5);
        REQUIRE(req.m_add.size() == 1);
        CHECK(req.m_add[0].m_perms == share_perm_mkdir);
        REQUIRE(req.m_remove.size() == 1);
        CHECK(req.m_remove[0] == 12);
        REQUIRE(req.m_update.size() == 1);
        CHECK(req.m_update[0].m_userId == 13);

        //三组都空
        CHECK_FALSE(FileShareProtocol::parseUpdateMembersRequest("{\"shareid\":\"5\"}", req));
        //同一个人既追加又移除
        CHECK_FALSE(FileShareProtocol::parseUpdateMembersRequest(
            "{\"shareid\":\"5\",\"add\":[{\"userid\":\"11\",\"perms\":0}],\"remove\":[\"11\"]}", req));
        //同一个人在同一组里出现两次
        CHECK_FALSE(FileShareProtocol::parseUpdateMembersRequest(
            "{\"shareid\":\"5\",\"remove\":[\"11\",\"11\"]}", req));
        //权限越界
        CHECK_FALSE(FileShareProtocol::parseUpdateMembersRequest(
            "{\"shareid\":\"5\",\"update\":[{\"userid\":\"13\",\"perms\":64}]}", req));
    }

    SUBCASE("拉取列表")
    {
        ShareListRequest req;
        REQUIRE(FileShareProtocol::parseListRequest("{\"scope\":1}", req));
        CHECK(req.m_scope == share_list_scope_mine);
        CHECK(req.m_ctype == 0);
        REQUIRE(FileShareProtocol::parseListRequest("{\"scope\":2,\"ctype\":2,\"cid\":\"268435457\"}", req));
        CHECK(req.m_ctype == conversation_group);
        CHECK(req.m_cid == 268435457LL);
        CHECK_FALSE(FileShareProtocol::parseListRequest("{\"scope\":3}", req));
        //ctype 与 cid 只给了一个
        CHECK_FALSE(FileShareProtocol::parseListRequest("{\"scope\":1,\"ctype\":2}", req));
        CHECK_FALSE(FileShareProtocol::parseListRequest("{\"scope\":1,\"cid\":\"9\"}", req));
    }

    SUBCASE("在线提供报告")
    {
        std::vector<ShareServeReportItem> items;
        REQUIRE(FileShareProtocol::parseServeReport(
            "{\"items\":[{\"shareid\":\"1\",\"available\":1},{\"shareid\":\"2\",\"available\":0,\"reason\":3}]}",
            items));
        REQUIRE(items.size() == 2);
        CHECK(items[0].m_available);
        CHECK(items[0].m_reason == 0);
        CHECK_FALSE(items[1].m_available);
        CHECK(items[1].m_reason == share_unavailable_root_missing);

        CHECK_FALSE(FileShareProtocol::parseServeReport("{\"items\":[]}", items));
        //不可提供却没给原因、或原因越界
        CHECK_FALSE(FileShareProtocol::parseServeReport("{\"items\":[{\"shareid\":\"2\",\"available\":0}]}", items));
        CHECK_FALSE(FileShareProtocol::parseServeReport(
            "{\"items\":[{\"shareid\":\"2\",\"available\":0,\"reason\":9}]}", items));
        //可提供却带了原因
        CHECK_FALSE(FileShareProtocol::parseServeReport(
            "{\"items\":[{\"shareid\":\"2\",\"available\":1,\"reason\":3}]}", items));
    }

    SUBCASE("结束访问")
    {
        std::string accessId;
        int errCode = -1;
        REQUIRE(FileShareProtocol::parseCloseAccessRequest("{\"accessid\":\"a1\"}", accessId, errCode));
        CHECK(accessId == "a1");
        CHECK(errCode == 0);
        REQUIRE(FileShareProtocol::parseCloseAccessRequest("{\"accessid\":\"a1\",\"reason\":816}", accessId, errCode));
        CHECK(errCode == error_code_share_owner_timeout);
        CHECK_FALSE(FileShareProtocol::parseCloseAccessRequest("{\"accessid\":\"\"}", accessId, errCode));
    }

    SUBCASE("申请传输")
    {
        ShareTransferRequest req;
        REQUIRE(FileShareProtocol::parseTransferRequest(
            "{\"accessid\":\"a1\",\"direction\":1,\"items\":[{\"path\":\"docs/a.txt\",\"isdir\":0,\"size\":12},"
            "{\"path\":\"docs/sub\",\"isdir\":1,\"size\":0}],\"totalbytes\":12,\"totalfiles\":1}", req));
        CHECK(req.m_direction == share_transfer_download);
        REQUIRE(req.m_items.size() == 2);
        CHECK(req.m_items[1].m_isDir);
        CHECK(req.m_totalBytes == 12);

        //方向越界、条目为空、大小为负
        CHECK_FALSE(FileShareProtocol::parseTransferRequest(
            "{\"accessid\":\"a1\",\"direction\":3,\"items\":[{\"path\":\"a\",\"isdir\":0,\"size\":1}],\"totalbytes\":1,\"totalfiles\":1}", req));
        CHECK_FALSE(FileShareProtocol::parseTransferRequest(
            "{\"accessid\":\"a1\",\"direction\":1,\"items\":[],\"totalbytes\":0,\"totalfiles\":0}", req));
        CHECK_FALSE(FileShareProtocol::parseTransferRequest(
            "{\"accessid\":\"a1\",\"direction\":1,\"items\":[{\"path\":\"a\",\"isdir\":0,\"size\":-1}],\"totalbytes\":1,\"totalfiles\":1}", req));
        //上传时条目名不能为空
        CHECK_FALSE(FileShareProtocol::parseTransferRequest(
            "{\"accessid\":\"a1\",\"direction\":2,\"items\":[{\"path\":\"\",\"isdir\":0,\"size\":1}],\"totalbytes\":1,\"totalfiles\":1}", req));
    }

    SUBCASE("结束传输")
    {
        std::string transferId;
        int result = -1;
        int errCode = -1;
        REQUIRE(FileShareProtocol::parseTransferClose("{\"transferid\":\"t1\",\"result\":0}", transferId, result, errCode));
        CHECK(result == share_transfer_result_success);
        CHECK(errCode == 0);
        REQUIRE(FileShareProtocol::parseTransferClose(
            "{\"transferid\":\"t1\",\"result\":1,\"errcode\":818}", transferId, result, errCode));
        CHECK(errCode == error_code_share_transfer_canceled);
        //成功却带非零错误码、result 越界
        CHECK_FALSE(FileShareProtocol::parseTransferClose(
            "{\"transferid\":\"t1\",\"result\":0,\"errcode\":818}", transferId, result, errCode));
        CHECK_FALSE(FileShareProtocol::parseTransferClose("{\"transferid\":\"t1\",\"result\":2}", transferId, result, errCode));
    }

    SUBCASE("审计上报：客户端只能上报 4~9，每条都要带正数 shareid")
    {
        std::vector<ShareAuditItem> items;
        REQUIRE(FileShareProtocol::parseAuditReport(
            "{\"items\":[{\"shareid\":\"42\",\"accessid\":\"a1\",\"visitorid\":\"10002\",\"action\":7,\"path\":\"x.txt\","
            "\"topath\":\"y.txt\",\"size\":0,\"result\":0,\"clienttime\":1759370000000}]}", items));
        REQUIRE(items.size() == 1);
        CHECK(items[0].m_shareId == 42);
        CHECK(items[0].m_action == share_audit_rename);
        CHECK(items[0].m_toPath == "y.txt");
        CHECK(items[0].m_visitorId == 10002);

        const int forbidden[] = { share_audit_open, share_audit_request_upload, share_audit_create_share,
                                  share_audit_cancel_share, share_audit_update_members, 0, 13 };
        for (size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i)
        {
            std::string json = std::string("{\"items\":[{\"shareid\":\"42\",\"accessid\":\"a1\",\"visitorid\":\"10002\",\"action\":")
                + std::to_string(forbidden[i]) + ",\"path\":\"x\",\"size\":0,\"result\":0}]}";
            INFO("action " << forbidden[i]);
            CHECK_FALSE(FileShareProtocol::parseAuditReport(json, items));
        }
        CHECK_FALSE(FileShareProtocol::parseAuditReport("{\"items\":[]}", items));

        //shareid 缺失、为 0、为负、不是数字：整个请求解析失败
        const char* badShareIds[] =
        {
            "{\"items\":[{\"accessid\":\"a1\",\"visitorid\":\"10002\",\"action\":7,\"path\":\"x\",\"size\":0}]}",
            "{\"items\":[{\"shareid\":\"0\",\"accessid\":\"a1\",\"visitorid\":\"10002\",\"action\":7,\"path\":\"x\",\"size\":0}]}",
            "{\"items\":[{\"shareid\":\"-5\",\"accessid\":\"a1\",\"visitorid\":\"10002\",\"action\":7,\"path\":\"x\",\"size\":0}]}",
            "{\"items\":[{\"shareid\":\"abc\",\"accessid\":\"a1\",\"visitorid\":\"10002\",\"action\":7,\"path\":\"x\",\"size\":0}]}",
        };
        for (size_t i = 0; i < sizeof(badShareIds) / sizeof(badShareIds[0]); ++i)
        {
            INFO("bad shareid case " << i);
            CHECK_FALSE(FileShareProtocol::parseAuditReport(badShareIds[i], items));
        }
    }
}

TEST_CASE("FSP3 应答与推送的构造")
{
    SUBCASE("发起共享应答：shareid 与 invalidmembers 以字符串写出")
    {
        std::vector<int64_t> none;
        Json::Value ok = parseJson(FileShareProtocol::buildCreateResponse(0, "ok", 1024, "uuid-1", none));
        CHECK(ok["code"].asInt() == 0);
        CHECK(ok["shareid"].asString() == "1024");
        CHECK(ok["clientshareid"].asString() == "uuid-1");
        CHECK_FALSE(ok.isMember("invalidmembers"));

        std::vector<int64_t> invalid;
        invalid.push_back(10005);
        Json::Value bad = parseJson(FileShareProtocol::buildCreateResponse(
            error_code_share_cross_enterprise, "cross enterprise", 0, "uuid-1", invalid));
        CHECK(bad["code"].asInt() == error_code_share_cross_enterprise);
        CHECK_FALSE(bad.isMember("shareid"));
        REQUIRE(bad["invalidmembers"].isArray());
        CHECK(bad["invalidmembers"][0].asString() == "10005");
    }

    SUBCASE("状态推送、权限推送、会话结束推送")
    {
        ShareStateView view;
        view.m_state = share_state_unavailable;
        view.m_reason = share_unavailable_other_device;
        Json::Value st = parseJson(FileShareProtocol::buildStateNotify(77, view));
        CHECK(st["shareid"].asString() == "77");
        CHECK(st["state"].asInt() == share_state_unavailable);
        CHECK(st["reason"].asInt() == share_unavailable_other_device);

        Json::Value perms = parseJson(FileShareProtocol::buildPermsNotify(77, 10002, 9));
        CHECK(perms["userid"].asString() == "10002");
        CHECK(perms["perms"].asInt() == 9);

        Json::Value ended = parseJson(FileShareProtocol::buildAccessEndedNotify("a1", "", error_code_share_canceled));
        CHECK(ended["accessid"].asString() == "a1");
        CHECK(ended["transferid"].asString() == "");
        CHECK(ended["errcode"].asInt() == error_code_share_canceled);
    }

    SUBCASE("中转参数推送：只有推给分享者的那一帧带访问者信息与搜索上限")
    {
        ShareRelayInfo info;
        info.m_accessId = "share-a1";
        info.m_relayIp = "10.0.0.1";
        info.m_relayPort = 20001;
        info.m_token = "tok";
        info.m_role = 10;
        info.m_openTimeoutMs = 15000;
        info.m_idleTimeoutMs = 600000;
        info.m_shareId = 77;
        info.m_visitorId = 10002;
        Json::Value visitor = parseJson(FileShareProtocol::buildRelayInfo(info));
        CHECK(visitor["accessid"].asString() == "share-a1");
        CHECK(visitor["relayip"].asString() == "10.0.0.1");
        CHECK(visitor["relayport"].asInt() == 20001);
        CHECK(visitor["role"].asInt() == 10);
        CHECK(visitor["lanes"].asInt() == 0);
        CHECK(visitor["opentimeoutms"].asInt() == 15000);
        CHECK(visitor["idletimeoutms"].asInt() == 600000);
        CHECK_FALSE(visitor.isMember("visitorid"));
        CHECK_FALSE(visitor.isMember("searchmaxresults"));

        info.m_forOwner = true;
        info.m_role = 9;
        info.m_visitorName = "韩晓燕888";
        info.m_perms = 3;
        info.m_searchMaxResults = 500;
        info.m_searchTimeoutMs = 10000;
        Json::Value owner = parseJson(FileShareProtocol::buildRelayInfo(info));
        CHECK(owner["shareid"].asString() == "77");
        CHECK(owner["visitorid"].asString() == "10002");
        CHECK(owner["visitorname"].asString() == "韩晓燕888");
        CHECK(owner["perms"].asInt() == 3);
        CHECK(owner["searchmaxresults"].asInt() == 500);
        CHECK(owner["searchtimeoutms"].asInt() == 10000);
    }

    SUBCASE("列表应答：两种 scope 写出各自专有的字段")
    {
        ShareListItem item;
        item.m_shareId = 5;
        item.m_ownerId = 10001;
        item.m_ctype = conversation_group;
        item.m_cid = 268435457LL;
        item.m_kind = share_kind_disk;
        item.m_name = "D";
        item.m_displayPath = "D:\\";
        item.m_state = share_state_available;
        item.m_onThisDevice = true;
        ShareMemberSpec m;
        m.m_userId = 10002;
        m.m_perms = 1;
        item.m_members.push_back(m);
        item.m_myPerms = 4;
        std::vector<ShareListItem> items(1, item);

        Json::Value mine = parseJson(FileShareProtocol::buildListResponse(0, "ok", share_list_scope_mine, items));
        REQUIRE(mine["shares"].isArray());
        const Json::Value& a = mine["shares"][0];
        CHECK(a["shareid"].asString() == "5");
        CHECK(a["cid"].asString() == "268435457");
        CHECK(a["onthisdevice"].asInt() == 1);
        CHECK(a["members"][0]["userid"].asString() == "10002");
        CHECK_FALSE(a.isMember("myperms"));

        Json::Value toMe = parseJson(FileShareProtocol::buildListResponse(0, "ok", share_list_scope_shared_me, items));
        const Json::Value& b = toMe["shares"][0];
        CHECK(b["ownerid"].asString() == "10001");
        CHECK(b["myperms"].asInt() == 4);
        CHECK_FALSE(b.isMember("members"));
        CHECK_FALSE(b.isMember("onthisdevice"));
    }

    SUBCASE("打开与申请传输的应答：失败时不写 id")
    {
        Json::Value ok = parseJson(FileShareProtocol::buildOpenResponse(0, "ok", "share-a1"));
        CHECK(ok["accessid"].asString() == "share-a1");
        Json::Value busy = parseJson(FileShareProtocol::buildTransferResponse(error_code_share_transfer_busy, "busy", ""));
        CHECK(busy["code"].asInt() == error_code_share_transfer_busy);
        CHECK_FALSE(busy.isMember("transferid"));
    }
}

TEST_CASE("FSP4 共享卡片正文的构造、解析与合并")
{
    ShareCardContent card;
    card.m_shareId = 1024;
    card.m_kind = share_kind_folder;
    card.m_name = "项目资料";
    card.m_displayPath = "D:\\项目资料";
    card.m_ownerId = 10001;

    std::string content = FileShareProtocol::buildCardContent(card);
    ShareCardContent back;
    REQUIRE(FileShareProtocol::parseCardContent(content, back));
    CHECK(back.m_shareId == 1024);
    CHECK(back.m_kind == share_kind_folder);
    CHECK(back.m_name == "项目资料");
    CHECK(back.m_displayPath == "D:\\项目资料");
    CHECK(back.m_ownerId == 10001);

    //入库的正文不含任何合并字段
    Json::Value stored = parseJson(content);
    CHECK_FALSE(stored.isMember("sharestate"));
    CHECK_FALSE(stored.isMember("myperms"));

    //合并给成员：带 myperms
    ShareStateView view;
    view.m_state = share_state_available;
    Json::Value forMember = parseJson(FileShareProtocol::augmentCardContent(content, view, true, 5, NULL));
    CHECK(forMember["sharestate"].asInt() == share_state_available);
    CHECK(forMember["sharereason"].asInt() == 0);
    CHECK(forMember["myperms"].asInt() == 5);
    CHECK(forMember["shareid"].asString() == "1024");

    //合并给分享者本人：不带 myperms
    const std::string ownerPath = "D:\\项目资料";
    Json::Value forOwner = parseJson(FileShareProtocol::augmentCardContent(content, view, false, 0, &ownerPath));
    CHECK_FALSE(forOwner.isMember("myperms"));

    //合并后的正文仍然能按卡片正文解析
    REQUIRE(FileShareProtocol::parseCardContent(FileShareProtocol::augmentCardContent(content, view, true, 5, NULL),
                                                back));

    //解析不了的正文原样返回
    CHECK(FileShareProtocol::augmentCardContent("not json", view, true, 5, NULL) == "not json");

    //必填字段缺失时解析失败
    CHECK_FALSE(FileShareProtocol::parseCardContent("{\"shareid\":\"1\",\"kind\":2,\"name\":\"n\"}", back));
    CHECK_FALSE(FileShareProtocol::parseCardContent("{\"shareid\":\"1\",\"kind\":9,\"name\":\"n\",\"ownerid\":\"2\"}", back));
}

TEST_CASE("FSP5 computeShareState 的组合表")
{
    //一份「一切正常」的输入：成员看一个可用的共享
    ShareStateInput base;
    base.m_viewerIsMember = true;
    base.m_ownerOnline = true;
    base.m_ownerOnBoundDevice = true;
    base.m_rootAvailable = true;

    SUBCASE("可用")
    {
        ShareStateView v = FileShareProtocol::computeShareState(base);
        CHECK(v.m_state == share_state_available);
        CHECK(v.m_reason == 0);

        ShareStateInput owner = base;
        owner.m_viewerIsOwner = true;
        owner.m_viewerIsMember = false;
        CHECK(FileShareProtocol::computeShareState(owner).m_state == share_state_available);
    }

    SUBCASE("已取消：五种原因原样带出，对分享者与成员一样")
    {
        const int reasons[] = { share_invalid_owner_canceled, share_invalid_group_admin_canceled,
                                share_invalid_admin_canceled, share_invalid_group_dismissed,
                                share_invalid_owner_left_group };
        for (size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); ++i)
        {
            ShareStateInput in = base;
            in.m_canceled = true;
            in.m_cancelReason = reasons[i];
            ShareStateView v = FileShareProtocol::computeShareState(in);
            CHECK(v.m_state == share_state_invalid);
            CHECK(v.m_reason == reasons[i]);

            in.m_viewerIsOwner = true;
            CHECK(FileShareProtocol::computeShareState(in).m_reason == reasons[i]);
        }
    }

    SUBCASE("看的人已不是成员：仍在会话里为被移出，已不在群里为已离群")
    {
        ShareStateInput removed = base;
        removed.m_viewerIsMember = false;
        ShareStateView v = FileShareProtocol::computeShareState(removed);
        CHECK(v.m_state == share_state_invalid);
        CHECK(v.m_reason == share_invalid_member_removed);

        ShareStateInput left = removed;
        left.m_viewerInConversation = false;
        CHECK(FileShareProtocol::computeShareState(left).m_reason == share_invalid_member_left_group);
    }

    SUBCASE("暂不可用：按功能关闭、不在线、在别的电脑、共享根不存在的顺序取第一个命中的原因")
    {
        ShareStateInput disabled = base;
        disabled.m_featureEnabled = false;
        disabled.m_ownerOnline = false;
        ShareStateView v = FileShareProtocol::computeShareState(disabled);
        CHECK(v.m_state == share_state_unavailable);
        CHECK(v.m_reason == share_unavailable_feature_disabled);

        ShareStateInput offline = base;
        offline.m_ownerOnline = false;
        offline.m_ownerOnBoundDevice = false;
        CHECK(FileShareProtocol::computeShareState(offline).m_reason == share_unavailable_owner_offline);

        ShareStateInput otherDevice = base;
        otherDevice.m_ownerOnBoundDevice = false;
        CHECK(FileShareProtocol::computeShareState(otherDevice).m_reason == share_unavailable_other_device);

        ShareStateInput rootMissing = base;
        rootMissing.m_rootAvailable = false;
        CHECK(FileShareProtocol::computeShareState(rootMissing).m_reason == share_unavailable_root_missing);

        //分享者本人看自己的共享，暂不可用的原因同样成立
        ShareStateInput ownerView = rootMissing;
        ownerView.m_viewerIsOwner = true;
        ownerView.m_viewerIsMember = false;
        CHECK(FileShareProtocol::computeShareState(ownerView).m_reason == share_unavailable_root_missing);
    }

    SUBCASE("已取消优先于一切；被移出优先于暂不可用")
    {
        ShareStateInput in = base;
        in.m_canceled = true;
        in.m_cancelReason = share_invalid_admin_canceled;
        in.m_viewerIsMember = false;
        in.m_featureEnabled = false;
        CHECK(FileShareProtocol::computeShareState(in).m_reason == share_invalid_admin_canceled);

        ShareStateInput in2 = base;
        in2.m_viewerIsMember = false;
        in2.m_ownerOnline = false;
        CHECK(FileShareProtocol::computeShareState(in2).m_reason == share_invalid_member_removed);
    }
}

TEST_CASE("FSP7 2222 的 p2psessionid：只在传输子会话且非空时写出（P2P 方案第 6 期）")
{
    ShareRelayInfo visitor;
    visitor.m_accessId = "share-3-1759370000000";
    visitor.m_transferId = "share-3-1759370000000-t1";
    visitor.m_relayIp = "10.0.0.9";
    visitor.m_relayPort = 20001;
    visitor.m_token = "tok2";
    visitor.m_role = 10;
    visitor.m_lanes = 2;
    visitor.m_openTimeoutMs = 15000;
    visitor.m_idleTimeoutMs = 600000;
    visitor.m_direction = share_transfer_download;
    visitor.m_p2pSessionId = "p2p-10002-10001-1759370000000-7";
    CHECK(FileShareProtocol::buildRelayInfo(visitor) == kWireNotifyRelayVisitorP2P);

    //会话号为空：不写这个字段，与第 6 期之前的帧逐字相同
    visitor.m_p2pSessionId.clear();
    CHECK(FileShareProtocol::buildRelayInfo(visitor) == kWireNotifyRelayVisitor);

    //访问会话本身（没有 transferid）：即使给了会话号也不写
    ShareRelayInfo access;
    access.m_accessId = "share-3-1759370000000";
    access.m_p2pSessionId = "p2p-10002-10001-1759370000000-7";
    Json::Value v = parseJson(FileShareProtocol::buildRelayInfo(access));
    CHECK_FALSE(v.isMember("p2psessionid"));
}

TEST_CASE("FSP6 与客户端共用的线上格式样例：请求能解析，构造出的应答与样例逐字相同")
{
    SUBCASE("请求样例能被服务端解析，字段取值正确")
    {
        ShareCreateRequest create;
        REQUIRE(FileShareProtocol::parseCreateRequest(kWireReqCreate, create));
        CHECK(create.m_clientShareId == "u-1");
        CHECK(create.m_ctype == conversation_group);
        CHECK(create.m_cid == 900001);
        CHECK(create.m_kind == share_kind_folder);
        CHECK(create.m_name == "Projects");
        CHECK(create.m_displayPath == "D:\\Projects");
        CHECK(create.m_includeHidden);
        REQUIRE(create.m_members.size() == 2);
        CHECK(create.m_members[1].m_userId == 10003);
        CHECK(create.m_members[1].m_perms == kSharePermAll);

        int64_t shareId = 0;
        REQUIRE(FileShareProtocol::parseShareIdRequest(kWireReqShareId, shareId));
        CHECK(shareId == 42);
        REQUIRE(FileShareProtocol::parseShareIdRequest(kWireReqShareIdBig, shareId));
        CHECK(shareId == 9007199254740993LL);

        ShareUpdateMembersRequest update;
        REQUIRE(FileShareProtocol::parseUpdateMembersRequest(kWireReqUpdateMembers, update));
        REQUIRE(update.m_add.size() == 1);
        CHECK(update.m_add[0].m_userId == 10004);
        REQUIRE(update.m_remove.size() == 1);
        CHECK(update.m_remove[0] == 10003);
        REQUIRE(update.m_update.size() == 1);
        CHECK(update.m_update[0].m_perms == 0);

        ShareListRequest list;
        REQUIRE(FileShareProtocol::parseListRequest(kWireReqListSharedMe, list));
        CHECK(list.m_scope == share_list_scope_shared_me);
        CHECK(list.m_ctype == conversation_single);
        CHECK(list.m_cid == 10001);
        REQUIRE(FileShareProtocol::parseListRequest(kWireReqListMine, list));
        CHECK(list.m_scope == share_list_scope_mine);
        CHECK(list.m_ctype == 0);

        std::vector<ShareServeReportItem> serve;
        REQUIRE(FileShareProtocol::parseServeReport(kWireReqServeReport, serve));
        REQUIRE(serve.size() == 2);
        CHECK(serve[0].m_available);
        CHECK_FALSE(serve[1].m_available);
        CHECK(serve[1].m_reason == share_unavailable_root_missing);

        std::string accessId;
        int errCode = -1;
        REQUIRE(FileShareProtocol::parseCloseAccessRequest(kWireReqCloseAccess, accessId, errCode));
        CHECK(accessId == "share-3-1759370000000");
        CHECK(errCode == 0);

        ShareTransferRequest transfer;
        REQUIRE(FileShareProtocol::parseTransferRequest(kWireReqTransfer, transfer));
        CHECK(transfer.m_direction == share_transfer_download);
        REQUIRE(transfer.m_items.size() == 2);
        CHECK(transfer.m_items[1].m_isDir);
        CHECK(transfer.m_totalBytes == 2100);
        CHECK(transfer.m_totalFiles == 5);

        std::string transferId;
        int result = -1;
        REQUIRE(FileShareProtocol::parseTransferClose(kWireReqTransferClose, transferId, result, errCode));
        CHECK(transferId == "share-3-1759370000000-t1");
        CHECK(result == share_transfer_result_failed);
        CHECK(errCode == error_code_share_io_fail);

        std::vector<ShareAuditItem> audits;
        REQUIRE(FileShareProtocol::parseAuditReport(kWireReqAuditReport, audits));
        REQUIRE(audits.size() == 1);
        CHECK(audits[0].m_shareId == 42);
        CHECK(audits[0].m_visitorId == 10002);
        CHECK(audits[0].m_action == share_audit_rename);
        CHECK(audits[0].m_toPath == "b.txt");
        CHECK(audits[0].m_clientTime == 1759370000000LL);
    }

    SUBCASE("服务端构造的应答与推送与样例逐字相同")
    {
        CHECK(FileShareProtocol::buildSimpleResponse(0, "ok") == kWireRespSimple);
        CHECK(FileShareProtocol::buildCreateResponse(0, "ok", 42, "u-1", std::vector<int64_t>()) == kWireRespCreateOk);
        std::vector<int64_t> invalid;
        invalid.push_back(10003);
        invalid.push_back(10005);
        CHECK(FileShareProtocol::buildCreateResponse(error_code_share_cross_enterprise, "share create rejected", 0, "u-1",
                                                     invalid) == kWireRespCreateRejected);
        CHECK(FileShareProtocol::buildUpdateMembersResponse(error_code_share_cross_enterprise,
                                                            "share update members rejected",
                                                            std::vector<int64_t>(1, 10003)) == kWireRespUpdateMembersRejected);
        CHECK(FileShareProtocol::buildUpdateMembersResponse(0, "ok", std::vector<int64_t>()) == kWireRespSimple);

        ShareListItem item;
        item.m_shareId = 42;
        item.m_ownerId = 10001;
        item.m_ctype = conversation_group;
        item.m_cid = 900001;
        item.m_kind = share_kind_folder;
        item.m_name = "Projects";
        item.m_displayPath = "D:\\Projects";
        item.m_includeHidden = true;
        item.m_state = share_state_available;
        item.m_reason = 0;
        item.m_createTime = 1759370000000LL;
        item.m_onThisDevice = true;
        ShareMemberSpec member;
        member.m_userId = 10002;
        member.m_perms = share_perm_upload;
        item.m_members.push_back(member);
        CHECK(FileShareProtocol::buildListResponse(0, "ok", share_list_scope_mine,
                                                   std::vector<ShareListItem>(1, item)) == kWireRespListMine);

        ShareListItem shared = item;
        shared.m_includeHidden = false;
        shared.m_state = share_state_unavailable;
        shared.m_reason = share_unavailable_owner_offline;
        shared.m_members.clear();
        shared.m_myPerms = share_perm_upload | share_perm_rename;
        CHECK(FileShareProtocol::buildListResponse(0, "ok", share_list_scope_shared_me,
                                                   std::vector<ShareListItem>(1, shared)) == kWireRespListSharedMe);

        CHECK(FileShareProtocol::buildOpenResponse(0, "ok", "share-3-1759370000000", "Projects") == kWireRespOpen);
        //不带名称的写法（失败应答）不写出 name
        CHECK(FileShareProtocol::buildOpenResponse(0, "ok", "share-3-1759370000000").find("\"name\"") == std::string::npos);
        CHECK(FileShareProtocol::buildTransferResponse(0, "ok", "share-3-1759370000000-t1") == kWireRespTransfer);

        ShareStateView removed;
        removed.m_state = share_state_invalid;
        removed.m_reason = share_invalid_member_removed;
        CHECK(FileShareProtocol::buildStateNotify(42, removed) == kWireNotifyState);
        ShareStateView available;
        CHECK(FileShareProtocol::buildStateNotify(9007199254740993LL, available) == kWireNotifyStateBig);
        CHECK(FileShareProtocol::buildPermsNotify(42, 10002, share_perm_upload | share_perm_rename) == kWireNotifyPerms);

        ShareRelayInfo owner;
        owner.m_accessId = "share-3-1759370000000";
        owner.m_relayIp = "10.0.0.9";
        owner.m_relayPort = 20001;
        owner.m_token = "tok";
        owner.m_role = 9;
        owner.m_lanes = 0;
        owner.m_openTimeoutMs = 15000;
        owner.m_idleTimeoutMs = 600000;
        owner.m_forOwner = true;
        owner.m_shareId = 42;
        owner.m_visitorId = 10002;
        owner.m_visitorName = "Bob";
        owner.m_perms = share_perm_upload;
        owner.m_searchMaxResults = 500;
        owner.m_searchTimeoutMs = 10000;
        CHECK(FileShareProtocol::buildRelayInfo(owner) == kWireNotifyRelayOwner);

        ShareRelayInfo visitor;
        visitor.m_accessId = "share-3-1759370000000";
        visitor.m_transferId = "share-3-1759370000000-t1";
        visitor.m_relayIp = "10.0.0.9";
        visitor.m_relayPort = 20001;
        visitor.m_token = "tok2";
        visitor.m_role = 10;
        visitor.m_lanes = 2;
        visitor.m_openTimeoutMs = 15000;
        visitor.m_idleTimeoutMs = 600000;
        visitor.m_direction = share_transfer_download;
        CHECK(FileShareProtocol::buildRelayInfo(visitor) == kWireNotifyRelayVisitor);

        CHECK(FileShareProtocol::buildAccessEndedNotify("share-3-1759370000000", "", error_code_share_relay_broken)
              == kWireNotifyAccessEnded);

        //入库的卡片不带真实路径；带路径构造出的是 2026-10-04 之前的旧格式
        ShareCardContent card;
        card.m_shareId = 42;
        card.m_kind = share_kind_folder;
        card.m_name = "Projects";
        card.m_ownerId = 10001;
        const std::string stored = FileShareProtocol::buildCardContent(card);
        CHECK(stored == kWireCardStored);
        card.m_displayPath = "D:\\Projects";
        CHECK(FileShareProtocol::buildCardContent(card) == kWireCardStoredLegacy);

        //成员看到的不带路径，旧卡片也一样；分享者本人看到的带路径、不带权限
        ShareStateView cardView;
        const int memberPerms = share_perm_upload | share_perm_rename;
        CHECK(FileShareProtocol::augmentCardContent(stored, cardView, true, memberPerms, NULL) == kWireCardAugmented);
        CHECK(FileShareProtocol::augmentCardContent(kWireCardStoredLegacy, cardView, true, memberPerms, NULL)
              == kWireCardAugmented);
        const std::string ownerPath = "D:\\Projects";
        CHECK(FileShareProtocol::augmentCardContent(stored, cardView, false, 0, &ownerPath) == kWireCardAugmentedOwner);
    }
}

TEST_CASE("FSP8 卡片正文里分享者的真实路径只给分享者本人")
{
    ShareCardContent card;
    card.m_shareId = 77;
    card.m_kind = share_kind_disk;
    card.m_name = u8"张勇的文件共享";
    card.m_ownerId = 10001;

    //入库的正文不带路径
    const std::string stored = FileShareProtocol::buildCardContent(card);
    CHECK_FALSE(parseJson(stored).isMember("displaypath"));
    ShareCardContent back;
    REQUIRE(FileShareProtocol::parseCardContent(stored, back));
    CHECK(back.m_displayPath.empty());
    CHECK(back.m_name == u8"张勇的文件共享");

    ShareStateView view;
    view.m_state = share_state_available;
    const std::string ownerPath = "D:\\";

    //分享者本人：补上真实路径
    Json::Value forOwner = parseJson(FileShareProtocol::augmentCardContent(stored, view, false, 0, &ownerPath));
    CHECK(forOwner["displaypath"].asString() == ownerPath);

    //成员：没有路径
    Json::Value forMember = parseJson(FileShareProtocol::augmentCardContent(stored, view, true, 1, NULL));
    CHECK_FALSE(forMember.isMember("displaypath"));

    //空串视同没有路径，不写出空的 displaypath
    const std::string emptyPath;
    CHECK_FALSE(parseJson(FileShareProtocol::augmentCardContent(stored, view, false, 0, &emptyPath))
                    .isMember("displaypath"));

    //旧卡片（正文里带路径）：成员看到的路径被去掉，分享者本人看到的换成共享记录里的那一份
    card.m_displayPath = "E:\\old";
    const std::string legacy = FileShareProtocol::buildCardContent(card);
    REQUIRE(parseJson(legacy).isMember("displaypath"));
    CHECK_FALSE(parseJson(FileShareProtocol::augmentCardContent(legacy, view, true, 1, NULL)).isMember("displaypath"));
    CHECK(parseJson(FileShareProtocol::augmentCardContent(legacy, view, false, 0, &ownerPath))["displaypath"]
              .asString() == ownerPath);

    //removeCardDisplayPath：只去掉路径，其余字段不变；不带路径或解析不了时原样返回
    Json::Value stripped = parseJson(FileShareProtocol::removeCardDisplayPath(legacy));
    CHECK_FALSE(stripped.isMember("displaypath"));
    CHECK(stripped["name"].asString() == u8"张勇的文件共享");
    CHECK(stripped["shareid"].asString() == "77");
    CHECK(FileShareProtocol::removeCardDisplayPath(stored) == stored);
    CHECK(FileShareProtocol::removeCardDisplayPath("not json") == "not json");
}

TEST_CASE("FSP9 共享列表里的真实路径：「我的共享」带，「共享给我的」不带")
{
    ShareListItem item;
    item.m_shareId = 42;
    item.m_ownerId = 10001;
    item.m_ctype = conversation_group;
    item.m_cid = 900001;
    item.m_kind = share_kind_folder;
    item.m_name = u8"张勇的文件共享";
    item.m_displayPath = "D:\\Projects";

    Json::Value mine = parseJson(FileShareProtocol::buildListResponse(0, "ok", share_list_scope_mine,
                                                                      std::vector<ShareListItem>(1, item)));
    REQUIRE(mine["shares"].size() == 1);
    CHECK(mine["shares"][0]["displaypath"].asString() == "D:\\Projects");
    CHECK(mine["shares"][0]["name"].asString() == u8"张勇的文件共享");

    Json::Value shared = parseJson(FileShareProtocol::buildListResponse(0, "ok", share_list_scope_shared_me,
                                                                        std::vector<ShareListItem>(1, item)));
    REQUIRE(shared["shares"].size() == 1);
    CHECK_FALSE(shared["shares"][0].isMember("displaypath"));
    CHECK(shared["shares"][0]["name"].asString() == u8"张勇的文件共享");
}

TEST_CASE("FSP10 共享名称敏感词检查用到的取值")
{
    //拦截来源写进 t_sensitive_hit_log.f_source，管理后台按它显示「文件共享」；错误码由客户端换成提示文字
    CHECK(chatserver::sensitive_source_fileshare == 15);
    CHECK(error_code_share_name_sensitive == 822);
}
