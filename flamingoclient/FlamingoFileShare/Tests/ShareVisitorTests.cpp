/**
 *  访问者状态机单元测试（Suite SVS，标签 [sharevisitor]），ShareVisitorTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 13 步的 13b；假传输层记录发出的信令与应用帧，时间由用例给定）：
 *    - SVS1 正常打开：第一次列目录触发 2220；应答、2222、连上中转、sf_hello 依次推进状态，就绪后发出排队的 sf_list；
 *           重复的 sf_hello 不再触发请求；
 *    - SVS2 列目录：more 为真时用同一个请求号带游标续取，取完一次性交给界面；新请求作废旧请求，旧应答忽略；
 *           结果码非 0 时交给界面；
 *    - SVS3 打开失败：2220 回错误码；中转连接失败回 2221（817）；等不到 sf_hello 超时回 2221（816）；
 *           等 2220 应答超时；排队的列目录请求一并按失败交给界面；
 *    - SVS4 空闲回收：就绪后长时间没有请求时发 sf_bye 与 2221、回到空闲；下一次列目录自动重新打开；
 *    - SVS5 会话中途：sf_perms 与 2206 更新权限；中转断开回 2221（817）；2223 结束会话、不再发 2221；
 *           分享者 sf_bye 按其原因结束；2203 暂不可用只转告界面、已失效时结束会话；accessid 对不上的 2222 / 2223 不处理；
 *    - SVS6 界面关闭：就绪时发 sf_bye 与 2221 并关闭中转；打开途中关闭的，迟到的 2220 成功应答立即补发 2221；
 *    - SVS7 写操作（第 2 期第 2 步）：空闲时发起触发打开、就绪后连同排队的列目录一起发出；四种请求的字段；
 *           结果按请求号回报，请求号对不上的忽略；超时回 816；会话失败时排队与已发出的都按失败回报；
 *           界面关闭后不再回报；
 *    - SVS8 搜索（第 2 期第 3 步）：空闲时发起触发打开、就绪后发出；结果分批回报、done 时结束；新搜索先取消旧的
 *           （发 sf_cancel、旧结果不再回报）；CancelSearch 发 sf_cancel；分享者拒绝时带结果码结束；太久没有动静时
 *           发 sf_cancel 并按 816 结束；会话失败时按失败结束；
 *    - SVS10 整棵目录树（第 2 期第 5 步）：发出带 all 与 max 的 sf_search；分批收齐后一次交出（含截断标志）；
 *           分享者拒绝时带结果码交出；新的取代旧的（发 sf_cancel）；超时 816；
 *    - SVS11 单文件共享的「列目录」（第 2 期第 6 步）：发 sf_stat 查共享根本身，结果按只有这一项的列表交出（带大小）；
 *           分享者拒绝时带结果码交出；文件夹共享照旧发 sf_list；
 *    - SVS9 缩略图（第 2 期第 4 步）：空闲时发起触发打开、就绪后发出（带长边）；结果带路径、尺寸与 JPEG；
 *           CancelThumb 发 sf_cancel、迟到的结果忽略；超时回 816；会话失败时按失败回报。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Engine/ShareVisitor.h"
#include "../../Source/net/Protocol.h"

using namespace fileshare;

namespace {

//被访问的共享与访问会话
const int64_t kShareId = 9001;
const char* const kAccessId = "share-7-1759370000000";
const char* const kOtherAccessId = "share-8-1759370000000";

//2222 给出的两个时长（毫秒），取小值让超时用例好写
const int kOpenTimeoutMs = 5000;
const int kIdleTimeoutMs = 60000;

//几个时间点（毫秒）
const int64_t kT0 = 1000;
const int64_t kStep = 100;

//sf_hello 带的权限与之后变化的权限
const int kHelloPerms = share_perm_upload;
const int kNewPerms = share_perm_upload | share_perm_delete;

//假传输层：记录发出的信令与应用帧，seq 从 1 起递增
class FakeTransport : public IShareVisitorTransport
{
public:
    FakeTransport()
        : m_nextSeq(1)
        , m_startRelayOk(true)
        , m_startRelayCount(0)
        , m_closeRelayCount(0)
    {
    }

    virtual uint32_t SendCommand(uint16_t cmd, const std::string& body) override
    {
        m_cmds.push_back(cmd);
        m_bodies.push_back(body);
        return m_nextSeq++;
    }

    virtual bool StartRelay(const ShareRelayParams& relay) override
    {
        ++m_startRelayCount;
        m_lastRelay = relay;
        return m_startRelayOk;
    }

    virtual bool SendFrame(const std::string& frame) override
    {
        ShareControlMessage msg;
        REQUIRE(ShareControlFraming::DecodeFrame(frame.data(), frame.size(), msg));
        m_frames.push_back(msg);
        return true;
    }

    virtual void CloseRelay() override
    {
        ++m_closeRelayCount;
    }

    //某个命令发了几次
    int Count(uint16_t cmd) const
    {
        int n = 0;
        for (size_t i = 0; i < m_cmds.size(); ++i)
        {
            if (m_cmds[i] == cmd)
                ++n;
        }
        return n;
    }

    //最后一次发某个命令的包体
    std::string Last(uint16_t cmd) const
    {
        for (size_t i = m_cmds.size(); i > 0; --i)
        {
            if (m_cmds[i - 1] == cmd)
                return m_bodies[i - 1];
        }
        return std::string();
    }

    //最后一次发出的命令的 seq
    uint32_t LastSeq() const
    {
        return m_nextSeq - 1;
    }

    uint32_t                         m_nextSeq;          //下一个 seq
    bool                             m_startRelayOk;     //StartRelay 的返回值
    int                              m_startRelayCount;  //StartRelay 调用次数
    int                              m_closeRelayCount;  //CloseRelay 调用次数
    ShareRelayParams                 m_lastRelay;        //最近一次 StartRelay 的参数
    std::vector<uint16_t>            m_cmds;             //发出的命令
    std::vector<std::string>         m_bodies;           //发出的包体
    std::vector<ShareControlMessage> m_frames;           //发出的应用帧（已解码）
};

//假观察者：记录状态变化与列目录结果
class FakeObserver : public IShareVisitorObserver
{
public:
    FakeObserver()
        : m_lastState(-1)
        , m_lastErr(-1)
        , m_perms(-1)
        , m_listedCount(0)
        , m_lastCode(-1)
        , m_shareState(-1)
        , m_shareReason(-1)
        , m_treeCount(0)
        , m_treeTruncated(false)
        , m_treeCode(-1)
        , m_thumbWidth(0)
        , m_searchHits(0)
        , m_searchDone(0)
        , m_searchTruncated(false)
        , m_searchCode(-1)
    {
    }

    virtual void OnVisitorStateChanged(int state, int errCode) override
    {
        m_states.push_back(state);
        m_lastState = state;
        m_lastErr = errCode;
    }

    virtual void OnVisitorListed(const std::string& path, int code, const std::vector<ShareEntry>& entries) override
    {
        ++m_listedCount;
        m_lastPath = path;
        m_lastCode = code;
        m_lastEntries = entries;
    }

    virtual void OnVisitorPermsChanged(int perms) override
    {
        m_perms = perms;
    }

    virtual void OnVisitorShareState(int state, int reason) override
    {
        m_shareState = state;
        m_shareReason = reason;
    }

    virtual void OnVisitorTreeResult(int64_t reqId, const std::string& path, const std::vector<ShareEntry>& entries,
                                     bool truncated, int code) override
    {
        (void)path;
        m_treeReqIds.push_back(reqId);
        m_treeCount = entries.size();
        m_treeTruncated = truncated;
        m_treeCode = code;
    }

    virtual void OnVisitorThumbResult(int64_t reqId, const std::string& path, int code, int width, int height,
                                      const std::string& jpeg) override
    {
        (void)height;
        m_thumbReqIds.push_back(reqId);
        m_thumbCodes.push_back(code);
        m_thumbPath = path;
        m_thumbJpeg = jpeg;
        m_thumbWidth = width;
    }

    virtual void OnVisitorSearchResult(int64_t reqId, const std::vector<ShareEntry>& hits, bool done, bool truncated,
                                       int code) override
    {
        m_searchReqIds.push_back(reqId);
        m_searchHits += hits.size();
        if (done)
        {
            ++m_searchDone;
            m_searchTruncated = truncated;
            m_searchCode = code;
        }
    }

    virtual void OnVisitorOpResult(int64_t reqId, int opType, int code, const std::vector<ShareOpItem>& items) override
    {
        m_opReqIds.push_back(reqId);
        m_opTypes.push_back(opType);
        m_opCodes.push_back(code);
        m_opItems = items;
    }

    std::vector<int>        m_states;       //依次经过的状态
    int                     m_lastState;    //最近的状态
    int                     m_lastErr;      //最近一次状态变化带的原因
    int                     m_perms;        //最近一次权限变化
    int                     m_listedCount;  //列目录结果的次数
    std::string             m_lastPath;     //最近一次结果的目录
    int                     m_lastCode;     //最近一次结果的结果码
    std::vector<ShareEntry> m_lastEntries;  //最近一次结果的条目
    int                     m_shareState;   //最近一次 2203 的状态
    int                     m_shareReason;  //最近一次 2203 的原因
    std::vector<int64_t>    m_opReqIds;     //写操作结果的请求号，按回报顺序
    std::vector<int>        m_opTypes;      //写操作结果的帧类型
    std::vector<int>        m_opCodes;      //写操作结果的结果码
    std::vector<ShareOpItem> m_opItems;     //最近一次写操作结果的逐条结果
    std::vector<int64_t>    m_treeReqIds;   //目录树交出的请求号，按顺序
    size_t                  m_treeCount;    //最近一次目录树的条目数
    bool                    m_treeTruncated;//最近一次目录树是否截断
    int                     m_treeCode;     //最近一次目录树的结果码
    std::vector<int64_t>    m_thumbReqIds;  //缩略图回报的请求号，按回报顺序
    std::vector<int>        m_thumbCodes;   //缩略图回报的结果码
    std::string             m_thumbPath;    //最近一次缩略图回报的路径
    std::string             m_thumbJpeg;    //最近一次缩略图回报的数据
    int                     m_thumbWidth;   //最近一次缩略图回报的宽
    std::vector<int64_t>    m_searchReqIds; //搜索回报的请求号，按回报顺序
    size_t                  m_searchHits;   //累计回报的命中条数
    int                     m_searchDone;   //done 为真的回报次数
    bool                    m_searchTruncated; //最近一次结束时是否截断
    int                     m_searchCode;   //最近一次结束时的结果码
};

//推给访问者的 2222
ShareRelayParams VisitorRelay(const char* accessId)
{
    ShareRelayParams p;
    p.m_accessId = accessId;
    p.m_relayIp = "127.0.0.1";
    p.m_relayPort = 25001;
    p.m_token = "tok";
    p.m_role = 10;
    p.m_openTimeoutMs = kOpenTimeoutMs;
    p.m_idleTimeoutMs = kIdleTimeoutMs;
    return p;
}

//分享者的 sf_hello
ShareControlMessage Hello()
{
    ShareControlMessage m;
    m.m_type = sf_hello;
    m.m_version = kShareControlVersion;
    m.m_kind = share_kind_folder;
    m.m_rootName = "Projects";
    m.m_perms = kHelloPerms;
    return m;
}

//一批列目录结果
ShareControlMessage ListResult(int64_t reqId, int code, const char* name, bool more, const char* cursor)
{
    ShareControlMessage m;
    m.m_type = sf_list_result;
    m.m_reqId = reqId;
    m.m_code = code;
    if (name != NULL)
    {
        ShareEntry e;
        e.m_name = name;
        m.m_entries.push_back(e);
    }
    m.m_more = more;
    m.m_cursor = cursor;
    return m;
}

//从空闲开始走到就绪：列根目录触发打开，最后就绪并发出 sf_list
void OpenToReady(CShareVisitor& v, FakeTransport& t, int64_t now)
{
    v.List("", now);
    REQUIRE(v.GetState() == share_visitor_opening);
    REQUIRE(v.OnOpenResponse(t.LastSeq(), error_code_ok, kAccessId, now));
    REQUIRE(v.OnRelayInfo(VisitorRelay(kAccessId), now));
    v.OnRelayConnected(true, now);
    v.OnFrame(Hello(), now);
    REQUIRE(v.GetState() == share_visitor_ready);
}

} // namespace

TEST_CASE("SVS1 正常打开并发出排队的列目录请求", "[sharevisitor]")
{
    FakeTransport t;
    FakeObserver o;
    CShareVisitor v(kShareId, &t, &o);
    CHECK(v.GetState() == share_visitor_idle);

    v.List("", kT0);
    CHECK(v.GetState() == share_visitor_opening);
    REQUIRE(t.Count(cmd_share_open) == 1);
    CHECK(t.Last(cmd_share_open) == ShareProtocolCodec::BuildShareIdRequest(kShareId));
    const uint32_t openSeq = t.LastSeq();

    //别的 seq 的应答不处理
    CHECK_FALSE(v.OnOpenResponse(openSeq + 1, error_code_ok, kAccessId, kT0));
    REQUIRE(v.OnOpenResponse(openSeq, error_code_ok, kAccessId, kT0));
    CHECK(v.GetState() == share_visitor_awaiting_relay);
    CHECK(v.GetAccessId() == kAccessId);

    REQUIRE(v.OnRelayInfo(VisitorRelay(kAccessId), kT0));
    CHECK(v.GetState() == share_visitor_connecting);
    CHECK(t.m_startRelayCount == 1);
    CHECK(t.m_lastRelay.m_token == "tok");

    v.OnRelayConnected(true, kT0);
    CHECK(v.GetState() == share_visitor_waiting_hello);
    CHECK(t.m_frames.empty());

    v.OnFrame(Hello(), kT0 + kStep);
    CHECK(v.GetState() == share_visitor_ready);
    CHECK(v.GetRootName() == "Projects");
    CHECK(v.GetKind() == share_kind_folder);
    CHECK(v.GetPerms() == kHelloPerms);
    REQUIRE(t.m_frames.size() == 1);
    CHECK(t.m_frames[0].m_type == sf_list);
    CHECK(t.m_frames[0].m_path == "");
    CHECK(t.m_frames[0].m_cursor == "");

    //重复的 sf_hello：不再发请求
    v.OnFrame(Hello(), kT0 + kStep);
    CHECK(t.m_frames.size() == 1);

    //状态依次经过打开、等 2222、连接、等握手、就绪
    std::vector<int> expected;
    expected.push_back(share_visitor_opening);
    expected.push_back(share_visitor_awaiting_relay);
    expected.push_back(share_visitor_connecting);
    expected.push_back(share_visitor_waiting_hello);
    expected.push_back(share_visitor_ready);
    CHECK(o.m_states == expected);
}

TEST_CASE("SVS2 列目录按游标续取、新请求作废旧请求", "[sharevisitor]")
{
    FakeTransport t;
    FakeObserver o;
    CShareVisitor v(kShareId, &t, &o);
    OpenToReady(v, t, kT0);
    REQUIRE(t.m_frames.size() == 1);
    const int64_t rootReq = t.m_frames[0].m_reqId;

    //第一批 more 为真：用同一个请求号带游标续取，界面还没有收到结果
    v.OnFrame(ListResult(rootReq, error_code_ok, "a", true, "1"), kT0);
    REQUIRE(t.m_frames.size() == 2);
    CHECK(t.m_frames[1].m_reqId == rootReq);
    CHECK(t.m_frames[1].m_cursor == "1");
    CHECK(o.m_listedCount == 0);

    //最后一批：两批合在一起交给界面
    v.OnFrame(ListResult(rootReq, error_code_ok, "b", false, "2"), kT0);
    REQUIRE(o.m_listedCount == 1);
    CHECK(o.m_lastPath == "");
    CHECK(o.m_lastCode == error_code_ok);
    REQUIRE(o.m_lastEntries.size() == 2);
    CHECK(o.m_lastEntries[1].m_name == "b");

    //新请求作废旧请求：先列 docs，再列 pics；docs 的应答迟到，忽略
    v.List("docs", kT0);
    const int64_t docsReq = t.m_frames.back().m_reqId;
    v.List("pics", kT0);
    const int64_t picsReq = t.m_frames.back().m_reqId;
    CHECK(picsReq != docsReq);
    v.OnFrame(ListResult(docsReq, error_code_ok, "old", false, "1"), kT0);
    CHECK(o.m_listedCount == 1);
    v.OnFrame(ListResult(picsReq, error_code_ok, "new", false, "1"), kT0);
    REQUIRE(o.m_listedCount == 2);
    CHECK(o.m_lastPath == "pics");
    REQUIRE(o.m_lastEntries.size() == 1);
    CHECK(o.m_lastEntries[0].m_name == "new");

    //结果码非 0：交给界面，没有条目
    v.List("gone", kT0);
    v.OnFrame(ListResult(t.m_frames.back().m_reqId, error_code_share_path_not_found, NULL, false, ""), kT0);
    REQUIRE(o.m_listedCount == 3);
    CHECK(o.m_lastPath == "gone");
    CHECK(o.m_lastCode == error_code_share_path_not_found);
    CHECK(o.m_lastEntries.empty());
    CHECK(v.GetState() == share_visitor_ready);
}

TEST_CASE("SVS3 打开失败的几种情况", "[sharevisitor]")
{
    //2220 回错误码：失败，排队的列目录请求按同一个原因交给界面
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        v.List("", kT0);
        REQUIRE(v.OnOpenResponse(t.LastSeq(), error_code_share_unavailable, "", kT0));
        CHECK(v.GetState() == share_visitor_failed);
        CHECK(v.GetErrorCode() == error_code_share_unavailable);
        CHECK(o.m_lastCode == error_code_share_unavailable);
        CHECK(t.Count(cmd_share_close_access) == 0);
    }
    //中转连接失败：回 2221（817）
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        v.List("", kT0);
        v.OnOpenResponse(t.LastSeq(), error_code_ok, kAccessId, kT0);
        v.OnRelayInfo(VisitorRelay(kAccessId), kT0);
        v.OnRelayConnected(false, kT0);
        CHECK(v.GetState() == share_visitor_failed);
        CHECK(v.GetErrorCode() == error_code_share_relay_broken);
        CHECK(t.Last(cmd_share_close_access)
              == ShareProtocolCodec::BuildCloseAccessRequest(kAccessId, error_code_share_relay_broken));
        CHECK(o.m_lastCode == error_code_share_relay_broken);
    }
    //连上之后等不到 sf_hello：到截止时间回 2221（816），关闭中转
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        v.List("", kT0);
        v.OnOpenResponse(t.LastSeq(), error_code_ok, kAccessId, kT0);
        v.OnRelayInfo(VisitorRelay(kAccessId), kT0);
        v.OnRelayConnected(true, kT0);
        v.OnTick(kT0 + kOpenTimeoutMs - 1);
        CHECK(v.GetState() == share_visitor_waiting_hello);
        v.OnTick(kT0 + kOpenTimeoutMs);
        CHECK(v.GetState() == share_visitor_failed);
        CHECK(v.GetErrorCode() == error_code_share_owner_timeout);
        CHECK(t.Last(cmd_share_close_access)
              == ShareProtocolCodec::BuildCloseAccessRequest(kAccessId, error_code_share_owner_timeout));
        CHECK(t.m_closeRelayCount >= 1);
        CHECK(o.m_lastCode == error_code_share_owner_timeout);
    }
    //2220 迟迟没有应答：判失败；之后迟到的成功应答立即补发 2221
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        v.List("", kT0);
        const uint32_t seq = t.LastSeq();
        v.OnTick(kT0 + kShareVisitorSignalTimeoutMs);
        CHECK(v.GetState() == share_visitor_failed);
        CHECK(t.Count(cmd_share_close_access) == 0);
        REQUIRE(v.OnOpenResponse(seq, error_code_ok, kAccessId, kT0 + kShareVisitorSignalTimeoutMs + kStep));
        CHECK(v.GetState() == share_visitor_failed);
        CHECK(t.Last(cmd_share_close_access) == ShareProtocolCodec::BuildCloseAccessRequest(kAccessId, 0));
    }
}

TEST_CASE("SVS4 空闲回收后自动重新打开", "[sharevisitor]")
{
    FakeTransport t;
    FakeObserver o;
    CShareVisitor v(kShareId, &t, &o);
    OpenToReady(v, t, kT0);
    v.OnFrame(ListResult(t.m_frames.back().m_reqId, error_code_ok, "a", false, "1"), kT0);

    //未到空闲时长：保持就绪
    v.OnTick(kT0 + kIdleTimeoutMs - 1);
    CHECK(v.GetState() == share_visitor_ready);

    //到了：发 sf_bye 与 2221，关闭中转，回到空闲（不是失败）
    const size_t framesBefore = t.m_frames.size();
    v.OnTick(kT0 + kIdleTimeoutMs);
    CHECK(v.GetState() == share_visitor_idle);
    CHECK(o.m_lastErr == 0);
    REQUIRE(t.m_frames.size() == framesBefore + 1);
    CHECK(t.m_frames.back().m_type == sf_bye);
    CHECK(t.Last(cmd_share_close_access) == ShareProtocolCodec::BuildCloseAccessRequest(kAccessId, 0));

    //下一次列目录：重新打开
    v.List("docs", kT0 + kIdleTimeoutMs + kStep);
    CHECK(v.GetState() == share_visitor_opening);
    CHECK(t.Count(cmd_share_open) == 2);
}

TEST_CASE("SVS5 会话中途的权限变化与结束", "[sharevisitor]")
{
    //sf_perms 与 2206 都更新权限
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        OpenToReady(v, t, kT0);
        ShareControlMessage perms;
        perms.m_type = sf_perms;
        perms.m_perms = kNewPerms;
        v.OnFrame(perms, kT0);
        CHECK(v.GetPerms() == kNewPerms);
        CHECK(o.m_perms == kNewPerms);
        v.OnPermsNotify(kHelloPerms);
        CHECK(v.GetPerms() == kHelloPerms);
        CHECK(o.m_perms == kHelloPerms);
    }
    //中转断开：失败（817）并回 2221，尚未完成的列目录请求一并失败
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        OpenToReady(v, t, kT0);
        v.OnRelayClosed(kT0);
        CHECK(v.GetState() == share_visitor_failed);
        CHECK(v.GetErrorCode() == error_code_share_relay_broken);
        CHECK(t.Count(cmd_share_close_access) == 1);
        CHECK(o.m_lastCode == error_code_share_relay_broken);
    }
    //2223：accessid 对不上不处理；对上了结束会话，不再发 2221
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        OpenToReady(v, t, kT0);
        CHECK_FALSE(v.OnAccessEnded(kOtherAccessId, error_code_share_not_member));
        CHECK(v.GetState() == share_visitor_ready);
        CHECK(v.OnAccessEnded(kAccessId, error_code_share_not_member));
        CHECK(v.GetState() == share_visitor_failed);
        CHECK(v.GetErrorCode() == error_code_share_not_member);
        CHECK(t.Count(cmd_share_close_access) == 0);
    }
    //分享者发来 sf_bye：按其原因结束；原因为 0 时按分享者不在线处理
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        OpenToReady(v, t, kT0);
        ShareControlMessage bye;
        bye.m_type = sf_bye;
        bye.m_errCode = error_code_share_canceled;
        v.OnFrame(bye, kT0);
        CHECK(v.GetErrorCode() == error_code_share_canceled);
    }
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        OpenToReady(v, t, kT0);
        ShareControlMessage bye;
        bye.m_type = sf_bye;
        v.OnFrame(bye, kT0);
        CHECK(v.GetErrorCode() == error_code_share_unavailable);
    }
    //2203：暂不可用只转告界面；已失效时结束会话（被移出成员按「不是成员」），不发 2221
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        OpenToReady(v, t, kT0);
        v.OnShareStateNotify(share_state_unavailable, share_unavailable_owner_offline);
        CHECK(o.m_shareState == share_state_unavailable);
        CHECK(v.GetState() == share_visitor_ready);
        v.OnShareStateNotify(share_state_invalid, share_invalid_member_removed);
        CHECK(o.m_shareReason == share_invalid_member_removed);
        CHECK(v.GetState() == share_visitor_failed);
        CHECK(v.GetErrorCode() == error_code_share_not_member);
        CHECK(t.Count(cmd_share_close_access) == 0);
    }
    //accessid 对不上的 2222 不处理
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        v.List("", kT0);
        v.OnOpenResponse(t.LastSeq(), error_code_ok, kAccessId, kT0);
        CHECK_FALSE(v.OnRelayInfo(VisitorRelay(kOtherAccessId), kT0));
        CHECK(v.GetState() == share_visitor_awaiting_relay);
        CHECK(t.m_startRelayCount == 0);
    }
}

TEST_CASE("SVS6 界面关闭", "[sharevisitor]")
{
    //就绪时关闭：发 sf_bye 与 2221，关闭中转，回到空闲；之后的帧忽略
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        OpenToReady(v, t, kT0);
        const size_t framesBefore = t.m_frames.size();
        v.Close();
        CHECK(v.GetState() == share_visitor_idle);
        REQUIRE(t.m_frames.size() == framesBefore + 1);
        CHECK(t.m_frames.back().m_type == sf_bye);
        CHECK(t.Last(cmd_share_close_access) == ShareProtocolCodec::BuildCloseAccessRequest(kAccessId, 0));
        CHECK(t.m_closeRelayCount >= 1);
        const int listedBefore = o.m_listedCount;
        v.OnFrame(ListResult(1, error_code_ok, "late", false, "1"), kT0);
        CHECK(o.m_listedCount == listedBefore);
    }
    //打开途中（2220 已发、尚无应答）关闭：不发 2221；迟到的成功应答立即补发 2221
    {
        FakeTransport t;
        FakeObserver o;
        CShareVisitor v(kShareId, &t, &o);
        v.List("", kT0);
        const uint32_t seq = t.LastSeq();
        v.Close();
        CHECK(t.Count(cmd_share_close_access) == 0);
        REQUIRE(v.OnOpenResponse(seq, error_code_ok, kAccessId, kT0));
        CHECK(v.GetState() == share_visitor_idle);
        CHECK(t.Last(cmd_share_close_access) == ShareProtocolCodec::BuildCloseAccessRequest(kAccessId, 0));
    }
}

TEST_CASE("SVS7 写操作的排队、发出、结果与失败", "[sharevisitor]")
{
    FakeTransport t;
    FakeObserver o;
    CShareVisitor v(kShareId, &t, &o);

    //空闲时发起写操作：触发打开，就绪之前不发帧
    const int64_t mk = v.MakeDirectory("docs/new", kT0);
    CHECK(mk > 0);
    CHECK(v.GetState() == share_visitor_opening);
    CHECK(t.Count(cmd_share_open) == 1);
    CHECK(v.GetPendingOpCount() == 1);
    REQUIRE(v.OnOpenResponse(t.LastSeq(), error_code_ok, kAccessId, kT0));
    REQUIRE(v.OnRelayInfo(VisitorRelay(kAccessId), kT0));
    v.OnRelayConnected(true, kT0);
    CHECK(t.m_frames.empty());
    v.OnFrame(Hello(), kT0);
    REQUIRE(t.m_frames.size() == 1);
    CHECK(t.m_frames[0].m_type == sf_mkdir);
    CHECK(t.m_frames[0].m_reqId == mk);
    CHECK(t.m_frames[0].m_path == "docs/new");

    //就绪后的三种请求立即发出，字段正确
    const int64_t rn = v.Rename("docs/a.txt", "b.txt", kT0);
    std::vector<std::string> paths;
    paths.push_back("a");
    paths.push_back("b");
    const int64_t del = v.Delete(paths, kT0);
    const int64_t mv = v.Move(paths, "dst", kT0);
    REQUIRE(t.m_frames.size() == 4);
    CHECK(t.m_frames[1].m_type == sf_rename);
    CHECK(t.m_frames[1].m_newName == "b.txt");
    CHECK(t.m_frames[2].m_type == sf_delete);
    CHECK(t.m_frames[2].m_paths == paths);
    CHECK(t.m_frames[3].m_type == sf_move);
    CHECK(t.m_frames[3].m_toDir == "dst");
    CHECK(v.GetPendingOpCount() == 4);

    //结果按请求号回报；对不上的忽略
    ShareControlMessage res;
    res.m_type = sf_op_result;
    res.m_reqId = rn + del + mv + mk;   //不存在的请求号
    v.OnFrame(res, kT0);
    CHECK(o.m_opReqIds.empty());
    res.m_reqId = del;
    res.m_code = error_code_share_recycle_unavailable;
    ShareOpItem item;
    item.m_path = "a";
    item.m_code = error_code_share_recycle_unavailable;
    res.m_items.push_back(item);
    v.OnFrame(res, kT0);
    REQUIRE(o.m_opReqIds.size() == 1);
    CHECK(o.m_opReqIds[0] == del);
    CHECK(o.m_opTypes[0] == sf_delete);
    CHECK(o.m_opCodes[0] == error_code_share_recycle_unavailable);
    REQUIRE(o.m_opItems.size() == 1);
    CHECK(o.m_opItems[0].m_path == "a");
    CHECK(v.GetPendingOpCount() == 3);

    //新建文件夹与移动等不到结果：超时回 816，会话仍然就绪
    res.m_reqId = rn;
    res.m_code = error_code_ok;
    res.m_items.clear();
    v.OnFrame(res, kT0 + kShareVisitorRequestTimeoutMs - kStep);
    v.OnTick(kT0 + kShareVisitorRequestTimeoutMs);
    CHECK(v.GetState() == share_visitor_ready);
    CHECK(o.m_opCodes.back() == error_code_share_owner_timeout);
    CHECK(v.GetPendingOpCount() == 0);
    CHECK(o.m_opReqIds.size() == 4);

    //会话失败：排队的写操作按失败回报
    const int64_t mk2 = v.MakeDirectory("x", kT0 + kShareVisitorRequestTimeoutMs);
    v.OnRelayClosed(kT0 + kShareVisitorRequestTimeoutMs);
    CHECK(v.GetState() == share_visitor_failed);
    CHECK(o.m_opReqIds.back() == mk2);
    CHECK(o.m_opCodes.back() == error_code_share_relay_broken);
    CHECK(v.GetPendingOpCount() == 0);

    //失败之后再发起：重新打开
    v.MakeDirectory("y", kT0 + kShareVisitorRequestTimeoutMs + kStep);
    CHECK(v.GetState() == share_visitor_opening);
    //界面关闭：未完成的写操作不再回报
    const size_t reported = o.m_opReqIds.size();
    v.Close();
    CHECK(v.GetPendingOpCount() == 0);
    CHECK(o.m_opReqIds.size() == reported);
}

namespace {

//一批搜索结果
ShareControlMessage SearchResult(int64_t reqId, const char* name, bool done, bool truncated, int code)
{
    ShareControlMessage m;
    m.m_type = sf_search_result;
    m.m_reqId = reqId;
    if (name != NULL)
    {
        ShareEntry e;
        e.m_name = name;
        m.m_entries.push_back(e);
    }
    m.m_done = done;
    m.m_truncated = truncated;
    m.m_code = code;
    return m;
}

//发出的最后一个某类型的应用帧；没有时 m_type 为 sf_invalid
ShareControlMessage LastFrame(const FakeTransport& t, int type)
{
    for (size_t i = t.m_frames.size(); i > 0; --i)
    {
        if (t.m_frames[i - 1].m_type == type)
            return t.m_frames[i - 1];
    }
    return ShareControlMessage();
}

} // namespace

TEST_CASE("SVS8 搜索的排队、分批、取消、超时与失败", "[sharevisitor]")
{
    FakeTransport t;
    FakeObserver o;
    CShareVisitor v(kShareId, &t, &o);

    //空闲时发起：触发打开，就绪后发出
    const int64_t s1 = v.Search("docs", "plan", kT0);
    CHECK(v.GetState() == share_visitor_opening);
    CHECK(v.GetSearchReqId() == s1);
    REQUIRE(v.OnOpenResponse(t.LastSeq(), error_code_ok, kAccessId, kT0));
    REQUIRE(v.OnRelayInfo(VisitorRelay(kAccessId), kT0));
    v.OnRelayConnected(true, kT0);
    v.OnFrame(Hello(), kT0);
    ShareControlMessage sent = LastFrame(t, sf_search);
    REQUIRE(sent.m_type == sf_search);
    CHECK(sent.m_reqId == s1);
    CHECK(sent.m_path == "docs");
    CHECK(sent.m_keyword == "plan");

    //分批回报，done 时结束
    v.OnFrame(SearchResult(s1, "docs/plan-a.txt", false, false, 0), kT0);
    v.OnFrame(SearchResult(s1, "docs/sub/plan-b.txt", true, true, 0), kT0);
    REQUIRE(o.m_searchReqIds.size() == 2);
    CHECK(o.m_searchHits == 2);
    CHECK(o.m_searchDone == 1);
    CHECK(o.m_searchTruncated);
    CHECK(o.m_searchCode == 0);
    CHECK(v.GetSearchReqId() == 0);
    //结束之后同一请求号的帧忽略
    v.OnFrame(SearchResult(s1, "late", true, false, 0), kT0);
    CHECK(o.m_searchReqIds.size() == 2);

    //新搜索先取消旧的：发 sf_cancel，旧结果不再回报
    const int64_t s2 = v.Search("", "a", kT0);
    const int64_t s3 = v.Search("", "b", kT0);
    ShareControlMessage cancel = LastFrame(t, sf_cancel);
    REQUIRE(cancel.m_type == sf_cancel);
    CHECK(cancel.m_reqId == s2);
    v.OnFrame(SearchResult(s2, "old", true, false, 0), kT0);
    CHECK(o.m_searchReqIds.size() == 2);

    //分享者拒绝：带结果码结束
    v.OnFrame(SearchResult(s3, NULL, true, false, error_code_share_path_not_found), kT0);
    CHECK(o.m_searchDone == 2);
    CHECK(o.m_searchCode == error_code_share_path_not_found);

    //CancelSearch：发 sf_cancel，不回报
    const int64_t s4 = v.Search("", "c", kT0);
    v.CancelSearch();
    CHECK(LastFrame(t, sf_cancel).m_reqId == s4);
    CHECK(v.GetSearchReqId() == 0);
    CHECK(o.m_searchDone == 2);

    //太久没有动静：到期之前保持就绪，到期时发 sf_cancel 并按 816 结束
    const int64_t s5 = v.Search("", "d", kT0);
    v.OnTick(kT0 + kShareVisitorRequestTimeoutMs - kStep);
    CHECK(v.GetState() == share_visitor_ready);
    v.OnTick(kT0 + kShareVisitorRequestTimeoutMs);
    CHECK(LastFrame(t, sf_cancel).m_reqId == s5);
    CHECK(o.m_searchDone == 3);
    CHECK(o.m_searchCode == error_code_share_owner_timeout);

    //会话失败：按失败结束
    v.Search("", "e", kT0 + kShareVisitorRequestTimeoutMs);
    v.OnRelayClosed(kT0 + kShareVisitorRequestTimeoutMs);
    CHECK(o.m_searchDone == 4);
    CHECK(o.m_searchCode == error_code_share_relay_broken);
    CHECK(v.GetSearchReqId() == 0);
}

TEST_CASE("SVS9 缩略图请求", "[sharevisitor]")
{
    const int kEdge = 96;
    const int kWidth = 96;
    const int kHeight = 48;
    FakeTransport t;
    FakeObserver o;
    CShareVisitor v(kShareId, &t, &o);

    //空闲时发起：触发打开，就绪后发出
    const int64_t a = v.RequestThumb("pics/a.png", kEdge, kT0);
    CHECK(v.GetState() == share_visitor_opening);
    OpenToReady(v, t, kT0);
    ShareControlMessage sent = LastFrame(t, sf_thumb);
    REQUIRE(sent.m_type == sf_thumb);
    CHECK(sent.m_reqId == a);
    CHECK(sent.m_path == "pics/a.png");
    CHECK(sent.m_maxEdge == kEdge);

    //结果：带路径、尺寸与 JPEG
    ShareControlMessage result;
    result.m_type = sf_thumb_result;
    result.m_reqId = a;
    result.m_width = kWidth;
    result.m_height = kHeight;
    result.m_binary = "JPEGDATA";
    v.OnFrame(result, kT0);
    REQUIRE(o.m_thumbReqIds.size() == 1);
    CHECK(o.m_thumbCodes[0] == 0);
    CHECK(o.m_thumbPath == "pics/a.png");
    CHECK(o.m_thumbJpeg == "JPEGDATA");
    CHECK(o.m_thumbWidth == kWidth);
    CHECK(v.GetPendingThumbCount() == 0);

    //CancelThumb：发 sf_cancel，迟到的结果忽略
    const int64_t b = v.RequestThumb("pics/b.png", kEdge, kT0);
    v.CancelThumb(b);
    CHECK(LastFrame(t, sf_cancel).m_reqId == b);
    result.m_reqId = b;
    v.OnFrame(result, kT0);
    CHECK(o.m_thumbReqIds.size() == 1);

    //超时回 816
    const int64_t c = v.RequestThumb("pics/c.png", kEdge, kT0);
    v.OnTick(kT0 + kShareVisitorRequestTimeoutMs);
    REQUIRE(o.m_thumbReqIds.size() == 2);
    CHECK(o.m_thumbReqIds[1] == c);
    CHECK(o.m_thumbCodes[1] == error_code_share_owner_timeout);

    //会话失败：按失败回报
    const int64_t d = v.RequestThumb("pics/d.png", kEdge, kT0 + kShareVisitorRequestTimeoutMs);
    v.OnRelayClosed(kT0 + kShareVisitorRequestTimeoutMs);
    REQUIRE(o.m_thumbReqIds.size() == 3);
    CHECK(o.m_thumbReqIds[2] == d);
    CHECK(o.m_thumbCodes[2] == error_code_share_relay_broken);
    CHECK(v.GetPendingThumbCount() == 0);
}

TEST_CASE("SVS10 整棵目录树", "[sharevisitor]")
{
    FakeTransport t;
    FakeObserver o;
    CShareVisitor v(kShareId, &t, &o);
    OpenToReady(v, t, kT0);

    const int64_t a = v.ListTree("docs", 0, kT0);
    ShareControlMessage sent = LastFrame(t, sf_search);
    REQUIRE(sent.m_type == sf_search);
    CHECK(sent.m_reqId == a);
    CHECK(sent.m_matchAll);
    CHECK(sent.m_maxItems == kShareTreeMaxItems + 1);
    CHECK(sent.m_path == "docs");
    //分批收齐后一次交出
    v.OnFrame(SearchResult(a, "docs/a.txt", false, false, 0), kT0);
    CHECK(o.m_treeReqIds.empty());
    v.OnFrame(SearchResult(a, "docs/sub", true, true, 0), kT0);
    REQUIRE(o.m_treeReqIds.size() == 1);
    CHECK(o.m_treeCount == 2);
    CHECK(o.m_treeTruncated);
    CHECK(o.m_treeCode == 0);
    CHECK(v.GetTreeReqId() == 0);
    //目录树的结果不当作搜索结果
    CHECK(o.m_searchReqIds.empty());

    //分享者拒绝
    const int64_t b = v.ListTree("nope", 0, kT0);
    v.OnFrame(SearchResult(b, NULL, true, false, error_code_share_path_not_found), kT0);
    CHECK(o.m_treeCode == error_code_share_path_not_found);
    CHECK(o.m_treeCount == 0);

    //新的取代旧的
    const int64_t c = v.ListTree("x", 0, kT0);
    v.ListTree("y", 0, kT0);
    CHECK(LastFrame(t, sf_cancel).m_reqId == c);

    //超时
    v.OnTick(kT0 + kShareVisitorRequestTimeoutMs);
    CHECK(o.m_treeCode == error_code_share_owner_timeout);
    CHECK(v.GetTreeReqId() == 0);
}

namespace {

//SVS11：单文件共享里那个文件的字节数
const int64_t kSvsFileBytes = 1048579;

} // namespace

TEST_CASE("SVS11 单文件共享改用 sf_stat 取那一项", "[sharevisitor]")
{
    FakeTransport t;
    FakeObserver o;
    CShareVisitor v(kShareId, &t, &o);
    v.List("", kT0);
    REQUIRE(v.OnOpenResponse(t.LastSeq(), error_code_ok, kAccessId, kT0));
    REQUIRE(v.OnRelayInfo(VisitorRelay(kAccessId), kT0));
    v.OnRelayConnected(true, kT0);
    ShareControlMessage hello = Hello();
    hello.m_kind = share_kind_file;
    hello.m_rootName = "report.bin";
    v.OnFrame(hello, kT0);
    REQUIRE(v.GetState() == share_visitor_ready);

    //排队的列目录在就绪时发出：发的是 sf_stat，不是 sf_list
    const ShareControlMessage sent = LastFrame(t, sf_stat);
    REQUIRE(sent.m_type == sf_stat);
    CHECK(sent.m_path.empty());
    CHECK(LastFrame(t, sf_list).m_type == sf_invalid);

    ShareControlMessage result;
    result.m_type = sf_stat_result;
    result.m_reqId = sent.m_reqId;
    ShareEntry file;
    file.m_name = "report.bin";
    file.m_size = kSvsFileBytes;
    result.m_entries.push_back(file);
    v.OnFrame(result, kT0);
    REQUIRE(o.m_listedCount == 1);
    CHECK(o.m_lastCode == error_code_ok);
    REQUIRE(o.m_lastEntries.size() == 1);
    CHECK(o.m_lastEntries[0].m_name == "report.bin");
    CHECK(o.m_lastEntries[0].m_size == kSvsFileBytes);

    //分享者拒绝（例如文件已不存在）：带结果码交出、没有条目
    v.List("", kT0);
    const ShareControlMessage again = LastFrame(t, sf_stat);
    ShareControlMessage refused;
    refused.m_type = sf_stat_result;
    refused.m_reqId = again.m_reqId;
    refused.m_code = error_code_share_root_missing;
    v.OnFrame(refused, kT0);
    CHECK(o.m_listedCount == 2);
    CHECK(o.m_lastCode == error_code_share_root_missing);
    CHECK(o.m_lastEntries.empty());
    //迟到的、请求号对不上的结果不处理
    v.OnFrame(result, kT0);
    CHECK(o.m_listedCount == 2);
}
