/**
 *  ChatTextWarmupTests.cpp
 *
 *  文字测量缓存预热 ChatTextWarmup 的测试（标签 [warmup]）。
 *
 *  覆盖：
 *    WU1 字符表：GB2312 一级汉字 3755 个加常用中文标点，无重复、无代理对，首尾分别是"啊"与"座"，
 *        包含最常用的那些字
 *    WU2 预热写进缓存的键与真实测量一致：预热之后，再测量一个只由这些字组成的气泡，
 *        缓存条目数不再增加
 *    WU3 分批：每批最多处理指定字数，剩余字数相应减少；处理完之后再调用不做任何事
 *    WU4 预热不改变排版结果：同一条消息在缓存为空时与预热之后测得的气泡行高相同
 *    WU5 Start 重复调用只启动一次定时器；Stop 之后定时器不再运行
 *
 *  每个用例开始与结束时都把预热器与文字测量缓存恢复到初始状态，不影响其它用例。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <set>
#include <stdio.h>
#include <string>

#include "../UI/BalloonChatWnd/ChatTextWarmup.h"
#include "../UI/BalloonChatWnd/ChatTextExtentCache.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../Common/Util/PerfStopWatch.h"

using namespace balloonchatwnd;

namespace {

//GB2312 一级汉字的个数
const size_t kLevel1Count = 3755;
//WU3 每批的字数
const int kTestSliceChars = 100;
//WU2 / WU4 测量气泡时的可用宽度（像素）
const int kAvailWidth = 620;
//预热字符表末尾的常用中文标点，与 ChatTextWarmup.cpp 的 kWarmupPunctuation 一致
const wchar_t* const kPunctuation = L"，。！？、；：“”‘’（）《》【】…—～·";
//最常用的一批汉字，用来确认字符表覆盖了日常用字
const wchar_t* const kMostCommon = L"的一是了不在人有我他这个们中来上大为和国地到以说时要就出会可也你对生能";
//WU2 / WU4 用的一条只由常用字与标点组成的消息
const wchar_t* const kSampleText = L"我们明天下午三点在会议室开会，记得带上电脑和资料。";

//用例开始与结束时把预热器、文字测量缓存都恢复到初始状态
struct WarmupReset
{
    WarmupReset()
    {
        ChatTextWarmup::Inst().Reset();
        ChatTextExtentCache::Inst().SetEnabled(true);
        ChatTextExtentCache::Inst().Clear();
    }
    ~WarmupReset()
    {
        ChatTextWarmup::Inst().Reset();
        ChatTextExtentCache::Inst().Clear();
    }
};

//把预热器剩下的字全部测完
void RunWarmupToEnd()
{
    while (ChatTextWarmup::Inst().RunSlice(ChatTextWarmup::kSliceChars))
    {
    }
}

//新建一个文字气泡并测量，返回行高
int MeasureRowHeight(const wchar_t* text)
{
    ChatBubble bubble;
    bubble.SetMessage(MakeTextMessage(false, CString(text)));
    bubble.Measure(kAvailWidth);
    return bubble.GetRowHeight();
}

} // namespace

TEST_CASE("WU1 字符表为 GB2312 一级汉字加常用中文标点，无重复、无代理对", "[warmup]")
{
    const std::wstring chars = BuildWarmupCharacters();
    const std::wstring punctuation(kPunctuation);

    REQUIRE(chars.size() == kLevel1Count + punctuation.size());
    CHECK(chars[0] == L'啊');
    CHECK(chars[kLevel1Count - 1] == L'座');
    CHECK(chars.substr(kLevel1Count) == punctuation);

    std::set<wchar_t> seen;
    bool bAllBmpNonAscii = true;
    for (size_t i = 0; i < chars.size(); ++i)
    {
        seen.insert(chars[i]);
        if (chars[i] < 0x80 || (chars[i] >= 0xD800 && chars[i] <= 0xDFFF))
            bAllBmpNonAscii = false;
    }
    CHECK(seen.size() == chars.size());
    CHECK(bAllBmpNonAscii);

    const std::wstring common(kMostCommon);
    int nMissing = 0;
    for (size_t i = 0; i < common.size(); ++i)
    {
        if (seen.find(common[i]) == seen.end())
            ++nMissing;
    }
    CHECK(nMissing == 0);
}

TEST_CASE("WU2 预热写进缓存的键与真实测量一致：预热后再测量常用字组成的气泡不新增缓存条目", "[warmup]")
{
    WarmupReset reset;

    PerfStopWatch sw;
    RunWarmupToEnd();
    const double warmMs = sw.ElapsedMs();
    const size_t afterWarmup = ChatTextExtentCache::Inst().EntryCount();

    MeasureRowHeight(kSampleText);
    const size_t afterMeasure = ChatTextExtentCache::Inst().EntryCount();

    //把实测数字打到标准输出，通过时也能看到
    printf("[WU2] warmup of %u chars took %.1f ms, cache entries %u\n",
           (unsigned)BuildWarmupCharacters().size(), warmMs, (unsigned)afterWarmup);
    CHECK(afterWarmup > 0);
    CHECK(afterMeasure == afterWarmup);
}

TEST_CASE("WU3 分批处理：每批最多处理指定字数，处理完之后再调用不做任何事", "[warmup]")
{
    WarmupReset reset;
    ChatTextWarmup& warmup = ChatTextWarmup::Inst();
    const size_t total = warmup.Remaining();
    REQUIRE(total == kLevel1Count + std::wstring(kPunctuation).size());

    CHECK(warmup.RunSlice(kTestSliceChars));
    CHECK(warmup.Remaining() == total - kTestSliceChars);

    int nSlices = 1;
    while (warmup.RunSlice(kTestSliceChars))
        ++nSlices;
    ++nSlices;   //最后一次返回 false 的那一批同样处理了字

    const int nExpectedSlices = (int)((total + kTestSliceChars - 1) / kTestSliceChars);
    CHECK(nSlices == nExpectedSlices);
    CHECK(warmup.Remaining() == 0);

    const size_t entries = ChatTextExtentCache::Inst().EntryCount();
    CHECK_FALSE(warmup.RunSlice(kTestSliceChars));
    CHECK(warmup.Remaining() == 0);
    CHECK(ChatTextExtentCache::Inst().EntryCount() == entries);
}

TEST_CASE("WU4 预热不改变排版结果：缓存为空时与预热之后的气泡行高相同", "[warmup]")
{
    WarmupReset reset;

    const int coldHeight = MeasureRowHeight(kSampleText);
    ChatTextExtentCache::Inst().Clear();
    RunWarmupToEnd();
    const int warmHeight = MeasureRowHeight(kSampleText);

    CHECK(coldHeight > 0);
    CHECK(warmHeight == coldHeight);
}

TEST_CASE("WU5 Start 重复调用只启动一次定时器，Stop 之后定时器不再运行", "[warmup]")
{
    WarmupReset reset;
    ChatTextWarmup& warmup = ChatTextWarmup::Inst();

    CHECK_FALSE(warmup.IsRunning());
    warmup.Start();
    CHECK(warmup.IsRunning());
    //再次调用不会再建定时器，也不会把进度清零
    warmup.Start();
    CHECK(warmup.IsRunning());

    warmup.Stop();
    CHECK_FALSE(warmup.IsRunning());
    //已经启动过一次，Stop 之后再 Start 不会重新开始
    warmup.Start();
    CHECK_FALSE(warmup.IsRunning());
}
