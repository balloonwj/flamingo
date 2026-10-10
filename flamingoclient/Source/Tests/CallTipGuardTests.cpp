/**
 *  通话、远程桌面失败提示写进聊天窗的守卫用例（标签 [calltip]），CallTipGuardTests.cpp
 *  背景：2026-10-07 用户要求「对方当前无法接听（不在线或正在通话中）。」这类提示不要弹消息框，改为在与对方的单聊窗里插入
 *  灰色提示行（聊天窗没开时由 exe 打开并前置）。通话控制器与远程桌面控制器依赖网络与窗口，不在单元测试工程里，本用例在运行时
 *  读取两个控制器的源文件，检查这几处提示经宿主的 AppendCallTip 发出，不再直接弹消息框：
 *    - CTG1 通话邀请被服务端拒绝（HandleInviteAck）：调用 AppendCallTip，消息框只留在取不到对方的兜底分支；
 *    - CTG2 远程桌面邀请被拒绝（HandleInviteAck）与没有建成的状态推送（HandleStateNotify）：经 ShowResultTip 发出，
 *      两个函数里不再直接弹消息框；ShowResultTip 调用 AppendCallTip。
 *    - CTG3 通话没有建成的状态推送（HandleStateNotify，如对方不在线、正在通话中）：调用 AppendCallTip，取不到对方时
 *      与 CTG1 相同，弹消息框兜底（2026-10-07 补上，原先取不到对方时提示直接丢失）。
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string>

namespace {

//读文件时每次读的字节数
const size_t kCtgReadChunk = 4096;
//本文件在 flamingoclient 下的层数（Source\Tests\本文件）：去掉这么多级就是 flamingoclient
const int kCtgDepth = 3;

//读一个源文件的全部内容；读不到时返回空串
std::string CtgReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kCtgReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

//本测试文件所在目录往上两级就是 flamingoclient（Source\Tests\本文件）
std::string CtgClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kCtgDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//取一个成员函数的函数体：从定义开始到下一个同类成员函数定义（或文件末尾）
std::string CtgFunctionBody(const std::string& source, const std::string& signature, const std::string& classPrefix)
{
    const size_t begin = source.find(signature);
    if (begin == std::string::npos)
        return std::string();
    const size_t next = source.find("\n" + classPrefix, begin + signature.size());
    return source.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

size_t CtgCount(const std::string& text, const std::string& what)
{
    size_t count = 0;
    for (size_t pos = text.find(what); pos != std::string::npos; pos = text.find(what, pos + what.size()))
        ++count;
    return count;
}

} // namespace

TEST_CASE("CTG1 call invite failure tip goes to the chat window", "[calltip]")
{
    const std::string root = CtgClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source = CtgReadFile(root + "\\FlamingoMedia\\Call\\CallController.cpp");
    REQUIRE_FALSE(source.empty());
    const std::string body =
        CtgFunctionBody(source, "void CCallController::HandleInviteAck", "void CCallController::");
    REQUIRE_FALSE(body.empty());
    CHECK(body.find("m_host->AppendCallTip(peerId, tip)") != std::string::npos);
    //消息框只留一处，在取不到对方的兜底分支里
    CHECK(CtgCount(body, "::MessageBox(") == 1);
    CHECK(body.find("else\n            ::MessageBox(") != std::string::npos);
}

TEST_CASE("CTG2 remote desktop failure tips go to the chat window", "[calltip]")
{
    const std::string root = CtgClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source = CtgReadFile(root + "\\FlamingoMedia\\RemoteDesktop\\RdController.cpp");
    REQUIRE_FALSE(source.empty());
    const std::string ack = CtgFunctionBody(source, "void CRdController::HandleInviteAck", "void CRdController::");
    const std::string state = CtgFunctionBody(source, "void CRdController::HandleStateNotify", "void CRdController::");
    const std::string tip = CtgFunctionBody(source, "void CRdController::ShowResultTip", "void CRdController::");
    REQUIRE_FALSE(ack.empty());
    REQUIRE_FALSE(state.empty());
    REQUIRE_FALSE(tip.empty());
    CHECK(ack.find("ShowResultTip(") != std::string::npos);
    CHECK(ack.find("::MessageBox(") == std::string::npos);
    CHECK(state.find("ShowResultTip(peerId, tip)") != std::string::npos);
    CHECK(state.find("::MessageBox(") == std::string::npos);
    CHECK(tip.find("m_host->AppendCallTip(peerUserId, tip)") != std::string::npos);
}

TEST_CASE("CTG3 call state failure tip goes to the chat window with a fallback", "[calltip]")
{
    const std::string root = CtgClientRoot();
    REQUIRE_FALSE(root.empty());
    const std::string source = CtgReadFile(root + "\\FlamingoMedia\\Call\\CallController.cpp");
    REQUIRE_FALSE(source.empty());
    const std::string body =
        CtgFunctionBody(source, "void CCallController::HandleStateNotify", "void CCallController::");
    REQUIRE_FALSE(body.empty());
    CHECK(body.find("m_host->AppendCallTip(peerId, tip)") != std::string::npos);
    //取不到对方时才弹消息框，且只有这一处
    CHECK(body.find("if (peerId != 0 && m_host != NULL)") != std::string::npos);
    CHECK(CtgCount(body, "::MessageBox(") == 1);
}
