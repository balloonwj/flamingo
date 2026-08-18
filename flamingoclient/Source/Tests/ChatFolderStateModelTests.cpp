/**
 *  单聊文件夹气泡「状态 → 可用操作」映射单元测试（Suite CFOLD），
 *  ChatFolderStateModelTests.cpp
 *
 *  文件夹传输要走完「邀请 → 应答 → 建目录 → 传字节 → 收尾」一整条链路，中途还有失败、
 *  重试续传、保留期过期、任一端取消这些岔路，而<u>同一个状态两端看到的按钮并不一样</u>。
 *  本套件把每个状态在 fromMe 两侧的按钮数、动作序列、进度条有无逐条锁死：
 *    CFOLD1  邀请态：接收端给「接收 / 另存为 / 拒绝」（接收在左、另存为居中、拒绝在右），
 *            发送端只给「取消」（撤回邀请）；
 *    CFOLD2  传输中：两端都只给「取消」，且画进度条；
 *    CFOLD3  接收端建目录中：两端都只给「取消」，但<u>不</u>画进度条（还没开始传字节）；
 *    CFOLD4  传输失败：发送端给「重试」，接收端无操作，进度条停在中断处；
 *    CFOLD5  失败且已过保留期：发送端给「重新发送」代替「重试」；
 *    CFOLD6  用户取消：发送端给「重新发送」而<u>不</u>给「重试」——取消可能来自接收端，
 *            直接续传会绕过对方刚作出的决定；
 *    CFOLD7  完成态：两端各给一个「打开文件夹」，不画进度条；
 *    CFOLD8  会话在开传之前就作废的四种状态（拒绝 / 撤回邀请 / 对方离线 / 超时）两端
 *            都没有操作，也不画进度条；
 *    CFOLD9  重试可用性：仅「失败态 + 发送端 + 保留期内」为真，保留期外与过期态为假；
 *    CFOLD10 未知状态按无操作保守处理，不越界写 out[]；
 *    CFOLD11 每个状态的按钮数与实际写入的动作数一致，且不超过上限、动作 id 合法；
 *    CFOLD12 每个失败原因错误码都能拿到非空、含中文、且互不相同的文案；
 *    CFOLD13 未知错误码走兜底文案，兜底文案与任何一条具体原因都不同；
 *    CFOLD14 状态取值与协议 folder_transfer_state 逐值一致，本端新增状态不与协议撞号；
 *    CFOLD15 按钮数上限常量与实际最大按钮数相等（改了动作序列却漏改上限时在此报警）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-13
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonChatWnd/ChatFolderStateModel.h"
#include "../net/Protocol.h"   // enum folder_transfer_state 与 error_code_folder_*（取值须与气泡侧一致）

#include <string>

using namespace balloonchatwnd;

//两端的可读别名：fromMe 为真表示这条气泡属于发起传输的一方。
static const bool kFromSender   = true;
static const bool kFromReceiver = false;

//保留期内 / 保留期外，供 FolderRetryEnabled 的用例读起来贴近业务。
static const bool kWithinWindow = true;
static const bool kWindowPassed = false;

//本模块认得的全部状态，CFOLD11 与 CFOLD14 都要逐个走一遍。
static const int kAllFolderStates[] =
{
    kFolderInviting, kFolderTransferring, kFolderRejected, kFolderInviteCanceled,
    kFolderPeerOffline, kFolderTimeout, kFolderFailed, kFolderCompleted,
    kFolderPreparing, kFolderExpired, kFolderUserCanceled
};

//文件夹传输专用的全部失败原因错误码（net/Protocol.h 的 606~620）。
static const int kAllFolderErrorCodes[] =
{
    error_code_folder_rejected, error_code_folder_peer_offline,
    error_code_folder_invite_timeout, error_code_folder_too_many_files,
    error_code_folder_too_large, error_code_folder_mkdir_fail,
    error_code_folder_disk_full, error_code_folder_write_fail,
    error_code_folder_read_fail, error_code_folder_verify_fail,
    error_code_folder_relay_broken, error_code_folder_canceled,
    error_code_folder_session_expired, error_code_folder_server_busy,
    error_code_folder_busy
};

//判断一段文案里是否出现了非 ASCII 字符。界面文案要求是中文，纯 ASCII 的返回值
//（英文措辞、或误把日志串拿来当文案）在这里就会被拦下。
static bool ContainsNonAscii(const std::wstring& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] > 127)
            return true;
    }
    return false;
}

TEST_CASE("CFOLD1 邀请态：接收端给「接收 / 另存为 / 拒绝」，发送端只能撤回")
{
    //接收端要当场决定收不收，且「收到哪里」分成两条路：「接收」直接落到本机默认的
    //文件夹接收目录、不打断用户，「另存为」才弹选目录框由用户挑位置（2026-08-14 按
    //实测反馈从原先的「接收 / 拒绝」两个改成三个）。顺序固定：主动作「接收」在左，
    //「另存为」居中，否定动作「拒绝」在右。
    REQUIRE(FolderStateButtonCount(kFolderInviting, kFromReceiver) == 3);

    int recvActs[kMaxChatFolderActions] = { -1, -1, -1 };
    REQUIRE(FolderButtonActions(kFolderInviting, kFromReceiver, recvActs) == 3);
    CHECK(recvActs[0] == kChatFolderAccept);
    CHECK(recvActs[1] == kChatFolderSaveAs);
    CHECK(recvActs[2] == kChatFolderReject);

    //发送端此刻只是在等对方应答，唯一能做的是撤回邀请
    REQUIRE(FolderStateButtonCount(kFolderInviting, kFromSender) == 1);

    int sendActs[kMaxChatFolderActions] = { -1, -1, -1 };
    REQUIRE(FolderButtonActions(kFolderInviting, kFromSender, sendActs) == 1);
    CHECK(sendActs[0] == kChatFolderCancel);
    //后两格不该被写入
    CHECK(sendActs[1] == -1);
    CHECK(sendActs[2] == -1);

    //还没有一个字节传出去，画进度条只会让人误以为在传
    CHECK(FolderStateHasProgress(kFolderInviting) == false);
}

TEST_CASE("CFOLD2 传输中：两端都只给「取消」并画进度条")
{
    int acts[kMaxChatFolderActions] = { -1, -1, -1 };

    REQUIRE(FolderStateButtonCount(kFolderTransferring, kFromSender) == 1);
    REQUIRE(FolderButtonActions(kFolderTransferring, kFromSender, acts) == 1);
    CHECK(acts[0] == kChatFolderCancel);

    //传输中任一端都可以中止本次传输，故接收端的按钮与发送端相同
    REQUIRE(FolderStateButtonCount(kFolderTransferring, kFromReceiver) == 1);
    REQUIRE(FolderButtonActions(kFolderTransferring, kFromReceiver, acts) == 1);
    CHECK(acts[0] == kChatFolderCancel);

    CHECK(FolderStateHasProgress(kFolderTransferring));
}

TEST_CASE("CFOLD3 建目录中：两端都只给「取消」，但不画进度条")
{
    int acts[kMaxChatFolderActions] = { -1, -1, -1 };

    REQUIRE(FolderStateButtonCount(kFolderPreparing, kFromSender) == 1);
    REQUIRE(FolderButtonActions(kFolderPreparing, kFromSender, acts) == 1);
    CHECK(acts[0] == kChatFolderCancel);

    REQUIRE(FolderStateButtonCount(kFolderPreparing, kFromReceiver) == 1);
    REQUIRE(FolderButtonActions(kFolderPreparing, kFromReceiver, acts) == 1);
    CHECK(acts[0] == kChatFolderCancel);

    //接收端正在逐级建目录，字节一个都还没传，进度条无从谈起
    CHECK(FolderStateHasProgress(kFolderPreparing) == false);
}

TEST_CASE("CFOLD4 失败态：只有发送端能重试，进度条停在中断处")
{
    REQUIRE(FolderStateButtonCount(kFolderFailed, kFromSender) == 1);

    int sendActs[kMaxChatFolderActions] = { -1, -1, -1 };
    REQUIRE(FolderButtonActions(kFolderFailed, kFromSender, sendActs) == 1);
    CHECK(sendActs[0] == kChatFolderRetry);

    //重试由发送端发起，接收端全程只是看到气泡从失败变回传输中，没有可点的东西
    REQUIRE(FolderStateButtonCount(kFolderFailed, kFromReceiver) == 0);

    int recvActs[kMaxChatFolderActions] = { -1, -1, -1 };
    REQUIRE(FolderButtonActions(kFolderFailed, kFromReceiver, recvActs) == 0);
    CHECK(recvActs[0] == -1);

    //进度停在中断处，用户据此能看出传到哪儿断的
    CHECK(FolderStateHasProgress(kFolderFailed));
}

TEST_CASE("CFOLD5 已过保留期：「重新发送」代替「重试」")
{
    REQUIRE(FolderStateButtonCount(kFolderExpired, kFromSender) == 1);

    int sendActs[kMaxChatFolderActions] = { -1, -1, -1 };
    REQUIRE(FolderButtonActions(kFolderExpired, kFromSender, sendActs) == 1);
    //服务端已销毁会话、续传位点作废，只能从零发起一次完整传输
    CHECK(sendActs[0] == kChatFolderResend);
    CHECK(sendActs[0] != kChatFolderRetry);

    REQUIRE(FolderStateButtonCount(kFolderExpired, kFromReceiver) == 0);

    int recvActs[kMaxChatFolderActions] = { -1, -1, -1 };
    REQUIRE(FolderButtonActions(kFolderExpired, kFromReceiver, recvActs) == 0);
    CHECK(recvActs[0] == -1);

    CHECK(FolderStateHasProgress(kFolderExpired));
}

TEST_CASE("CFOLD6 用户取消：给「重新发送」而不给「重试」")
{
    REQUIRE(FolderStateButtonCount(kFolderUserCanceled, kFromSender) == 1);

    int sendActs[kMaxChatFolderActions] = { -1, -1, -1 };
    REQUIRE(FolderButtonActions(kFolderUserCanceled, kFromSender, sendActs) == 1);
    //取消可能来自接收端，直接续传会绕过对方刚刚作出的决定；重新发送会再征求一次同意
    CHECK(sendActs[0] == kChatFolderResend);
    CHECK(sendActs[0] != kChatFolderRetry);

    REQUIRE(FolderStateButtonCount(kFolderUserCanceled, kFromReceiver) == 0);

    //取消态不画进度条：这一笔已由用户自己作废，"传到哪儿了"不再有参考价值；
    //且取消多半发生在刚开始，进度条停在 0% 上看着像出了故障（2026-08-14 实测反馈）
    CHECK(FolderStateHasProgress(kFolderUserCanceled) == false);
}

TEST_CASE("CFOLD7 完成态：两端各给一个「打开文件夹」")
{
    int acts[kMaxChatFolderActions] = { -1, -1, -1 };

    //动作 id 两端相同：接收端开落地目录、发送端开源目录，开哪个由宿主按 fromMe 决定
    REQUIRE(FolderStateButtonCount(kFolderCompleted, kFromSender) == 1);
    REQUIRE(FolderButtonActions(kFolderCompleted, kFromSender, acts) == 1);
    CHECK(acts[0] == kChatFolderOpenFolder);

    REQUIRE(FolderStateButtonCount(kFolderCompleted, kFromReceiver) == 1);
    REQUIRE(FolderButtonActions(kFolderCompleted, kFromReceiver, acts) == 1);
    CHECK(acts[0] == kChatFolderOpenFolder);

    //已成定局，不必再画进度条
    CHECK(FolderStateHasProgress(kFolderCompleted) == false);
}

TEST_CASE("CFOLD8 会话作废的四种状态两端都没有操作")
{
    //这四种都是「一个字节还没传就结束了」：对方拒绝、发送方撤回邀请、对方不在线、
    //邀请超时。气泡上只显示一行结论，不再提供按钮，用户要发就从工具栏重新发起。
    const int kDeadStates[] =
    {
        kFolderRejected, kFolderInviteCanceled, kFolderPeerOffline, kFolderTimeout
    };

    for (size_t i = 0; i < sizeof(kDeadStates) / sizeof(kDeadStates[0]); ++i)
    {
        const int st = kDeadStates[i];
        INFO("state = " << st);

        CHECK(FolderStateButtonCount(st, kFromSender) == 0);
        CHECK(FolderStateButtonCount(st, kFromReceiver) == 0);
        CHECK(FolderStateHasProgress(st) == false);

        int acts[kMaxChatFolderActions] = { -1, -1, -1 };
        CHECK(FolderButtonActions(st, kFromSender, acts) == 0);
        CHECK(FolderButtonActions(st, kFromReceiver, acts) == 0);
        //返回 0 时不该写 out[]
        CHECK(acts[0] == -1);
        CHECK(acts[1] == -1);
        CHECK(acts[2] == -1);
    }
}

TEST_CASE("CFOLD9 重试可用性随保留期与端别变化")
{
    //保留期内的失败会话可以断点续传，这是唯一为真的组合
    CHECK(FolderRetryEnabled(kFolderFailed, kFromSender, kWithinWindow));

    //期满服务端已销毁会话，再点也只会被回「会话已过期」，故按钮置灰
    CHECK(FolderRetryEnabled(kFolderFailed, kFromSender, kWindowPassed) == false);

    //重试由发送端发起，接收端两种情形都不可用
    CHECK(FolderRetryEnabled(kFolderFailed, kFromReceiver, kWithinWindow) == false);
    CHECK(FolderRetryEnabled(kFolderFailed, kFromReceiver, kWindowPassed) == false);

    //kFolderExpired 的语义就是保留期已过，即便调用方误传了「期内」也不能可用
    CHECK(FolderRetryEnabled(kFolderExpired, kFromSender, kWithinWindow) == false);
    CHECK(FolderRetryEnabled(kFolderExpired, kFromSender, kWindowPassed) == false);

    //用户取消不提供续传入口，理由同 CFOLD6
    CHECK(FolderRetryEnabled(kFolderUserCanceled, kFromSender, kWithinWindow) == false);

    //除失败态外的其余状态都谈不上重试，逐个确认一遍，免得哪天被误改成随处可重试
    for (size_t i = 0; i < sizeof(kAllFolderStates) / sizeof(kAllFolderStates[0]); ++i)
    {
        const int st = kAllFolderStates[i];
        if (st == kFolderFailed)
            continue;

        INFO("state = " << st);
        CHECK(FolderRetryEnabled(st, kFromSender, kWithinWindow) == false);
        CHECK(FolderRetryEnabled(st, kFromReceiver, kWithinWindow) == false);
    }
}

TEST_CASE("CFOLD10 未知状态按无操作保守处理")
{
    //将来若有人加了新状态却漏改本映射，宁可不给操作，也不要越界或给错操作
    const int kNotAState = 999;

    CHECK(FolderStateButtonCount(kNotAState, kFromSender) == 0);
    CHECK(FolderStateButtonCount(kNotAState, kFromReceiver) == 0);
    CHECK(FolderStateHasProgress(kNotAState) == false);
    CHECK(FolderRetryEnabled(kNotAState, kFromSender, kWithinWindow) == false);

    int acts[kMaxChatFolderActions] = { -1, -1, -1 };
    CHECK(FolderButtonActions(kNotAState, kFromSender, acts) == 0);
    CHECK(FolderButtonActions(kNotAState, kFromReceiver, acts) == 0);
    CHECK(acts[0] == -1);
    CHECK(acts[1] == -1);
}

TEST_CASE("CFOLD11 按钮数与写入的动作数处处一致且不超上限")
{
    //气泡高度按 FolderStateButtonCount 算、按钮命中区按 FolderButtonActions 的返回值建，
    //两者一旦不一致就会出现「画了按钮点不到」或「按钮被裁掉」，故逐个状态对齐检查。
    //fromMe 两侧都要走一遍——邀请态的按钮数正是随它变的。
    const bool kBothSides[] = { false, true };

    for (size_t s = 0; s < sizeof(kBothSides) / sizeof(kBothSides[0]); ++s)
    {
        const bool fromMe = kBothSides[s];
        for (size_t i = 0; i < sizeof(kAllFolderStates) / sizeof(kAllFolderStates[0]); ++i)
        {
            const int st = kAllFolderStates[i];
            int acts[kMaxChatFolderActions] = { 0, 0, 0 };
            const int count   = FolderStateButtonCount(st, fromMe);
            const int written = FolderButtonActions(st, fromMe, acts);

            INFO("state = " << st << ", fromMe = " << (fromMe ? 1 : 0));
            CHECK(written == count);
            //数量必须落在合法区间，否则 out[] 会被写越界
            CHECK(count >= 0);
            CHECK(count <= kMaxChatFolderActions);

            //每个写入的动作都得是有效的动作 id（不能是没初始化的 0）。上界取
            //ChatFolderActionKind 里编号最大的那个（目前是 kChatFolderSaveAs = 7），
            //今后再加动作时这里要跟着改成新的最大值
            for (int k = 0; k < written; ++k)
            {
                INFO("state = " << st << ", fromMe = " << (fromMe ? 1 : 0) << ", slot = " << k);
                CHECK(acts[k] >= kChatFolderAccept);
                CHECK(acts[k] <= kChatFolderSaveAs);
            }
        }
    }
}

TEST_CASE("CFOLD12 每个错误码都有非空、含中文且互不相同的文案")
{
    const size_t codeCount = sizeof(kAllFolderErrorCodes) / sizeof(kAllFolderErrorCodes[0]);
    //606~620 共 15 个码，一个都不能漏。数量对不上说明协议加了码而本表没跟上
    REQUIRE(codeCount == 15);

    for (size_t i = 0; i < codeCount; ++i)
    {
        const int code = kAllFolderErrorCodes[i];
        INFO("errcode = " << code);

        const std::wstring text = FolderErrorText(code);
        //空文案会让气泡上「传输失败」下面空着一行，用户无从判断发生了什么
        CHECK(text.empty() == false);
        //界面文案要求中文，纯 ASCII 说明这里被填成了英文措辞或日志串
        CHECK(ContainsNonAscii(text));
    }

    //两两互不相同：文案若撞车，用户看到同一句话却对应两种完全不同的故障，
    //排查时会被引到错误的方向
    for (size_t i = 0; i < codeCount; ++i)
    {
        for (size_t j = i + 1; j < codeCount; ++j)
        {
            INFO("errcode " << kAllFolderErrorCodes[i] << " vs " << kAllFolderErrorCodes[j]);
            CHECK(std::wstring(FolderErrorText(kAllFolderErrorCodes[i]))
                  != std::wstring(FolderErrorText(kAllFolderErrorCodes[j])));
        }
    }
}

TEST_CASE("CFOLD13 未知错误码走兜底文案")
{
    const std::wstring fallback = FolderErrorText(0);
    CHECK(fallback.empty() == false);
    CHECK(ContainsNonAscii(fallback));

    //取几个不属于本组的取值：0（没填错误码）、文件上传域的码、远超本组的码
    CHECK(std::wstring(FolderErrorText(605)) == fallback);
    CHECK(std::wstring(FolderErrorText(621)) == fallback);
    CHECK(std::wstring(FolderErrorText(99999)) == fallback);
    CHECK(std::wstring(FolderErrorText(-1)) == fallback);

    //兜底文案不得与任何一条具体原因相同，否则「未知」与那一条就分不开了
    const size_t codeCount = sizeof(kAllFolderErrorCodes) / sizeof(kAllFolderErrorCodes[0]);
    for (size_t i = 0; i < codeCount; ++i)
    {
        INFO("errcode = " << kAllFolderErrorCodes[i]);
        CHECK(std::wstring(FolderErrorText(kAllFolderErrorCodes[i])) != fallback);
    }
}

TEST_CASE("CFOLD14 状态取值与协议枚举一一对应")
{
    //cmd_folder_state_notify(1815) 推上来的 state 字段会被直接填进气泡，两边一旦对不上，
    //气泡就会显示成另一个状态（比如对方拒绝了却显示成传输中），而且这种错在编译期
    //毫无征兆。
    CHECK(static_cast<int>(kFolderInviting)       == static_cast<int>(folder_state_ringing));
    CHECK(static_cast<int>(kFolderTransferring)   == static_cast<int>(folder_state_transferring));
    CHECK(static_cast<int>(kFolderRejected)       == static_cast<int>(folder_state_rejected));
    CHECK(static_cast<int>(kFolderInviteCanceled) == static_cast<int>(folder_state_canceled));
    CHECK(static_cast<int>(kFolderPeerOffline)    == static_cast<int>(folder_state_peer_offline));
    CHECK(static_cast<int>(kFolderTimeout)        == static_cast<int>(folder_state_timeout));
    CHECK(static_cast<int>(kFolderFailed)         == static_cast<int>(folder_state_failed));
    CHECK(static_cast<int>(kFolderCompleted)      == static_cast<int>(folder_state_completed));

    //本端另加的三个状态编号必须落在协议取值之上，且留有余量：协议将来新增状态时
    //不至于与它们撞号（撞号的后果是一条推送被当成另一种状态处理）
    const int kProtocolStateMax = static_cast<int>(folder_state_completed);
    CHECK(kFolderPreparing    > kProtocolStateMax);
    CHECK(kFolderExpired      > kProtocolStateMax);
    CHECK(kFolderUserCanceled > kProtocolStateMax);

    //三个本端状态之间也不能重号
    CHECK(kFolderPreparing != kFolderExpired);
    CHECK(kFolderPreparing != kFolderUserCanceled);
    CHECK(kFolderExpired   != kFolderUserCanceled);
}

TEST_CASE("CFOLD15 按钮数上限常量与实际最大按钮数相等")
{
    //kMaxChatFolderActions 同时是 FolderButtonActions 出参数组的长度、以及气泡里几个
    //按钮数组（m_folderBtnRect / m_folderBtnAction / m_folderBtnEnabled）的长度。
    //改大了只是浪费几个字节，改小了或者漏改就是数组越界写，故这里把它与逐状态实测出来
    //的最大值钉死：加了新按钮却忘了调上限，本用例会当场失败。
    int maxCount = 0;
    const bool kBothSides[] = { false, true };

    for (size_t s = 0; s < sizeof(kBothSides) / sizeof(kBothSides[0]); ++s)
    {
        for (size_t i = 0; i < sizeof(kAllFolderStates) / sizeof(kAllFolderStates[0]); ++i)
        {
            const int count = FolderStateButtonCount(kAllFolderStates[i], kBothSides[s]);
            if (count > maxCount)
                maxCount = count;
        }
    }

    CHECK(maxCount == kMaxChatFolderActions);
    //上限就出在邀请态的接收端：「接收 / 另存为 / 拒绝」三个
    CHECK(FolderStateButtonCount(kFolderInviting, kFromReceiver) == kMaxChatFolderActions);
}

TEST_CASE("CFOLD16 FolderStateFromRecord 把记录条的结局翻成气泡状态")
{
    //五种结局是恒等映射：协议的 3~6、8 与气泡状态逐值相同
    CHECK(FolderStateFromRecord(folder_state_rejected, 0) == kFolderRejected);
    CHECK(FolderStateFromRecord(folder_state_canceled, 0) == kFolderInviteCanceled);
    CHECK(FolderStateFromRecord(folder_state_peer_offline, 0) == kFolderPeerOffline);
    CHECK(FolderStateFromRecord(folder_state_timeout, 0) == kFolderTimeout);
    CHECK(FolderStateFromRecord(folder_state_completed, 0) == kFolderCompleted);
}

TEST_CASE("CFOLD17 失败态按错误码再分三档，与实时路径的判法一致")
{
    //协议把「用户取消」与「会话已过保留期」都表达成失败 + 错误码，气泡上各自成一档：
    //措辞是「已取消」「请重新发送」，而不是笼统的「传输失败」
    CHECK(FolderStateFromRecord(folder_state_failed, error_code_folder_canceled)
          == kFolderUserCanceled);
    CHECK(FolderStateFromRecord(folder_state_failed, error_code_folder_session_expired)
          == kFolderExpired);

    //其余失败原因一律归到「传输失败」这一档
    CHECK(FolderStateFromRecord(folder_state_failed, error_code_folder_write_fail)
          == kFolderFailed);
    CHECK(FolderStateFromRecord(folder_state_failed, error_code_folder_relay_broken)
          == kFolderFailed);
    //没给具体原因时同样是「传输失败」，文案回落到兜底那句
    CHECK(FolderStateFromRecord(folder_state_failed, 0) == kFolderFailed);
}

TEST_CASE("CFOLD18 非终态与越界结局返回 0，气泡据此降级")
{
    //记录条按协议只带终态。走到这里说明 content 是脏的或协议实现不一致：
    //返回 0（不是合法状态）让气泡退化成无进度条无按钮，而不是硬套一个看着
    //像那么回事、实则不对的状态
    CHECK(FolderStateFromRecord(0, 0) == 0);
    CHECK(FolderStateFromRecord(folder_state_ringing, 0) == 0);
    CHECK(FolderStateFromRecord(folder_state_transferring, 0) == 0);
    CHECK(FolderStateFromRecord(folder_state_completed + 1, 0) == 0);
    CHECK(FolderStateFromRecord(-1, 0) == 0);

    //0 不是合法状态，气泡按无进度条 / 无按钮处理 —— 这条同时守住了降级的下游行为
    CHECK_FALSE(FolderStateHasProgress(0));
    CHECK(FolderStateButtonCount(0, true) == 0);
    CHECK(FolderStateButtonCount(0, false) == 0);
}

TEST_CASE("CFOLD19 记录条翻出来的每种状态都能算出按钮与进度条，不会撞进未知分支")
{
    //六种终态各走一遍收发两端：状态一旦落在 FolderButtonActions 的未知分支上，
    //气泡就会一个按钮都不给（症状是"传完了却没有打开文件夹"），故在此钉死
    const int kResults[] =
    {
        folder_state_rejected,
        folder_state_canceled,
        folder_state_peer_offline,
        folder_state_timeout,
        folder_state_failed,
        folder_state_completed,
    };
    const int kResultCount = (int)(sizeof(kResults) / sizeof(kResults[0]));

    for (int i = 0; i < kResultCount; ++i)
    {
        const int state = FolderStateFromRecord(kResults[i], 0);
        REQUIRE(state != 0);

        int acts[kMaxChatFolderActions] = { 0 };
        CHECK(FolderButtonActions(state, true, acts) == FolderStateButtonCount(state, true));
        CHECK(FolderButtonActions(state, false, acts) == FolderStateButtonCount(state, false));
    }

    //完成态两端都给「打开文件夹」——记录条上唯一还能点的东西
    int done[kMaxChatFolderActions] = { 0 };
    REQUIRE(FolderButtonActions(kFolderCompleted, true, done) == 1);
    CHECK(done[0] == kChatFolderOpenFolder);
    REQUIRE(FolderButtonActions(kFolderCompleted, false, done) == 1);
    CHECK(done[0] == kChatFolderOpenFolder);
}

TEST_CASE("CFOLD20 邀请被拒时只有「对方不在线」这一种要撤掉气泡")
{
    //对方不在线：服务端在收到邀请的那一刻就直接拒绝并返回，会话没建、消息记录也没写，
    //本端那条占位气泡必须整行撤掉 —— 留成"发送失败"会在聊天记录里多出一条并不存在的
    //失败消息，而实际上一个字节都没发出去
    CHECK(FolderInviteRejectShouldRemoveBubble(error_code_folder_peer_offline) == true);

    //其余拒绝理由都属于"这个文件夹发不出去"，气泡留在原地转失败态才说得清原因
    const int kKeepBubbleCodes[] =
    {
        error_code_folder_rejected,
        error_code_folder_invite_timeout,
        error_code_folder_too_many_files,
        error_code_folder_too_large,
        error_code_folder_mkdir_fail,
        error_code_folder_disk_full,
        error_code_folder_write_fail,
        error_code_folder_read_fail,
        error_code_folder_verify_fail,
        error_code_folder_relay_broken,
        error_code_folder_canceled,
        error_code_folder_session_expired,
        error_code_folder_server_busy,
        error_code_folder_busy,
    };
    const int kKeepCount = (int)(sizeof(kKeepBubbleCodes) / sizeof(kKeepBubbleCodes[0]));
    for (int i = 0; i < kKeepCount; ++i)
    {
        INFO("errcode = " << kKeepBubbleCodes[i]);
        CHECK(FolderInviteRejectShouldRemoveBubble(kKeepBubbleCodes[i]) == false);
    }

    //成功码传进来也不该被当成"要撤气泡"：成功时调用方根本不会走到这个判断，
    //这里只是兜住"万一被误用"的情形
    CHECK(FolderInviteRejectShouldRemoveBubble(error_code_ok) == false);
}
