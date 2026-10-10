/**
 *  FolderInviteNotifyPolicyTests.cpp
 *
 *  文件夹邀请提醒判据（BalloonMain/FolderInviteNotifyPolicy.h）的测试：会话列表预览
 *  文字怎么拼、什么情况下记未读、什么情况下闪托盘并播提示音。
 *
 *  这些判据支撑的是"收到文件夹邀请不再凭空弹出聊天窗"这一改动 —— 不弹窗之后，用户
 *  能否察觉有人发来文件夹，全靠会话列表未读与托盘闪烁两条线索，判据一旦写错的直接
 *  后果是"邀请悄无声息地过了 60 秒超时"，故逐条钉死。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不发协议。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonMain/FolderInviteNotifyPolicy.h"
#include "../net/Protocol.h"   // enum conversation_notify_mode

using namespace balloonmain;

namespace {

//"[文件夹] " 的 UTF-8 字节序列（文=E6 96 87、件=E4 BB B6、夹=E5 A4 B9）。刻意写成
//转义形式而不是直接写中文：这样断言的是"落盘字节确实是 UTF-8"，即使哪天测试工程漏了
///utf-8 编译选项、源文件被当成 GBK 解析，本用例也会立刻失败而不是跟着一起错。
const char* const kPrefixUtf8 = "[\xE6\x96\x87\xE4\xBB\xB6\xE5\xA4\xB9] ";

//没有文件夹名字时的预览文字，即上面那个前缀去掉尾部空格。
const char* const kNoNameUtf8 = "[\xE6\x96\x87\xE4\xBB\xB6\xE5\xA4\xB9]";

} // anonymous namespace

// =====================================================================
// FIN1~FIN4 —— 会话列表预览文字
// =====================================================================

TEST_CASE("FIN1 预览文字为类型占位加文件夹名", "[folderinvite]")
{
    //"项目资料" 的 UTF-8 字节。传进去的名字应当原样接在前缀之后，不做任何改写。
    const std::string name = "\xE9\xA1\xB9\xE7\x9B\xAE\xE8\xB5\x84\xE6\x96\x99";
    CHECK(BuildFolderInviteDigest(name) == std::string(kPrefixUtf8) + name);
}

//FIN2（导出的前缀常量与期望字节一致）已于 2026-10-03 删除：多语言改造后预览文字经 TR8 取译文，
//模块不再导出前缀常量

TEST_CASE("FIN3 文件夹名为空时不留悬空空格", "[folderinvite]")
{
    CHECK(BuildFolderInviteDigest(std::string()) == std::string(kNoNameUtf8));
}

TEST_CASE("FIN4 ASCII 文件夹名同样原样拼接", "[folderinvite]")
{
    //对端可能是英文名目录；这条顺带保证函数不对名字做本地化 / 截断。
    CHECK(BuildFolderInviteDigest("build") == std::string(kPrefixUtf8) + "build");
}

// =====================================================================
// FIN5~FIN8 —— 未读判定
// =====================================================================

TEST_CASE("FIN5 对方发来且没在看该窗口时记未读", "[folderinvite]")
{
    CHECK(ShouldCountFolderInviteAsUnread(false, false));
}

TEST_CASE("FIN6 用户正看着该聊天窗时不记未读", "[folderinvite]")
{
    //邀请气泡已经画在他眼前，再记未读就成了"邀请就在眼前、列表还亮着红点"。
    CHECK_FALSE(ShouldCountFolderInviteAsUnread(false, true));
}

TEST_CASE("FIN7 自己发起的邀请一律不记未读", "[folderinvite]")
{
    CHECK_FALSE(ShouldCountFolderInviteAsUnread(true, false));
    CHECK_FALSE(ShouldCountFolderInviteAsUnread(true, true));
}

// =====================================================================
// FIN8~FIN11 —— 托盘闪烁 / 提示音判定
// =====================================================================

TEST_CASE("FIN8 窗口没开且非自己发起且允许提醒时才提醒", "[folderinvite]")
{
    CHECK(ShouldNotifyFolderInvite(false, false, notify_mode_allow));
}

TEST_CASE("FIN9 聊天窗已经打开时不提醒", "[folderinvite]")
{
    //窗口开着时邀请气泡已渲染进去，再闪托盘属于重复打扰。
    CHECK_FALSE(ShouldNotifyFolderInvite(true, false, notify_mode_allow));
}

TEST_CASE("FIN10 自己发起的邀请不提醒", "[folderinvite]")
{
    //否则闪的是自己的头像，单击还打开与自己的会话。
    CHECK_FALSE(ShouldNotifyFolderInvite(false, true, notify_mode_allow));
}

TEST_CASE("FIN11 会话设成接收消息但不提醒时不提醒", "[folderinvite]")
{
    //接收不提醒只压提醒、不压未读：未读仍由 ShouldCountFolderInviteAsUnread 单独判定，
    //这里一并断言两者互不影响。其余模式的判定见 NotifyModeTests 的 NMPL10。
    CHECK_FALSE(ShouldNotifyFolderInvite(false, false, notify_mode_silent));
    CHECK(ShouldCountFolderInviteAsUnread(false, false));
}
