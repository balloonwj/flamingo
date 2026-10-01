/**
 *  聊天文件气泡「状态 → 可用操作」映射单元测试（Suite CFSM），
 *  ChatFileStateModelTests.cpp
 *
 *  覆盖 balloonchatwnd 的文件态映射，重点锁死本轮新增的两个状态：
 *    CFSM1  待接收态、对方发来：给「接收 / 拒绝」两个操作，顺序固定（接收在左）；
 *    CFSM2  待接收 / 已拒绝都不画进度条（还没开始传，没有进度可言）；
 *    CFSM3  已拒绝态给一个「重试」，用户可反悔重新下载；
 *    CFSM4  传输中（上传 / 下载）只给「取消」；
 *    CFSM5  完成态给「打开 / 打开所在文件夹 / 另存为」三个，顺序固定；
 *    CFSM6  失败态只给「重试」：失败时传输已经结束，没有可取消的任务，原先给出的「取消」
 *           点了没有任何反应（bugs.md 的 BUG-37）；
 *    CFSM7  已删除态无操作；
 *    CFSM8  未知状态按无操作保守处理，不越界写 out[]；
 *    CFSM9  每个状态的按钮数与实际写入的动作数一致（防止画了按钮却点不到，
 *           或高度按 N 个算、实际写了 M 个的错位）；
 *    CFSM10 待接收态、自己发出：只给一个「下载」，不给「拒绝」——自己要发的文件
 *           谈不上拒绝，那个按钮出现在这里是无意义的；
 *    CFSM11 除待接收外，其余状态不受 fromMe 影响（避免哪天误把区分蔓延出去）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-03
 */
#include "catch_amalgamated.hpp"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"
#include "../net/Protocol.h"   // enum file_ack_state（气泡侧常量须与协议取值一致）

using namespace balloonchatwnd;

//以下多数用例与 fromMe 无关，统一用"对方发来"这一侧驱动；涉及区分的见 CFSM10 / CFSM11。
static const bool kFromPeer = false;
static const bool kFromMe   = true;

TEST_CASE("CFSM1 待接收态(对方发来)给「接收 / 拒绝」")
{
    REQUIRE(FileStateButtonCount(kFilePendingAccept, kFromPeer) == 2);

    int acts[kMaxChatFileActions] = { 0 };
    REQUIRE(FileButtonActions(kFilePendingAccept, kFromPeer, acts) == 2);
    //顺序即左右顺序：主动作「接收」在左，次要的「拒绝」在右
    CHECK(acts[0] == kChatFileAccept);
    CHECK(acts[1] == kChatFileReject);
}

TEST_CASE("CFSM2 待接收 / 已拒绝都不画进度条")
{
    //这两个状态都还没开始传，画一条空进度条只会让人以为在传
    CHECK(FileStateHasProgress(kFilePendingAccept) == false);
    CHECK(FileStateHasProgress(kFileRejected) == false);

    //传输中与中断态才有进度（后两者停在中断处，能看出传到哪儿断的）
    CHECK(FileStateHasProgress(kFileUploading));
    CHECK(FileStateHasProgress(kFileDownloading));
    CHECK(FileStateHasProgress(kFileFailed));
    CHECK(FileStateHasProgress(kFileCancelled));

    //完成与已删除已成定局，无需进度条
    CHECK(FileStateHasProgress(kFileNormal) == false);
    CHECK(FileStateHasProgress(kFileDeleted) == false);
}

TEST_CASE("CFSM3 已拒绝态可反悔重试")
{
    REQUIRE(FileStateButtonCount(kFileRejected, kFromPeer) == 1);

    int acts[kMaxChatFileActions] = { 0 };
    REQUIRE(FileButtonActions(kFileRejected, kFromPeer, acts) == 1);
    CHECK(acts[0] == kChatFileRetry);
}

TEST_CASE("CFSM4 传输中只给「取消」")
{
    int acts[kMaxChatFileActions] = { 0 };

    REQUIRE(FileStateButtonCount(kFileUploading, kFromMe) == 1);
    REQUIRE(FileButtonActions(kFileUploading, kFromMe, acts) == 1);
    CHECK(acts[0] == kChatFileCancel);

    REQUIRE(FileStateButtonCount(kFileDownloading, kFromPeer) == 1);
    REQUIRE(FileButtonActions(kFileDownloading, kFromPeer, acts) == 1);
    CHECK(acts[0] == kChatFileCancel);
}

TEST_CASE("CFSM5 完成态给三个文件操作")
{
    REQUIRE(FileStateButtonCount(kFileNormal, kFromPeer) == 3);

    int acts[kMaxChatFileActions] = { 0 };
    REQUIRE(FileButtonActions(kFileNormal, kFromPeer, acts) == 3);
    CHECK(acts[0] == kChatFileOpen);
    CHECK(acts[1] == kChatFileOpenFolder);
    CHECK(acts[2] == kChatFileSaveAs);
}

TEST_CASE("CFSM6 失败态只给「重试」，不给「取消」")
{
    //失败时传输任务已经结束，「取消」没有可取消的对象（BUG-37），只留重新发送 / 重新下载
    REQUIRE(FileStateButtonCount(kFileFailed, kFromMe) == 1);
    REQUIRE(FileStateButtonCount(kFileFailed, kFromPeer) == 1);

    int acts[kMaxChatFileActions] = { 0 };
    REQUIRE(FileButtonActions(kFileFailed, kFromMe, acts) == 1);
    CHECK(acts[0] == kChatFileRetry);
}

