/**
 *  录屏时屏幕上实时字幕的显示规则（docs/语音字幕方案.md 14.12），RecordCaptionLiveRules.h
 *
 *    - RecordCaptionSentences：每一路（麦克风、电脑声音）各自的句子编号。字幕板（ShareCaptionBoard）按句子编号区分
 *      「同一句的中间结果不断替换」与「新的一句」，录制时的识别结果本身不带编号，由这里按到达顺序分配；
 *    - RecordCaptionSpeaker：某一路的说话人标注。麦克风与电脑声音同时在录时，两路的字幕可能同时出现，分别标上本人名字与
 *      「电脑声音」以便区分；只有一路在录时不标说话人；
 *    - RecordCaptionHasSource：勾了「生成字幕」之后有没有声音可识别。2026-10-06 起「生成字幕」同时识别麦克风与电脑声音
 *      （用户定，不再单设「为电脑声音生成字幕」），两路都没在录时录制选项窗与录制时的字幕浮窗给出提示。
 *  全部为纯逻辑，不依赖窗口与字幕 DLL，单测见 Source/Tests/RecordCaptionLiveTests.cpp（标签 [captionlive]）。
 *
 *  典型用法：
 *      const uint32_t id = sentences.Take(source, final);
 *      const std::wstring speaker = RecordCaptionSpeaker(source, micOn, systemOn, selfName, TR(L"电脑声音"));
 *      board.OnCaption(source, speaker, text, final, id, nowMs);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <map>
#include <string>

namespace flamingomedia {

/**
 *  RecordCaptionSentences —— 录制实时字幕每一路的句子编号。只在 UI 线程上使用。
 */
class RecordCaptionSentences
{
public:
    RecordCaptionSentences();

    /**
     *  取一条识别结果的句子编号：中间结果用这一路当前的编号；定稿同样用当前编号，随后这一路换成下一个编号。
     *  @param source 哪一路，取值同 FlamingoCaption/Model/CaptionCue.h 的 CaptionSource
     *  @param final  是否为定稿
     *  @return 句子编号，每一路各自从 1 起
     */
    uint32_t Take(int source, bool final);

    //重新开始（每次录制开始时调用）：各路都回到 1
    void Reset();

private:
    std::map<int, uint32_t> m_current;   //来源 → 这一路当前这句的编号；没有记录的来源当前编号为 1
};

/**
 *  录制实时字幕某一路的说话人标注。
 *  @param source      哪一路，取值同 CaptionSource：caption_source_mic 或 caption_source_system，其余来源一律不标
 *  @param micOn       麦克风一路当前是否在录
 *  @param systemOn    电脑声音一路当前是否在录
 *  @param selfName    本人显示名（麦克风一路的标注）
 *  @param systemLabel 电脑声音一路的标注（调用方传入已翻译的「电脑声音」）
 *  @return 两路都在录时返回对应的标注，否则返回空串（不标说话人）
 */
std::wstring RecordCaptionSpeaker(int source, bool micOn, bool systemOn, const std::wstring& selfName,
                                  const std::wstring& systemLabel);

/**
 *  勾了「生成字幕」之后有没有声音可识别：麦克风与电脑声音哪一路在录就识别哪一路，两路都没在录时没有声音可识别。
 *  @param micOn    麦克风是否在录（录制前为「录制麦克风」勾选框，录制中为录制条上的麦克风开关）
 *  @param systemOn 电脑声音是否在录（录制前为「录制电脑声音」勾选框，录制中为录制条上的电脑声音开关）
 *  @return 至少一路在录时返回 true；返回 false 时调用方提示「没有可识别的声音」
 */
bool RecordCaptionHasSource(bool micOn, bool systemOn);

} // namespace flamingomedia
