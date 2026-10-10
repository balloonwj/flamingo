/**
 *  主持人关闭别人的白板（标签 [boardclose]），ShareBoardCloseTests.cpp
 *
 *  背景：docs/屏幕共享与录制方案.md 第 14.11 节第 1 条（2026-10-08 用户确认）。方案第 2 节第 32 条定为
 *  「建立者可关，主持人也能关」，服务端已支持（SSWB3），客户端原先没有入口。现在原主持人控制条的「更多」与经移交主持人的
 *  「主持人 ▾」各加一项「关闭白板 ▸」，子菜单列出别人的白板；选中后先弹确认框「关闭某某的白板？白板上的内容不会保留。」，
 *  确认后才发出 1737（停止，target 为建立者，kind 为白板），取消则不发。
 *  覆盖：
 *    - SBCL1 子菜单的各项：只列别人的白板、按快照顺序、名字写「某某 的白板」，名字找不到时写 userid；没有别人的白板时为空；
 *    - SBCL2 源文件守卫：控制条与观看窗都有这个子菜单，选中后把建立者交给控制器；控制器收到后只打开确认框，
 *            确认框的结果为「关闭」时才发停止白板的请求；结果消息号已定义。
 *  源文件守卫在运行时读取源文件，位置取自 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC）。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <map>
#include <string>
#include <vector>
#include "../../FlamingoMedia/Share/ShareRoomPolicy.h"
#include "../Common/I18n/I18n.h"

using namespace screenshare;

namespace {

//测试用的用户与白板的画面座位
const int64_t kSbclSelf = 7000;
const int64_t kSbclAlice = 7001;
const int64_t kSbclBob = 7002;
const int64_t kSbclNoName = 7003;
const int kSbclSeatSelf = 5;
const int kSbclSeatAlice = 6;
const int kSbclSeatBob = 7;
const int kSbclSeatNoName = 8;
//读文件时每次读的字节数、本文件在 flamingoclient 下的层数（Source\Tests\本文件）
const size_t kSbclReadChunk = 4096;
const int kSbclDepth = 3;

ShareWhiteboardInfo SbclBoard(int seat, int64_t owner)
{
    ShareWhiteboardInfo b;
    b.m_seat = seat;
    b.m_ownerId = owner;
    return b;
}

std::string SbclReadFile(const std::string& path)
{
    std::string data;
    FILE* f = NULL;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == NULL)
        return data;
    char buf[kSbclReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    fclose(f);
    return data;
}

std::string SbclClientRoot()
{
    std::string self(__FILE__);
    for (int i = 0; i < kSbclDepth; ++i)
    {
        const size_t pos = self.find_last_of("\\/");
        if (pos == std::string::npos)
            return std::string();
        self = self.substr(0, pos);
    }
    return self;
}

//从 begin 起截到下一个 marker（不含）为止；找不到 begin 时为空
std::string SbclSlice(const std::string& text, const std::string& begin, const std::string& marker)
{
    const size_t b = text.find(begin);
    if (b == std::string::npos)
        return std::string();
    const size_t e = text.find(marker, b + begin.size());
    return text.substr(b, e == std::string::npos ? std::string::npos : e - b);
}

} // namespace

TEST_CASE("SBCL1 the close-board submenu lists other people's boards", "[boardclose]")
{
    std::map<int64_t, std::wstring> names;
    names[kSbclAlice] = L"Alice";
    names[kSbclBob] = L"Bob";
    std::vector<ShareWhiteboardInfo> boards;
    boards.push_back(SbclBoard(kSbclSeatBob, kSbclBob));
    boards.push_back(SbclBoard(kSbclSeatSelf, kSbclSelf));
    boards.push_back(SbclBoard(kSbclSeatAlice, kSbclAlice));
    std::vector<ShareBoardCloseItem> items;
    BuildShareBoardCloseItems(boards, kSbclSelf, names, items);
    //本人的白板不在其中，顺序与快照相同
    REQUIRE(items.size() == 2);
    CHECK(items[0].m_ownerId == kSbclBob);
    CHECK(items[0].m_text == TRF(L"{0} 的白板", L"Bob"));
    CHECK(items[1].m_ownerId == kSbclAlice);
    CHECK(items[1].m_text == TRF(L"{0} 的白板", L"Alice"));
    //名字找不到：写 userid
    boards.push_back(SbclBoard(kSbclSeatNoName, kSbclNoName));
    BuildShareBoardCloseItems(boards, kSbclSelf, names, items);
    REQUIRE(items.size() == 3);
    CHECK(items[2].m_ownerId == kSbclNoName);
    CHECK(items[2].m_text == TRF(L"{0} 的白板", std::to_wstring(kSbclNoName)));
    //只有本人的白板、或没有白板：为空
    std::vector<ShareWhiteboardInfo> mine(1, SbclBoard(kSbclSeatSelf, kSbclSelf));
    BuildShareBoardCloseItems(mine, kSbclSelf, names, items);
    CHECK(items.empty());
    BuildShareBoardCloseItems(std::vector<ShareWhiteboardInfo>(), kSbclSelf, names, items);
    CHECK(items.empty());
}

TEST_CASE("SBCL2 closing someone else's board asks first, then sends the stop", "[boardclose]")
{
    const std::string root = SbclClientRoot();
    REQUIRE_FALSE(root.empty());
    //控制条的「更多」：子菜单与选中后的命令（带建立者）
    const std::string bar = SbclReadFile(root + "\\FlamingoMedia\\Share\\ShareControlBar.cpp");
    REQUIRE_FALSE(bar.empty());
    CHECK(bar.find("kMenuCloseBoardBase") != std::string::npos);
    CHECK(bar.find("kCmdCloseBoard") != std::string::npos);
    CHECK(bar.find("AppendSubMenu(") != std::string::npos);
    //观看窗的「主持人 ▾」：同样的子菜单
    const std::string viewer = SbclReadFile(root + "\\FlamingoMedia\\Share\\ShareViewerWindow.cpp");
    REQUIRE_FALSE(viewer.empty());
    CHECK(viewer.find("kCmdCloseBoard") != std::string::npos);
    const bool viewerHasBoards = viewer.find("BuildShareBoardCloseItems") != std::string::npos ||
                                 viewer.find("m_hostBoards") != std::string::npos;
    CHECK(viewerHasBoards);
    //控制器：两处命令都只打开确认框
    const std::string controller = SbclReadFile(root + "\\FlamingoMedia\\Share\\ShareController.cpp");
    REQUIRE_FALSE(controller.empty());
    const std::string barCase = SbclSlice(controller, "case CShareControlBar::kCmdCloseBoard:", "case ");
    const std::string viewerCase = SbclSlice(controller, "CShareViewerWindow::kCmdCloseBoard", "\n    }");
    REQUIRE_FALSE(barCase.empty());
    REQUIRE_FALSE(viewerCase.empty());
    CHECK(barCase.find("requestCloseBoardOf(") != std::string::npos);
    CHECK(barCase.find("sendSharing(") == std::string::npos);
    CHECK(viewerCase.find("requestCloseBoardOf(") != std::string::npos);
    CHECK(viewerCase.find("sendSharing(") == std::string::npos);
    //确认框：打开时不发请求；结果为「关闭」时才发停止白板（target 为建立者）
    const std::string board = SbclReadFile(root + "\\FlamingoMedia\\Share\\ShareControllerBoard.cpp");
    REQUIRE_FALSE(board.empty());
    const std::string request =
        SbclSlice(board, "void CShareController::requestCloseBoardOf(", "\nvoid CShareController::");
    REQUIRE_FALSE(request.empty());
    CHECK(request.find("OpenCentered(") != std::string::npos);
    CHECK(request.find("sendSharing(") == std::string::npos);
    const std::string result =
        SbclSlice(board, "LRESULT CShareController::OnBoardCloseResultMsg(", "\nLRESULT CShareController::");
    REQUIRE_FALSE(result.empty());
    const size_t confirmed = result.find("wParam == 0");
    const size_t send = result.find("sendSharing(share_sharing_stop, owner, share_kind_whiteboard)");
    REQUIRE(confirmed != std::string::npos);
    REQUIRE(send != std::string::npos);
    CHECK(confirmed < send);
    //结果消息号
    const std::string messages = SbclReadFile(root + "\\FlamingoMedia\\Share\\ShareMessages.h");
    CHECK(messages.find("kMsgShareBoardCloseResult = WM_APP + 0xD7") != std::string::npos);
}
