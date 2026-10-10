/**
 *  CaptionModelTests.cpp
 *
 *  语音字幕第 1 期（docs/语音字幕方案.md 第 12 节）纯逻辑的单元测试，标签 [caption]：
 *    CAPX   —— 文字工具（CaptionText）：显示宽度、文字整理、行首标点、拼接、查找替换、UTF-8 互转；
 *    CAPTM  —— 时间格式（CaptionTime）：SRT 时间的写出与解析、界面时间；
 *    CAPT   —— 字幕轨（CaptionTrack）：增删改、排序、拆分、合并、平移、批量替换、查找；
 *    CAPU   —— 撤销与重做（CaptionUndoStack）；
 *    CAPR   —— 自动替换表（CaptionReplaceTable）；
 *    CAPS   —— 字幕条切分（CaptionSegmenter）：宽度与时长上限、优先在标点处断开、不拆英文单词、时间按宽度分配；
 *    CAPA   —— 识别结果装配（CaptionAssembler）：行首标点并回上一条、替换表；
 *    CAPSRT —— SRT 读写（CaptionSrt）；
 *    CAPD   —— 草稿（CaptionDraftIo）：JSON 往返、版本与容错、存盘读盘、导出记录；
 *    CAPF   —— 字幕文件（CaptionFiles）：导出路径、状态查询、录像改名与删除时同步处理草稿。
 *
 *  纯内存断言；CAPD3、CAPF2、CAPF3 在系统临时目录里建、删几个小文件。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../Model/CaptionCue.h"
#include "../Model/CaptionDraft.h"
#include "../Model/CaptionFiles.h"
#include "../Model/CaptionSegmenter.h"
#include "../Model/CaptionText.h"
#include "../Model/CaptionTime.h"
#include "../Model/CaptionTrack.h"

using namespace caption;

namespace {

CaptionCue MakeCue(int64_t startMs, int64_t endMs, const std::wstring& text)
{
    CaptionCue c;
    c.m_startMs = startMs;
    c.m_endMs = endMs;
    c.m_text = text;
    c.m_source = caption_source_mic;
    return c;
}

std::wstring TempFilePath(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

} // namespace

//==================== CAPX 文字工具 ====================

TEST_CASE("CAPX1 显示宽度：汉字与全角算 2，英文数字算 1，换行不算，扩展区汉字算 2", "[caption]")
{
    CHECK(DisplayUnits(L"中文") == 4);
    CHECK(DisplayUnits(L"ab") == 2);
    CHECK(DisplayUnits(L"中a") == 3);
    CHECK(DisplayUnits(L"中\n文") == 4);
    CHECK(DisplayUnits(L"，。") == 4);
    //U+20000（扩展区 B 的第一个汉字）由一对代理组成，只算一个宽字符
    CHECK(DisplayUnits(L"\xD840\xDC00") == 2);
    CHECK(DisplayUnits(L"") == 0);
}

TEST_CASE("CAPX2 文字整理：去首尾空白、合并空白、去掉全角标点与两个汉字之间的空格，保留英文之间的空格", "[caption]")
{
    CHECK(NormalizeText(L"  模式， 两边的模式  ") == L"模式，两边的模式");
    CHECK(NormalizeText(L"原有的 MySQL 模式") == L"原有的 MySQL 模式");
    CHECK(NormalizeText(L"这是 第一种") == L"这是第一种");
    CHECK(NormalizeText(L"a   b\tc") == L"a b c");
    CHECK(NormalizeText(L"第一行 \r\n 第二行") == L"第一行\n第二行");
    CHECK(NormalizeText(L"\n\n第一行\n\n\n第二行\n") == L"第一行\n第二行");
    CHECK(NormalizeText(L"。本节结论") == L"。本节结论");
    CHECK(NormalizeText(L"   ") == L"");
}

TEST_CASE("CAPX3 行首标点：只有标点（可夹空格）才算，纯空白不算", "[caption]")
{
    CHECK(LeadingPunctLength(L"。本节") == 1);
    CHECK(LeadingPunctLength(L" ， 本节") == 3);
    CHECK(LeadingPunctLength(L"本节") == 0);
    CHECK(LeadingPunctLength(L"  本") == 0);
    CHECK(LeadingPunctLength(L"?ok") == 1);
}

TEST_CASE("CAPX4 拼接：两边相邻处都是英文数字时加空格，否则直接相连", "[caption]")
{
    CHECK(JoinText(L"SQLite", L"open") == L"SQLite open");
    CHECK(JoinText(L"中", L"文") == L"中文");
    CHECK(JoinText(L"a。", L"b") == L"a。b");
    CHECK(JoinText(L"", L"b") == L"b");
    CHECK(JoinText(L"a", L"") == L"a");
}

TEST_CASE("CAPX5 查找替换：大小写只对英文生效，整词匹配不改动更长单词里的部分", "[caption]")
{
    CHECK(FindInText(L"本地 Sclight 文件", L"sclight", 0, false, false) == 3);
    CHECK(FindInText(L"本地 Sclight 文件", L"sclight", 0, true, false) == std::wstring::npos);
    CHECK(FindInText(L"MySQL 和 SQL", L"SQL", 0, true, true) == 8);
    CHECK(FindInText(L"MySQL", L"SQL", 0, true, false) == 2);
    CHECK(FindInText(L"abc", L"", 0, true, false) == std::wstring::npos);
    int n = 0;
    CHECK(ReplaceInText(L"Sclight 和 sclight", L"SCLIGHT", L"SQLite", false, true, &n) == L"SQLite 和 SQLite");
    CHECK(n == 2);
    CHECK(ReplaceInText(L"MySQL 与 SQL", L"SQL", L"S", true, true, &n) == L"MySQL 与 S");
    CHECK(n == 1);
}

TEST_CASE("CAPX6 UTF-8 与 UTF-16 互转往返不变", "[caption]")
{
    const std::wstring w = L"张三：SQLite 与 MySQL，测试\xD83D\xDE00";
    CHECK(Utf8ToWide(WideToUtf8(w)) == w);
    CHECK(WideToUtf8(L"") == "");
    CHECK(Utf8ToWide("") == L"");
    CHECK(WideToUtf8(L"中") == "\xE4\xB8\xAD");
}

//==================== CAPTM 时间格式 ====================

TEST_CASE("CAPTM1 SRT 时间的写出", "[caption]")
{
    CHECK(FormatSrtTime(0) == "00:00:00,000");
    CHECK(FormatSrtTime(62345) == "00:01:02,345");
    CHECK(FormatSrtTime(3723004) == "01:02:03,004");
    CHECK(FormatSrtTime(-5) == "00:00:00,000");
}

TEST_CASE("CAPTM2 SRT 时间的解析：逗号或点号、毫秒位数不足按小数、非法格式拒绝", "[caption]")
{
    int64_t ms = 0;
    CHECK(ParseSrtTime("00:01:02,345", &ms));
    CHECK(ms == 62345);
    CHECK(ParseSrtTime("0:01:02.5", &ms));
    CHECK(ms == 62500);
    CHECK(ParseSrtTime("00:01:02", &ms));
    CHECK(ms == 62000);
    CHECK(ParseSrtTime(" 01:00:00,000 ", &ms));
    CHECK(ms == 3600000);
    CHECK_FALSE(ParseSrtTime("00:61:00,000", &ms));
    CHECK_FALSE(ParseSrtTime("abc", &ms));
    CHECK_FALSE(ParseSrtTime("00:01:02,3456", &ms));
}

TEST_CASE("CAPTM3 界面时间：不足一小时不写小时", "[caption]")
{
    CHECK(FormatClock(62345) == L"1:02.3");
    CHECK(FormatClock(3723004) == L"1:02:03.0");
    CHECK(FormatClock(0) == L"0:00.0");
}

//==================== CAPT 字幕轨 ====================

TEST_CASE("CAPT1 加字幕：按开始时刻排序、编号递增、起止时刻不合法时拒绝", "[caption]")
{
    CaptionTrack t;
    const int64_t a = t.Add(MakeCue(5000, 6000, L"后"));
    const int64_t b = t.Add(MakeCue(1000, 2000, L"前"));
    CHECK(a == 1);
    CHECK(b == 2);
    REQUIRE(t.Count() == 2);
    CHECK(t.At(0).m_text == L"前");
    CHECK(t.At(1).m_text == L"后");
    CHECK(t.Add(MakeCue(3000, 3000, L"x")) == 0);
    CHECK(t.Add(MakeCue(-1, 300, L"x")) == 0);
    CHECK(t.Count() == 2);
    CHECK(t.IndexOfId(a) == 1);
    CHECK(t.IndexOfId(99) == -1);
}

TEST_CASE("CAPT2 改文字与说话人：文字会整理，并标记为改过；下标越界返回 false", "[caption]")
{
    CaptionTrack t;
    t.Add(MakeCue(0, 1000, L"原文"));
    CHECK_FALSE(t.At(0).m_edited);
    CHECK(t.SetText(0, L"  新的 文字 "));
    CHECK(t.At(0).m_text == L"新的文字");
    CHECK(t.At(0).m_edited);
    CHECK(t.SetSpeaker(0, L"张三"));
    CHECK(t.At(0).m_speaker == L"张三");
    CHECK_FALSE(t.SetText(1, L"x"));
    CHECK_FALSE(t.SetSpeaker(5, L"x"));
}

TEST_CASE("CAPT3 调时间：会重新排序；短于最短时长或开始为负时拒绝", "[caption]")
{
    CaptionTrack t;
    const int64_t a = t.Add(MakeCue(0, 1000, L"甲"));
    t.Add(MakeCue(2000, 3000, L"乙"));
    CHECK(t.SetTimes(0, 4000, 5000));
    CHECK(t.At(1).m_id == a);
    CHECK(t.At(1).m_edited);
    CHECK_FALSE(t.SetTimes(0, 2000, 2000 + kCaptionMinCueMs - 1));
    CHECK_FALSE(t.SetTimes(0, -10, 1000));
    CHECK(t.At(0).m_startMs == 2000);
}

TEST_CASE("CAPT4 拆分：按文字宽度比例分时间，后一半得到新编号；位置不合法或太短时拒绝", "[caption]")
{
    CaptionTrack t;
    const int64_t a = t.Add(MakeCue(0, 3000, L"前一半后半句"));
    int64_t b = 0;
    REQUIRE(t.Split(0, 3, &b));
    REQUIRE(t.Count() == 2);
    CHECK(t.At(0).m_id == a);
    CHECK(t.At(0).m_text == L"前一半");
    CHECK(t.At(0).m_endMs == 1500);
    CHECK(t.At(1).m_id == b);
    CHECK(t.At(1).m_text == L"后半句");
    CHECK(t.At(1).m_startMs == 1500);
    CHECK(t.At(1).m_endMs == 3000);
    CHECK(t.At(0).m_edited);
    CHECK(t.At(1).m_edited);
    CHECK_FALSE(t.Split(0, 0, NULL));
    CHECK_FALSE(t.Split(0, 3, NULL));
    t.Add(MakeCue(10000, 10000 + kCaptionMinCueMs + 100, L"很短的字幕"));
    CHECK_FALSE(t.Split(2, 2, NULL));
}

TEST_CASE("CAPT5 合并：文字按规则拼接，结束取较晚者，沿用前一条的编号与说话人", "[caption]")
{
    CaptionTrack t;
    CaptionCue first = MakeCue(0, 1000, L"SQLite");
    first.m_speaker = L"张三";
    const int64_t a = t.Add(first);
    t.Add(MakeCue(1200, 2500, L"open"));
    REQUIRE(t.MergeWithNext(0));
    REQUIRE(t.Count() == 1);
    CHECK(t.At(0).m_id == a);
    CHECK(t.At(0).m_text == L"SQLite open");
    CHECK(t.At(0).m_endMs == 2500);
    CHECK(t.At(0).m_speaker == L"张三");
    CHECK(t.At(0).m_edited);
    CHECK_FALSE(t.MergeWithNext(0));
}

TEST_CASE("CAPT6 整体平移：往前移出 0 的截到 0，不短于最短时长", "[caption]")
{
    CaptionTrack t;
    t.Add(MakeCue(100, 150 + kCaptionMinCueMs, L"甲"));
    t.Add(MakeCue(2000, 3000, L"乙"));
    t.ShiftAll(-500);
    CHECK(t.At(0).m_startMs == 0);
    CHECK(t.At(0).m_endMs == kCaptionMinCueMs);
    CHECK(t.At(1).m_startMs == 1500);
    CHECK(t.At(1).m_endMs == 2500);
    t.ShiftAll(1000);
    CHECK(t.At(1).m_startMs == 2500);
    CHECK(t.At(0).m_edited);
}

TEST_CASE("CAPT7 批量替换只标记改动了的字幕；批量改说话人", "[caption]")
{
    CaptionTrack t;
    CaptionCue a = MakeCue(0, 1000, L"Sclight 很好用");
    a.m_speaker = L"甲";
    CaptionCue b = MakeCue(1000, 2000, L"没有要改的");
    b.m_speaker = L"甲";
    t.Add(a);
    t.Add(b);
    CHECK(t.ReplaceAll(L"sclight", L"SQLite", false) == 1);
    CHECK(t.At(0).m_text == L"SQLite 很好用");
    CHECK(t.At(0).m_edited);
    CHECK_FALSE(t.At(1).m_edited);
    CHECK(t.ReplaceAll(L"sclight", L"x", true) == 0);
    CHECK(t.RenameSpeaker(L"甲", L"张三") == 2);
    CHECK(t.At(1).m_speaker == L"张三");
}

TEST_CASE("CAPT8 查找：向后找并从头绕回；按时刻定位（含开始、不含结束，重叠时取开始最晚的）", "[caption]")
{
    CaptionTrack t;
    t.Add(MakeCue(0, 1000, L"苹果"));
    t.Add(MakeCue(1000, 2000, L"香蕉"));
    t.Add(MakeCue(1500, 3000, L"苹果派"));
    CHECK(t.FindNext(L"苹果", 1, true) == 2);
    CHECK(t.FindNext(L"苹果", 3, true) == 0);
    CHECK(t.FindNext(L"葡萄", 0, true) == -1);
    CHECK(t.IndexAtTime(0) == 0);
    CHECK(t.IndexAtTime(999) == 0);
    CHECK(t.IndexAtTime(1000) == 1);
    CHECK(t.IndexAtTime(1600) == 2);
    CHECK(t.IndexAtTime(3000) == -1);
}

TEST_CASE("CAPT9 整体替换内容：编号为 0 的分配新编号，下一个编号只增不减", "[caption]")
{
    CaptionTrack t;
    t.Add(MakeCue(0, 1000, L"a"));
    t.Add(MakeCue(1000, 2000, L"b"));
    std::vector<CaptionCue> cues;
    CaptionCue x = MakeCue(500, 900, L"x");
    x.m_id = 7;
    cues.push_back(x);
    cues.push_back(MakeCue(0, 400, L"y"));
    t.Assign(cues);
    REQUIRE(t.Count() == 2);
    CHECK(t.At(0).m_text == L"y");
    CHECK(t.At(0).m_id == 8);
    CHECK(t.NextId() == 9);
    t.SetNextId(3);
    CHECK(t.NextId() == 9);
    t.SetNextId(20);
    CHECK(t.NextId() == 20);
    t.Clear();
    CHECK(t.Count() == 0);
    CHECK(t.Add(MakeCue(0, 1000, L"z")) == 20);
}

//==================== CAPU 撤销与重做 ====================

TEST_CASE("CAPU1 撤销与重做：新的修改清空重做；超过上限丢弃最早的快照", "[caption]")
{
    CaptionTrack t;
    CaptionUndoStack undo(2);
    CHECK_FALSE(undo.CanUndo());
    undo.Push(t.Cues());
    t.Add(MakeCue(0, 1000, L"一"));
    undo.Push(t.Cues());
    t.Add(MakeCue(1000, 2000, L"二"));
    undo.Push(t.Cues());
    t.Add(MakeCue(2000, 3000, L"三"));

    std::vector<CaptionCue> cur = t.Cues();
    REQUIRE(undo.Undo(cur));
    CHECK(cur.size() == 2);
    REQUIRE(undo.Undo(cur));
    CHECK(cur.size() == 1);
    //上限为 2：最早那份（空的）已被丢弃
    CHECK_FALSE(undo.Undo(cur));
    REQUIRE(undo.Redo(cur));
    CHECK(cur.size() == 2);
    undo.Push(cur);
    CHECK_FALSE(undo.CanRedo());
    undo.Clear();
    CHECK_FALSE(undo.CanUndo());
}

//==================== CAPR 自动替换表 ====================

TEST_CASE("CAPR1 替换表：默认不区分大小写、英文整词匹配；可关闭整词匹配", "[caption]")
{
    CaptionReplaceTable table;
    CaptionReplaceRule r;
    r.m_from = L"Sclight";
    r.m_to = L"SQLite";
    REQUIRE(table.Add(r));
    int n = 0;
    CHECK(table.Apply(L"本地 sclight 与 SCLIGHT", &n) == L"本地 SQLite 与 SQLite");
    CHECK(n == 2);

    CaptionReplaceRule s;
    s.m_from = L"SQL";
    s.m_to = L"S-Q-L";
    table.Add(s);
    CHECK(table.Apply(L"MySQL 和 SQL", NULL) == L"MySQL 和 S-Q-L");
    s.m_wholeWord = false;
    table.Add(s);
    CHECK(table.Apply(L"MySQL", NULL) == L"MyS-Q-L");
}

TEST_CASE("CAPR2 替换表：长的规则先应用", "[caption]")
{
    CaptionReplaceTable table;
    CaptionReplaceRule a;
    a.m_from = L"Sql";
    a.m_to = L"SQL";
    a.m_wholeWord = false;
    CaptionReplaceRule b;
    b.m_from = L"Sqlight";
    b.m_to = L"SQLite";
    table.Add(a);
    table.Add(b);
    CHECK(table.Apply(L"Sqlight 与 Sql", NULL) == L"SQLite 与 SQL");
}

TEST_CASE("CAPR3 替换表：相同的原文（不区分大小写）覆盖；删除；空原文不加入", "[caption]")
{
    CaptionReplaceTable table;
    CaptionReplaceRule r;
    r.m_from = L"abc";
    r.m_to = L"1";
    table.Add(r);
    r.m_from = L"ABC";
    r.m_to = L"2";
    table.Add(r);
    REQUIRE(table.Rules().size() == 1);
    CHECK(table.Rules()[0].m_to == L"2");
    CHECK(table.Remove(L"Abc"));
    CHECK(table.Rules().empty());
    CHECK_FALSE(table.Remove(L"abc"));
    r.m_from = L"";
    CHECK_FALSE(table.Add(r));
}

TEST_CASE("CAPR4 替换表的 JSON 往返；格式不对时保持原有规则", "[caption]")
{
    CaptionReplaceTable table;
    CaptionReplaceRule r;
    r.m_from = L"马斯克";
    r.m_to = L"MySQL";
    r.m_matchCase = true;
    r.m_wholeWord = false;
    table.Add(r);
    const std::string json = table.ToJson();
    CaptionReplaceTable other;
    REQUIRE(other.FromJson(json));
    REQUIRE(other.Rules().size() == 1);
    CHECK(other.Rules()[0].m_from == L"马斯克");
    CHECK(other.Rules()[0].m_to == L"MySQL");
    CHECK(other.Rules()[0].m_matchCase);
    CHECK_FALSE(other.Rules()[0].m_wholeWord);
    CHECK_FALSE(other.FromJson("{not json"));
    CHECK_FALSE(other.FromJson("{\"a\":1}"));
    CHECK(other.Rules().size() == 1);
}

//==================== CAPS 字幕条切分 ====================

TEST_CASE("CAPS1 短句一条字幕：时间与说话人、来源照抄；空文字不出字幕", "[caption]")
{
    const std::vector<CaptionCue> cues =
        CaptionSegmenter::Split(L"  你好 世界  ", 1000, 2500, L"张三", caption_source_others, CaptionSegmentRules());
    REQUIRE(cues.size() == 1);
    CHECK(cues[0].m_text == L"你好世界");
    CHECK(cues[0].m_startMs == 1000);
    CHECK(cues[0].m_endMs == 2500);
    CHECK(cues[0].m_speaker == L"张三");
    CHECK(cues[0].m_source == caption_source_others);
    CHECK(cues[0].m_id == 0);
    CHECK(CaptionSegmenter::Split(L"   ", 0, 1000, L"", 0, CaptionSegmentRules()).empty());
}

TEST_CASE("CAPS2 长句：每条不超过两行宽度与 6 秒，时间首尾相接、覆盖整句", "[caption]")
{
    const std::wstring text =
        L"本方案的目标是增加一种本地 SQLite 文件数据库模式，同时保留原有的 MySQL 模式，"
        L"两边的模式共用一套业务代码，由 ETC 目录下的 chat server 点 conf 里面的配置项运行时选择";
    const CaptionSegmentRules rules;
    const std::vector<CaptionCue> cues = CaptionSegmenter::Split(text, 20000, 40000, L"", 0, rules);
    REQUIRE(cues.size() >= 4);
    CHECK(cues.front().m_startMs == 20000);
    CHECK(cues.back().m_endMs == 40000);
    for (size_t i = 0; i < cues.size(); ++i)
    {
        CHECK(DisplayUnits(cues[i].m_text) <= rules.m_maxLineUnits * rules.m_maxLines);
        CHECK(cues[i].m_endMs - cues[i].m_startMs <= rules.m_maxCueMs + 1);
        if (i > 0)
            CHECK(cues[i].m_startMs == cues[i - 1].m_endMs);
    }
}

TEST_CASE("CAPS3 优先在句末标点、其次在句中标点处断开", "[caption]")
{
    const std::vector<std::wstring> parts =
        CaptionSegmenter::SplitText(L"第一句话说完了。第二句话接着说，第三句", 24, 2);
    REQUIRE(parts.size() == 2);
    CHECK(parts[0] == L"第一句话说完了。");
    CHECK(parts[1] == L"第二句话接着说，第三句");
}

TEST_CASE("CAPS4 不拆开英文单词；超长单词才从中间断开", "[caption]")
{
    const std::vector<std::wstring> parts = CaptionSegmenter::SplitText(L"aaaa bbbbbbbb cccc dd", 10, 2);
    REQUIRE(parts.size() >= 2);
    for (size_t i = 0; i < parts.size(); ++i)
    {
        CHECK((parts[i].find(L"bbbbbbbb") != std::wstring::npos || parts[i].find(L'b') == std::wstring::npos));
        CHECK(DisplayUnits(parts[i]) <= 10);
    }
    const std::vector<std::wstring> longWord = CaptionSegmenter::SplitText(L"abcdefghijklmnopqrst", 8, 1);
    REQUIRE(longWord.size() == 3);
    CHECK(longWord[0] == L"abcdefgh");
}

TEST_CASE("CAPS5 文字太长时以宽度限制为先：两百个汉字两秒也要切成多条", "[caption]")
{
    const std::wstring text(200, L'字');
    const CaptionSegmentRules rules;
    const std::vector<CaptionCue> cues = CaptionSegmenter::Split(text, 0, 2000, L"", 0, rules);
    CHECK(cues.size() >= 6);
    for (size_t i = 0; i < cues.size(); ++i)
        CHECK(DisplayUnits(cues[i].m_text) <= rules.m_maxLineUnits * rules.m_maxLines);
}

TEST_CASE("CAPS6 结束时刻缺失时按每单位 100 毫秒估算", "[caption]")
{
    const std::vector<CaptionCue> cues =
        CaptionSegmenter::Split(L"十个汉字的一句话呀", 1000, 1000, L"", 0, CaptionSegmentRules());
    REQUIRE(cues.size() == 1);
    CHECK(cues[0].m_endMs == 1000 + 18 * 100);
}

//==================== CAPA 识别结果装配 ====================

TEST_CASE("CAPA1 行首标点并回同一路的上一条，且不标记为改过", "[caption]")
{
    CaptionTrack t;
    CaptionAssembler a(CaptionSegmentRules(), NULL);
    CHECK(a.OnFinal(t, L"第一句", 0, 2000, L"张三", caption_source_mic) == 1);
    CHECK(a.OnFinal(t, L"。第二句", 2500, 4000, L"张三", caption_source_mic) == 1);
    REQUIRE(t.Count() == 2);
    CHECK(t.At(0).m_text == L"第一句。");
    CHECK_FALSE(t.At(0).m_edited);
    CHECK(t.At(1).m_text == L"第二句");
}

TEST_CASE("CAPA2 行首标点：不同说话人或间隔太久时丢弃，不并回", "[caption]")
{
    CaptionTrack t;
    CaptionAssembler a(CaptionSegmentRules(), NULL);
    a.OnFinal(t, L"甲说的", 0, 1000, L"甲", caption_source_others);
    a.OnFinal(t, L"，乙说的", 1200, 2000, L"乙", caption_source_others);
    CHECK(t.At(0).m_text == L"甲说的");
    CHECK(t.At(1).m_text == L"乙说的");
    a.OnFinal(t, L"。很久以后", 2000 + kCaptionPunctMergeGapMs + 1000, 9000, L"乙", caption_source_others);
    CHECK(t.At(1).m_text == L"乙说的");
    a.Reset();
    a.OnFinal(t, L"。重置以后", 9100, 9900, L"乙", caption_source_others);
    CHECK(t.At(1).m_text == L"乙说的");
}

TEST_CASE("CAPA3 装配时应用替换表；只有标点的结果不出新字幕", "[caption]")
{
    CaptionReplaceTable table;
    CaptionReplaceRule r;
    r.m_from = L"Sclight";
    r.m_to = L"SQLite";
    table.Add(r);
    CaptionTrack t;
    CaptionAssembler a(CaptionSegmentRules(), &table);
    a.OnFinal(t, L"这里用 Sclight 存储", 0, 2000, L"", caption_source_mic);
    CHECK(t.At(0).m_text == L"这里用 SQLite 存储");
    CHECK(a.OnFinal(t, L"。", 2100, 2200, L"", caption_source_mic) == 0);
    CHECK(t.Count() == 1);
    CHECK(t.At(0).m_text == L"这里用 SQLite 存储。");
}

//==================== CAPSRT SRT 读写 ====================

TEST_CASE("CAPSRT1 写 SRT：带 BOM、CRLF、序号从 1 起、按时间排序、可加说话人前缀", "[caption]")
{
    std::vector<CaptionCue> cues;
    CaptionCue b = MakeCue(3000, 4500, L"第二条\n第二行");
    CaptionCue a = MakeCue(500, 2000, L"第一条");
    a.m_speaker = L"张三";
    cues.push_back(b);
    cues.push_back(a);
    const std::string srt = CaptionSrt::Write(cues, true);
    const std::string expected = std::string("\xEF\xBB\xBF") + "1\r\n00:00:00,500 --> 00:00:02,000\r\n" +
        WideToUtf8(L"张三：第一条") + "\r\n\r\n2\r\n00:00:03,000 --> 00:00:04,500\r\n" +
        WideToUtf8(L"第二条") + "\r\n" + WideToUtf8(L"第二行") + "\r\n\r\n";
    CHECK(srt == expected);
    const std::string plain = CaptionSrt::Write(cues, false);
    CHECK(plain.find(WideToUtf8(L"张三")) == std::string::npos);
}

TEST_CASE("CAPSRT2 读 SRT：兼容 LF、缺序号、点号毫秒、时间后的位置信息、多行；坏段落跳过并计数", "[caption]")
{
    const std::string srt =
        "5\n00:00:05,000 --> 00:00:06,000\n" + WideToUtf8(L"后面的") + "\n\n"
        "00:00:01.5 --> 00:00:02.25 X1:10 X2:20\n" + WideToUtf8(L"第一行") + "\n" + WideToUtf8(L"第二行") + "\n\n"
        "7\nbad time line\ntext\n\n"
        "8\n00:00:09,000 --> 00:00:08,000\nreversed\n";
    std::vector<CaptionCue> out;
    int skipped = 0;
    REQUIRE(CaptionSrt::Parse(srt, out, &skipped));
    CHECK(skipped == 2);
    REQUIRE(out.size() == 2);
    CHECK(out[0].m_startMs == 1500);
    CHECK(out[0].m_endMs == 2250);
    CHECK(out[0].m_text == L"第一行\n第二行");
    CHECK(out[1].m_text == L"后面的");
    CHECK(out[1].m_source == caption_source_manual);
    CHECK_FALSE(CaptionSrt::Parse("", out, NULL));
}

TEST_CASE("CAPSRT3 写出再读回：时间与文字不变", "[caption]")
{
    std::vector<CaptionCue> cues;
    cues.push_back(MakeCue(0, 1234, L"甲 SQLite"));
    cues.push_back(MakeCue(3600000, 3601001, L"一小时以后"));
    std::vector<CaptionCue> back;
    REQUIRE(CaptionSrt::Parse(CaptionSrt::Write(cues, false), back, NULL));
    REQUIRE(back.size() == 2);
    CHECK(back[0].m_startMs == 0);
    CHECK(back[0].m_endMs == 1234);
    CHECK(back[0].m_text == L"甲 SQLite");
    CHECK(back[1].m_startMs == 3600000);
    CHECK(back[1].m_text == L"一小时以后");
}

//==================== CAPD 草稿 ====================

TEST_CASE("CAPD1 草稿的 JSON 往返：录像信息、样式、画中画、字幕与下一个编号", "[caption]")
{
    CaptionDraft d;
    d.m_videoName = L"FlamingoRecord_测试.mp4";
    d.m_videoBytes = 123456789;
    d.m_videoDurationMs = 600000;
    d.m_style.m_fontPx = 40;
    d.m_style.m_boxAlpha = 128;
    d.m_style.m_showSpeaker = false;
    d.m_style.m_textColor = 0xFFEE00;
    d.m_pip.m_present = true;
    d.m_pip.m_left = 0.75;
    d.m_pip.m_top = 0.7;
    d.m_pip.m_right = 0.98;
    d.m_pip.m_bottom = 0.97;
    CaptionCue c = MakeCue(1000, 2000, L"张三说的话");
    c.m_id = 3;
    c.m_speaker = L"张三";
    c.m_source = caption_source_others;
    c.m_edited = true;
    d.m_cues.push_back(c);
    d.m_nextId = 4;

    CaptionDraft back;
    std::string err;
    REQUIRE(CaptionDraftIo::FromJson(CaptionDraftIo::ToJson(d), back, &err));
    CHECK(back.m_videoName == d.m_videoName);
    CHECK(back.m_videoBytes == d.m_videoBytes);
    CHECK(back.m_videoDurationMs == d.m_videoDurationMs);
    CHECK(back.m_style.m_fontPx == 40);
    CHECK(back.m_style.m_boxAlpha == 128);
    CHECK_FALSE(back.m_style.m_showSpeaker);
    CHECK(back.m_style.m_textColor == 0xFFEE00u);
    CHECK(back.m_pip.m_present);
    CHECK(back.m_pip.m_left == Catch::Approx(0.75));
    CHECK(back.m_pip.m_bottom == Catch::Approx(0.97));
    REQUIRE(back.m_cues.size() == 1);
    CHECK(back.m_cues[0].m_id == 3);
    CHECK(back.m_cues[0].m_text == L"张三说的话");
    CHECK(back.m_cues[0].m_speaker == L"张三");
    CHECK(back.m_cues[0].m_source == caption_source_others);
    CHECK(back.m_cues[0].m_edited);
    CHECK(back.m_nextId == 4);
}

TEST_CASE("CAPD2 读草稿的容错：版本过新拒绝；坏字幕跳过；缺字段取默认；来源越界截断", "[caption]")
{
    CaptionDraft d;
    CHECK_FALSE(CaptionDraftIo::FromJson("{\"version\":99}", d, NULL));
    CHECK_FALSE(CaptionDraftIo::FromJson("[1,2]", d, NULL));
    CHECK_FALSE(CaptionDraftIo::FromJson("not json", d, NULL));
    const std::string json =
        "{\"version\":1,\"cues\":[{\"start\":5,\"end\":3,\"text\":\"bad\"},"
        "{\"start\":0,\"end\":100,\"text\":\"ok\",\"source\":42}]}";
    REQUIRE(CaptionDraftIo::FromJson(json, d, NULL));
    REQUIRE(d.m_cues.size() == 1);
    CHECK(d.m_cues[0].m_text == L"ok");
    CHECK(d.m_cues[0].m_source == caption_source_count - 1);
    CHECK(d.m_style.m_fontPx == CaptionStyle().m_fontPx);
    CHECK_FALSE(d.m_pip.m_present);
}

TEST_CASE("CAPD3 草稿存盘与读盘：不留临时文件；路径为录像路径加后缀；文件不存在时失败", "[caption]")
{
    const std::wstring video = TempFilePath(L"caption_capd3_video.mp4");
    const std::wstring path = CaptionDraftIo::DraftPathFor(video);
    CHECK(path == video + L".caption.json");
    DeleteFileW(path.c_str());

    CaptionDraft d;
    d.m_cues.push_back(MakeCue(0, 1000, L"存盘测试"));
    REQUIRE(CaptionDraftIo::SaveFile(path, d));
    CHECK(GetFileAttributesW((path + L".tmp").c_str()) == INVALID_FILE_ATTRIBUTES);
    d.m_cues[0].m_text = L"第二次存盘";
    REQUIRE(CaptionDraftIo::SaveFile(path, d));

    CaptionDraft back;
    std::string err;
    REQUIRE(CaptionDraftIo::LoadFile(path, back, &err));
    REQUIRE(back.m_cues.size() == 1);
    CHECK(back.m_cues[0].m_text == L"第二次存盘");
    DeleteFileW(path.c_str());
    CHECK_FALSE(CaptionDraftIo::LoadFile(path, back, &err));
}

TEST_CASE("CAPD4 草稿的导出记录往返；种类不认识或文件名为空的记录丢弃", "[caption]")
{
    CaptionDraft d;
    CaptionExport e;
    e.m_name = L"录像-字幕.mp4";
    e.m_kind = caption_export_hard;
    d.m_exports.push_back(e);
    e.m_name = L"录像.srt";
    e.m_kind = caption_export_srt;
    d.m_exports.push_back(e);
    CaptionDraft back;
    REQUIRE(CaptionDraftIo::FromJson(CaptionDraftIo::ToJson(d), back, NULL));
    REQUIRE(back.m_exports.size() == 2);
    CHECK(back.m_exports[0].m_name == L"录像-字幕.mp4");
    CHECK(back.m_exports[1].m_kind == caption_export_srt);
    REQUIRE(CaptionDraftIo::FromJson("{\"exports\":[{\"name\":\"a\",\"kind\":9},{\"name\":\"\",\"kind\":1}]}", back, NULL));
    CHECK(back.m_exports.empty());
}

//==================== CAPF 字幕文件 ====================

TEST_CASE("CAPF1 导出路径与文件名、目录的拆分", "[caption]")
{
    CHECK(MakeExportPath(L"C:\\v\\rec.mp4", L"-字幕", L".mp4") == L"C:\\v\\rec-字幕.mp4");
    CHECK(MakeExportPath(L"C:\\v\\rec.mp4", L"", L".srt") == L"C:\\v\\rec.srt");
    CHECK(MakeExportPath(L"C:\\v\\my.rec.mp4", L"", L".srt") == L"C:\\v\\my.rec.srt");
    CHECK(MakeExportPath(L"rec", L"-x", L".mp4") == L"rec-x.mp4");
    CHECK(FileNameOf(L"C:\\v\\rec.mp4") == L"rec.mp4");
    CHECK(FileNameOf(L"C:/v/rec.mp4") == L"rec.mp4");
    CHECK(DirectoryOf(L"C:\\v\\rec.mp4") == L"C:\\v\\");
    CHECK(DirectoryOf(L"rec.mp4") == L"");
}

TEST_CASE("CAPF2 状态查询：没有草稿为 0；有草稿时按导出记录检查文件是否还在", "[caption]")
{
    const std::wstring video = TempFilePath(L"caption_capf2.mp4");
    const std::wstring draft = CaptionDraftIo::DraftPathFor(video);
    const std::wstring hard = MakeExportPath(video, L"-hard", L".mp4");
    const std::wstring srt = MakeExportPath(video, L"", L".srt");
    DeleteFileW(draft.c_str());
    DeleteFileW(hard.c_str());
    DeleteFileW(srt.c_str());
    CHECK(QueryCaptionFileStatus(video) == kCaptionFileNone);

    CaptionDraft d;
    CaptionExport e;
    e.m_name = FileNameOf(hard);
    e.m_kind = caption_export_hard;
    d.m_exports.push_back(e);
    e.m_name = FileNameOf(srt);
    e.m_kind = caption_export_srt;
    d.m_exports.push_back(e);
    REQUIRE(CaptionDraftIo::SaveFile(draft, d));
    CHECK(QueryCaptionFileStatus(video) == kCaptionFileDraft);

    HANDLE f = CreateFileW(hard.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    REQUIRE(f != INVALID_HANDLE_VALUE);
    CloseHandle(f);
    CHECK(QueryCaptionFileStatus(video) == (kCaptionFileDraft | kCaptionFileHard));
    DeleteFileW(hard.c_str());
    DeleteFileW(draft.c_str());
}

TEST_CASE("CAPF3 录像改名时草稿一并改名并更新记录的录像名；删除录像时删除草稿", "[caption]")
{
    const std::wstring oldVideo = TempFilePath(L"caption_capf3_old.mp4");
    const std::wstring newVideo = TempFilePath(L"caption_capf3_new.mp4");
    const std::wstring oldDraft = CaptionDraftIo::DraftPathFor(oldVideo);
    const std::wstring newDraft = CaptionDraftIo::DraftPathFor(newVideo);
    DeleteFileW(oldDraft.c_str());
    DeleteFileW(newDraft.c_str());
    CHECK_FALSE(RenameCaptionDraft(oldVideo, newVideo));

    CaptionDraft d;
    d.m_videoName = FileNameOf(oldVideo);
    REQUIRE(CaptionDraftIo::SaveFile(oldDraft, d));
    REQUIRE(RenameCaptionDraft(oldVideo, newVideo));
    CHECK(GetFileAttributesW(oldDraft.c_str()) == INVALID_FILE_ATTRIBUTES);
    CaptionDraft back;
    REQUIRE(CaptionDraftIo::LoadFile(newDraft, back, NULL));
    CHECK(back.m_videoName == FileNameOf(newVideo));

    CHECK(DeleteCaptionDraft(newVideo));
    CHECK(GetFileAttributesW(newDraft.c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK_FALSE(DeleteCaptionDraft(newVideo));
}
