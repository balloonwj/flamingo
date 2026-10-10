/**
 *  字幕编辑器的纯逻辑（docs/语音字幕方案.md 第 7 节），CaptionEditorModel.h
 *
 *  编辑器窗口只负责显示与转交操作，字幕数据与编辑规则都在本类：
 *    - 载入：读录像旁的草稿（没有草稿时从空字幕开始，外观取默认值）；保存：整理成草稿写回；
 *    - 选中项按字幕编号跟踪（排序后下标会变，编号不变）；
 *    - 改文字、改说话人、改起止时刻、拆分、与下一条合并、插入、删除、整体平移、全部替换、批量改说话人、
 *      用事后识别的结果整体替换：每一项改动之前先把当前字幕压进撤销栈，可以撤销与重做；
 *    - 外观（样式）与导出记录随草稿保存，不进撤销栈；
 *    - 时间文字的格式化与解析（编辑区的起止时刻输入框用「分:秒.十分之一秒」，超过一小时加「时:」）。
 *  不依赖窗口，单测见 Tests/CaptionEditorTests.cpp（CAPED1～CAPED8）。
 *
 *  典型用法：
 *      caption::CaptionEditorModel model;
 *      model.Load(videoPath, &error);
 *      model.EditText(model.Selected(), L"新的文字");
 *      if (model.IsDirty()) model.Save(&error);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "../Model/CaptionDraft.h"
#include "../Model/CaptionTrack.h"

namespace caption {

//撤销栈的深度（步）
const size_t kCaptionEditorUndoLimit = 200;
//「插入」新字幕的默认时长（毫秒）
const int64_t kCaptionInsertDefaultMs = 2000;

/**
 *  CaptionEditorModel —— 一个录像的字幕编辑数据。只在 UI 线程上使用。
 */
class CaptionEditorModel
{
public:
    CaptionEditorModel();

    /**
     *  载入录像的草稿。草稿不存在时从空字幕开始；草稿损坏时也从空字幕开始，并在 error 里说明（原草稿文件不动，
     *  第一次保存时才覆盖）。
     *  @param videoPath 录像完整路径
     *  @param error     [出参，可为 NULL] 草稿损坏时的原因；草稿正常或不存在时清空
     *  @return 草稿正常或不存在返回 true；草稿存在但读不出返回 false（此时模型仍可使用）
     */
    bool Load(const std::wstring& videoPath, std::string* error);

    /**
     *  直接用一份草稿初始化（单测与「重新识别后整体替换」以外的场合不用）。清空撤销栈，不算改动。
     *  @param videoPath 录像完整路径
     *  @param draft     草稿
     */
    void Reset(const std::wstring& videoPath, const CaptionDraft& draft);

    /**
     *  保存到录像旁的草稿文件（先写临时文件再替换）。成功后不再算有未保存的改动。
     *  @param error [出参，可为 NULL] 失败原因
     *  @return 写入成功返回 true
     */
    bool Save(std::string* error);

    //整理成草稿（保存与导出用）
    CaptionDraft ToDraft() const;

    //录像完整路径
    const std::wstring& VideoPath() const;

    //全部字幕（按开始时刻排序）
    const CaptionTrack& Track() const;

    //是否有未保存的改动
    bool IsDirty() const;

    //---- 选中项 ----

    //选中项的下标；没有选中时返回 -1
    int Selected() const;

    //选中项的编号；没有选中时返回 0
    int64_t SelectedId() const;

    /**
     *  选中第 index 条。
     *  @param index 下标；越界或为 -1 时取消选中
     */
    void Select(int index);

    //---- 编辑（每项先压撤销栈；返回 false 时不修改、不压栈）----

    //改文字：整理后为空时返回 false（要删除请用 Remove）
    bool EditText(int index, const std::wstring& text);

    //改说话人（可改成空）
    bool EditSpeaker(int index, const std::wstring& speaker);

    //改起止时刻：规则见 CaptionTrack::SetTimes（开始不能为负、时长不短于 kCaptionMinCueMs）
    bool EditTimes(int index, int64_t startMs, int64_t endMs);

    //在文字的第 charPos 个字符处拆成两条，选中后一半；规则见 CaptionTrack::Split
    bool Split(int index, size_t charPos);

