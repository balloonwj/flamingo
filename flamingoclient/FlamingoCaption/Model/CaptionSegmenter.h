/**
 *  语音字幕的字幕条切分与识别结果的装配（docs/语音字幕方案.md 5.2、6.2、3.5），CaptionSegmenter.h
 *
 *  识别器的端点检测给出的一句话可能长达 20 秒、几十个字，不能直接做成一条字幕。本文件提供：
 *    - CaptionReplaceTable：自动替换表，把常见的误识别（如 Sclight）替换为正确的词（如 SQLite）；
 *    - CaptionSegmenter：把一句话按「每行宽度、行数、单条时长」切成若干条字幕，优先在句末标点、其次在句中标点、
 *      再次在空格处断开，不拆开英文单词；时间按每条文字的显示宽度比例分配；
 *    - CaptionAssembler：识别结果进入字幕轨前的整套处理：整理文字 → 替换 → 行首标点并回同一路的上一条 → 切分。
 *
 *  纯逻辑，不依赖识别引擎与界面。
 *
 *  典型用法：
 *      caption::CaptionAssembler assembler(caption::CaptionSegmentRules(), &replaceTable);
 *      assembler.OnFinal(track, text, startMs, endMs, speaker, caption::caption_source_mic);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "CaptionCue.h"

namespace caption {

class CaptionTrack;

//每行最多的显示宽度单位（约 18 个汉字）
const int kCaptionMaxLineUnits = 36;
//每条字幕最多的行数
const int kCaptionMaxLines = 2;
//每条字幕的最长时长（毫秒）
const int64_t kCaptionMaxCueMs = 6000;
//每条字幕的最短时长（毫秒）：切分时尽量不短于它（文字过长时以宽度限制为先）
const int64_t kCaptionMinSegmentMs = 800;
//行首标点并回上一条时，上一条结束到这一句开始的最大间隔（毫秒）；间隔更长时直接丢弃行首标点
const int64_t kCaptionPunctMergeGapMs = 3000;

//切分规则
struct CaptionSegmentRules
{
    CaptionSegmentRules();

    int     m_maxLineUnits;   //每行最多的显示宽度单位
    int     m_maxLines;       //每条最多的行数
    int64_t m_maxCueMs;       //每条最长时长（毫秒）
    int64_t m_minCueMs;       //每条最短时长（毫秒）
};

//一条替换规则
struct CaptionReplaceRule
{
    CaptionReplaceRule();

    std::wstring m_from;        //要替换的文字（误识别的写法）
    std::wstring m_to;          //替换为
    bool         m_matchCase;   //是否区分大小写（只对 ASCII 字母有意义），默认不区分
    bool         m_wholeWord;   //英文是否只匹配整词（m_from 首尾为 ASCII 字母或数字时生效），默认是
};

/**
 *  自动替换表（按账号保存，编辑器里维护）。规则按 m_from 的长度从长到短依次应用，避免短规则先改坏长规则要匹配的文字。
 */
class CaptionReplaceTable
{
public:
    /**
     *  加一条规则；m_from 与已有规则相同（不区分大小写）时覆盖。m_from 为空的规则不加入。
     *  @return 是否加入或覆盖了
     */
    bool Add(const CaptionReplaceRule& rule);

    //删除 m_from 等于 from（不区分大小写）的规则；没有则返回 false
    bool Remove(const std::wstring& from);

    //全部规则（按加入顺序）
    const std::vector<CaptionReplaceRule>& Rules() const;

    /**
     *  对一段文字应用全部规则。
     *  @param count [出参，可为 NULL] 替换的总次数
     */
    std::wstring Apply(const std::wstring& text, int* count) const;

    /**
     *  序列化为 UTF-8 JSON 数组：[{"from":..,"to":..,"matchcase":0/1,"wholeword":0/1}, ...]
     */
    std::string ToJson() const;

    /**
     *  从 ToJson 的结果恢复（替换现有规则）。格式不对时返回 false，现有规则不变。
     */
    bool FromJson(const std::string& json);

private:
    std::vector<CaptionReplaceRule> m_rules;   //全部规则，按加入顺序
};

/**
 *  字幕条切分。全部为静态方法。
 */
class CaptionSegmenter
{
public:
    /**
     *  把一句话切成若干条字幕。文字先经 NormalizeText 整理；整理后为空时返回空列表。
     *  @param text     这句话的文字
     *  @param startMs  这句话的开始时刻（毫秒）
     *  @param endMs    这句话的结束时刻（毫秒）；不晚于 startMs 时按「开始 + 每单位 100 毫秒」估算
     *  @param speaker  说话人
     *  @param source   来源，取值见 CaptionSource
     *  @param rules    切分规则
     *  @return 切好的字幕（m_id 为 0，由字幕轨分配），按时间顺序
     */
    static std::vector<CaptionCue> Split(const std::wstring& text, int64_t startMs, int64_t endMs,
                                         const std::wstring& speaker, int source, const CaptionSegmentRules& rules);

    /**
     *  把文字切成若干段（只看文字，不分配时间）：每段宽度不超过 maxUnits，段数为 pieces（至少按宽度所需的段数）。
     *  测试与 Split 共用。
     */
    static std::vector<std::wstring> SplitText(const std::wstring& text, int maxUnits, int pieces);
};

/**
 *  识别结果进入字幕轨前的装配：整理 → 替换 → 行首标点并回同一路的上一条 → 切分 → 加入字幕轨。
 *  「同一路」按说话人加来源区分（共享时不同发言人各是一路）。
 */
class CaptionAssembler
{
public:
    /**
     *  @param rules   切分规则（复制保存）
     *  @param replace 自动替换表（不拥有，可为 NULL；调用方保证在本对象存续期间有效）
     */
    CaptionAssembler(const CaptionSegmentRules& rules, const CaptionReplaceTable* replace);

    /**
     *  一句识别结果定稿：处理后加入字幕轨。
     *  @return 新加入的字幕条数（只有行首标点、并回上一条后没有剩余文字时为 0）
     */
    int OnFinal(CaptionTrack& track, const std::wstring& text, int64_t startMs, int64_t endMs,
                const std::wstring& speaker, int source);

    //忘掉各路的上一条（开始新的录制或重新识别时调用）
    void Reset();

private:
    CaptionSegmentRules                 m_rules;     //切分规则
    const CaptionReplaceTable*          m_replace;   //自动替换表（不拥有，可为 NULL）
    std::map<std::wstring, int64_t>     m_lastCue;   //各路最后一条字幕的编号，键为「来源 + 说话人」
};

} // namespace caption
