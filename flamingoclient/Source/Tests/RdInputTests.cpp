/**
 *  远程桌面输入编码 / 解析 / 坐标映射单元测试（Suite RDIN），RdInputTests.cpp
 *  覆盖：鼠标 / 键盘命令 encode→parse round-trip（含负坐标、滚轮正负值）、
 *       观看窗客户区坐标→被控端屏幕坐标的等比映射与边界夹取、畸形命令安全解析。
 *  不测真实注入（mouse_event/keybd_event 为系统调用，留端到端验证）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-08
 */
#include "catch_amalgamated.hpp"
#include "../RemoteDesktop/RdInputEncoder.h"
#include "../RemoteDesktop/RdInputInjector.h"
#include "../RemoteDesktop/RdInputDef.h"

using namespace remotedesktop;

TEST_CASE("RDIN1 鼠标命令 encode→parse round-trip（含负坐标）")
{
    std::vector<BYTE> buf;
    RdInputEncoder::encodeMouse(buf, WM_LBUTTONDOWN, -50, 720, 0);
    REQUIRE(buf.size() == (size_t)rd_input_mouse_len);

    RdInputCommand cmd;
    REQUIRE(RdInputInjector::parse(buf.data(), buf.size(), cmd));
    CHECK(cmd.type == rd_input_mouse);
    CHECK(cmd.message == (uint32_t)WM_LBUTTONDOWN);
    CHECK(cmd.x == -50);
    CHECK(cmd.y == 720);
    CHECK(cmd.wheel == 0);
}

TEST_CASE("RDIN2 键盘命令 encode→parse round-trip")
{
    std::vector<BYTE> buf;
    RdInputEncoder::encodeKey(buf, WM_KEYDOWN, 'A');
    REQUIRE(buf.size() == (size_t)rd_input_key_len);

    RdInputCommand cmd;
    REQUIRE(RdInputInjector::parse(buf.data(), buf.size(), cmd));
    CHECK(cmd.type == rd_input_key);
    CHECK(cmd.message == (uint32_t)WM_KEYDOWN);
    CHECK(cmd.vkey == (uint32_t)'A');
}

TEST_CASE("RDIN3 滚轮增量正负值 round-trip")
{
    std::vector<BYTE> buf;
    RdInputCommand cmd;

    RdInputEncoder::encodeMouse(buf, WM_MOUSEWHEEL, 10, 20, 120);
    REQUIRE(RdInputInjector::parse(buf.data(), buf.size(), cmd));
    CHECK(cmd.wheel == 120);

    RdInputEncoder::encodeMouse(buf, WM_MOUSEWHEEL, 10, 20, -120);
    REQUIRE(RdInputInjector::parse(buf.data(), buf.size(), cmd));
    CHECK(cmd.wheel == -120);
}

TEST_CASE("RDIN4 客户区坐标→被控端屏幕坐标等比映射与夹取")
{
    int rx = 0;
    int ry = 0;

    //观看窗 800x600，被控端 1920x1080：中心点映射到屏幕中心
    RdInputEncoder::mapClientToRemote(400, 300, 800, 600, 1920, 1080, rx, ry);
    CHECK(rx == 960);
    CHECK(ry == 540);

    //右下角越界点夹取到屏幕最右下像素
    RdInputEncoder::mapClientToRemote(800, 600, 800, 600, 1920, 1080, rx, ry);
    CHECK(rx == 1919);
    CHECK(ry == 1079);

    //负坐标夹取到 0
    RdInputEncoder::mapClientToRemote(-5, -10, 800, 600, 1920, 1080, rx, ry);
    CHECK(rx == 0);
    CHECK(ry == 0);

    //客户区尺寸为 0 时除零保护，返回 0
    RdInputEncoder::mapClientToRemote(100, 100, 0, 0, 1920, 1080, rx, ry);
    CHECK(rx == 0);
    CHECK(ry == 0);
}

TEST_CASE("RDIN5 畸形命令安全解析")
{
    RdInputCommand cmd;
    //空 / 空指针
    CHECK_FALSE(RdInputInjector::parse(NULL, 0, cmd));
    //鼠标命令长度不足
    BYTE shortMouse[5] = { (BYTE)rd_input_mouse, 1, 2, 3, 4 };
    CHECK_FALSE(RdInputInjector::parse(shortMouse, sizeof(shortMouse), cmd));
    //键盘命令长度不足
    BYTE shortKey[3] = { (BYTE)rd_input_key, 1, 2 };
    CHECK_FALSE(RdInputInjector::parse(shortKey, sizeof(shortKey), cmd));
    //未知类型
    BYTE bad[20] = { 99 };
    CHECK_FALSE(RdInputInjector::parse(bad, sizeof(bad), cmd));
}