    //与下一条合并，选中合并后的那条
    bool MergeWithNext(int index);

    /**
     *  在 atMs 处插入一条空白字幕（来源记为手工添加），时长 kCaptionInsertDefaultMs，碰到后一条时截到它开始，
     *  不足 kCaptionMinCueMs 时返回 false。文字先填「（新字幕）」占位，选中它。
     *  @param atMs        开始时刻（毫秒）；小于 0 按 0
     *  @param placeholder 占位文字（界面文字，调用方翻译好传入）
     *  @return 插入成功返回 true
     */
    bool Insert(int64_t atMs, const std::wstring& placeholder);

    //删除第 index 条；删除后选中原位置的下一条（没有时选中上一条）
    bool Remove(int index);

    //全部字幕整体平移 deltaMs 毫秒；delta 为 0 或没有字幕时返回 false
    bool ShiftAll(int64_t deltaMs);

    /**
     *  全部替换。
     *  @return 替换次数；为 0 时不压栈、不算改动
     */
    int ReplaceAll(const std::wstring& needle, const std::wstring& replacement, bool matchCase);

    /**
     *  把说话人为 from 的全部改成 to。
     *  @return 改动条数；为 0 时不压栈
     */
    int RenameSpeaker(const std::wstring& from, const std::wstring& to);

    /**
     *  用事后识别的结果整体替换全部字幕（可撤销）。
     *  @param cues 新的字幕
     */
    void ReplaceAllCues(const std::vector<CaptionCue>& cues);

    //从选中项的下一条起向后查找（到末尾后从头继续），找到时选中它
    int FindNext(const std::wstring& needle, bool matchCase);

    //撤销 / 重做；选中项按编号恢复（那条已不存在时取消选中）
    bool Undo();
    bool Redo();
    bool CanUndo() const;
    bool CanRedo() const;

    //---- 外观与导出记录（不进撤销栈）----

    const CaptionStyle& Style() const;
    void SetStyle(const CaptionStyle& style);
    const CaptionPip& Pip() const;

    /**
     *  记下一次导出（同一种类同一文件名只记一条），算作改动。
     *  @param kind 种类，取值见 CaptionExportKind
     *  @param name 导出文件的文件名（不含目录）
     */
    void AddExport(int kind, const std::wstring& name);

    //出现过的说话人（去重，按首次出现的顺序），批量改说话人时列出
    std::vector<std::wstring> Speakers() const;

private:
    //改动之前压栈
    void pushUndo();
    //改动之后：标记有改动；按编号找回选中项
    void touched(int64_t selectId);

    std::wstring       m_videoPath;   //录像完整路径
    CaptionDraft       m_draft;       //草稿里字幕以外的部分（外观、画中画、导出记录、录像信息）
    CaptionTrack       m_track;       //字幕
    CaptionUndoStack   m_undo;        //撤销栈
    int64_t            m_selectedId;  //选中项的编号；0 表示没有选中
    bool               m_dirty;       //是否有未保存的改动
};

/**
 *  编辑区起止时刻的文字：不足一小时为「分:秒.十分之一秒」（如 01:23.4），否则为「时:分:秒.十分之一秒」。
 *  @param ms 时刻（毫秒），小于 0 按 0
 */
std::wstring FormatEditTime(int64_t ms);

/**
 *  解析编辑区输入的时刻：接受「秒」「秒.小数」「分:秒[.小数]」「时:分:秒[.小数]」，小数最多三位（毫秒），
 *  分与秒不超过 59（只有一段时不限），前后空白忽略。
 *  @param text 输入
 *  @param ms   [出参] 毫秒
 *  @return 格式正确返回 true
 */
bool ParseEditTime(const std::wstring& text, int64_t& ms);

/**
 *  字幕列表一行的主文字：「说话人：文字」，说话人为空时只有文字。
 */
std::wstring CueListMainText(const CaptionCue& cue);

/**
 *  字幕列表一行的副文字：「起 – 止」，如「01:02.3 – 01:04.8」；改过的字幕后面加「 · 已修改」（文字由调用方翻译好传入）。
 *  @param cue        字幕
 *  @param editedMark 改过的标记文字；为空时不加
 */
std::wstring CueListSubText(const CaptionCue& cue, const std::wstring& editedMark);

} // namespace caption
