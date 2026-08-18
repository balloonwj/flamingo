/**
 *  通话窗底部操作条纯数据模型单元测试（Suite AB / AH），CallActionBarModelTests.cpp
 *
 *  覆盖：BuildActionBar 在三种形态 × 媒体类型 × 摄像头 / 静音开关下的按钮集合、顺序、
 *  激活态与危险态；ShouldShowActionBar 的自动隐藏时序（含边界、悬停、语音态恒显、
 *  时钟回拨）。纯内存断言，不创建窗口、不碰绘制。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-07-31
 */
#include "catch_amalgamated.hpp"
#include "../Feature/Call/CallActionBarModel.h"

#include <algorithm>

using namespace call;

namespace {

//取按钮 id 序列，便于逐条比对顺序。
std::vector<int> IdsOf(const std::vector<CallActionItem>& items)
{
    std::vector<int> ids;
    for (size_t i = 0; i < items.size(); ++i)
    {
        ids.push_back(items[i].m_id);
    }
    return ids;
}

//在序列里找某个 id 的下标；找不到返回 -1。
int IndexOf(const std::vector<CallActionItem>& items, int id)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_id == id)
        {
            return (int)i;
        }
    }
    return -1;
}

bool Contains(const std::vector<CallActionItem>& items, int id)
{
    return IndexOf(items, id) >= 0;
}

} // anonymous namespace

// =====================================================================
// AB. BuildActionBar —— 按钮集合、顺序、激活态、危险态
// =====================================================================

TEST_CASE("AB1 通话中视频态：静音 / 音量 / 摄像头 / 切换 / 挂断 五项，顺序固定", "[callbar]")
{
    std::vector<CallActionItem> items = BuildActionBar(kBarConnected, true, true, false);
    REQUIRE(items.size() == 5);

    std::vector<int> ids = IdsOf(items);
    CHECK(ids[0] == kActionMute);
    CHECK(ids[1] == kActionVolume);
    CHECK(ids[2] == kActionCamera);
    CHECK(ids[3] == kActionSwitchMedia);
    CHECK(ids[4] == kActionHangup);

    //挂断是本形态下唯一的危险操作
    int dangerCount = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_danger)
        {
            ++dangerCount;
        }
    }
    CHECK(dangerCount == 1);
    CHECK(items[4].m_danger);
}

TEST_CASE("AB2 通话中语音态：不含摄像头，其余四项在位", "[callbar]")
{
    std::vector<CallActionItem> items = BuildActionBar(kBarConnected, false, false, false);
    REQUIRE(items.size() == 4);

    CHECK_FALSE(Contains(items, kActionCamera));   //语音态没有本端画面可开关
    CHECK(Contains(items, kActionMute));
    CHECK(Contains(items, kActionVolume));
    CHECK(Contains(items, kActionSwitchMedia));
    CHECK(Contains(items, kActionHangup));
}

TEST_CASE("AB3 来电态：拒绝在左、接听在右，拒绝为危险操作", "[callbar]")
{
    std::vector<CallActionItem> items = BuildActionBar(kBarIncoming, false, false, false);
    REQUIRE(items.size() == 2);

    CHECK(items[0].m_id == kActionReject);
    CHECK(items[0].m_danger);          //危险操作在左，避免误点接听的位置
    CHECK(items[1].m_id == kActionAccept);
    CHECK_FALSE(items[1].m_danger);

    //通话尚未接通，这些操作无从谈起
    CHECK_FALSE(Contains(items, kActionMute));
    CHECK_FALSE(Contains(items, kActionVolume));
    CHECK_FALSE(Contains(items, kActionCamera));
    CHECK_FALSE(Contains(items, kActionSwitchMedia));
}

TEST_CASE("AB4 呼叫中态：只有取消且为危险操作", "[callbar]")
{
    std::vector<CallActionItem> items = BuildActionBar(kBarCalling, false, false, false);
    REQUIRE(items.size() == 1);
    CHECK(items[0].m_id == kActionCancel);
    CHECK(items[0].m_danger);
}

TEST_CASE("AB5 已静音时静音项呈激活态，其余项不受影响", "[callbar]")
{
    std::vector<CallActionItem> off = BuildActionBar(kBarConnected, true, true, false);
    int muteIdx = IndexOf(off, kActionMute);
    REQUIRE(muteIdx >= 0);
    CHECK_FALSE(off[muteIdx].m_active);

    std::vector<CallActionItem> on = BuildActionBar(kBarConnected, true, true, true);
    muteIdx = IndexOf(on, kActionMute);
    REQUIRE(muteIdx >= 0);
    CHECK(on[muteIdx].m_active);

    //静音与否不改变按钮集合，只改激活态
    CHECK(IdsOf(off) == IdsOf(on));
    //切换与挂断不受静音影响
    CHECK_FALSE(on[IndexOf(on, kActionSwitchMedia)].m_active);
    CHECK_FALSE(on[IndexOf(on, kActionHangup)].m_active);
}

TEST_CASE("AB6 摄像头已关闭时该项呈激活态（需高亮提示的状态）", "[callbar]")
{
    std::vector<CallActionItem> camOn = BuildActionBar(kBarConnected, true, true, false);
    int idx = IndexOf(camOn, kActionCamera);
    REQUIRE(idx >= 0);
    CHECK_FALSE(camOn[idx].m_active);   //摄像头开着=常态，不高亮

    std::vector<CallActionItem> camOff = BuildActionBar(kBarConnected, true, false, false);
    idx = IndexOf(camOff, kActionCamera);
    REQUIRE(idx >= 0);
    CHECK(camOff[idx].m_active);        //已关闭=需要提示的状态，高亮
}

