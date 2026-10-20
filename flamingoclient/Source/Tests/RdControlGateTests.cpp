/**
 *  远程桌面被控端的控制把关单元测试（Suite RDC，标签 [rdctl]），RdControlGateTests.cpp
 *  覆盖 docs/远程桌面控制权限与观看工具条方案.md 第 6.3 节 RDC1~RDC4（2026-10-12 与用户确认）：
 *    - RDC1 默认关闭：鼠标帧、键盘帧都不注入；
 *    - RDC2 打开后注入；关闭时对仍按下的键与鼠标按钮各补发一次松开，没按下的不补发；
 *    - RDC3 落在本进程窗口上的输入不注入（判定函数可替换，这里注入假的判定），松开类输入不受限；
 *    - RDC4 新会话开始时复位为关闭、清空按下记录，不补发松开。
 *  被测类是纯逻辑（FlamingoMedia/RemoteDesktop/RdControlGate），不调用任何注入 API。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-12
 */
#include "catch_amalgamated.hpp"

#include <vector>
#include "../../FlamingoMedia/RemoteDesktop/RdControlGate.h"

using namespace remotedesktop;

namespace {

//用例里鼠标点击的被控端屏幕坐标（取值无意义，只要固定）
const int32_t kRdcX = 300;
const int32_t kRdcY = 200;

//关闭把关时传入的光标当前位置：补发的鼠标松开命令应当落在这里
const POINT kRdcCursor = { 640, 360 };

//构造一条鼠标命令
RdInputCommand mouseCmd(uint32_t message, int32_t x, int32_t y)
{
    RdInputCommand cmd;
    cmd.type = rd_input_mouse;
    cmd.message = message;
    cmd.x = x;
    cmd.y = y;
    return cmd;
}

//构造一条键盘命令
RdInputCommand keyCmd(uint32_t message, uint32_t vkey)
{
    RdInputCommand cmd;
    cmd.type = rd_input_key;
    cmd.message = message;
    cmd.vkey = vkey;
    return cmd;
}

//假的「本进程窗口」判定：鼠标横坐标不小于 1000、或按键是 F12 时，视为落在本进程的窗口上
bool fakeOwnWindowProbe(const RdInputCommand& cmd)
{
    const int32_t ownWindowLeft = 1000;
    if (cmd.type == rd_input_mouse)
        return cmd.x >= ownWindowLeft;
    return cmd.vkey == VK_F12;
}

} // namespace

TEST_CASE("RDC1 默认关闭：鼠标帧、键盘帧都不注入", "[rdctl]")
{
    RdControlGate gate;
    CHECK_FALSE(gate.IsAllowed());
    CHECK_FALSE(gate.Filter(mouseCmd(WM_MOUSEMOVE, kRdcX, kRdcY)));
    CHECK_FALSE(gate.Filter(mouseCmd(WM_LBUTTONDOWN, kRdcX, kRdcY)));
    CHECK_FALSE(gate.Filter(mouseCmd(WM_LBUTTONUP, kRdcX, kRdcY)));
    CHECK_FALSE(gate.Filter(keyCmd(WM_KEYDOWN, 'A')));
    CHECK_FALSE(gate.Filter(keyCmd(WM_KEYUP, 'A')));

    //关闭状态下被丢弃的按下不算「按住」，打开再关闭时不会补发
    std::vector<RdInputCommand> releases;
    gate.SetAllowed(true, kRdcCursor, releases);
    CHECK(releases.empty());
    gate.SetAllowed(false, kRdcCursor, releases);
    CHECK(releases.empty());
}

