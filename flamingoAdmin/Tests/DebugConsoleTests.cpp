/**
 *  flamingoAdmin —— 调试控制台数据层（DebugConsole）单元测试。
 *
 *  覆盖 6 组用例：敏感字段掩码、业务 code 提取、环形缓冲淘汰、Begin/End
 *  配对、暂停录制、Kind 结果类别推导。
 *
 *  DebugConsole 是进程级单例：每个用例在开头都先 SetPaused(false) + Clear()
 *  把单例复位到「未暂停、空缓冲」的已知状态，避免用例间相互污染（注意
 *  Clear 不重置序号计数器，故凡断言序号的用例都以「相对差值」方式判断）。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/DebugConsole.h"

namespace flamingoadmin { namespace test {

namespace {

// 测试里用到的固定参数：主机、端口、路径。具体取值与测试结论无关，
// 仅为让 BeginEntry 能拼出 URL；命名成常量避免散落的魔数。
const wchar_t* const kTestHost = L"127.0.0.1";
const int            kTestPort = 8080;
const wchar_t* const kTestPath = L"/admin/debug/test.do";

// 把单例复位到「未暂停、空缓冲」的已知起点。每个用例开头调用。
void ResetConsole()
{
    DebugConsole::Instance().SetPaused(false);
    DebugConsole::Instance().Clear();
}

// 用例①：敏感字段掩码 —— password 被替成 ***、token 被截断，
// 非敏感字段（keyword / hello）原样保留。
void Test_Masking()
{
    BeginCase("DebugConsole：敏感字段掩码");
    ResetConsole();

    // 请求体含一个 12 字符的 token、一个明文 password、一个普通字段。
    const std::string body =
        "{\"token\":\"abcdef123456\","
        "\"password\":\"secret\","
        "\"keyword\":\"hello\"}";
    DebugConsole::Instance().BeginEntry(kTestHost, kTestPort, false,
                                        kTestPath, body);

    std::vector<DebugEntry> snap = DebugConsole::Instance().Snapshot();
    FA_EXPECT_EQ(snap.size(), 1);
    if (snap.size() != 1)
    {
        return;
    }

    const std::string& req = snap[0].m_reqBodyUtf8;

    // 明文密码不得出现。
    FA_EXPECT_TRUE(req.find("secret") == std::string::npos);
    // 完整令牌不得出现（12 字符 > 保留前缀 6，应被截断）。
    FA_EXPECT_TRUE(req.find("abcdef123456") == std::string::npos);
    // 掩码占位文本应出现（password 被替成 ***）。
    FA_EXPECT_TRUE(req.find("***") != std::string::npos);
    // 非敏感字段键名与值应原样保留。
    FA_EXPECT_TRUE(req.find("keyword") != std::string::npos);
    FA_EXPECT_TRUE(req.find("hello") != std::string::npos);
}

// 用例②：业务 code 提取 —— 合法 JSON 取顶层整数 code；非 JSON 响应
// 取哨兵值 kDebugNoBusinessCode。
void Test_BusinessCode()
{
    BeginCase("DebugConsole：业务 code 提取");
    ResetConsole();

    // 第一条：响应是合法 JSON，顶层 code 为 101。
    unsigned long long seqA = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");
    DebugConsole::Instance().EndEntry(seqA, true, 200,
                                      "{\"code\":101,\"msg\":\"fail\"}",
                                      _T(""), 10);

    // 第二条：响应不是 JSON（CSV 风格文本），应取哨兵值。
    unsigned long long seqB = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");
    DebugConsole::Instance().EndEntry(seqB, true, 200,
                                      "hello,world,not json", _T(""), 10);

    std::vector<DebugEntry> snap = DebugConsole::Instance().Snapshot();
    FA_EXPECT_EQ(snap.size(), 2);
    if (snap.size() != 2)
    {
        return;
    }

    // snap[0] 是先入的第一条，snap[1] 是后入的第二条。
    FA_EXPECT_EQ(snap[0].m_businessCode, 101);
    FA_EXPECT_EQ(snap[1].m_businessCode, kDebugNoBusinessCode);
}

// 用例③：环形缓冲 —— 写入 kDebugMaxEntries + 50 条后，缓冲恒为上限条数，
// 最旧 50 条被淘汰，留存记录的序号严格逐 1 递增。
void Test_RingBuffer()
{
    BeginCase("DebugConsole：环形缓冲淘汰");
    ResetConsole();

    // 超出上限 50 条。
    const size_t kOverflowCount = 50;
    const size_t totalWrites = kDebugMaxEntries + kOverflowCount;
    for (size_t i = 0; i < totalWrites; ++i)
    {
        DebugConsole::Instance().BeginEntry(kTestHost, kTestPort, false,
                                            kTestPath, "{}");
    }

    // 缓冲条数应被截至上限。
    FA_EXPECT_EQ(DebugConsole::Instance().Count(), kDebugMaxEntries);

    std::vector<DebugEntry> snap = DebugConsole::Instance().Snapshot();
    FA_EXPECT_EQ(snap.size(), kDebugMaxEntries);
    if (snap.size() != kDebugMaxEntries)
    {
        return;
    }

    // 留存记录的序号应严格逐 1 递增（最旧 50 条被淘汰，余下连续）。
    bool strictlyIncreasing = true;
    for (size_t i = 1; i < snap.size(); ++i)
    {
        if (snap[i].m_seq != snap[i - 1].m_seq + 1)
        {
            strictlyIncreasing = false;
            break;
        }
    }
    FA_EXPECT_TRUE(strictlyIncreasing);

    // 队尾序号 - 队首序号应等于上限 - 1，印证中间无缺口。
    FA_EXPECT_EQ(snap.back().m_seq - snap.front().m_seq,
                 kDebugMaxEntries - 1);
}

// 用例④：Begin/End 配对 —— EndEntry 只回填指定 seq 的记录，其余保持
// Pending；seq 为 0 或不存在时 EndEntry 为安全空操作。
void Test_BeginEndPairing()
{
    BeginCase("DebugConsole：Begin/End 配对");
    ResetConsole();

    unsigned long long seqA = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");
    unsigned long long seqB = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");

    // 仅结束 seqA，seqB 保持进行中。
    DebugConsole::Instance().EndEntry(seqA, true, 200,
                                      "{\"code\":0}", _T(""), 12);

    std::vector<DebugEntry> snap = DebugConsole::Instance().Snapshot();
    FA_EXPECT_EQ(snap.size(), 2);
    if (snap.size() != 2)
    {
        return;
    }

    // snap[0] 为先入的 seqA，应已完成；snap[1] 为 seqB，应仍进行中。
    FA_EXPECT_EQ(snap[0].m_seq, seqA);
    FA_EXPECT_EQ(static_cast<int>(snap[0].m_state),
                 static_cast<int>(DebugEntryState::Done));
    FA_EXPECT_EQ(snap[1].m_seq, seqB);
    FA_EXPECT_EQ(static_cast<int>(snap[1].m_state),
                 static_cast<int>(DebugEntryState::Pending));

    // seq 为 0 与不存在的大 seq：EndEntry 须为安全空操作，不崩溃、不改条数。
    const size_t countBefore = DebugConsole::Instance().Count();
    DebugConsole::Instance().EndEntry(0, true, 200, "{}", _T(""), 1);
    const unsigned long long kNonexistentSeq = 999999ULL;
    DebugConsole::Instance().EndEntry(kNonexistentSeq, true, 200,
                                      "{}", _T(""), 1);
    FA_EXPECT_EQ(DebugConsole::Instance().Count(), countBefore);
}

// 用例⑤：暂停 —— 暂停期间 BeginEntry 返回 0 且不入缓冲；恢复后行为正常。
void Test_Paused()
{
    BeginCase("DebugConsole：暂停录制");
    ResetConsole();

    DebugConsole::Instance().SetPaused(true);
    FA_EXPECT_TRUE(DebugConsole::Instance().IsPaused());

    // 暂停期间：BeginEntry 应返回 0，缓冲条数不增长。
    unsigned long long seqPaused = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");
    FA_EXPECT_EQ(seqPaused, 0);
    FA_EXPECT_EQ(DebugConsole::Instance().Count(), 0);

    // 恢复录制：BeginEntry 应返回非 0 序号，缓冲条数增长。
    DebugConsole::Instance().SetPaused(false);
    FA_EXPECT_FALSE(DebugConsole::Instance().IsPaused());
    unsigned long long seqResumed = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");
    FA_EXPECT_TRUE(seqResumed != 0);
    FA_EXPECT_EQ(DebugConsole::Instance().Count(), 1);
}

// 用例⑥：Kind 推导 —— 构造分别落在 Pending / Success / BusinessFail /
// TransportFail 四类的记录，逐一断言 DebugEntry::Kind()。
void Test_KindDerivation()
{
    BeginCase("DebugConsole：Kind 结果类别推导");
    ResetConsole();

    // 第一条：只 Begin 不 End —— 进行中。
    DebugConsole::Instance().BeginEntry(kTestHost, kTestPort, false,
                                        kTestPath, "{}");

    // 第二条：传输成功且业务 code 为 0 —— 业务成功。
    unsigned long long seqSuccess = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");
    DebugConsole::Instance().EndEntry(seqSuccess, true, 200,
                                      "{\"code\":0}", _T(""), 5);

    // 第三条：传输成功但业务 code 非 0 —— 业务失败。
    unsigned long long seqBizFail = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");
    DebugConsole::Instance().EndEntry(seqBizFail, true, 200,
                                      "{\"code\":500}", _T(""), 5);

    // 第四条：传输失败 —— 未拿到应答。
    unsigned long long seqTransFail = DebugConsole::Instance().BeginEntry(
        kTestHost, kTestPort, false, kTestPath, "{}");
    DebugConsole::Instance().EndEntry(seqTransFail, false, 0,
                                      "", _T("连接超时"), 30000);

    std::vector<DebugEntry> snap = DebugConsole::Instance().Snapshot();
    FA_EXPECT_EQ(snap.size(), 4);
    if (snap.size() != 4)
    {
        return;
    }

    // 入缓冲顺序即上面 4 条的构造顺序。
    FA_EXPECT_EQ(static_cast<int>(snap[0].Kind()),
                 static_cast<int>(DebugEntryKind::Pending));
    FA_EXPECT_EQ(static_cast<int>(snap[1].Kind()),
                 static_cast<int>(DebugEntryKind::Success));
    FA_EXPECT_EQ(static_cast<int>(snap[2].Kind()),
                 static_cast<int>(DebugEntryKind::BusinessFail));
    FA_EXPECT_EQ(static_cast<int>(snap[3].Kind()),
                 static_cast<int>(DebugEntryKind::TransportFail));
}

} // anonymous namespace

void RunAllDebugConsoleTests()
{
    PrintLine(_T("======== flamingoAdmin 调试控制台数据层单元测试 ========\n"));

    Test_Masking();
    Test_BusinessCode();
    Test_RingBuffer();
    Test_BeginEndPairing();
    Test_Paused();
    Test_KindDerivation();
}

}} // namespace flamingoadmin::test
