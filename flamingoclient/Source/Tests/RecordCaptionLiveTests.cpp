/**
 *  RecordCaptionLiveTests.cpp
 *
 *  录屏时屏幕上实时字幕的显示规则（docs/语音字幕方案.md 14.12，FlamingoMedia/Caption/RecordCaptionLiveRules），标签 [captionlive]：
 *    CAPRL1 —— 句子编号：每一路各自从 1 起；中间结果沿用当前编号，定稿也用当前编号、之后换下一个；Reset 后回到 1；
 *    CAPRL2 —— 说话人标注：麦克风与电脑声音都在录时分别标本人名字与「电脑声音」，只有一路在录时不标，其他来源不标；
 *    CAPRL3 —— 有没有声音可识别：「生成字幕」同时识别麦克风与电脑声音，任一路在录就有，两路都没在录时没有（要提示）。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../../FlamingoCaption/Model/CaptionCue.h"
#include "../../FlamingoMedia/Caption/RecordCaptionLiveRules.h"

using flamingomedia::RecordCaptionSentences;
using flamingomedia::RecordCaptionSpeaker;

TEST_CASE("CAPRL1 录制实时字幕的句子编号：各路从 1 起，定稿后换下一句，Reset 回到 1", "[captionlive]")
{
    const int mic = caption::caption_source_mic;
    const int system = caption::caption_source_system;
    const uint32_t kFirst = 1;
    const uint32_t kSecond = 2;
    RecordCaptionSentences s;
    //麦克风一路：两次中间结果、一次定稿都是第 1 句，之后的中间结果是第 2 句
    CHECK(s.Take(mic, false) == kFirst);
    CHECK(s.Take(mic, false) == kFirst);
    CHECK(s.Take(mic, true) == kFirst);
    CHECK(s.Take(mic, false) == kSecond);
    //电脑声音一路不受麦克风一路影响
    CHECK(s.Take(system, false) == kFirst);
    CHECK(s.Take(system, true) == kFirst);
    CHECK(s.Take(system, true) == kSecond);
    //重新开始
    s.Reset();
    CHECK(s.Take(mic, false) == kFirst);
    CHECK(s.Take(system, true) == kFirst);
}

TEST_CASE("CAPRL2 录制实时字幕的说话人：两路都在录才标注，麦克风标本人、电脑声音标「电脑声音」", "[captionlive]")
{
    const int mic = caption::caption_source_mic;
    const int system = caption::caption_source_system;
    const std::wstring self = L"张三";
    const std::wstring label = L"电脑声音";
    //两路都在录
    CHECK(RecordCaptionSpeaker(mic, true, true, self, label) == self);
    CHECK(RecordCaptionSpeaker(system, true, true, self, label) == label);
    //只有一路在录：都不标
    CHECK(RecordCaptionSpeaker(mic, true, false, self, label).empty());
    CHECK(RecordCaptionSpeaker(system, false, true, self, label).empty());
    CHECK(RecordCaptionSpeaker(mic, false, false, self, label).empty());
    //其他来源不标
    CHECK(RecordCaptionSpeaker(caption::caption_source_others, true, true, self, label).empty());
}

TEST_CASE("CAPRL3 有没有声音可识别：麦克风或电脑声音任一路在录就有，两路都没在录时没有", "[captionlive]")
{
    using flamingomedia::RecordCaptionHasSource;
    CHECK(RecordCaptionHasSource(true, false));
    //只录电脑声音（用户 2026-10-06 遇到的情况）：电脑声音也识别，有字幕
    CHECK(RecordCaptionHasSource(false, true));
    CHECK(RecordCaptionHasSource(true, true));
    CHECK_FALSE(RecordCaptionHasSource(false, false));
}
