/**
 *  CaptionEditorTests.cpp
 *
 *  字幕编辑器纯逻辑（docs/语音字幕方案.md 第 7 节）的单元测试，标签 [captioneditor]：
 *    CAPED1 —— 时间文字：格式化（不足一小时 / 超过一小时）与解析（秒、分:秒、时:分:秒、小数、非法输入）；
 *    CAPED2 —— 载入与保存：没有草稿时从空字幕开始；保存后再载入内容一致；损坏的草稿返回 false 但模型可用；
 *    CAPED3 —— 改文字、说话人、时间：选中项按编号跟踪（改时间重新排序后仍选中同一条）；无变化或非法时不算改动；
 *    CAPED4 —— 拆分、合并、插入、删除后的选中项；插入碰到后一条时截短；
 *    CAPED5 —— 撤销与重做：多步撤销恢复原状、重做再回来；撤销后有新改动时清空重做；
 *    CAPED6 —— 整体平移、全部替换、批量改说话人、查找下一处（到末尾从头找）；
 *    CAPED7 —— 外观与导出记录随草稿保存，不进撤销栈；说话人列表去重；
 *    CAPED8 —— 列表文字：主文字带说话人前缀、副文字为起止时间与「已修改」标记；
 *    CAPTL1 —— 时间轴：毫秒与横坐标互换；
 *    CAPTL2 —— 时间轴命中：边缘、本体、空白处、行外；窄块的边缘缩小；
 *    CAPTL3 —— 时间轴拖动：平移保持时长且夹在前后两条之间；拖边缘不短于最短时长、不越过邻条与录像时长；
 *    CAPTL4 —— 时间轴缩放、滚动范围与刻度间隔；
 *    CAPTL5 —— 时间轴横向滚动条：内容超过一屏时的最大位置、页大小、当前位置；滚动条位置换回时刻并夹在合法范围内；
 *              内容放得下时不显示；
 *    CAPED9 —— 有所有者的三个弹窗（导出、替换表、表单）建窗时不用 WS_POPUP：balloonui 按 GetParent 决定控件通知发给谁，
 *              有所有者的 WS_POPUP 窗口的通知会发到编辑器，按钮全部失灵（2026-10-06 导出窗口点不动、关不掉）。按源码检查。
 *
 *  CAPED2 在 E:\flamingo\tmp（取不到时用系统临时目录）下建临时文件，用完删除。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../Editor/CaptionEditorModel.h"
#include "../Editor/CaptionTimelineMath.h"

using namespace caption;

namespace {

std::wstring TempDirEd()
{
    const wchar_t* const kRepoTmp = L"E:\\flamingo\\tmp\\";
    const DWORD attr = ::GetFileAttributesW(kRepoTmp);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return kRepoTmp;
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, buf);
    return buf;
}

CaptionCue EdCue(int64_t start, int64_t end, const wchar_t* text, const wchar_t* speaker)
{
    CaptionCue c;
    c.m_startMs = start;
    c.m_endMs = end;
    c.m_text = text;
    c.m_speaker = speaker;
    return c;
}

//三条字幕的草稿：A 0~2000，B 3000~5000（说话人张三），C 6000~8000
CaptionDraft ThreeCueDraft()
{
    CaptionDraft d;
    d.m_cues.push_back(EdCue(0, 2000, L"\u7b2c\u4e00\u53e5", L""));
    d.m_cues.push_back(EdCue(3000, 5000, L"\u7b2c\u4e8c\u53e5\u8bdd\u5f88\u957f", L"\u5f20\u4e09"));
    d.m_cues.push_back(EdCue(6000, 8000, L"third line", L"\u5f20\u4e09"));
    for (size_t i = 0; i < d.m_cues.size(); ++i)
        d.m_cues[i].m_id = static_cast<int64_t>(i + 1);
    d.m_nextId = 4;
    return d;
}

std::vector<CaptionCue> TlCues()
{
    std::vector<CaptionCue> c;
    c.push_back(EdCue(1000, 2000, L"a", L""));
    c.push_back(EdCue(3000, 5000, L"b", L""));
    c.push_back(EdCue(6000, 6300, L"c", L""));
    return c;
}

TimelineView TlView()
{
    TimelineView v;
    v.m_scrollMs = 0;
    v.m_pxPerSec = 100.0;
    v.m_width = 800;
    v.m_trackTop = 20;
    v.m_trackBottom = 60;
    return v;
}

} // namespace

TEST_CASE("CAPED1 时间文字的格式化与解析", "[captioneditor]")
{
    CHECK(FormatEditTime(0) == L"00:00.0");
    CHECK(FormatEditTime(83456) == L"01:23.4");
    CHECK(FormatEditTime(3723400) == L"1:02:03.4");
    CHECK(FormatEditTime(-5) == L"00:00.0");
    int64_t ms = 0;
    REQUIRE(ParseEditTime(L"83.4", ms));
    CHECK(ms == 83400);
    REQUIRE(ParseEditTime(L" 01:23.456 ", ms));
    CHECK(ms == 83456);
    REQUIRE(ParseEditTime(L"1:02:03", ms));
    CHECK(ms == 3723000);
    REQUIRE(ParseEditTime(L"90", ms));
    CHECK(ms == 90000);
    CHECK_FALSE(ParseEditTime(L"", ms));
    CHECK_FALSE(ParseEditTime(L"1:60", ms));
    CHECK_FALSE(ParseEditTime(L"1:2:3:4", ms));
    CHECK_FALSE(ParseEditTime(L"1.2345", ms));
    CHECK_FALSE(ParseEditTime(L"a:10", ms));
    CHECK_FALSE(ParseEditTime(L"1:", ms));
    CHECK_FALSE(ParseEditTime(L"1.", ms));
}

TEST_CASE("CAPED2 载入与保存：没有草稿时从空开始；保存后再载入一致；损坏的草稿返回 false", "[captioneditor]")
{
    const std::wstring video = TempDirEd() + L"caped2.mp4";
    const std::wstring draftPath = CaptionDraftIo::DraftPathFor(video);
    ::DeleteFileW(draftPath.c_str());
    CaptionEditorModel m;
    std::string error;
    REQUIRE(m.Load(video, &error));
    CHECK(m.Track().Count() == 0);
    CHECK_FALSE(m.IsDirty());
    REQUIRE(m.Insert(1000, L"\u65b0\u5b57\u5e55"));
    CHECK(m.IsDirty());
    REQUIRE(m.Save(&error));
    CHECK_FALSE(m.IsDirty());

    CaptionEditorModel back;
    REQUIRE(back.Load(video, &error));
    REQUIRE(back.Track().Count() == 1);
    CHECK(back.Track().At(0).m_text == L"\u65b0\u5b57\u5e55");
    CHECK(back.Track().At(0).m_source == caption_source_manual);

    //损坏的草稿：返回 false，模型照样可用（空字幕）
    HANDLE f = ::CreateFileW(draftPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    REQUIRE(f != INVALID_HANDLE_VALUE);
    DWORD put = 0;
    ::WriteFile(f, "{not json", 9, &put, NULL);
    ::CloseHandle(f);
    CaptionEditorModel bad;
    CHECK_FALSE(bad.Load(video, &error));
    CHECK_FALSE(error.empty());
    CHECK(bad.Track().Count() == 0);
    CHECK(bad.Insert(0, L"x"));
    ::DeleteFileW(draftPath.c_str());
}

TEST_CASE("CAPED3 改文字、说话人、时间；选中项按编号跟踪；无变化或非法时不算改动", "[captioneditor]")
{
    CaptionEditorModel m;
    m.Reset(L"C:\\v\\a.mp4", ThreeCueDraft());
    m.Select(2);
    CHECK(m.SelectedId() == 3);
    //把第三条挪到第一、二条之间：重新排序后仍选中它
    REQUIRE(m.EditTimes(2, 2500, 2800));
    CHECK(m.Selected() == 1);
    CHECK(m.SelectedId() == 3);
    CHECK(m.IsDirty());
    //无变化、太短、开始为负：不改
    CHECK_FALSE(m.EditTimes(1, 2500, 2800));
    CHECK_FALSE(m.EditTimes(1, 2500, 2600));
    CHECK_FALSE(m.EditTimes(1, -1, 500));
    //改文字：整理空白；空文字不改
    REQUIRE(m.EditText(1, L"  third   LINE  "));
    CHECK(m.Track().At(1).m_text == L"third LINE");
    CHECK(m.Track().At(1).m_edited);
    CHECK_FALSE(m.EditText(1, L"   "));
    //改说话人：可改成空
    REQUIRE(m.EditSpeaker(1, L""));
    CHECK(m.Track().At(1).m_speaker.empty());
    CHECK_FALSE(m.EditSpeaker(1, L""));
    CHECK_FALSE(m.EditText(5, L"x"));
    m.Select(-1);
    CHECK(m.Selected() == -1);
}

TEST_CASE("CAPED4 拆分、合并、插入、删除后的选中项；插入碰到后一条时截短", "[captioneditor]")
{
    CaptionEditorModel m;
    m.Reset(L"C:\\v\\a.mp4", ThreeCueDraft());
    //拆第二条（第二句话很长）：在第 3 个字处，选中后一半
    REQUIRE(m.Split(1, 3));
    REQUIRE(m.Track().Count() == 4);
    CHECK(m.Track().At(1).m_text == L"\u7b2c\u4e8c\u53e5");
    CHECK(m.Selected() == 2);
    CHECK(m.Track().At(2).m_speaker == L"\u5f20\u4e09");
    //拆在开头：失败、不改
    CHECK_FALSE(m.Split(0, 0));
    //合并回去
    REQUIRE(m.MergeWithNext(1));
    CHECK(m.Track().Count() == 3);
    CHECK(m.Selected() == 1);
    CHECK_FALSE(m.MergeWithNext(2));
    //插入在 2500：后一条 3000 开始，截到 3000（500 毫秒，够最短时长）
    REQUIRE(m.Insert(2500, L"\u63d2\u5165"));
    const int ins = m.Selected();
    REQUIRE(ins >= 0);
    CHECK(m.Track().At(static_cast<size_t>(ins)).m_startMs == 2500);
    CHECK(m.Track().At(static_cast<size_t>(ins)).m_endMs == 3000);
    //插入在 2900：只剩 100 毫秒，失败
    CHECK_FALSE(m.Insert(2900, L"x"));
    //删除：选中原位置的下一条
    const int64_t nextId = m.Track().At(static_cast<size_t>(ins) + 1).m_id;
    REQUIRE(m.Remove(ins));
    CHECK(m.SelectedId() == nextId);
    //删最后一条：选中上一条
    const int last = static_cast<int>(m.Track().Count()) - 1;
    const int64_t prevId = m.Track().At(static_cast<size_t>(last) - 1).m_id;
    REQUIRE(m.Remove(last));
    CHECK(m.SelectedId() == prevId);
}

TEST_CASE("CAPED5 撤销与重做", "[captioneditor]")
{
    CaptionEditorModel m;
    m.Reset(L"C:\\v\\a.mp4", ThreeCueDraft());
    const std::vector<CaptionCue> original = m.Track().Cues();
    CHECK_FALSE(m.CanUndo());
    REQUIRE(m.EditText(0, L"changed"));
    REQUIRE(m.Remove(2));
    REQUIRE(m.ShiftAll(1000));
    CHECK(m.CanUndo());
    REQUIRE(m.Undo());
    REQUIRE(m.Undo());
    REQUIRE(m.Undo());
    CHECK_FALSE(m.Undo());
    REQUIRE(m.Track().Count() == original.size());
    for (size_t i = 0; i < original.size(); ++i)
    {
        CHECK(m.Track().At(i).m_text == original[i].m_text);
        CHECK(m.Track().At(i).m_startMs == original[i].m_startMs);
    }
    CHECK(m.CanRedo());
    REQUIRE(m.Redo());
    CHECK(m.Track().At(0).m_text == L"changed");
    //撤销后有新改动：重做清空
    REQUIRE(m.Undo());
    REQUIRE(m.EditSpeaker(0, L"new"));
    CHECK_FALSE(m.CanRedo());
}

TEST_CASE("CAPED6 整体平移、全部替换、批量改说话人、查找下一处", "[captioneditor]")
{
    CaptionEditorModel m;
    m.Reset(L"C:\\v\\a.mp4", ThreeCueDraft());
    REQUIRE(m.ShiftAll(-500));
    CHECK(m.Track().At(0).m_startMs == 0);
    CHECK(m.Track().At(1).m_startMs == 2500);
    CHECK_FALSE(m.ShiftAll(0));
    CHECK(m.ReplaceAll(L"line", L"\u884c", true) == 1);
    CHECK(m.Track().At(2).m_text == L"third \u884c");
    CHECK(m.ReplaceAll(L"zzz", L"y", true) == 0);
    CHECK(m.RenameSpeaker(L"\u5f20\u4e09", L"\u674e\u56db") == 2);
    CHECK(m.Track().At(1).m_speaker == L"\u674e\u56db");
    CHECK(m.RenameSpeaker(L"\u738b\u4e94", L"x") == 0);
    //查找：从选中项的下一条起，到末尾后从头继续
    m.Select(2);
    CHECK(m.FindNext(L"\u7b2c", true) == 0);
    CHECK(m.FindNext(L"\u7b2c", true) == 1);
    CHECK(m.FindNext(L"\u7b2c", true) == 0);
    CHECK(m.FindNext(L"nothing", true) == -1);
}

TEST_CASE("CAPED7 外观与导出记录随草稿保存，不进撤销栈；说话人列表去重", "[captioneditor]")
{
    CaptionEditorModel m;
    m.Reset(L"C:\\v\\a.mp4", ThreeCueDraft());
    CaptionStyle s = m.Style();
    s.m_fontPx = 64;
    m.SetStyle(s);
    CHECK(m.IsDirty());
    CHECK_FALSE(m.CanUndo());
    m.AddExport(caption_export_srt, L"a.srt");
    m.AddExport(caption_export_srt, L"a.srt");
    const CaptionDraft d = m.ToDraft();
    CHECK(d.m_style.m_fontPx == 64);
    REQUIRE(d.m_exports.size() == 1);
    CHECK(d.m_cues.size() == 3);
    CHECK(d.m_nextId >= 4);
    const std::vector<std::wstring> sp = m.Speakers();
    REQUIRE(sp.size() == 1);
    CHECK(sp[0] == L"\u5f20\u4e09");
}

TEST_CASE("CAPED8 列表文字：说话人前缀；起止时间与已修改标记", "[captioneditor]")
{
    CaptionCue c = EdCue(62300, 64800, L"\u4f60\u597d", L"\u5f20\u4e09");
    CHECK(CueListMainText(c) == L"\u5f20\u4e09\uff1a\u4f60\u597d");
    CHECK(CueListSubText(c, L"\u5df2\u4fee\u6539") == L"01:02.3 \u2013 01:04.8");
    c.m_edited = true;
    CHECK(CueListSubText(c, L"\u5df2\u4fee\u6539") == L"01:02.3 \u2013 01:04.8 \u00b7 \u5df2\u4fee\u6539");
    c.m_speaker.clear();
    CHECK(CueListMainText(c) == L"\u4f60\u597d");
}

TEST_CASE("CAPTL1 时间轴：毫秒与横坐标互换", "[captioneditor]")
{
    TimelineView v = TlView();
    CHECK(TimelineMsToX(v, 1000) == 100);
    CHECK(TimelineXToMs(v, 250) == 2500);
    v.m_scrollMs = 2000;
    CHECK(TimelineMsToX(v, 1000) == -100);
    CHECK(TimelineXToMs(v, 0) == 2000);
    v.m_scrollMs = 0;
    CHECK(TimelineXToMs(v, -50) == 0);
}

TEST_CASE("CAPTL2 时间轴命中：边缘、本体、空白、行外；窄块边缘缩小", "[captioneditor]")
{
    const TimelineView v = TlView();
    const std::vector<CaptionCue> cues = TlCues();
    const int kEdge = 6;
    //b：300~500 像素
    TimelineHit h = TimelineHitTest(v, cues, 302, 30, kEdge);
    CHECK(h.m_kind == timeline_hit_left);
    CHECK(h.m_index == 1);
    h = TimelineHitTest(v, cues, 400, 30, kEdge);
    CHECK(h.m_kind == timeline_hit_body);
    h = TimelineHitTest(v, cues, 497, 30, kEdge);
    CHECK(h.m_kind == timeline_hit_right);
    h = TimelineHitTest(v, cues, 250, 30, kEdge);
    CHECK(h.m_kind == timeline_hit_none);
    CHECK(h.m_index == -1);
    h = TimelineHitTest(v, cues, 400, 10, kEdge);
    CHECK(h.m_kind == timeline_hit_none);
    //c 只有 30 像素宽：边缘各 10 像素，正中是本体
    h = TimelineHitTest(v, cues, 615, 30, 20);
    CHECK(h.m_kind == timeline_hit_body);
    h = TimelineHitTest(v, cues, 601, 30, 20);
    CHECK(h.m_kind == timeline_hit_left);
}

TEST_CASE("CAPTL3 时间轴拖动：平移保持时长且夹在前后两条之间；拖边缘不短于最短时长", "[captioneditor]")
{
    const std::vector<CaptionCue> cues = TlCues();
    int64_t s = 0;
    int64_t e = 0;
    //b 往左拖 5000：夹到 a 的结束 2000
    REQUIRE(TimelineDragTimes(timeline_hit_body, cues, 1, -5000, 10000, s, e));
    CHECK(s == 2000);
    CHECK(e == 4000);
    //b 往右拖 5000：夹到 c 的开始 6000
    REQUIRE(TimelineDragTimes(timeline_hit_body, cues, 1, 5000, 10000, s, e));
    CHECK(e == 6000);
    CHECK(s == 4000);
    //b 右边缘往左拖：不短于最短时长
    REQUIRE(TimelineDragTimes(timeline_hit_right, cues, 1, -5000, 10000, s, e));
    CHECK(s == 3000);
    CHECK(e == 3000 + kCaptionMinCueMs);
    //c 右边缘往右拖：不超过录像时长
    REQUIRE(TimelineDragTimes(timeline_hit_right, cues, 2, 10000, 7000, s, e));
    CHECK(e == 7000);
    //a 左边缘往左拖：不早于 0
    REQUIRE(TimelineDragTimes(timeline_hit_left, cues, 0, -5000, 10000, s, e));
    CHECK(s == 0);
    CHECK(e == 2000);
    CHECK_FALSE(TimelineDragTimes(timeline_hit_none, cues, 0, 0, 10000, s, e));
    CHECK_FALSE(TimelineDragTimes(timeline_hit_body, cues, 9, 0, 10000, s, e));
}

TEST_CASE("CAPTL4 时间轴缩放、滚动范围与刻度间隔", "[captioneditor]")
{
    CHECK(TimelineZoom(kTimelineDefaultPxPerSec, 0) == kTimelineDefaultPxPerSec);
    CHECK(TimelineZoom(kTimelineDefaultPxPerSec, 1) > kTimelineDefaultPxPerSec);
    CHECK(TimelineZoom(kTimelineMaxPxPerSec, 5) == kTimelineMaxPxPerSec);
    CHECK(TimelineZoom(kTimelineMinPxPerSec, -5) == kTimelineMinPxPerSec);
    TimelineView v = TlView();
    //800 像素、每秒 100 像素：一屏 8 秒；录像 20 秒，最多滚到 12 秒
    CHECK(TimelineClampScroll(v, 15000, 20000) == 12000);
    CHECK(TimelineClampScroll(v, -1, 20000) == 0);
    CHECK(TimelineClampScroll(v, 5000, 3000) == 0);
    //每秒 100 像素：1 秒一个主刻度（100 像素）
    CHECK(TimelineMajorStepMs(100.0) == 1000);
    CHECK(TimelineMajorStepMs(40.0) == 2000);
    CHECK(TimelineMajorStepMs(2.0) == 60000);
    const std::vector<TimelineTick> ticks = TimelineTicks(v, 20000);
    REQUIRE(ticks.size() >= 2);
    CHECK(ticks[0].m_ms == 0);
    CHECK(ticks[0].m_major);
    CHECK_FALSE(ticks[1].m_major);
    CHECK(ticks[1].m_x == 50);
}

namespace {

//读取与本用例文件同在 FlamingoCaption 下的源文件（按 __FILE__ 定位）；读不到时返回空串
std::string Caped9ReadSource(const char* relative)
{
    std::string dir = __FILE__;
    const std::string::size_type slash = dir.find_last_of("\\/");
    if (slash == std::string::npos)
        return std::string();
    //__FILE__ 在 FlamingoCaption\Tests 下，弹窗源码在 FlamingoCaption\Editor 下
    const std::string path = dir.substr(0, slash) + "\\..\\Editor\\" + relative;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("CAPED9 有所有者的弹窗建窗时不用 WS_POPUP（否则按钮通知发到编辑器、全部失灵）", "[captioneditor]")
{
    const char* const files[] = { "CaptionExportDialog.cpp", "CaptionReplaceDialog.cpp", "CaptionFormDialog.cpp" };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
    {
        INFO(files[i]);
        const std::string src = Caped9ReadSource(files[i]);
        REQUIRE_FALSE(src.empty());
        //逐行找建窗语句「Create(owner, ...」，它不能带 WS_POPUP
        std::istringstream lines(src);
        std::string line;
        int creates = 0;
        while (std::getline(lines, line))
        {
            if (line.find("Create(owner") == std::string::npos)
                continue;
            ++creates;
            CHECK(line.find("WS_POPUP") == std::string::npos);
            CHECK(line.find("WS_OVERLAPPEDWINDOW") != std::string::npos);
        }
        CHECK(creates == 1);
    }
}

TEST_CASE("CAPTL5 时间轴横向滚动条：超过一屏时给出范围与位置，位置能换回时刻，放得下时不显示", "[captioneditor]")
{
    //录像 60 秒、每秒 100 像素：内容 6000 像素；可见宽度 1000 像素
    const int64_t kDurationMs = 60000;
    const double kPxPerSec = 100.0;
    const int kWidth = 1000;
    TimelineView v;
    v.m_pxPerSec = kPxPerSec;
    v.m_width = kWidth;
    v.m_scrollMs = 12000;
    TimelineScrollBarState s = TimelineScrollBar(v, kDurationMs);
    CHECK(s.m_maxPos == 5000);
    CHECK(s.m_page == kWidth);
    CHECK(s.m_pos == 1200);
    //位置换回时刻；超出范围时夹到「时长减去一屏」
    CHECK(TimelineScrollFromBar(v, 2500, kDurationMs) == 25000);
    CHECK(TimelineScrollFromBar(v, 0, kDurationMs) == 0);
    CHECK(TimelineScrollFromBar(v, 9000, kDurationMs) == 50000);
    CHECK(TimelineScrollFromBar(v, -10, kDurationMs) == 0);
    //内容放得下（5 秒 = 500 像素）：不显示
    s = TimelineScrollBar(v, 5000);
    CHECK(s.m_maxPos == 0);
    //宽度为 0（还没排版）：不显示
    v.m_width = 0;
    s = TimelineScrollBar(v, kDurationMs);
    CHECK(s.m_maxPos == 0);
}
