/**
 *  屏幕共享选择窗规则的单元测试（标签 [sharepassword] 与 [sharepicker]），SharePickerPolicyTests.cpp
 *
 *  覆盖 FlamingoMedia/Share/SharePickerPolicy.h（docs/屏幕共享与录制方案.md 第 12 期）：
 *    - SSPWC1 自动生成的共享密码总是 6 位数字，不足 6 位时前面补 0；
 *    - SSPWC2 密码校验只认 6 位 ASCII 数字；输入过滤只留 ASCII 数字、最多 6 位；
 *    - SSPWC4 只有「发起共享」时选择窗提供「设置密码」；勾了「设置密码」而密码不是 6 位数字时不能点「开始共享」；
 *    - SSREF1 点「刷新」重新列出后，原先选中的内容还在就继续选中（显示器按显卡与输出序号认、窗口按句柄认），
 *             已经不在了就不选。
 *    - SSCAP1 字幕勾选项（2026-10-07）：录制、发起共享、通话中发起时可以有，切换内容时没有。
 *  框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[sharepassword],[sharepicker]"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../../FlamingoMedia/Share/SharePickerPolicy.h"

using namespace screenshare;

namespace {

//自动生成密码时连续生成的次数：次数足够多才能覆盖到不足 6 位、需要补 0 的随机数
const int kSspwcGenerateRounds = 2000;

//两个不同的窗口句柄（只作比较用，不是真窗口）
const HWND kSsrefWndA = reinterpret_cast<HWND>(static_cast<INT_PTR>(0x10010));
const HWND kSsrefWndB = reinterpret_cast<HWND>(static_cast<INT_PTR>(0x20020));
const HWND kSsrefWndC = reinterpret_cast<HWND>(static_cast<INT_PTR>(0x30030));

ShareSourceKey ssrefMonitor(int adapter, int output)
{
    ShareSourceKey key;
    key.m_monitor = true;
    key.m_adapter = adapter;
    key.m_output = output;
    key.m_hwnd = NULL;
    return key;
}

ShareSourceKey ssrefWindow(HWND hwnd)
{
    ShareSourceKey key;
    key.m_monitor = false;
    key.m_adapter = 0;
    key.m_output = 0;
    key.m_hwnd = hwnd;
    return key;
}

} // namespace

TEST_CASE("SSPWC1 自动生成的共享密码总是 6 位数字", "[sharepassword]")
{
    //由随机数得到密码：取随机数的末 6 位，不足 6 位前面补 0
    CHECK(MakeSharePassword(0) == L"000000");
    CHECK(MakeSharePassword(42) == L"000042");
    CHECK(MakeSharePassword(482915) == L"482915");
    CHECK(MakeSharePassword(999999) == L"999999");
    CHECK(MakeSharePassword(1000000) == L"000000");
    CHECK(MakeSharePassword(4294967295u) == L"967295");

    //真随机生成的密码都合法，且多次生成不会总是同一个
    std::wstring first = GenerateSharePassword();
    bool allSame = true;
    for (int i = 0; i < kSspwcGenerateRounds; ++i)
    {
        std::wstring pwd = GenerateSharePassword();
        CHECK(IsValidSharePassword(pwd));
        if (pwd != first)
            allSame = false;
    }
    CHECK_FALSE(allSame);
}

TEST_CASE("SSPWC2 密码校验与输入过滤", "[sharepassword]")
{
    CHECK(IsValidSharePassword(L"482915"));
    CHECK(IsValidSharePassword(L"000000"));
    //空、位数不对
    CHECK_FALSE(IsValidSharePassword(L""));
    CHECK_FALSE(IsValidSharePassword(L"48291"));
    CHECK_FALSE(IsValidSharePassword(L"4829150"));
    //夹了字母、空格、全角数字
    CHECK_FALSE(IsValidSharePassword(L"48a915"));
    CHECK_FALSE(IsValidSharePassword(L"48 915"));
    CHECK_FALSE(IsValidSharePassword(L"\xFF14\xFF18\xFF12\xFF19\xFF11\xFF15"));

    //输入过滤：只留 ASCII 数字，超过 6 位的部分截掉
    CHECK(FilterSharePasswordInput(L"482915") == L"482915");
    CHECK(FilterSharePasswordInput(L"48a29 15") == L"482915");
    CHECK(FilterSharePasswordInput(L"1234567") == L"123456");
    CHECK(FilterSharePasswordInput(L"\xFF11\xFF12") == L"");
    CHECK(FilterSharePasswordInput(L"") == L"");

    //协议里的密码是 UTF-8 字符串，6 位 ASCII 数字转换后原样不变
    CHECK(SharePasswordToUtf8(L"000042") == "000042");
    CHECK(SharePasswordToUtf8(L"") == "");
}

TEST_CASE("SSPWC4 只有发起共享时提供密码，密码不合法时不能开始共享", "[sharepassword]")
{
    CHECK(SharePickerOffersPassword(share_picker_start));
    //切换内容、录屏、通话中发起都不提供
    CHECK_FALSE(SharePickerOffersPassword(share_picker_switch));
    CHECK_FALSE(SharePickerOffersPassword(share_picker_record));
    CHECK_FALSE(SharePickerOffersPassword(share_picker_start_in_call));

    //没有选中任何内容：一律不能提交
    CHECK_FALSE(SharePickerCanSubmit(false, false, L""));
    CHECK_FALSE(SharePickerCanSubmit(false, true, L"482915"));
    //选中了、没勾密码：密码框里是什么都不影响
    CHECK(SharePickerCanSubmit(true, false, L""));
    CHECK(SharePickerCanSubmit(true, false, L"12"));
    //选中了、勾了密码：密码必须是 6 位数字
    CHECK(SharePickerCanSubmit(true, true, L"482915"));
    CHECK_FALSE(SharePickerCanSubmit(true, true, L"48291"));
    CHECK_FALSE(SharePickerCanSubmit(true, true, L""));
}

TEST_CASE("SSREF1 刷新后原先选中的内容还在就继续选中", "[sharepicker]")
{
    std::vector<ShareSourceKey> items;
    items.push_back(ssrefMonitor(0, 0));
    items.push_back(ssrefMonitor(0, 1));
    items.push_back(ssrefWindow(kSsrefWndA));
    items.push_back(ssrefWindow(kSsrefWndB));

    //显示器按显卡与输出序号认
    CHECK(FindShareSourceAfterRefresh(ssrefMonitor(0, 1), items) == 1);
    CHECK(FindShareSourceAfterRefresh(ssrefMonitor(1, 0), items) == -1);
    //窗口按句柄认；刷新后列表里新出现的窗口排在前面时，下标跟着变
    CHECK(FindShareSourceAfterRefresh(ssrefWindow(kSsrefWndB), items) == 3);
    std::vector<ShareSourceKey> reordered;
    reordered.push_back(ssrefMonitor(0, 0));
    reordered.push_back(ssrefWindow(kSsrefWndC));
    reordered.push_back(ssrefWindow(kSsrefWndB));
    CHECK(FindShareSourceAfterRefresh(ssrefWindow(kSsrefWndB), reordered) == 2);
    //窗口已经关掉：不选
    CHECK(FindShareSourceAfterRefresh(ssrefWindow(kSsrefWndA), reordered) == -1);
    //窗口句柄为 NULL 时不会与显示器项混认
    ShareSourceKey nullWindow = ssrefWindow(NULL);
    CHECK(FindShareSourceAfterRefresh(nullWindow, items) == -1);
    //空列表
    CHECK(FindShareSourceAfterRefresh(ssrefMonitor(0, 0), std::vector<ShareSourceKey>()) == -1);
}

TEST_CASE("SSCAP1 caption check is offered when recording and when starting a share", "[sharepicker]")
{
    CHECK(SharePickerOffersCaption(share_picker_record));
    CHECK(SharePickerOffersCaption(share_picker_start));
    CHECK(SharePickerOffersCaption(share_picker_start_in_call));
    CHECK_FALSE(SharePickerOffersCaption(share_picker_switch));
}
