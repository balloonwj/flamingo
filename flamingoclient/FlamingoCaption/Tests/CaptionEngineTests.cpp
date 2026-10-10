/**
 *  CaptionEngineTests.cpp
 *
 *  语音字幕识别前处理与设置的单元测试，标签 [caption]：
 *    CAPE  —— 声音处理（Engine/CaptionAudio）：48 kHz → 16 kHz 重采样的连续性与频率响应、采样序号到真实时刻的换算；
 *    CAPST —— 按账号的字幕设置（Model/CaptionSettings）：默认值、JSON 往返、保留不认识的键。
 *
 *  纯内存断言。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <math.h>
#include <string>
#include <vector>

#include "../Engine/CaptionAudio.h"
#include "../Model/CaptionLayout.h"
#include "../Model/CaptionSettings.h"
#include "../Model/CaptionText.h"

using namespace caption;

namespace {

//测试信号的幅度（16 位采样）
const double kAmplitude = 16000.0;
const double kTwoPi = 6.283185307179586;

std::vector<int16_t> Sine(double hz, size_t samples)
{
    std::vector<int16_t> v(samples);
    for (size_t i = 0; i < samples; ++i)
        v[i] = static_cast<int16_t>(kAmplitude * sin(kTwoPi * hz * i / kInputSampleRate));
    return v;
}

//跳过开头 skip 个输出后的最大绝对值，归一化到输入幅度
double PeakAfter(const std::vector<float>& v, size_t skip)
{
    double peak = 0.0;
    for (size_t i = skip; i < v.size(); ++i)
    {
        const double a = fabs(v[i]);
        if (a > peak)
            peak = a;
    }
    return peak * 32768.0 / kAmplitude;
}

} // namespace

TEST_CASE("CAPE1 重采样：输出个数为输入的三分之一（向上取整），分段处理与一次处理结果相同", "[caption]")
{
    const std::vector<int16_t> in = Sine(440.0, 4801);
    CaptionResampler whole;
    std::vector<float> a;
    whole.Process(in.data(), in.size(), a);
    CHECK(a.size() == 1601);

    CaptionResampler parts;
    std::vector<float> b;
    size_t pos = 0;
    const size_t chunks[] = { 1, 2, 7, 480, 1000, 3311 };
    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i)
    {
        parts.Process(in.data() + pos, chunks[i], b);
        pos += chunks[i];
    }
    REQUIRE(pos == in.size());
    REQUIRE(b.size() == a.size());
    for (size_t i = 0; i < a.size(); ++i)
        CHECK(b[i] == Catch::Approx(a[i]).margin(1e-6));
    parts.Reset();
    std::vector<float> c;
    parts.Process(in.data(), 3, c);
    CHECK(c.size() == 1);
}

TEST_CASE("CAPE2 重采样的频率响应：1 kHz 基本不衰减，12 kHz（会折叠到 4 kHz）被滤掉", "[caption]")
{
    const size_t n = kInputSampleRate / 2;
    CaptionResampler low;
    std::vector<float> lowOut;
    const std::vector<int16_t> lowIn = Sine(1000.0, n);
    low.Process(lowIn.data(), lowIn.size(), lowOut);
    CHECK(PeakAfter(lowOut, 100) == Catch::Approx(1.0).margin(0.05));

    CaptionResampler high;
    std::vector<float> highOut;
    const std::vector<int16_t> highIn = Sine(12000.0, n);
    high.Process(highIn.data(), highIn.size(), highOut);
    CHECK(PeakAfter(highOut, 100) < 0.05);
}

TEST_CASE("CAPE3 时间换算：首尾相接的段合并；时间跳变另起一段；段前段后按采样率推算；可丢弃旧段", "[caption]")
{
    CaptionTimeMap m;
    CHECK(m.ToPts(100) == 0);
    m.Append(0, 1000, 1600);          //0～0.1 秒的声音在 1000 毫秒
    m.Append(1600, 1100, 1600);       //紧接着
    CHECK(m.SegmentCount() == 1);
    CHECK(m.ToPts(0) == 1000);
    CHECK(m.ToPts(1600) == 1100);
    CHECK(m.ToPts(3199) == 1199);
    m.Append(3200, 5000, 1600);       //麦克风关了一阵再打开：时刻跳到 5000
    CHECK(m.SegmentCount() == 2);
    CHECK(m.ToPts(3200) == 5000);
    CHECK(m.ToPts(4000) == 5050);
    CHECK(m.ToPts(8000) == 5300);     //最后一段之后按采样率往后推
    CHECK(m.ToPts(-1600) == 900);     //第一段之前按采样率往前推
    m.DropBefore(3300);
    CHECK(m.SegmentCount() == 1);
    CHECK(m.ToPts(3200) == 5000);
    m.DropBefore(100000);
    CHECK(m.SegmentCount() == 1);     //至少保留最后一段
    m.Clear();
    CHECK(m.SegmentCount() == 0);
}

TEST_CASE("CAPST1 设置：空文本取默认值；不是 JSON 对象时返回 false 并取默认值", "[caption]")
{
    CaptionSettings s;
    CHECK(s.FromJson(""));
    CHECK(s.m_recordMic);
    CHECK_FALSE(s.m_recordSystem);
    CHECK(s.m_exportHard);
    CHECK_FALSE(s.m_exportTrack);
    CHECK_FALSE(s.m_exportSrt);
    CHECK_FALSE(s.m_liveSelf);
    CHECK(s.m_liveShow);
    CHECK(s.m_replace.Rules().empty());
    s.m_exportTrack = true;
    CHECK_FALSE(s.FromJson("[1]"));
    CHECK_FALSE(s.m_exportTrack);
}

TEST_CASE("CAPST2 设置的 JSON 往返；保留不认识的键与各对象里不认识的项", "[caption]")
{
    CaptionSettings s;
    REQUIRE(s.FromJson("{\"other\":{\"x\":5},\"record\":{\"mic\":0,\"future\":7}}"));
    CHECK_FALSE(s.m_recordMic);
    s.m_liveSelf = true;
    s.m_exportSrt = true;
    CaptionReplaceRule r;
    r.m_from = L"Sclight";
    r.m_to = L"SQLite";
    s.m_replace.Add(r);
    const std::string json = s.ToJson();
    CHECK(json.find("\"other\"") != std::string::npos);
    CHECK(json.find("\"future\"") != std::string::npos);

    CaptionSettings back;
    REQUIRE(back.FromJson(json));
    CHECK_FALSE(back.m_recordMic);
    CHECK(back.m_liveSelf);
    CHECK(back.m_exportSrt);
    REQUIRE(back.m_replace.Rules().size() == 1);
    CHECK(back.m_replace.Rules()[0].m_to == L"SQLite");
}

//==================== CAPL 字幕折行 ====================

namespace {

//假的测量函数：每个显示宽度单位算 10（汉字 20、英文字母 10）
float FakeMeasure(void* ctx, const wchar_t* text, int len)
{
    (void)ctx;
    return static_cast<float>(DisplayUnits(std::wstring(text, static_cast<size_t>(len))) * 10);
}

} // namespace

TEST_CASE("CAPL1 折行：放得下不折；中文随处断开；英文不拆单词；行首不留空格", "[caption]")
{
    std::vector<std::wstring> l = WrapCaptionText(L"短句", 200.0f, &FakeMeasure, NULL);
    REQUIRE(l.size() == 1);
    CHECK(l[0] == L"短句");
    l = WrapCaptionText(L"一二三四五六七八九十", 100.0f, &FakeMeasure, NULL);
    REQUIRE(l.size() == 2);
    CHECK(l[0] == L"一二三四五");
    CHECK(l[1] == L"六七八九十");
    l = WrapCaptionText(L"hello world again", 120.0f, &FakeMeasure, NULL);
    REQUIRE(l.size() == 2);
    CHECK(l[0] == L"hello world");
    CHECK(l[1] == L"again");
    CHECK(WrapCaptionText(L"", 100.0f, &FakeMeasure, NULL).empty());
}

TEST_CASE("CAPL2 折行：标点不放到行首；换行符强制换行；超长单词硬断", "[caption]")
{
    std::vector<std::wstring> l = WrapCaptionText(L"一二三四，五六七", 80.0f, &FakeMeasure, NULL);
    REQUIRE(l.size() >= 2);
    CHECK(l[1][0] != L'，');
    l = WrapCaptionText(L"第一行\n第二行", 1000.0f, &FakeMeasure, NULL);
    REQUIRE(l.size() == 2);
    CHECK(l[1] == L"第二行");
    l = WrapCaptionText(L"abcdefghijkl", 50.0f, &FakeMeasure, NULL);
    REQUIRE(l.size() == 3);
    CHECK(l[0] == L"abcde");
}