TEST_CASE("AB7 任意组合下按钮 id 不重复，非接通态不出现通话中专属按钮", "[callbar]")
{
    const int modes[] = { kBarCalling, kBarIncoming, kBarConnected };
    for (int m = 0; m < 3; ++m)
    {
        for (int v = 0; v < 2; ++v)
        {
            for (int c = 0; c < 2; ++c)
            {
                for (int mu = 0; mu < 2; ++mu)
                {
                    INFO("mode=" << modes[m] << " video=" << v << " cam=" << c << " muted=" << mu);
                    std::vector<CallActionItem> items =
                        BuildActionBar(modes[m], v != 0, c != 0, mu != 0);

                    std::vector<int> ids = IdsOf(items);
                    std::sort(ids.begin(), ids.end());
                    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

                    if (modes[m] != kBarConnected)
                    {
                        CHECK_FALSE(Contains(items, kActionMute));
                        CHECK_FALSE(Contains(items, kActionVolume));
                        CHECK_FALSE(Contains(items, kActionCamera));
                        CHECK_FALSE(Contains(items, kActionSwitchMedia));
                    }
                }
            }
        }
    }
}

TEST_CASE("AB8 音量项恒非激活（它是弹滑块的入口，没有开关两态）", "[callbar][volume]")
{
    //静音开与关、摄像头开与关，都不该让音量项变成激活态
    for (int mu = 0; mu < 2; ++mu)
    {
        for (int c = 0; c < 2; ++c)
        {
            std::vector<CallActionItem> items =
                BuildActionBar(kBarConnected, true, c != 0, mu != 0);
            int idx = IndexOf(items, kActionVolume);
            REQUIRE(idx >= 0);
            INFO("muted=" << mu << " cam=" << c);
            CHECK_FALSE(items[idx].m_active);
            CHECK_FALSE(items[idx].m_danger);
        }
    }
}

TEST_CASE("AB9 音量项紧跟在静音之后、摄像头之前，顺序稳定", "[callbar][volume]")
{
    //音量调的是"我听到的"、静音掐的是"对方听到的"，两者相邻成组便于理解
    std::vector<CallActionItem> video = BuildActionBar(kBarConnected, true, true, false);
    CHECK(IndexOf(video, kActionVolume) == IndexOf(video, kActionMute) + 1);
    CHECK(IndexOf(video, kActionCamera) == IndexOf(video, kActionVolume) + 1);

    //语音态没有摄像头，音量之后直接是切换
    std::vector<CallActionItem> audio = BuildActionBar(kBarConnected, false, false, false);
    CHECK(IndexOf(audio, kActionVolume) == IndexOf(audio, kActionMute) + 1);
    CHECK(IndexOf(audio, kActionSwitchMedia) == IndexOf(audio, kActionVolume) + 1);
}

TEST_CASE("AB10 非接通态不含音量项", "[callbar][volume]")
{
    CHECK_FALSE(Contains(BuildActionBar(kBarCalling, false, false, false), kActionVolume));
    CHECK_FALSE(Contains(BuildActionBar(kBarIncoming, false, false, false), kActionVolume));
}

// =====================================================================
// AH. ShouldShowActionBar —— 视频态操作条的自动隐藏时序
// =====================================================================

TEST_CASE("AH1 刚有鼠标活动时显示", "[callbar][autohide]")
{
    const unsigned long long kNow = 1000000ULL;
    CHECK(ShouldShowActionBar(kNow, kNow, false, true));
}

TEST_CASE("AH2 静止未达阈值仍显示", "[callbar][autohide]")
{
    const unsigned long long kLast = 1000000ULL;
    CHECK(ShouldShowActionBar(kLast, kLast + kActionBarIdleHideMs - 1, false, true));
}

TEST_CASE("AH3 静止超过阈值即隐藏", "[callbar][autohide]")
{
    const unsigned long long kLast = 1000000ULL;
    CHECK_FALSE(ShouldShowActionBar(kLast, kLast + kActionBarIdleHideMs + 1, false, true));
    //远超阈值同样隐藏
    CHECK_FALSE(ShouldShowActionBar(kLast, kLast + 60ULL * 1000ULL, false, true));
}

TEST_CASE("AH4 边界：正好等于阈值时按显示处理", "[callbar][autohide]")
{
    //与项目里滚动条 auto-hide 的口径一致：严格大于才算超时
    const unsigned long long kLast = 1000000ULL;
    CHECK(ShouldShowActionBar(kLast, kLast + kActionBarIdleHideMs, false, true));
}

TEST_CASE("AH5 悬停在操作条上时恒显示，无论静止多久", "[callbar][autohide]")
{
    //否则鼠标刚移上去它就淡出，根本点不着
    const unsigned long long kLast = 1000000ULL;
    CHECK(ShouldShowActionBar(kLast, kLast + 60ULL * 1000ULL, true, true));
}

TEST_CASE("AH6 语音态恒显示", "[callbar][autohide]")
{
    //语音态画面区只有头像与昵称，操作条不挡任何东西，藏起来只会让用户找不到按钮
    const unsigned long long kLast = 1000000ULL;
    CHECK(ShouldShowActionBar(kLast, kLast + 60ULL * 1000ULL, false, false));
}

TEST_CASE("AH7 时钟回拨（now 早于 lastActivity）按显示处理", "[callbar][autohide]")
{
    //无符号相减会绕回成极大值，若不先判回拨会被误判成"静止了很久"而隐藏
    const unsigned long long kLast = 1000000ULL;
    CHECK(ShouldShowActionBar(kLast, kLast - 5000ULL, false, true));
}
