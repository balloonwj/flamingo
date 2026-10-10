/**
 *  文件共享信令（cmd_share_* 2201~2230）的线上格式样例，fileshare_wire_samples.h
 *
 *  客户端与服务端各有一份编解码（FlamingoFileShare/Engine/ShareProtocolCodec 与 chatserversrc/FileShareProtocol），
 *  本文件是两边共同的对照表：每条样例都是 jsoncpp 紧凑输出（键名按字典序、无空白），只用 ASCII 字符。
 *    - kWireReq*：客户端编出的请求。客户端测试断言编码结果与样例逐字相同，服务端测试断言能解析它；
 *    - kWireResp* / kWireNotify* / kWireCard*：服务端构造的应答与推送。服务端测试断言构造结果与样例逐字相同，
 *      客户端测试断言能解析它。
 *  任何一边改了字段名或取值类型，两边的测试至少有一边会失败。
 *  使用方：tests/test_fileshareprotocol.cpp（FSP6）、flamingoclient/FlamingoFileShare/Tests/ShareProtocolCodecTests.cpp（SPC1~3）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

//==== 请求（客户端 → 服务端）====

//2201 发起共享：群 900001 里共享文件夹 Projects，包含隐藏文件，成员 10002（上传）与 10003（全部权限）
static const char* const kWireReqCreate =
    "{\"cid\":\"900001\",\"clientshareid\":\"u-1\",\"ctype\":2,\"displaypath\":\"D:\\\\Projects\",\"includehidden\":1,"
    "\"kind\":2,\"members\":[{\"perms\":1,\"userid\":\"10002\"},{\"perms\":31,\"userid\":\"10003\"}],\"name\":\"Projects\"}";

//2202 取消 / 2220 打开：只带 shareid
static const char* const kWireReqShareId = "{\"shareid\":\"42\"}";

//2202 / 2220 的超大 shareid（超过 2^53，双精度浮点表示不了）
static const char* const kWireReqShareIdBig = "{\"shareid\":\"9007199254740993\"}";

//2204 调整成员：追加 10004（新建文件夹）、移除 10003、把 10002 的权限改成 0
static const char* const kWireReqUpdateMembers =
    "{\"add\":[{\"perms\":2,\"userid\":\"10004\"}],\"remove\":[\"10003\"],\"shareid\":\"42\","
    "\"update\":[{\"perms\":0,\"userid\":\"10002\"}]}";

//2205 拉取「共享给我的」，只要与 10001 单聊里的
static const char* const kWireReqListSharedMe = "{\"cid\":\"10001\",\"ctype\":1,\"scope\":2}";

//2205 拉取「我的共享」，不按会话筛选
static const char* const kWireReqListMine = "{\"scope\":1}";

//2210 在线提供报告：42 可提供，43 共享根不存在
static const char* const kWireReqServeReport =
    "{\"items\":[{\"available\":1,\"reason\":0,\"shareid\":\"42\"},{\"available\":0,\"reason\":3,\"shareid\":\"43\"}]}";

//2221 结束访问会话
static const char* const kWireReqCloseAccess = "{\"accessid\":\"share-3-1759370000000\",\"reason\":0}";

//2224 申请下载：一个文件与一个目录，共 2100 字节、5 个文件
static const char* const kWireReqTransfer =
    "{\"accessid\":\"share-3-1759370000000\",\"direction\":1,\"items\":[{\"isdir\":0,\"path\":\"docs/a.txt\",\"size\":100},"
    "{\"isdir\":1,\"path\":\"pics\",\"size\":2000}],\"totalbytes\":2100,\"totalfiles\":5}";

//2225 结束传输：失败，原因 811（读写失败）
static const char* const kWireReqTransferClose =
    "{\"errcode\":811,\"result\":1,\"transferid\":\"share-3-1759370000000-t1\"}";

//2230 审计上报：一条重命名
static const char* const kWireReqAuditReport =
    "{\"items\":[{\"accessid\":\"share-3-1759370000000\",\"action\":7,\"clienttime\":1759370000000,\"path\":\"a.txt\","
    "\"result\":0,\"shareid\":\"42\",\"size\":0,\"topath\":\"b.txt\",\"visitorid\":\"10002\"}]}";

//==== 应答与推送（服务端 → 客户端）====

//只有结果码的应答
static const char* const kWireRespSimple = "{\"code\":0,\"msg\":\"ok\"}";

//2201 成功
static const char* const kWireRespCreateOk = "{\"clientshareid\":\"u-1\",\"code\":0,\"msg\":\"ok\",\"shareid\":\"42\"}";

//2201 因跨企业被拒，带不满足条件的成员
static const char* const kWireRespCreateRejected =
    "{\"clientshareid\":\"u-1\",\"code\":821,\"invalidmembers\":[\"10003\",\"10005\"],\"msg\":\"share create rejected\"}";

//2204 因跨企业被拒，带不满足条件的新增成员（第 15 步补充；成功时与 kWireRespSimple 相同）
static const char* const kWireRespUpdateMembersRejected =
    "{\"code\":821,\"invalidmembers\":[\"10003\"],\"msg\":\"share update members rejected\"}";

//2205「我的共享」：带成员与是否位于本机
static const char* const kWireRespListMine =
    "{\"code\":0,\"msg\":\"ok\",\"shares\":[{\"cid\":\"900001\",\"createtime\":1759370000000,\"ctype\":2,"
    "\"displaypath\":\"D:\\\\Projects\",\"includehidden\":1,\"kind\":2,\"members\":[{\"perms\":1,\"userid\":\"10002\"}],"
    "\"name\":\"Projects\",\"onthisdevice\":1,\"ownerid\":\"10001\",\"reason\":0,\"shareid\":\"42\",\"state\":1}]}";

//2205「共享给我的」：带我的权限，暂不可用（分享者不在线）；不带分享者的真实路径（2026-10-04 起）
static const char* const kWireRespListSharedMe =
    "{\"code\":0,\"msg\":\"ok\",\"shares\":[{\"cid\":\"900001\",\"createtime\":1759370000000,\"ctype\":2,"
    "\"includehidden\":0,\"kind\":2,\"myperms\":5,\"name\":\"Projects\","
    "\"ownerid\":\"10001\",\"reason\":1,\"shareid\":\"42\",\"state\":2}]}";

//2220 成功：带共享名称（2026-10-04 起），访问者的浏览窗口用它作标题
static const char* const kWireRespOpen =
    "{\"accessid\":\"share-3-1759370000000\",\"code\":0,\"msg\":\"ok\",\"name\":\"Projects\"}";

//2224 成功
static const char* const kWireRespTransfer = "{\"code\":0,\"msg\":\"ok\",\"transferid\":\"share-3-1759370000000-t1\"}";

//2203 已失效：你已被移出成员
static const char* const kWireNotifyState = "{\"reason\":6,\"shareid\":\"42\",\"state\":3}";

//2203，shareid 超过 2^53
static const char* const kWireNotifyStateBig = "{\"reason\":0,\"shareid\":\"9007199254740993\",\"state\":1}";

//2206 权限变化
static const char* const kWireNotifyPerms = "{\"perms\":5,\"shareid\":\"42\",\"userid\":\"10002\"}";

//2222 推给分享者的那一帧（访问会话本身）
static const char* const kWireNotifyRelayOwner =
    "{\"accessid\":\"share-3-1759370000000\",\"idletimeoutms\":600000,\"lanes\":0,\"opentimeoutms\":15000,\"perms\":1,"
    "\"relayip\":\"10.0.0.9\",\"relayport\":20001,\"role\":9,\"searchmaxresults\":500,\"searchtimeoutms\":10000,"
    "\"shareid\":\"42\",\"token\":\"tok\",\"transferid\":\"\",\"visitorid\":\"10002\",\"visitorname\":\"Bob\"}";

//2222 推给访问者的那一帧（传输子会话，带传输方向）
static const char* const kWireNotifyRelayVisitor =
    "{\"accessid\":\"share-3-1759370000000\",\"direction\":1,\"idletimeoutms\":600000,\"lanes\":2,\"opentimeoutms\":15000,"
    "\"relayip\":\"10.0.0.9\",\"relayport\":20001,\"role\":10,\"token\":\"tok2\",\"transferid\":\"share-3-1759370000000-t1\"}";

//2222 推给访问者的那一帧（传输子会话，服务端为这次传输建了 P2P 会话，P2P 方案第 6 期）
static const char* const kWireNotifyRelayVisitorP2P =
    "{\"accessid\":\"share-3-1759370000000\",\"direction\":1,\"idletimeoutms\":600000,\"lanes\":2,\"opentimeoutms\":15000,"
    "\"p2psessionid\":\"p2p-10002-10001-1759370000000-7\",\"relayip\":\"10.0.0.9\",\"relayport\":20001,\"role\":10,"
    "\"token\":\"tok2\",\"transferid\":\"share-3-1759370000000-t1\"}";

//2223 访问会话结束：中转通道断开
static const char* const kWireNotifyAccessEnded = "{\"accessid\":\"share-3-1759370000000\",\"errcode\":817,\"transferid\":\"\"}";

//共享卡片正文（入库的那份；2026-10-04 起不带分享者的真实路径）
static const char* const kWireCardStored =
    "{\"kind\":2,\"name\":\"Projects\",\"ownerid\":\"10001\",\"shareid\":\"42\"}";

//2026-10-04 之前入库的共享卡片正文：带着分享者的真实路径，下发给分享者以外的人之前要去掉
static const char* const kWireCardStoredLegacy =
    "{\"displaypath\":\"D:\\\\Projects\",\"kind\":2,\"name\":\"Projects\",\"ownerid\":\"10001\",\"shareid\":\"42\"}";

//共享卡片正文合并了成员看到的状态与权限（可用，权限 5）：不带真实路径
static const char* const kWireCardAugmented =
    "{\"kind\":2,\"myperms\":5,\"name\":\"Projects\",\"ownerid\":\"10001\","
    "\"shareid\":\"42\",\"sharereason\":0,\"sharestate\":1}";

//共享卡片正文合并了分享者本人看到的状态（可用）：带真实路径，不带权限
static const char* const kWireCardAugmentedOwner =
    "{\"displaypath\":\"D:\\\\Projects\",\"kind\":2,\"name\":\"Projects\",\"ownerid\":\"10001\","
    "\"shareid\":\"42\",\"sharereason\":0,\"sharestate\":1}";