TEST_CASE("CFSM7 已删除态无操作")
{
    REQUIRE(FileStateButtonCount(kFileDeleted, kFromPeer) == 0);

    int acts[kMaxChatFileActions] = { -1, -1, -1 };
    REQUIRE(FileButtonActions(kFileDeleted, kFromPeer, acts) == 0);
    //返回 0 时不该写 out[]，调用方也不会去读
    CHECK(acts[0] == -1);
}

TEST_CASE("CFSM8 未知状态按无操作保守处理")
{
    //将来若有人加了新状态却漏改本映射，宁可不给操作，也不要越界或给错操作
    const int kNotAState = 999;
    CHECK(FileStateButtonCount(kNotAState, kFromPeer) == 0);
    CHECK(FileStateButtonCount(kNotAState, kFromMe) == 0);
    CHECK(FileStateHasProgress(kNotAState) == false);

    int acts[kMaxChatFileActions] = { -1, -1, -1 };
    CHECK(FileButtonActions(kNotAState, kFromPeer, acts) == 0);
    CHECK(acts[0] == -1);
    CHECK(acts[1] == -1);
    CHECK(acts[2] == -1);
}

TEST_CASE("CFSM9 按钮数与写入的动作数处处一致")
{
    //气泡高度按 FileStateButtonCount 算、按钮命中区按 FileButtonActions 的返回值建，
    //两者一旦不一致就会出现「画了按钮点不到」或「按钮被裁掉」，故逐个状态对齐检查。
    //fromMe 两侧都要走一遍——待接收态的按钮数正是随它变的。
    const int kAllStates[] =
    {
        kFileNormal, kFileUploading, kFileFailed, kFileDownloading,
        kFileCancelled, kFileDeleted, kFilePendingAccept, kFileRejected
    };
    const bool kBothSides[] = { false, true };

    for (size_t s = 0; s < sizeof(kBothSides) / sizeof(kBothSides[0]); ++s)
    {
        const bool fromMe = kBothSides[s];
        for (size_t i = 0; i < sizeof(kAllStates) / sizeof(kAllStates[0]); ++i)
        {
            const int st = kAllStates[i];
            int acts[kMaxChatFileActions] = { 0 };
            const int count   = FileStateButtonCount(st, fromMe);
            const int written = FileButtonActions(st, fromMe, acts);

            INFO("state = " << st << ", fromMe = " << (fromMe ? 1 : 0));
            CHECK(written == count);
            //数量必须落在合法区间，否则 out[] 会被写越界
            CHECK(count >= 0);
            CHECK(count <= kMaxChatFileActions);

            //每个写入的动作都得是有效的动作 id（不能是没初始化的 0）
            for (int k = 0; k < written; ++k)
            {
                INFO("state = " << st << ", fromMe = " << (fromMe ? 1 : 0) << ", slot = " << k);
                CHECK(acts[k] >= kChatFileOpen);
                CHECK(acts[k] <= kChatFileReject);
            }
        }
    }
}

TEST_CASE("CFSM10 待接收态(自己发出)只给「下载」、不给「拒绝」")
{
    //自己要发的文件谈不上"拒绝"，那个按钮摆在这里没有意义；只留一条把它取回本地的路。
    REQUIRE(FileStateButtonCount(kFilePendingAccept, kFromMe) == 1);

    int acts[kMaxChatFileActions] = { -1, -1, -1 };
    REQUIRE(FileButtonActions(kFilePendingAccept, kFromMe, acts) == 1);
    //动作仍是 kChatFileAccept（含义即"开始下载"），宿主处理逻辑与对方发来的完全一致，
    //区别只在气泡上的文案（「下载」而非「接收」）。
    CHECK(acts[0] == kChatFileAccept);
    //第二格不该被写入
    CHECK(acts[1] == -1);
}

TEST_CASE("CFSM12 对端收取状态常量与协议枚举一一对应")
{
    //协议里传的就是这些数字，气泡直接拿来用。两边一旦对不上，发送方会显示成错误的
    //状态文字（比如对方拒绝了却显示"已接收"），而且这种错在编译期毫无征兆。
    CHECK(static_cast<int>(kFilePeerReceiving) == static_cast<int>(file_ack_receiving));
    CHECK(static_cast<int>(kFilePeerReceived)  == static_cast<int>(file_ack_received));
    CHECK(static_cast<int>(kFilePeerRejected)  == static_cast<int>(file_ack_rejected));
    //0 是"还没有任何动作"，协议侧不占用这个值
    CHECK(static_cast<int>(kFilePeerNone) == 0);
    CHECK(static_cast<int>(file_ack_receiving) != 0);
}

TEST_CASE("CFSM11 除待接收外其余状态不受 fromMe 影响")
{
    //区分只应存在于待接收态。其余状态若哪天被误改成随 fromMe 变，这里会立刻报出来。
    const int kOtherStates[] =
    {
        kFileNormal, kFileUploading, kFileFailed, kFileDownloading,
        kFileCancelled, kFileDeleted, kFileRejected
    };

    for (size_t i = 0; i < sizeof(kOtherStates) / sizeof(kOtherStates[0]); ++i)
    {
        const int st = kOtherStates[i];
        INFO("state = " << st);
        CHECK(FileStateButtonCount(st, kFromPeer) == FileStateButtonCount(st, kFromMe));

        int peerActs[kMaxChatFileActions] = { 0 };
        int mineActs[kMaxChatFileActions] = { 0 };
        const int n = FileButtonActions(st, kFromPeer, peerActs);
        CHECK(FileButtonActions(st, kFromMe, mineActs) == n);
        for (int k = 0; k < n; ++k)
        {
            INFO("state = " << st << ", slot = " << k);
            CHECK(peerActs[k] == mineActs[k]);
        }
    }
}