TEST_CASE("RDC2 打开后注入；关闭时只对仍按下的键与鼠标按钮补发松开", "[rdctl]")
{
    RdControlGate gate;
    std::vector<RdInputCommand> releases;
    gate.SetAllowed(true, kRdcCursor, releases);
    REQUIRE(gate.IsAllowed());

    CHECK(gate.Filter(mouseCmd(WM_MOUSEMOVE, kRdcX, kRdcY)));
    //左键按下、右键按下又松开：关闭时只该补发左键松开
    CHECK(gate.Filter(mouseCmd(WM_LBUTTONDOWN, kRdcX, kRdcY)));
    CHECK(gate.Filter(mouseCmd(WM_RBUTTONDOWN, kRdcX, kRdcY)));
    CHECK(gate.Filter(mouseCmd(WM_RBUTTONUP, kRdcX, kRdcY)));
    //Ctrl、Shift 按下，A 按下又松开：关闭时补发 Shift、Ctrl 的抬起（按下的逆序）
    CHECK(gate.Filter(keyCmd(WM_KEYDOWN, VK_CONTROL)));
    CHECK(gate.Filter(keyCmd(WM_KEYDOWN, VK_SHIFT)));
    CHECK(gate.Filter(keyCmd(WM_KEYDOWN, 'A')));
    CHECK(gate.Filter(keyCmd(WM_KEYUP, 'A')));
    //双击由注入器自己按下又松开，不留下按住的按钮
    CHECK(gate.Filter(mouseCmd(WM_LBUTTONDBLCLK, kRdcX, kRdcY)));
    CHECK(gate.Filter(mouseCmd(WM_MOUSEWHEEL, kRdcX, kRdcY)));

    gate.SetAllowed(false, kRdcCursor, releases);
    CHECK_FALSE(gate.IsAllowed());
    REQUIRE(releases.size() == 3);
    CHECK(releases[0].type == rd_input_key);
    CHECK(releases[0].message == (uint32_t)WM_KEYUP);
    CHECK(releases[0].vkey == (uint32_t)VK_SHIFT);
    CHECK(releases[1].type == rd_input_key);
    CHECK(releases[1].vkey == (uint32_t)VK_CONTROL);
    CHECK(releases[2].type == rd_input_mouse);
    CHECK(releases[2].message == (uint32_t)WM_LBUTTONUP);
    CHECK(releases[2].x == kRdcCursor.x);
    CHECK(releases[2].y == kRdcCursor.y);

    //关闭之后再来的输入一律丢弃；再次关闭不重复补发
    CHECK_FALSE(gate.Filter(mouseCmd(WM_LBUTTONUP, kRdcX, kRdcY)));
    gate.SetAllowed(false, kRdcCursor, releases);
    CHECK(releases.empty());
}

TEST_CASE("RDC3 落在本进程窗口上的输入不注入，松开类输入不受限", "[rdctl]")
{
    RdControlGate gate;
    gate.SetOwnWindowProbe(fakeOwnWindowProbe);
    std::vector<RdInputCommand> releases;
    gate.SetAllowed(true, kRdcCursor, releases);

    const int32_t ownX = 1200;
    CHECK(gate.Filter(mouseCmd(WM_MOUSEMOVE, kRdcX, kRdcY)));
    CHECK_FALSE(gate.Filter(mouseCmd(WM_MOUSEMOVE, ownX, kRdcY)));
    CHECK_FALSE(gate.Filter(mouseCmd(WM_LBUTTONDOWN, ownX, kRdcY)));
    CHECK_FALSE(gate.Filter(mouseCmd(WM_MOUSEWHEEL, ownX, kRdcY)));
    CHECK_FALSE(gate.Filter(keyCmd(WM_KEYDOWN, VK_F12)));

    //在别处按下、拖到本进程窗口上松开：松开必须照常注入，否则按钮会一直按着
    CHECK(gate.Filter(mouseCmd(WM_LBUTTONDOWN, kRdcX, kRdcY)));
    CHECK(gate.Filter(mouseCmd(WM_LBUTTONUP, ownX, kRdcY)));
    CHECK(gate.Filter(keyCmd(WM_KEYUP, VK_F12)));

    //被丢弃的按下不算按住：关闭时不补发
    gate.SetAllowed(false, kRdcCursor, releases);
    CHECK(releases.empty());
}

TEST_CASE("RDC4 新会话开始时复位为关闭、清空按下记录，不补发松开", "[rdctl]")
{
    RdControlGate gate;
    std::vector<RdInputCommand> releases;
    gate.SetAllowed(true, kRdcCursor, releases);
    CHECK(gate.Filter(keyCmd(WM_KEYDOWN, VK_MENU)));
    CHECK(gate.Filter(mouseCmd(WM_MBUTTONDOWN, kRdcX, kRdcY)));

    gate.Reset();
    CHECK_FALSE(gate.IsAllowed());
    CHECK_FALSE(gate.Filter(keyCmd(WM_KEYDOWN, 'B')));

    //复位已清空按下记录：再打开、关闭都没有要补发的
    gate.SetAllowed(true, kRdcCursor, releases);
    gate.SetAllowed(false, kRdcCursor, releases);
    CHECK(releases.empty());
}
