/**
 *  语音字幕的字幕轨：一段录像的全部字幕（docs/语音字幕方案.md 6.2、第 7 节），CaptionTrack.h
 *
 *  保存一组按开始时刻排序的字幕，提供编辑器需要的全部修改操作：增删、改文字、改说话人、调时间、在指定字处
 *  拆成两条、与下一条合并、整体平移、批量替换文字、批量改说话人；以及按时刻、按文字查找。
 *  修改类方法都会把受影响的字幕标记为「改过」（m_edited），开始时刻相同时按编号排序，保证顺序确定。
 *  撤销与重做由 CaptionUndoStack 按快照实现：编辑器在每次修改前把 Cues() 存一份。
 *
 *  不是线程安全的：识别线程产生的字幕先交到界面线程，再由界面线程调用本类。
 *
 *  典型用法：
 *      caption::CaptionTrack track;
 *      int64_t id = track.Add(cue);
 *      track.SetText(track.IndexOfId(id), L"...");
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "CaptionCue.h"

namespace caption {

//一条字幕的最短时长（毫秒）：编辑时调时间不得短于它，拆分时两半也不得短于它
const int64_t kCaptionMinCueMs = 200;

//字幕轨
class CaptionTrack
{
public:
    CaptionTrack();

    //字幕条数
    size_t Count() const;

    //第 index 条（调用方保证 index < Count()）
    const CaptionCue& At(size_t index) const;

    //全部字幕（按开始时刻排序），撤销快照与存盘用
    const std::vector<CaptionCue>& Cues() const;

    /**
     *  用一组字幕整体替换当前内容（撤销、重做、读草稿时用）。
     *  编号为 0 的字幕会分配新编号；下一个编号取「现有最大编号 + 1」与原有值中较大者，保证编号不重复使用。
     */
    void Assign(const std::vector<CaptionCue>& cues);

    //清空（下一个编号不回退）
    void Clear();

    /**
     *  加一条字幕，按开始时刻插到合适位置。
     *  @param cue 要加的字幕；m_id 为 0 时分配新编号，非 0 时沿用（调用方保证不重复）
     *  @return 这条字幕的编号；起止时刻不合法（开始为负、结束不晚于开始）时返回 0，不加入
     */
    int64_t Add(const CaptionCue& cue);

    //按编号找下标，找不到返回 -1
    int IndexOfId(int64_t id) const;

    //删除第 index 条；下标越界返回 false
    bool Remove(size_t index);

    //改文字（会先整理空白，见 NormalizeText）并标记为改过；下标越界返回 false
    bool SetText(size_t index, const std::wstring& text);

    //改说话人并标记为改过；下标越界返回 false
    bool SetSpeaker(size_t index, const std::wstring& speaker);

    /**
     *  在第 index 条的文字末尾追加 suffix，不标记为改过（识别结果的自动修整用，如把下一句开头的标点并回这一条）。
     *  @return 下标越界返回 false
     */
    bool AppendText(size_t index, const std::wstring& suffix);

    /**
     *  调起止时刻并标记为改过，之后重新排序（这条字幕的下标可能变化，用 IndexOfId 重新取）。
     *  @return 开始为负、时长短于 kCaptionMinCueMs 或下标越界时返回 false，不修改
     */
    bool SetTimes(size_t index, int64_t startMs, int64_t endMs);

    /**
     *  在文字的第 charPos 个字符处拆成两条：前一半保留原编号，后一半得到新编号；
     *  时间按两半文字的显示宽度比例划分。两半都标记为改过。
     *  @param newId [出参，可为 NULL] 后一半的编号
     *  @return 拆分后任一半整理后为空、或任一半时长短于 kCaptionMinCueMs、或下标越界时返回 false，不修改
     */
    bool Split(size_t index, size_t charPos, int64_t* newId);

    /**
     *  与下一条合并：文字用 JoinText 拼接，结束时刻取两者较晚者，说话人与编号沿用前一条，标记为改过。
     *  @return 没有下一条或下标越界时返回 false
     */
    bool MergeWithNext(size_t index);

    /**
     *  全部字幕整体平移 deltaMs 毫秒（可正可负）。平移后开始时刻小于 0 的字幕截到 0 开始，
     *  若因此短于 kCaptionMinCueMs 则把结束时刻延到「开始 + kCaptionMinCueMs」。全部标记为改过。
     */
    void ShiftAll(int64_t deltaMs);

    /**
     *  把所有字幕文字里的 needle 换成 replacement（规则见 ReplaceInText，不要求整词）。
     *  @return 替换的总次数；被改动的字幕标记为改过
     */
    int ReplaceAll(const std::wstring& needle, const std::wstring& replacement, bool matchCase);

    /**
     *  把说话人为 from 的字幕全部改成 to。
     *  @return 改动的条数
     */
    int RenameSpeaker(const std::wstring& from, const std::wstring& to);

    /**
     *  从第 fromIndex 条起向后查找文字里含有 needle 的字幕，到末尾后从头继续，最多找一圈。
     *  @return 下标；找不到返回 -1
     */
    int FindNext(const std::wstring& needle, size_t fromIndex, bool matchCase) const;

    /**
     *  时刻 ms 所在的字幕（开始 <= ms < 结束）；有多条重叠时取开始最晚的一条。
     *  @return 下标；没有字幕覆盖该时刻返回 -1
     */
    int IndexAtTime(int64_t ms) const;

    //下一个将要分配的编号（存草稿用）
    int64_t NextId() const;

    //设置下一个编号（读草稿用；小于「现有最大编号 + 1」时按后者处理）
    void SetNextId(int64_t nextId);

private:
    //按开始时刻排序，相同时按编号
    void SortByStart();

    std::vector<CaptionCue> m_cues;   //全部字幕，按开始时刻排序
    int64_t                 m_nextId; //下一个将要分配的编号，从 1 开始，只增不减
};

/**
 *  撤销与重做：按快照保存字幕轨的全部字幕。编辑器在每次修改之前调用 Push 存一份修改前的内容。
 *  快照超过上限时丢弃最早的一份。
 */
class CaptionUndoStack
{
public:
    /**
     *  @param limit 最多保留的撤销快照数（至少 1）
     */
    explicit CaptionUndoStack(size_t limit);

    //存一份修改前的内容，并清空重做栈
    void Push(const std::vector<CaptionCue>& before);

    bool CanUndo() const;
    bool CanRedo() const;

    /**
     *  撤销：current 换成上一份快照，原内容进重做栈。
     *  @param current [入参出参] 当前内容
     *  @return 没有可撤销的快照时返回 false，current 不变
     */
    bool Undo(std::vector<CaptionCue>& current);

    //重做：与 Undo 对称
    bool Redo(std::vector<CaptionCue>& current);

    //清空撤销与重做
    void Clear();

private:
    std::vector<std::vector<CaptionCue> > m_undo;   //撤销快照，末尾是最近一次
    std::vector<std::vector<CaptionCue> > m_redo;   //重做快照，末尾是最近一次撤销前的内容
    size_t                                m_limit;  //撤销快照的上限
};

} // namespace caption
