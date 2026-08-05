/**
 *  AvatarImageUtilTests.cpp
 *
 *  头像图片处理（BalloonMain/AvatarImageUtil.h）里纯函数 ComputeCenterSquareCrop
 *  的测试：横图 / 竖图 / 正方 / 奇数边 / 非法入参。GDI+ / 文件相关函数不在此测。
 *
 *  Catch2 v3（amalgamated），纯内存断言。全局 _Module 由其它测试文件统一定义。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/AvatarImageUtil.h"

using namespace balloonmain;

TEST_CASE("AIU1 横图居中裁方：取高为边、水平居中", "[avatarimg]")
{
    int x = -1;
    int y = -1;
    int side = -1;
    REQUIRE(ComputeCenterSquareCrop(200, 100, x, y, side));
    CHECK(side == 100);
    CHECK(x == 50);
    CHECK(y == 0);
}

TEST_CASE("AIU2 竖图居中裁方：取宽为边、垂直居中", "[avatarimg]")
{
    int x = -1;
    int y = -1;
    int side = -1;
    REQUIRE(ComputeCenterSquareCrop(100, 200, x, y, side));
    CHECK(side == 100);
    CHECK(x == 0);
    CHECK(y == 50);
}

TEST_CASE("AIU3 正方图：整张即裁剪区", "[avatarimg]")
{
    int x = -1;
    int y = -1;
    int side = -1;
    REQUIRE(ComputeCenterSquareCrop(128, 128, x, y, side));
    CHECK(side == 128);
    CHECK(x == 0);
    CHECK(y == 0);
}

TEST_CASE("AIU4 奇数边长：整除居中", "[avatarimg]")
{
    int x = -1;
    int y = -1;
    int side = -1;
    REQUIRE(ComputeCenterSquareCrop(201, 100, x, y, side));
    CHECK(side == 100);
    CHECK(x == 50);   // (201-100)/2 = 50（整除）
    CHECK(y == 0);
}

TEST_CASE("AIU5 非法入参返回 false、出参不动", "[avatarimg]")
{
    int x = 7;
    int y = 7;
    int side = 7;
    CHECK_FALSE(ComputeCenterSquareCrop(0, 100, x, y, side));
    CHECK_FALSE(ComputeCenterSquareCrop(100, 0, x, y, side));
    CHECK_FALSE(ComputeCenterSquareCrop(-5, 10, x, y, side));
    CHECK(x == 7);
    CHECK(y == 7);
    CHECK(side == 7);
}
